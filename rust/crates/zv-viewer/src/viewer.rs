use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use eframe::egui;

use crate::color_image::{ImageSRGBA, PixelSRGBA};
use crate::controls_window::ControlsWindow;
use crate::debug::{SelectedImageDebug, ViewerDebugState};
use crate::geometry::{
    controls_position_for_image_window, ImageWindowGeometryState, ViewportGeometry,
    ViewportResizeCommand, WindowResizeAction,
};
use crate::image_io::load_rgba_image;
use crate::image_item_data::ImageItemData;
use crate::image_window::{CursorPixelInfo, ImageWindow};
use crate::shortcuts::{collect_shortcuts, ShortcutViewport};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AppAction {
    NextImage,
    PreviousImage,
    Quit,
    ResizeWindow(WindowResizeAction),
}

pub struct Viewer {
    image_window: ImageWindow,
    controls_window: ControlsWindow,
    entries: Vec<ImageEntry>,
    selected_index: usize,
    pending_actions: Vec<AppAction>,
    controls_action_queue: Arc<Mutex<Vec<AppAction>>>,
    cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
    image_window_geometry: ImageWindowGeometryState,
    last_displayed_index: Option<usize>,
}

struct ImageEntry {
    path: PathBuf,
    data: Option<Arc<Mutex<ImageItemData>>>,
    error: Option<String>,
}

impl Viewer {
    pub fn new(image_paths: Vec<PathBuf>) -> Self {
        let entries = if image_paths.is_empty() {
            vec![ImageEntry::default_image()]
        } else {
            image_paths
                .into_iter()
                .map(ImageEntry::from_path)
                .collect::<Vec<_>>()
        };

        let cursor_info = Arc::new(Mutex::new(None));
        let controls_action_queue = Arc::new(Mutex::new(Vec::new()));
        Self {
            image_window: ImageWindow::default(),
            controls_window: ControlsWindow::new(
                cursor_info.clone(),
                controls_action_queue.clone(),
            ),
            entries,
            selected_index: 0,
            pending_actions: Vec::new(),
            controls_action_queue,
            cursor_info,
            image_window_geometry: ImageWindowGeometryState::default(),
            last_displayed_index: None,
        }
    }

    pub fn update(&mut self, ctx: &egui::Context) -> ViewerDebugState {
        self.observe_root_viewport_geometry(ctx);
        self.collect_keyboard_actions(ctx);
        self.collect_controls_actions();
        self.apply_pending_actions(ctx);

        let mut image_rect = None;
        let mut selected_image_debug = None;
        let selected = if let Some(entry) = self.entries.get_mut(self.selected_index) {
            entry.ensure_loaded();
            Some((
                entry.display_name(),
                entry.data.clone(),
                entry.error.clone(),
            ))
        } else {
            None
        };

        if let Some((image_name, data, error)) = selected {
            if let Some(data) = data.as_ref() {
                self.apply_image_window_geometry(ctx, data);
                if let Ok(data) = data.lock() {
                    selected_image_debug = Some(SelectedImageDebug {
                        name: image_name.clone(),
                        width: data.width(),
                        height: data.height(),
                        bytes_per_row: data.bytes_per_row(),
                    });
                }
            }

            let image_output = self.image_window.show(
                ctx,
                image_name,
                data,
                error.as_deref(),
                self.cursor_info.clone(),
            );
            if image_output.secondary_clicked {
                self.controls_window.toggle();
            }
            image_rect = image_output.image_rect;
        }

        self.update_controls_position(ctx);
        self.controls_window.show(ctx);

        ViewerDebugState {
            image_rect,
            controls_enabled: self.controls_window.is_enabled(),
            controls_viewport_id: self.controls_window.viewport_id(),
            controls_target_position: self.controls_window.target_position(),
            cursor_info: self.cursor_info.lock().ok().and_then(|info| info.clone()),
            selected_image: selected_image_debug,
        }
    }

    fn collect_keyboard_actions(&mut self, ctx: &egui::Context) {
        self.pending_actions
            .extend(collect_shortcuts(ctx, ShortcutViewport::MainImage));
    }

    fn observe_root_viewport_geometry(&mut self, ctx: &egui::Context) {
        let (monitor_size, outer_rect, inner_rect) = ctx.input(|input| {
            (
                input.viewport().monitor_size,
                input.viewport().outer_rect,
                input.viewport().inner_rect,
            )
        });
        let Some(monitor_size) = monitor_size else {
            return;
        };

        if let Some(command) = self
            .image_window_geometry
            .observe_viewport(ViewportGeometry {
                monitor_size,
                outer_rect,
                inner_rect,
            })
        {
            send_resize_command(ctx, command);
        }
    }

    fn collect_controls_actions(&mut self) {
        if let Ok(mut queued) = self.controls_action_queue.lock() {
            self.pending_actions.append(&mut queued);
        }
    }

