use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use rfd;

use eframe::{egui, egui_wgpu};

use crate::annotation_tool::{AnnotationMode, AnnotationTool};
use crate::annotations::{AnnotationElement, AnnotationRenderer};
use crate::controls_window::ControlsWindow;
use crate::debug::{
    AnnotationBoxDebug, AnnotationDebugState, AnnotationLineDebug, SelectedImageDebug, ViewerDebugState,
};
use crate::image_list::{ImageId, ImageList, PendingImageChange};
use crate::image_window::{CursorPixelInfo, ImageWindow};
use crate::image_window_geometry::{ImageWindowGeometryState, WindowResizeAction};
use crate::layout::{LayoutConfig, best_layout_for_image_count};
use crate::modified_image::ModifiedImage;
use crate::shortcuts::{ShortcutViewport, collect_shortcuts};
use crate::viewport_geometry::{ViewportGeometry, ViewportResizeCommand};

const CONFIRMATION_MIN_INNER_SIZE: egui::Vec2 = egui::vec2(420.0, 180.0);

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
    OpenImage,
    CloseImage,
    DeleteImageOnDisk,
    ToggleCurrentImageSelection,
    SelectAllImages,
    ClearImageSelection,
    RotateLeft,
    RotateRight,
}

#[derive(Clone, Debug, Eq, PartialEq)]
enum PendingConfirmation {
    Quit {
        current_index: usize,
        original_layout: LayoutConfig,
        original_selection_index: Option<usize>,
        original_selection_count: usize,
    },
    CloseImageAt {
        index: usize,
    },
    DeleteImages {
        indices: Vec<usize>,
    },
}

#[derive(Clone, Debug, Default)]
pub struct ImageEditorState {
    pub has_changes: bool,
    pub can_undo: bool,
    pub has_selection: bool,
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
    editor_state: Arc<Mutex<ImageEditorState>>,
    image_window_geometry: ImageWindowGeometryState,
    layout: LayoutConfig,
    last_displayed_signature: Option<(ImageId, LayoutConfig)>,
    logged_first_image_load: bool,
    pending_confirmation: Option<PendingConfirmation>,
    allow_close: bool,
}

impl Viewer {
    pub fn new(image_paths: Vec<PathBuf>) -> Self {
        let image_list = Arc::new(Mutex::new(ImageList::new(image_paths)));
        let cursor_info = Arc::new(Mutex::new(None));
        let image_widget_size = Arc::new(Mutex::new(None));
        let controls_action_queue = Arc::new(Mutex::new(Vec::new()));
        let annotation_tool = Arc::new(Mutex::new(AnnotationTool::default()));
        let editor_state = Arc::new(Mutex::new(ImageEditorState::default()));
        Self {
            image_window: ImageWindow::default(),
            controls_window: ControlsWindow::new(
                image_list.clone(),
                cursor_info.clone(),
                image_widget_size.clone(),
                controls_action_queue.clone(),
                annotation_tool.clone(),
                editor_state.clone(),
            ),
            image_list,
            pending_actions: Vec::new(),
            controls_action_queue,
            cursor_info,
            image_widget_size,
            annotation_tool,
            editor_state,
            image_window_geometry: ImageWindowGeometryState::default(),
            layout: LayoutConfig::default(),
            last_displayed_signature: None,
            logged_first_image_load: false,
            pending_confirmation: None,
            allow_close: false,
        }
    }

