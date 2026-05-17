use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use eframe::egui;

use crate::actions::ImageWindowAction;
use crate::controls_window::ControlsWindow;
use crate::debug::{SelectedImageDebug, ViewerDebugState};
use crate::geometry::{controls_position_for_image_window, initial_image_window_geometry};
use crate::image::{ImageItemData, RgbaImage, load_rgba_image};
use crate::image_window::{CursorPixelInfo, ImageWindow};

pub struct Viewer {
    image_window: ImageWindow,
    controls_window: ControlsWindow,
    entries: Vec<ImageEntry>,
    selected_index: usize,
    pending_actions: Vec<ImageWindowAction>,
    cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
    applied_image_geometry_for: Option<(u32, u32)>,
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
        Self {
            image_window: ImageWindow::default(),
            controls_window: ControlsWindow::new(cursor_info.clone()),
            entries,
            selected_index: 0,
            pending_actions: Vec::new(),
            cursor_info,
            applied_image_geometry_for: None,
        }
    }

    pub fn update(&mut self, ctx: &egui::Context) -> ViewerDebugState {
        self.collect_keyboard_actions(ctx);
        self.apply_pending_actions();

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
        let mut close_requested = false;
        ctx.input(|input| {
            if input.key_pressed(egui::Key::ArrowDown) || input.key_pressed(egui::Key::Space) {
                self.pending_actions.push(ImageWindowAction::NextImage);
            }
            if input.key_pressed(egui::Key::ArrowUp) || input.key_pressed(egui::Key::Backspace) {
                self.pending_actions.push(ImageWindowAction::PreviousImage);
            }
            if input.key_pressed(egui::Key::Q) {
                close_requested = true;
            }
        });
        if close_requested {
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
        }
    }

    fn apply_pending_actions(&mut self) {
        let actions = std::mem::take(&mut self.pending_actions);
        for action in actions {
            match action {
                ImageWindowAction::NextImage => self.select_relative(1),
                ImageWindowAction::PreviousImage => self.select_relative(-1),
            }
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
            self.applied_image_geometry_for = None;
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
        let image_key = (data.width(), data.height());
        if self.applied_image_geometry_for == Some(image_key) {
            return;
        }

        let Some(monitor_size) = ctx.input(|input| input.viewport().monitor_size) else {
            return;
        };

        let geometry = initial_image_window_geometry(
            egui::vec2(data.width() as f32, data.height() as f32),
            monitor_size,
            0,
        );
        ctx.send_viewport_cmd(egui::ViewportCommand::OuterPosition(geometry.origin));
        ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(geometry.size));
        self.applied_image_geometry_for = Some(image_key);
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

impl ImageEntry {
    fn from_path(path: PathBuf) -> Self {
        Self {
            path,
            data: None,
            error: None,
        }
    }

    fn default_image() -> Self {
        let mut image = RgbaImage::new(256, 256);
        let tight_bytes_per_row = image.width() as usize * RgbaImage::BYTES_PER_PIXEL;
        for row in 0..image.height() as usize {
            let row_start = row * image.bytes_per_row();
            for col in 0..image.width() as usize {
                let offset = row_start + col * RgbaImage::BYTES_PER_PIXEL;
                image.pixels_mut()[offset] = row as u8;
                image.pixels_mut()[offset + 1] = col as u8;
                image.pixels_mut()[offset + 2] = (row + col) as u8;
                image.pixels_mut()[offset + 3] = 255;
            }
            debug_assert!(tight_bytes_per_row <= image.bytes_per_row());
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
