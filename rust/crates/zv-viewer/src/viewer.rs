use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use eframe::{egui, egui_wgpu};

use crate::annotation_tool::{AnnotationMode, AnnotationTool};
use crate::annotations::AnnotationRenderer;
use crate::controls_window::ControlsWindow;
use crate::debug::{AnnotationDebugState, AnnotationLineDebug, SelectedImageDebug, ViewerDebugState};
use crate::image_list::{ImageId, ImageList};
use crate::image_window::{CursorPixelInfo, ImageWindow};
use crate::image_window_geometry::{ImageWindowGeometryState, WindowResizeAction};
use crate::layout::{LayoutConfig, best_layout_for_image_count};
use crate::modified_image::ModifiedImage;
use crate::shortcuts::{ShortcutViewport, collect_shortcuts};
use crate::viewport_geometry::{ViewportGeometry, ViewportResizeCommand};

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum AppAction {
    NextImage,
    PreviousImage,
    Quit,
    ResizeWindow(WindowResizeAction),
    SetLayout(LayoutConfig),
    AutoLayout,
    SetAnnotationMode(AnnotationMode),
    DeleteSelectedAnnotation,
    UndoImageEdit,
    DiscardImageEdits,
    SaveImageEdits,
}

pub struct Viewer {
    image_window: ImageWindow,
    controls_window: ControlsWindow,
    image_list: Arc<Mutex<ImageList>>,
    pending_actions: Vec<AppAction>,
    controls_action_queue: Arc<Mutex<Vec<AppAction>>>,
    cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
    image_widget_size: Arc<Mutex<Option<(u32, u32)>>>,
    annotation_tool: Arc<Mutex<AnnotationTool>>,
    image_window_geometry: ImageWindowGeometryState,
    layout: LayoutConfig,
    last_displayed_signature: Option<(ImageId, LayoutConfig)>,
    logged_first_image_load: bool,
}

impl Viewer {
    pub fn new(image_paths: Vec<PathBuf>) -> Self {
        let image_list = Arc::new(Mutex::new(ImageList::new(image_paths)));
        let cursor_info = Arc::new(Mutex::new(None));
        let image_widget_size = Arc::new(Mutex::new(None));
        let controls_action_queue = Arc::new(Mutex::new(Vec::new()));
        let annotation_tool = Arc::new(Mutex::new(AnnotationTool::default()));
        Self {
            image_window: ImageWindow::default(),
            controls_window: ControlsWindow::new(
                image_list.clone(),
                cursor_info.clone(),
                image_widget_size.clone(),
                controls_action_queue.clone(),
                annotation_tool.clone(),
            ),
            image_list,
            pending_actions: Vec::new(),
            controls_action_queue,
            cursor_info,
            image_widget_size,
            annotation_tool,
            image_window_geometry: ImageWindowGeometryState::default(),
            layout: LayoutConfig::default(),
            last_displayed_signature: None,
            logged_first_image_load: false,
        }
    }

    pub fn update(&mut self, ctx: &egui::Context, render_state: Option<&egui_wgpu::RenderState>) -> ViewerDebugState {
        self.observe_root_viewport_geometry(ctx);
        self.collect_keyboard_actions(ctx);
        self.collect_controls_actions();
        self.apply_pending_actions(ctx, render_state);

        let mut image_rect = None;
        let mut selected_image_debug = None;
        let (image_load_timing, selected_range) = if let Ok(mut image_list) = self.image_list.lock() {
            image_list.poll_preloads();
            let image_load_timing = image_list.ensure_selected_loaded();
            // Wake the UI when the preload finishes; without this, navigating to an
            // image mid-preload leaves the main view stuck on "Loading..." until the
            // next user interaction triggers a repaint.
            let ctx = ctx.clone();
            image_list.preload_next_from_selection(move || {
                ctx.request_repaint_of(egui::ViewportId::ROOT);
            });
            let selected_range = image_list.selected_range_views();
            (image_load_timing, selected_range)
        } else {
            tracing::warn!("image list lock is poisoned");
            (None, Vec::new())
        };
        self.update_visible_annotations(render_state);
        if !self.logged_first_image_load {
            if let Some(timing) = image_load_timing {
                self.logged_first_image_load = true;
                tracing::info!(
                    elapsed_ms = timing.elapsed.as_millis(),
                    image = %timing.path.display(),
                    succeeded = timing.succeeded,
                    "first image loaded"
                );
            }
        }

        if !selected_range.is_empty() {
            if let Some(selected) = selected_range.iter().flatten().find(|selected| selected.data.is_some()) {
                let data = selected.data.as_ref().expect("checked above");
                self.apply_image_window_geometry(ctx, selected.id, data, self.layout);
                if let Ok(data) = data.lock() {
                    let final_data = data.final_data();
                    selected_image_debug = Some(SelectedImageDebug {
                        name: selected.name.clone(),
                        width: final_data.width(),
                        height: final_data.height(),
                        bytes_per_row: final_data.bytes_per_row(),
                    });
                }
            }

            let cursor_before = self.current_cursor_signature();
            let image_output = self.image_window.show(
                ctx,
                self.layout,
                selected_range,
                self.cursor_info.clone(),
                self.annotation_tool.clone(),
            );
            let cursor_after = self.current_cursor_signature();
            if cursor_before != cursor_after && self.controls_window.is_enabled() {
                ctx.request_repaint_of(self.controls_window.viewport_id());
            }
            if image_output.secondary_clicked {
                self.controls_window.toggle();
            }
            image_rect = image_output.image_rect;
            if let Ok(mut size) = self.image_widget_size.lock() {
                let new_size = image_output.image_rect.map(|r| (r.width() as u32, r.height() as u32));
                if *size != new_size {
                    *size = new_size;
                    // The controls window displays the image widget size in its footer.
                    // It lives in a separate viewport and won't repaint on its own when
                    // the image window is resized, so we nudge it explicitly.
                    if self.controls_window.is_enabled() {
                        ctx.request_repaint_of(self.controls_window.viewport_id());
                    }
                }
            }
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
            annotation: self.annotation_debug_state(),
        }
    }