    pub fn update(&mut self, ctx: &egui::Context, render_state: Option<&egui_wgpu::RenderState>) -> ViewerDebugState {
        self.handle_root_close_request(ctx);
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

            let image_output = self.image_window.show(
                ctx,
                self.layout,
                selected_range,
                self.cursor_info.clone(),
                self.annotation_tool.clone(),
            );
            if image_output.shared_state_changed && self.controls_window.is_enabled() {
                ctx.request_repaint_of(self.controls_window.viewport_id());
            }
            if image_output.secondary_clicked {
                self.controls_window.toggle();
            }
            if let Some(index) = image_output.toggle_marked_index {
                if let Ok(mut image_list) = self.image_list.lock() {
                    image_list.toggle_marked_at(index);
                }
                ctx.request_repaint_of(self.controls_window.viewport_id());
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
        self.update_editor_state();
        self.controls_window.show(ctx);
        self.render_pending_confirmation(ctx, render_state);

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

    pub(crate) fn queue_action(&mut self, action: AppAction) {
        self.pending_actions.push(action);
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
                selected_box: None,
            };
        };
        let mode = match tool.mode() {
            AnnotationMode::Select => "select",
            AnnotationMode::AddLine => "add_line",
            AnnotationMode::AddArrow => "add_arrow",
            AnnotationMode::AddRectangle => "add_rectangle",
            AnnotationMode::AddEllipse => "add_ellipse",
            AnnotationMode::AddText => "add_text",
        };
        let selected_id = tool.selected_id();
        let visible = self.visible_modified_images();
        let counts_by_image: Vec<usize> = visible
            .iter()
            .filter_map(|image| Some(image.lock().ok()?.annotations().elements().len()))
            .collect();
        let (count, selected_line, selected_box) = visible
            .into_iter()
            .find_map(|image| {
                let image = image.lock().ok()?;
                let count = image.annotations().elements().len();
                let selected_element = image.annotations().find_by_id(selected_id);
                let selected_line = selected_element.and_then(|element| match element {
                    AnnotationElement::Line { segment, style, .. } => Some(AnnotationLineDebug {
                        p1: [segment.p1.x, segment.p1.y],
                        p2: [segment.p2.x, segment.p2.y],
                        stroke_width: style.stroke.width,
                        start_style: style.start_style.label(),
                        end_style: style.end_style.label(),
                    }),
                    AnnotationElement::Rectangle { .. }
                    | AnnotationElement::Ellipse { .. }
                    | AnnotationElement::Text { .. } => None,
                });
                let selected_box = selected_element.and_then(|element| match element {
                    AnnotationElement::Rectangle { bounds, stroke, .. } => Some(("rectangle", bounds, stroke)),
                    AnnotationElement::Ellipse { bounds, stroke, .. } => Some(("ellipse", bounds, stroke)),
                    AnnotationElement::Line { .. } | AnnotationElement::Text { .. } => None,
                });
                let selected_box = selected_box.map(|(kind, bounds, stroke)| AnnotationBoxDebug {
                    kind,
                    min: [bounds.min.x, bounds.min.y],
                    max: [bounds.max.x, bounds.max.y],
                    stroke_width: stroke.width,
                });
                Some((count, selected_line, selected_box))
            })
            .unwrap_or((0, None, None));
        AnnotationDebugState {
            mode,
            selected: tool.selected_id_is_valid(),
            creating: tool.is_creating(),
            editing: tool.is_editing(),
            count,
            counts_by_image,
            selected_line,
            selected_box,
        }
    }