    fn apply_pending_actions(&mut self, ctx: &egui::Context) {
        let actions = std::mem::take(&mut self.pending_actions);
        let applied_any = !actions.is_empty();
        for action in actions {
            match action {
                AppAction::NextImage => self.select_relative(1),
                AppAction::PreviousImage => self.select_relative(-1),
                AppAction::Quit => {
                    // In this app, ROOT is the main image viewport/window.
                    ctx.send_viewport_cmd_to(egui::ViewportId::ROOT, egui::ViewportCommand::Close)
                }
                AppAction::ResizeWindow(action) => self.apply_window_resize_action(ctx, action),
            }
        }
        if applied_any {
            // Keep the main image viewport and controls viewport visually in sync.
            ctx.request_repaint_of(egui::ViewportId::ROOT);
            ctx.request_repaint_of(self.controls_window.viewport_id());
        }
    }

    fn select_relative(&mut self, offset: isize) {
        if self.entries.is_empty() {
            self.selected_index = 0;
            return;
        }

        let count = self.entries.len() as isize;
        let next = (self.selected_index as isize + offset).rem_euclid(count);
        if next as usize != self.selected_index {
            self.selected_index = next as usize;
        }
    }

    fn apply_image_window_geometry(
        &mut self,
        ctx: &egui::Context,
        data: &Arc<Mutex<ImageItemData>>,
    ) {
        let Ok(data) = data.lock() else {
            return;
        };
        let image_size = egui::vec2(data.width() as f32, data.height() as f32);
        drop(data);

        let (monitor_size, outer_rect, inner_rect) = ctx.input(|input| {
            (
                input.viewport().monitor_size,
                input.viewport().outer_rect,
                input.viewport().inner_rect,
            )
        });
        let Some(monitor_size) = monitor_size else {
            return;
        };

        if let Some(command) =
            self.image_window_geometry
                .prepare_initial_geometry(image_size, monitor_size, 0)
        {
            send_resize_command(ctx, command);
            self.last_displayed_index = Some(self.selected_index);
            return;
        }

        if self.last_displayed_index != Some(self.selected_index) {
            self.last_displayed_index = Some(self.selected_index);
            if let Some(command) = self.image_window_geometry.on_image_changed(
                image_size,
                ViewportGeometry {
                    monitor_size,
                    outer_rect,
                    inner_rect,
                },
            ) {
                send_resize_command(ctx, command);
            }
        }
    }

    fn apply_window_resize_action(&mut self, ctx: &egui::Context, action: WindowResizeAction) {
        let (monitor_size, outer_rect, inner_rect) = ctx.input(|input| {
            (
                input.viewport().monitor_size,
                input.viewport().outer_rect,
                input.viewport().inner_rect,
            )
        });
        let Some(monitor_size) = monitor_size else {
            return;
        };

        if let Some(command) = self.image_window_geometry.apply_resize_action(
            ViewportGeometry {
                monitor_size,
                outer_rect,
                inner_rect,
            },
            action,
        ) {
            send_resize_command(ctx, command);
        }
    }

    fn update_controls_position(&mut self, ctx: &egui::Context) {
        let (monitor_size, outer_rect) =
            ctx.input(|input| (input.viewport().monitor_size, input.viewport().outer_rect));
        let target_position = match (monitor_size, outer_rect) {
            (Some(monitor_size), Some(outer_rect)) => {
                controls_position_for_image_window(outer_rect, monitor_size)
            }
            _ => None,
        };
        self.controls_window.set_target_position(target_position);
    }
}

fn send_resize_command(ctx: &egui::Context, command: ViewportResizeCommand) {
    if let Some(position) = command.outer_position {
        ctx.send_viewport_cmd(egui::ViewportCommand::OuterPosition(position));
    }
    if let Some(size) = command.inner_size {
        ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(size));
    }
}

impl ImageEntry {
    fn from_path(path: PathBuf) -> Self {
        Self {
            path,
            data: None,
            error: None,
        }
    }

    fn default_image() -> Self {
        let mut image = ImageSRGBA::new(256, 256);
        let width = image.width();
        let height = image.height();
        for row in 0..height {
            if let Some(row_pixels) = image.row_mut(row) {
                for col in 0..width {
                    row_pixels[col as usize] = PixelSRGBA {
                        r: row as u8,
                        g: col as u8,
                        b: (row + col) as u8,
                        a: 255,
                    };
                }
            }
        }

        Self {
            path: PathBuf::from("<<default>>"),
            data: Some(Arc::new(Mutex::new(ImageItemData::new(image)))),
            error: None,
        }
    }

    fn ensure_loaded(&mut self) {
        if self.data.is_some() || self.error.is_some() {
            return;
        }

        match load_rgba_image(&self.path) {
            Ok(image) => self.data = Some(Arc::new(Mutex::new(ImageItemData::new(image)))),
            Err(err) => self.error = Some(format!("{err:#}")),
        }
    }

    fn display_name(&self) -> String {
        self.path
            .file_name()
            .and_then(|name| name.to_str())
            .map(ToOwned::to_owned)
            .unwrap_or_else(|| self.path.display().to_string())
    }
}