    fn annotation_debug_state(&self) -> AnnotationDebugState {
        let Ok(tool) = self.annotation_tool.lock() else {
            return AnnotationDebugState {
                mode: "poisoned",
                selected: false,
                creating: false,
                editing: false,
                count: 0,
                counts_by_image: Vec::new(),
                selected_line: None,
            };
        };
        let mode = match tool.mode() {
            AnnotationMode::Select => "select",
            AnnotationMode::AddLine => "add_line",
        };
        let selected_id = tool.selected_id();
        let visible = self.visible_modified_images();
        let counts_by_image: Vec<usize> = visible
            .iter()
            .filter_map(|image| Some(image.lock().ok()?.annotations().elements().len()))
            .collect();
        let (count, selected_line) = visible
            .into_iter()
            .find_map(|image| {
                let image = image.lock().ok()?;
                let count = image.annotations().elements().len();
                let selected_line = image
                    .annotations()
                    .find_by_id(selected_id)
                    .and_then(|element| element.line())
                    .map(|line| AnnotationLineDebug {
                        p1: [line.p1.x, line.p1.y],
                        p2: [line.p2.x, line.p2.y],
                        stroke_width: line.stroke_width,
                    });
                Some((count, selected_line))
            })
            .unwrap_or((0, None));
        AnnotationDebugState {
            mode,
            selected: tool.selected_id_is_valid(),
            creating: tool.is_creating(),
            editing: tool.is_editing(),
            count,
            counts_by_image,
            selected_line,
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

        if let Some(command) = self.image_window_geometry.observe_viewport(ViewportGeometry {
            monitor_size,
            outer_rect,
            inner_rect,
        }) {
            send_resize_command(ctx, command);
        }
    }

    fn collect_controls_actions(&mut self) {
        if let Ok(mut queued) = self.controls_action_queue.lock() {
            self.pending_actions.append(&mut queued);
        }
    }

    fn apply_pending_actions(&mut self, ctx: &egui::Context, render_state: Option<&egui_wgpu::RenderState>) {
        let actions = std::mem::take(&mut self.pending_actions);
        let applied_any = !actions.is_empty();
        for action in actions {
            match action {
                AppAction::NextImage => {
                    if let Ok(mut image_list) = self.image_list.lock() {
                        let step = image_list.selection_count() as isize;
                        image_list.select_relative(step);
                    }
                }
                AppAction::PreviousImage => {
                    if let Ok(mut image_list) = self.image_list.lock() {
                        let step = image_list.selection_count() as isize;
                        image_list.select_relative(-step);
                    }
                }
                AppAction::Quit => {
                    // In this app, ROOT is the main image viewport/window.
                    ctx.send_viewport_cmd_to(egui::ViewportId::ROOT, egui::ViewportCommand::Close)
                }
                AppAction::ResizeWindow(action) => self.apply_window_resize_action(ctx, action),
                AppAction::SetLayout(layout) => self.set_layout(layout),
                AppAction::AutoLayout => {
                    let count = self
                        .image_list
                        .lock()
                        .ok()
                        .map(|image_list| image_list.num_enabled_images())
                        .unwrap_or(1);
                    self.set_layout(best_layout_for_image_count(count, 128, 4.0 / 3.0));
                }
                AppAction::SetAnnotationMode(mode) => {
                    if let Ok(mut tool) = self.annotation_tool.lock() {
                        tool.set_mode(mode);
                    }
                }
                AppAction::DeleteSelectedAnnotation => self.delete_selected_annotation(),
                AppAction::UndoImageEdit => {
                    self.apply_to_visible_images(|image| image.undo_last_change());
                    self.clear_missing_annotation_selection();
                }
                AppAction::DiscardImageEdits => {
                    self.apply_to_visible_images(|image| image.discard_changes());
                    self.clear_missing_annotation_selection();
                }
                AppAction::SaveImageEdits => {
                    self.update_visible_annotations(render_state);
                    self.apply_to_visible_images(|image| {
                        if let Err(err) = image.save_changes(None) {
                            tracing::warn!(error = %err, "failed to save image edits");
                        }
                    });
                    self.clear_missing_annotation_selection();
                }
            }
        }
        if applied_any {
            // Keep the main image viewport and controls viewport visually in sync.
            ctx.request_repaint_of(egui::ViewportId::ROOT);
            ctx.request_repaint_of(self.controls_window.viewport_id());
        }
    }

    fn visible_modified_images(&self) -> Vec<Arc<Mutex<ModifiedImage>>> {
        self.image_list
            .lock()
            .ok()
            .map(|image_list| {
                image_list
                    .selected_range_views()
                    .into_iter()
                    .filter_map(|image| image?.data)
                    .collect()
            })
            .unwrap_or_default()
    }

    fn apply_to_visible_images(&self, f: impl Fn(&mut ModifiedImage)) {
        for image in self.visible_modified_images() {
            if let Ok(mut image) = image.lock() {
                f(&mut image);
            }
        }
    }

    fn update_visible_annotations(&self, render_state: Option<&egui_wgpu::RenderState>) {
        let Some(render_state) = render_state else {
            return;
        };
        let mut renderer = render_state.renderer.write();
        let Some(annotation_renderer) = renderer.callback_resources.get_mut::<AnnotationRenderer>() else {
            tracing::warn!("missing AnnotationRenderer callback resource");
            return;
        };
        for image in self.visible_modified_images() {
            if let Ok(mut image) = image.lock() {
                image.update_annotations(annotation_renderer, &render_state.device, &render_state.queue);
            }
        }
    }

    fn delete_selected_annotation(&self) {
        let images = self.visible_modified_images();
        if let Ok(mut tool) = self.annotation_tool.lock() {
            tool.delete_selected(&images);
        }
    }

    fn clear_missing_annotation_selection(&self) {
        let images = self.visible_modified_images();
        if let Ok(mut tool) = self.annotation_tool.lock() {
            tool.clear_selection_if_missing(&images);
        }
    }

    fn apply_image_window_geometry(
        &mut self,
        ctx: &egui::Context,
        image_id: ImageId,
        data: &Arc<Mutex<ModifiedImage>>,
        layout: LayoutConfig,
    ) {
        let Ok(data) = data.lock() else {
            return;
        };
        let final_data = data.final_data();
        let image_size = layout_widget_size(
            egui::vec2(final_data.width() as f32, final_data.height() as f32),
            layout,
            1.0,
        );
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

        let viewport = ViewportGeometry {
            monitor_size,
            outer_rect,
            inner_rect,
        };

        if let Some(command) = self
            .image_window_geometry
            .prepare_initial_geometry(image_size, viewport, 0)
        {
            send_resize_command(ctx, command);
            self.last_displayed_signature = Some((image_id, layout));
            return;
        }

        if self.last_displayed_signature != Some((image_id, layout)) {
            self.last_displayed_signature = Some((image_id, layout));
            if let Some(command) = self.image_window_geometry.on_image_changed(image_size, viewport) {
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
                ControlsWindow::position_for_image_window(outer_rect, monitor_size)
            }
            _ => None,
        };
        self.controls_window.set_target_position(target_position);
    }

    fn current_cursor_signature(&self) -> Option<(String, u32, u32, [u8; 4])> {
        self.cursor_info.lock().ok().and_then(|info| {
            info.as_ref()
                .map(|cursor| (cursor.image_name.clone(), cursor.x, cursor.y, cursor.rgba))
        })
    }

    fn set_layout(&mut self, layout: LayoutConfig) {
        self.layout = layout;
        if let Ok(mut image_list) = self.image_list.lock() {
            image_list.set_selection_count(layout.image_count());
        }
    }
}

fn layout_widget_size(first_image_size: egui::Vec2, layout: LayoutConfig, padding: f32) -> egui::Vec2 {
    egui::vec2(
        first_image_size.x * layout.cols as f32 + layout.cols.saturating_sub(1) as f32 * padding,
        first_image_size.y * layout.rows as f32 + layout.rows.saturating_sub(1) as f32 * padding,
    )
}

fn send_resize_command(ctx: &egui::Context, command: ViewportResizeCommand) {
    if let Some(position) = command.outer_position {
        ctx.send_viewport_cmd(egui::ViewportCommand::OuterPosition(position));
    }
    if let Some(size) = command.inner_size {
        ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(size));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layout_widget_size_uses_first_image_and_grid_padding() {
        assert_eq!(
            layout_widget_size(egui::vec2(320.0, 240.0), LayoutConfig { rows: 2, cols: 3 }, 1.0),
            egui::vec2(962.0, 481.0)
        );
    }
}