    fn collect_keyboard_actions(&mut self, ctx: &egui::Context) {
        if self.pending_confirmation.is_some() {
            return;
        }
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
            if self.pending_confirmation.is_some() {
                queued.clear();
                return;
            }
            self.pending_actions.append(&mut queued);
        }
    }

    fn apply_pending_actions(&mut self, ctx: &egui::Context, render_state: Option<&egui_wgpu::RenderState>) {
        let actions = std::mem::take(&mut self.pending_actions);
        let applied_any = !actions.is_empty();
        for action in actions {
            if self.pending_confirmation.is_some() {
                continue;
            }
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
                    self.request_quit(ctx);
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
                    let images = self.visible_pending_change_images();
                    self.update_pending_image_annotations(&images, render_state);
                    self.save_pending_images_with_dialog(images);
                    self.clear_missing_annotation_selection();
                }
                AppAction::OpenImage => self.open_image(),
                AppAction::CloseImage => {
                    let index = self
                        .image_list
                        .lock()
                        .ok()
                        .and_then(|image_list| image_list.first_selected_index());
                    if let Some(index) = index {
                        self.request_close_image_at(ctx, index);
                    }
                }
                AppAction::DeleteImageOnDisk => {
                    let indices = self
                        .image_list
                        .lock()
                        .ok()
                        .map(|image_list| {
                            let marked = image_list.marked_indices();
                            if marked.is_empty() {
                                image_list.first_selected_index().into_iter().collect()
                            } else {
                                marked
                            }
                        })
                        .unwrap_or_default();
                    if !indices.is_empty() {
                        self.request_delete_images(ctx, indices);
                    }
                }
                AppAction::ToggleCurrentImageSelection => {
                    if let Ok(mut image_list) = self.image_list.lock()
                        && let Some(index) = image_list.first_selected_index()
                    {
                        image_list.toggle_marked_at(index);
                    }
                }
                AppAction::SelectAllImages => {
                    if let Ok(mut image_list) = self.image_list.lock() {
                        image_list.mark_all_enabled();
                    }
                }
                AppAction::ClearImageSelection => {
                    if let Ok(mut image_list) = self.image_list.lock() {
                        image_list.clear_marked();
                    }
                }
                AppAction::RotateLeft => {
                    self.apply_to_visible_images(|image| image.rotate_ccw());
                }
                AppAction::RotateRight => {
                    self.apply_to_visible_images(|image| image.rotate_cw());
                }
            }
        }
        if applied_any {
            // Keep the main image viewport and controls viewport visually in sync.
            ctx.request_repaint_of(egui::ViewportId::ROOT);
            ctx.request_repaint_of(self.controls_window.viewport_id());
        }
    }

    fn handle_root_close_request(&mut self, ctx: &egui::Context) {
        let close_requested = ctx.input(|input| input.viewport().close_requested());
        if !close_requested {
            return;
        }
        if self.allow_close {
            return;
        }
        ctx.send_viewport_cmd_to(egui::ViewportId::ROOT, egui::ViewportCommand::CancelClose);
        self.request_quit(ctx);
    }

    fn request_quit(&mut self, ctx: &egui::Context) {
        if self.pending_confirmation.is_some() {
            ctx.request_repaint_of(egui::ViewportId::ROOT);
            return;
        }
        let Some(current_index) = self.pending_change_images().first().map(|image| image.index) else {
            self.close_root_viewport(ctx);
            return;
        };
        let (original_selection_index, original_selection_count) = self
            .image_list
            .lock()
            .ok()
            .map(|image_list| (image_list.first_selected_index(), image_list.selection_count()))
            .unwrap_or((None, 1));
        let original_layout = self.layout;
        self.select_single_image(current_index);
        self.pending_confirmation = Some(PendingConfirmation::Quit {
            current_index,
            original_layout,
            original_selection_index,
            original_selection_count,
        });
        ctx.request_repaint_of(egui::ViewportId::ROOT);
    }

    fn request_close_image_at(&mut self, ctx: &egui::Context, index: usize) {
        if self.image_has_pending_changes(index) {
            self.pending_confirmation = Some(PendingConfirmation::CloseImageAt { index });
            ctx.request_repaint_of(egui::ViewportId::ROOT);
            return;
        }
        self.close_image_at(index);
    }

    fn request_delete_images(&mut self, ctx: &egui::Context, indices: Vec<usize>) {
        let indices = self
            .image_list
            .lock()
            .ok()
            .map(|image_list| {
                indices
                    .into_iter()
                    .filter(|&index| image_list.source_path_at(index).is_some())
                    .collect::<Vec<_>>()
            })
            .unwrap_or_default();
        if indices.is_empty() {
            return;
        }
        self.pending_confirmation = Some(PendingConfirmation::DeleteImages { indices });
        ctx.request_repaint_of(egui::ViewportId::ROOT);
    }

    fn close_root_viewport(&mut self, ctx: &egui::Context) {
        self.allow_close = true;
        // In this app, ROOT is the main image viewport/window.
        ctx.send_viewport_cmd_to(egui::ViewportId::ROOT, egui::ViewportCommand::Close);
    }

    fn close_image_at(&self, index: usize) {
        if let Ok(mut image_list) = self.image_list.lock() {
            image_list.remove_at(index);
        }
        self.clear_missing_annotation_selection();
    }

    fn delete_images(&self, mut indices: Vec<usize>) {
        let Ok(mut image_list) = self.image_list.lock() else {
            return;
        };
        indices.sort_unstable();
        indices.dedup();
        for index in indices.into_iter().rev() {
            let Some(path) = image_list.source_path_at(index).map(Path::to_path_buf) else {
                continue;
            };
            if let Err(error) = std::fs::remove_file(&path) {
                tracing::error!(image = %path.display(), %error, "failed to delete image from disk");
                continue;
            }
            image_list.remove_at(index);
        }
        drop(image_list);
        self.clear_missing_annotation_selection();
    }

    fn image_has_pending_changes(&self, index: usize) -> bool {
        self.image_list
            .lock()
            .ok()
            .is_some_and(|image_list| image_list.has_pending_changes_at(index))
    }

    fn render_pending_confirmation(&mut self, ctx: &egui::Context, render_state: Option<&egui_wgpu::RenderState>) {
        let Some(pending) = self.pending_confirmation.clone() else {
            return;
        };
        if let PendingConfirmation::Quit { current_index, .. } = &pending {
            self.select_single_image(*current_index);
        }
        ensure_viewport_can_fit_confirmation(ctx);
        let (title, message) = match &pending {
            PendingConfirmation::Quit { current_index, .. } => (
                "Quit zv",
                self.pending_change_image_at(*current_index)
                    .map(|image| format!("{} has unsaved changes. What would you like to do?", image.name))
                    .unwrap_or_else(|| "This image has unsaved changes. What would you like to do?".to_owned()),
            ),
            PendingConfirmation::CloseImageAt { .. } => (
                "Close Image",
                "This image has unsaved changes. What would you like to do?".to_owned(),
            ),
            PendingConfirmation::DeleteImages { indices } => {
                let paths = self
                    .image_list
                    .lock()
                    .ok()
                    .map(|image_list| {
                        indices
                            .iter()
                            .filter_map(|&index| image_list.source_path_at(index))
                            .map(|path| path.display().to_string())
                            .collect::<Vec<_>>()
                    })
                    .unwrap_or_default();
                let title = if paths.len() == 1 {
                    "Delete Image on Disk?"
                } else {
                    "Delete Selected Images on Disk?"
                };
                let summary = if paths.len() == 1 {
                    format!("{} will be deleted.", paths[0])
                } else {
                    format!("{} selected images will be deleted.", paths.len())
                };
                (title, format!("{summary}\nThis operation cannot be undone!"))
            }
        };
        let response = egui::Modal::new(egui::Id::new("pending_changes_confirm_modal")).show(ctx, |ui| {
            ui.set_width(340.0);
            ui.heading(title);
            ui.add_space(8.0);
            ui.label(message);
            ui.add_space(12.0);
            ui.horizontal(|ui| {
                if let PendingConfirmation::DeleteImages { .. } = &pending {
                    let ok_response = ui.button("OK");
                    ok_response.request_focus();
                    if ok_response.clicked() || ui.input(|input| input.key_pressed(egui::Key::Enter)) {
                        self.finish_pending_confirmation(ctx, pending.clone());
                    }
                    if ui.button("Cancel").clicked() {
                        self.cancel_pending_confirmation(pending.clone());
                    }
                    return;
                }
                let save_response = ui.button("Save");
                save_response.request_focus();
                let save_requested = save_response.clicked() || ui.input(|input| input.key_pressed(egui::Key::Enter));
                if save_requested {
                    if self.save_for_pending_confirmation(pending.clone(), render_state) {
                        self.finish_pending_confirmation(ctx, pending.clone());
                    }
                }
                if ui.button("Discard").clicked() {
                    self.discard_for_pending_confirmation(pending.clone());
                    self.finish_pending_confirmation(ctx, pending.clone());
                }
                if ui.button("Cancel").clicked() {
                    self.cancel_pending_confirmation(pending.clone());
                }
            });
        });
        if response.should_close() {
            self.cancel_pending_confirmation(pending);
        }
    }

    fn save_for_pending_confirmation(
        &self,
        pending: PendingConfirmation,
        render_state: Option<&egui_wgpu::RenderState>,
    ) -> bool {
        match pending {
            PendingConfirmation::Quit { current_index, .. } => {
                let Some(image) = self.pending_change_image_at(current_index) else {
                    return true;
                };
                let images = vec![image];
                self.update_pending_image_annotations(&images, render_state);
                self.save_pending_images_with_dialog(images)
            }
            PendingConfirmation::CloseImageAt { index } => {
                if let Some(image) = self.pending_change_image_at(index) {
                    let images = vec![image];
                    self.update_pending_image_annotations(&images, render_state);
                    self.save_pending_images_with_dialog(images)
                } else {
                    true
                }
            }
            PendingConfirmation::DeleteImages { .. } => true,
        }
    }

    fn discard_for_pending_confirmation(&self, pending: PendingConfirmation) {
        match pending {
            PendingConfirmation::Quit { current_index, .. } => {
                if let Some(image) = self.pending_change_image_at(current_index) {
                    if let Ok(mut image) = image.data.lock() {
                        image.discard_changes();
                    }
                }
            }
            PendingConfirmation::CloseImageAt { index } => {
                if let Some(image) = self.modified_image_at(index) {
                    if let Ok(mut image) = image.lock() {
                        image.discard_changes();
                    }
                }
            }
            PendingConfirmation::DeleteImages { .. } => {}
        }
        self.clear_missing_annotation_selection();
    }

    fn finish_pending_confirmation(&mut self, ctx: &egui::Context, pending: PendingConfirmation) {
        match pending {
            PendingConfirmation::Quit {
                original_layout,
                original_selection_index,
                original_selection_count,
                ..
            } => {
                if let Some(next_index) = self.pending_change_images().first().map(|image| image.index) {
                    self.select_single_image(next_index);
                    self.pending_confirmation = Some(PendingConfirmation::Quit {
                        current_index: next_index,
                        original_layout,
                        original_selection_index,
                        original_selection_count,
                    });
                } else {
                    self.pending_confirmation = None;
                    self.close_root_viewport(ctx);
                }
            }
            PendingConfirmation::CloseImageAt { index } => {
                self.pending_confirmation = None;
                self.close_image_at(index);
            }
            PendingConfirmation::DeleteImages { indices } => {
                self.pending_confirmation = None;
                self.delete_images(indices);
            }
        }
        ctx.request_repaint_of(egui::ViewportId::ROOT);
        ctx.request_repaint_of(self.controls_window.viewport_id());
    }

    fn cancel_pending_confirmation(&mut self, pending: PendingConfirmation) {
        if let PendingConfirmation::Quit {
            original_layout,
            original_selection_index,
            original_selection_count,
            ..
        } = pending
        {
            self.layout = original_layout;
            if let Ok(mut image_list) = self.image_list.lock() {
                image_list.set_selection_count(original_selection_count);
                if let Some(index) = original_selection_index {
                    image_list.select_index(index);
                }
            }
        }
        self.pending_confirmation = None;
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

    fn pending_change_images(&self) -> Vec<PendingImageChange> {
        self.image_list
            .lock()
            .ok()
            .map(|image_list| image_list.pending_change_images())
            .unwrap_or_default()
    }

    fn pending_change_image_at(&self, index: usize) -> Option<PendingImageChange> {
        self.image_list
            .lock()
            .ok()
            .and_then(|image_list| image_list.pending_change_image_at(index))
    }

    fn visible_pending_change_images(&self) -> Vec<PendingImageChange> {
        self.image_list
            .lock()
            .ok()
            .map(|image_list| {
                image_list
                    .selected_range_views()
                    .into_iter()
                    .flatten()
                    .filter_map(|image| {
                        let data = image.data?;
                        let has_changes = data.lock().ok().is_some_and(|data| data.has_pending_changes());
                        has_changes.then(|| pending_image_change(0, image.name, data))
                    })
                    .collect()
            })
            .unwrap_or_default()
    }

    fn modified_image_at(&self, index: usize) -> Option<Arc<Mutex<ModifiedImage>>> {
        self.image_list
            .lock()
            .ok()
            .and_then(|image_list| image_list.modified_image_at(index))
    }

    fn select_single_image(&mut self, index: usize) {
        self.layout = LayoutConfig::default();
        if let Ok(mut image_list) = self.image_list.lock() {
            image_list.set_selection_count(1);
            image_list.select_index(index);
        }
        self.clear_missing_annotation_selection();
    }

    fn apply_to_visible_images(&self, f: impl Fn(&mut ModifiedImage)) {
        for image in self.visible_modified_images() {
            if let Ok(mut image) = image.lock() {
                f(&mut image);
            }
        }
    }

    fn update_visible_annotations(&self, render_state: Option<&egui_wgpu::RenderState>) {
        let images = self.visible_modified_images();
        self.update_modified_images_annotations(&images, render_state);
    }

    fn update_pending_image_annotations(
        &self,
        images: &[PendingImageChange],
        render_state: Option<&egui_wgpu::RenderState>,
    ) {
        let images = images.iter().map(|image| image.data.clone()).collect::<Vec<_>>();
        self.update_modified_images_annotations(&images, render_state);
    }

    fn update_modified_images_annotations(
        &self,
        images: &[Arc<Mutex<ModifiedImage>>],
        render_state: Option<&egui_wgpu::RenderState>,
    ) {
        let Some(render_state) = render_state else {
            return;
        };
        let mut renderer = render_state.renderer.write();
        let Some(annotation_renderer) = renderer.callback_resources.get_mut::<AnnotationRenderer>() else {
            tracing::warn!("missing AnnotationRenderer callback resource");
            return;
        };
        for image in images {
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

    fn open_image(&self) {
        let paths = rfd::FileDialog::new()
            .set_title("Open Image")
            .add_filter(
                "Images",
                &["png", "jpg", "jpeg", "bmp", "gif", "tiff", "tif", "pnm", "tga"],
            )
            .pick_files();
        if let Some(paths) = paths {
            if let Ok(mut image_list) = self.image_list.lock() {
                image_list.add_image_paths(paths);
            }
        }
    }

    fn save_pending_images_with_dialog(&self, images: Vec<PendingImageChange>) -> bool {
        for image in images {
            let (has_changes, suggested_path) = image
                .data
                .lock()
                .ok()
                .map(|image| (image.has_pending_changes(), image.source_path().map(PathBuf::from)))
                .unwrap_or((false, None));
            if !has_changes {
                continue;
            }
            let Some(path) = choose_save_path(&image.name, suggested_path.as_deref()) else {
                return false;
            };
            if let Ok(mut image) = image.data.lock() {
                if let Err(err) = image.save_changes(Some(&path)) {
                    tracing::warn!(error = %err, "failed to save image edits");
                    return false;
                }
            }
        }
        self.clear_missing_annotation_selection();
        true
    }

    fn update_editor_state(&self) {
        let images = self.visible_modified_images();
        let has_changes = images
            .iter()
            .any(|image| image.lock().is_ok_and(|image| image.has_pending_changes()));
        let can_undo = images
            .iter()
            .any(|image| image.lock().is_ok_and(|image| image.can_undo()));
        let has_selection = self
            .annotation_tool
            .lock()
            .is_ok_and(|tool| tool.selected_id_is_valid());
        if let Ok(mut state) = self.editor_state.lock() {
            state.has_changes = has_changes;
            state.can_undo = can_undo;
            state.has_selection = has_selection;
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

fn ensure_viewport_can_fit_confirmation(ctx: &egui::Context) {
    let current_size = ctx.input(|input| input.viewport().inner_rect.map(|rect| rect.size()));
    let Some(current_size) = current_size else {
        return;
    };
    if current_size.x >= CONFIRMATION_MIN_INNER_SIZE.x && current_size.y >= CONFIRMATION_MIN_INNER_SIZE.y {
        return;
    }
    ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(egui::vec2(
        current_size.x.max(CONFIRMATION_MIN_INNER_SIZE.x),
        current_size.y.max(CONFIRMATION_MIN_INNER_SIZE.y),
    )));
}

fn pending_image_change(index: usize, name: String, data: Arc<Mutex<ModifiedImage>>) -> PendingImageChange {
    PendingImageChange { index, name, data }
}

fn choose_save_path(image_name: &str, suggested_path: Option<&Path>) -> Option<PathBuf> {
    let mut dialog = rfd::FileDialog::new()
        .set_title(format!("Save Image - {image_name}"))
        .add_filter("PNG", &["png"])
        .add_filter("JPEG", &["jpg", "jpeg"])
        .add_filter("BMP", &["bmp"])
        .add_filter("GIF", &["gif"])
        .add_filter("PNM", &["pnm", "pgm"])
        .add_filter("TIFF", &["tiff", "tif"])
        .add_filter("TGA", &["tga"]);
    if let Some(path) = suggested_path {
        if let Some(parent) = path.parent() {
            dialog = dialog.set_directory(parent);
        }
        if let Some(file_name) = path.file_name().and_then(|name| name.to_str()) {
            dialog = dialog.set_file_name(file_name);
        }
    } else {
        dialog = dialog.set_file_name("new_image.png");
    }
    dialog.save_file()
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
