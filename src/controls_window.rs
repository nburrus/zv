use std::borrow::Cow;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use eframe::egui;
use eframe::egui_wgpu;
use egui_extras::{Column, TableBuilder};
use egui_phosphor::regular as ph;

use crate::annotation_tool::{AnnotationMode, AnnotationTool};
use crate::annotations::{
    AnnotationElement, AnnotationId, AnnotationKind, LineEndpointStyle, LineStyle, StrokeStyle, TextStyle,
};
use crate::color_editor_ui::{ColorEditorUiState, render_color_editor_tab};
use crate::color_image::{
    PixelSRGBA, closest_color_entries, convert_srgba_to_lab, convert_srgba_to_linear_rgb, convert_srgba_to_xyz,
};
use crate::image_list::ImageList;
use crate::image_window::CursorPixelInfo;
use crate::image_window_geometry::WindowResizeAction;
use crate::layout::LAYOUT_MENU_ENTRIES;
use crate::modified_image::ModifiedImage;
use crate::render::{ColorPreview, WgpuImageCallback};
use crate::shortcuts::{ShortcutViewport, collect_shortcuts};
use crate::viewer::{AppAction, ImageEditorState};

const CONTROLS_WIDTH: f32 = 640.0;
const CONTROLS_HEIGHT: f32 = 640.0;
const CONTROLS_MIN_WIDTH: f32 = 280.0;
const CONTROLS_MIN_HEIGHT: f32 = 300.0;
const CONTROLS_WIDTH_WITH_PADDING: f32 = CONTROLS_WIDTH + 12.0;
const CONTROLS_GAP: f32 = 8.0;
const CURSOR_ROI_PIXELS: f32 = 15.0;
const CURSOR_OVERLAY_EM_SCALE: f32 = 1.12;
const MODIFIER_TOOL_BUTTON_SIZE: egui::Vec2 = egui::vec2(36.0, 28.0);
const MODIFIER_TOOL_ICON_SIZE: f32 = 18.0;
const LINE_STYLE_COLOR_SWATCH_WIDTH: f32 = 40.0;
const LINE_STYLE_WIDTH_VALUE_WIDTH: f32 = 40.0;
const ICON_ROTATE_LEFT: &str = ph::ARROW_COUNTER_CLOCKWISE;
const ICON_ROTATE_RIGHT: &str = ph::ARROW_CLOCKWISE;
const ICON_LINE: &str = ph::LINE_SEGMENT;

fn command_shortcut(key: char) -> String {
    let modifier = if cfg!(target_os = "macos") { "Cmd" } else { "Ctrl" };
    format!("{modifier}+{key}")
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum ControlsTab {
    ImageList,
    Modifiers,
    ColorEditor,
}

#[derive(Clone)]
struct ImageDragPayload {
    index: usize,
}

#[derive(Clone, Debug)]
struct AnnotationStyleEditState {
    selected_id: AnnotationId,
    before: AnnotationElement,
    color_popup_was_open: bool,
}

#[derive(Clone, Copy, Debug, Default)]
struct StyleControlResponse {
    changed: bool,
    committed: bool,
    color_popup_open: bool,
}

struct ControlsUiState {
    active_tab: ControlsTab,
    size_texts: [String; 2],
    size_being_edited: [bool; 2],
    lock_ratio: bool,
    annotation_style_edit: Option<AnnotationStyleEditState>,
}

impl Default for ControlsUiState {
    fn default() -> Self {
        Self {
            active_tab: ControlsTab::ImageList,
            size_texts: [String::new(), String::new()],
            size_being_edited: [false, false],
            lock_ratio: true,
            annotation_style_edit: None,
        }
    }
}

pub struct ControlsWindow {
    viewport_id: egui::ViewportId,
    image_list: Arc<Mutex<ImageList>>,
    cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
    image_widget_size: Arc<Mutex<Option<(u32, u32)>>>,
    action_queue: Arc<Mutex<Vec<AppAction>>>,
    annotation_tool: Arc<Mutex<AnnotationTool>>,
    editor_state: Arc<Mutex<ImageEditorState>>,
    ui_state: Arc<Mutex<ControlsUiState>>,
    color_editor_state: Arc<Mutex<ColorEditorUiState>>,
    enabled: bool,
    target_position: Option<egui::Pos2>,
    has_ever_been_shown: bool,
    apply_initial_position_on_show: bool,
    focus_on_show: bool,
    close_requested: Arc<AtomicBool>,
    last_auto_scrolled_selected: Arc<Mutex<Option<usize>>>,
}

impl ControlsWindow {
    pub fn new(
        image_list: Arc<Mutex<ImageList>>,
        cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
        image_widget_size: Arc<Mutex<Option<(u32, u32)>>>,
        action_queue: Arc<Mutex<Vec<AppAction>>>,
        annotation_tool: Arc<Mutex<AnnotationTool>>,
        editor_state: Arc<Mutex<ImageEditorState>>,
    ) -> Self {
        Self {
            viewport_id: egui::ViewportId::from_hash_of("zv-controls-window"),
            image_list,
            cursor_info,
            image_widget_size,
            action_queue,
            annotation_tool,
            editor_state,
            ui_state: Arc::new(Mutex::new(ControlsUiState::default())),
            color_editor_state: Arc::new(Mutex::new(ColorEditorUiState::default())),
            enabled: false,
            target_position: None,
            has_ever_been_shown: false,
            apply_initial_position_on_show: false,
            focus_on_show: false,
            close_requested: Arc::new(AtomicBool::new(false)),
            last_auto_scrolled_selected: Arc::new(Mutex::new(None)),
        }
    }

    pub fn toggle(&mut self) {
        self.enabled = !self.enabled;
        if self.enabled {
            self.apply_initial_position_on_show = !self.has_ever_been_shown;
            self.has_ever_been_shown = true;
            self.focus_on_show = true;
        }
    }

    pub fn show_color_editor(&mut self) {
        let already_visible = self.enabled
            && self
                .ui_state
                .lock()
                .is_ok_and(|state| state.active_tab == ControlsTab::ColorEditor);
        if already_visible {
            return;
        }
        if let Ok(mut state) = self.ui_state.lock() {
            state.active_tab = ControlsTab::ColorEditor;
        }
        if !self.enabled {
            self.enabled = true;
            self.apply_initial_position_on_show = !self.has_ever_been_shown;
            self.has_ever_been_shown = true;
        }
        self.focus_on_show = true;
    }

    pub fn set_target_position(&mut self, position: Option<egui::Pos2>) {
        self.target_position = position;
    }

    pub fn viewport_id(&self) -> egui::ViewportId {
        self.viewport_id
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    pub fn target_position(&self) -> Option<egui::Pos2> {
        self.target_position
    }

    pub fn consume_close_request(&mut self) {
        if self.close_requested.swap(false, Ordering::AcqRel) {
            self.enabled = false;
        }
    }

    /// The pending preview, or `None` whenever the color editor is not on screen.
    /// A preview only exists as a shader effect on the main image, so leaving one
    /// live after the tab is hidden would show an adjustment the user can neither
    /// see the controls for nor undo, since it never reached the stored pixels.
    pub fn color_preview(&self) -> ColorPreview {
        if !self.enabled {
            return ColorPreview::None;
        }
        let showing_color_editor = self
            .ui_state
            .lock()
            .is_ok_and(|state| state.active_tab == ControlsTab::ColorEditor);
        if !showing_color_editor {
            return ColorPreview::None;
        }
        self.color_editor_state
            .lock()
            .map(|state| state.color_preview())
            .unwrap_or_default()
    }

    // Note: monitor_size has no origin; outer_rect.min may be negative or past
    // monitor_size.x on multi-monitor setups. This logic assumes a single screen.
    pub fn position_for_image_window(viewer_outer_rect: egui::Rect, monitor_size: egui::Vec2) -> Option<egui::Pos2> {
        if viewer_outer_rect.min.x > CONTROLS_WIDTH_WITH_PADDING {
            Some(egui::pos2(
                viewer_outer_rect.min.x - CONTROLS_WIDTH_WITH_PADDING,
                viewer_outer_rect.min.y,
            ))
        } else if monitor_size.x - viewer_outer_rect.min.x - viewer_outer_rect.width() > CONTROLS_WIDTH_WITH_PADDING {
            Some(egui::pos2(
                viewer_outer_rect.min.x + viewer_outer_rect.width() + CONTROLS_GAP,
                viewer_outer_rect.min.y,
            ))
        } else {
            None
        }
    }

    pub fn show(&mut self, ctx: &egui::Context) {
        let image_list = self.image_list.clone();
        let cursor_info = self.cursor_info.clone();
        let image_widget_size = self.image_widget_size.clone();
        let last_auto_scrolled_selected = self.last_auto_scrolled_selected.clone();
        let ui_state = self.ui_state.clone();
        let color_editor_state = self.color_editor_state.clone();
        let action_queue = self.action_queue.clone();
        let annotation_tool = self.annotation_tool.clone();
        let editor_state = self.editor_state.clone();
        let close_requested = self.close_requested.clone();
        let mut builder = egui::ViewportBuilder::default()
            .with_title("zv controls")
            .with_inner_size(egui::vec2(CONTROLS_WIDTH, CONTROLS_HEIGHT))
            .with_min_inner_size(egui::vec2(CONTROLS_MIN_WIDTH, CONTROLS_MIN_HEIGHT))
            .with_resizable(true)
            .with_visible(self.enabled);
        let apply_initial_position_on_show = self.apply_initial_position_on_show;
        self.apply_initial_position_on_show = false;
        if apply_initial_position_on_show {
            if let Some(position) = self.target_position {
                builder = builder.with_position(position);
            }
        }

        ctx.show_viewport_deferred(self.viewport_id, builder, move |ctx, class| {
            if class == egui::ViewportClass::Deferred && ctx.input(|input| input.viewport().close_requested()) {
                ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
                close_requested.store(true, Ordering::Release);
                ctx.request_repaint_of(egui::ViewportId::ROOT);
            }

            apply_controls_visuals(ctx);
            let new_actions = collect_shortcuts(ctx, ShortcutViewport::Controls);
            if !new_actions.is_empty() {
                for action in new_actions {
                    push_action(&action_queue, action);
                }
                // Viewer::update runs on the root viewport; wake it so queued
                // controls actions are applied even when only controls is focused.
                // ROOT corresponds to the main image viewport/window.
                ctx.request_repaint_of(egui::ViewportId::ROOT);
            }

            egui::TopBottomPanel::top("menu_bar").show(ctx, |ui| {
                let ed = editor_state.lock().map(|g| g.clone()).unwrap_or_default();
                egui::MenuBar::new().ui(ui, |ui| {
                    ui.menu_button("File", |ui| {
                        if ui
                            .add(egui::Button::new("Open Image…").shortcut_text(command_shortcut('O')))
                            .clicked()
                        {
                            push_root_action(ctx, &action_queue, AppAction::OpenImage);
                            ui.close();
                        }
                        ui.separator();
                        if ui
                            .add_enabled(
                                ed.has_changes,
                                egui::Button::new("Save Image…").shortcut_text(command_shortcut('S')),
                            )
                            .clicked()
                        {
                            push_root_action(ctx, &action_queue, AppAction::SaveImageEdits);
                            ui.close();
                        }
                        ui.separator();
                        if ui
                            .add(egui::Button::new("Close Image").shortcut_text(command_shortcut('W')))
                            .clicked()
                        {
                            push_root_action(ctx, &action_queue, AppAction::CloseImage);
                            ui.close();
                        }
                        if ui
                            .add(egui::Button::new("Delete Image on Disk").shortcut_text("Shift+Del"))
                            .clicked()
                        {
                            push_root_action(ctx, &action_queue, AppAction::DeleteImageOnDisk);
                            ui.close();
                        }
                        ui.separator();
                        if ui.add(egui::Button::new("Close").shortcut_text("q")).clicked() {
                            push_root_action(ctx, &action_queue, AppAction::Quit);
                            ui.close();
                        }
                    });
                    ui.menu_button("Edit", |ui| {
                        if ui
                            .add_enabled(
                                ed.can_undo,
                                egui::Button::new("Undo").shortcut_text(command_shortcut('Z')),
                            )
                            .clicked()
                        {
                            push_root_action(ctx, &action_queue, AppAction::UndoImageEdit);
                            ui.close();
                        }
                        if ui
                            .add_enabled(ed.has_changes, egui::Button::new("Revert to Original"))
                            .clicked()
                        {
                            push_root_action(ctx, &action_queue, AppAction::DiscardImageEdits);
                            ui.close();
                        }
                        ui.separator();
                        if ui
                            .add(egui::Button::new("Copy to Clipboard").shortcut_text(command_shortcut('C')))
                            .clicked()
                        {
                            push_root_action(ctx, &action_queue, AppAction::CopyImageToClipboard);
                            ui.close();
                        }
                        if ui
                            .add(egui::Button::new("New from Clipboard").shortcut_text(command_shortcut('N')))
                            .clicked()
                        {
                            push_root_action(ctx, &action_queue, AppAction::PasteImageFromClipboard);
                            ui.close();
                        }
                        ui.separator();
                        if ui
                            .add_enabled(
                                ed.has_selection,
                                egui::Button::new("Delete Selected Annotation").shortcut_text("Del"),
                            )
                            .clicked()
                        {
                            push_root_action(ctx, &action_queue, AppAction::DeleteSelectedAnnotation);
                            ui.close();
                        }
                    });
                    ui.menu_button("Tools", |ui| {
                        if ui.add(egui::Button::new("Color Editor").shortcut_text("e")).clicked() {
                            push_root_action(ctx, &action_queue, AppAction::ShowColorEditor);
                            ui.close();
                        }
                        ui.separator();
                        let mode = annotation_tool
                            .lock()
                            .ok()
                            .map(|tool| tool.mode())
                            .unwrap_or(AnnotationMode::Select);
                        ui.menu_button("Annotate", |ui| {
                            if ui
                                .add(
                                    egui::Button::new("Add Line")
                                        .shortcut_text("Shift+L")
                                        .selected(mode == AnnotationMode::AddLine),
                                )
                                .clicked()
                            {
                                push_root_action(
                                    ctx,
                                    &action_queue,
                                    AppAction::SetAnnotationMode(AnnotationMode::AddLine),
                                );
                                ui.close();
                            }
                            if ui
                                .add(
                                    egui::Button::new("Add Text")
                                        .shortcut_text("Shift+T")
                                        .selected(mode == AnnotationMode::AddText),
                                )
                                .clicked()
                            {
                                push_root_action(
                                    ctx,
                                    &action_queue,
                                    AppAction::SetAnnotationMode(AnnotationMode::AddText),
                                );
                                ui.close();
                            }
                            if ui
                                .add(
                                    egui::Button::new("Add Rectangle")
                                        .shortcut_text("Shift+R")
                                        .selected(mode == AnnotationMode::AddRectangle),
                                )
                                .clicked()
                            {
                                push_root_action(
                                    ctx,
                                    &action_queue,
                                    AppAction::SetAnnotationMode(AnnotationMode::AddRectangle),
                                );
                                ui.close();
                            }
                            if ui
                                .add(
                                    egui::Button::new("Add Ellipse")
                                        .shortcut_text("Shift+E")
                                        .selected(mode == AnnotationMode::AddEllipse),
                                )
                                .clicked()
                            {
                                push_root_action(
                                    ctx,
                                    &action_queue,
                                    AppAction::SetAnnotationMode(AnnotationMode::AddEllipse),
                                );
                                ui.close();
                            }
                            if ui
                                .add(
                                    egui::Button::new("Add Arrow")
                                        .shortcut_text("Shift+A")
                                        .selected(mode == AnnotationMode::AddArrow),
                                )
                                .clicked()
                            {
                                push_root_action(
                                    ctx,
                                    &action_queue,
                                    AppAction::SetAnnotationMode(AnnotationMode::AddArrow),
                                );
                                ui.close();
                            }
                        });
                    });
                    ui.menu_button("Window", |ui| {
                        ui.menu_button("Layout", |ui| {
                            if ui
                                .add(egui::Button::new("Automatic mosaic").shortcut_text("0"))
                                .clicked()
                            {
                                push_root_action(ctx, &action_queue, AppAction::AutoLayout);
                                ui.close();
                            }
                            for entry in LAYOUT_MENU_ENTRIES {
                                let button = if let Some(shortcut) = entry.shortcut {
                                    egui::Button::new(entry.label).shortcut_text(shortcut)
                                } else {
                                    egui::Button::new(entry.label)
                                };
                                if ui.add(button).clicked() {
                                    push_root_action(ctx, &action_queue, AppAction::SetLayout(entry.config));
                                    ui.close();
                                }
                            }
                        });
                    });
                    ui.menu_button("Help", |_ui| {});
                });
            });

            egui::TopBottomPanel::bottom("size_footer").show(ctx, |ui| {
                render_size_footer(ui, &image_widget_size, &ui_state, &action_queue, ctx);
            });

            egui::CentralPanel::default().show(ctx, |ui| {
                let active_tab = render_tabs(ui, &ui_state);
                ui.separator();
                ui.add_space(ui.spacing().item_spacing.y);
                match active_tab {
                    ControlsTab::ImageList => {
                        render_image_list_tab(ui, ctx, &image_list, &cursor_info, &last_auto_scrolled_selected);
                    }
                    ControlsTab::Modifiers => {
                        render_annotation_tools_tab(ui, &image_list, &annotation_tool, &ui_state, &action_queue, ctx)
                    }
                    ControlsTab::ColorEditor => {
                        render_color_editor_tab(ui, ctx, &image_list, &color_editor_state, &action_queue);
                    }
                }
            });
        });

        if self.focus_on_show {
            self.focus_on_show = false;
            ctx.send_viewport_cmd_to(self.viewport_id, egui::ViewportCommand::Focus);
        }
    }
}

fn apply_controls_visuals(ctx: &egui::Context) {
    ctx.set_visuals(egui::Visuals::dark());
}

fn render_tabs(ui: &mut egui::Ui, ui_state: &Arc<Mutex<ControlsUiState>>) -> ControlsTab {
    if let Ok(mut state) = ui_state.lock() {
        let previous_tab = state.active_tab;
        ui.horizontal(|ui| {
            ui.selectable_value(&mut state.active_tab, ControlsTab::ImageList, "Image List");
            ui.selectable_value(&mut state.active_tab, ControlsTab::Modifiers, "Modifiers");
            ui.selectable_value(&mut state.active_tab, ControlsTab::ColorEditor, "Color Editor");
        });
        if state.active_tab != previous_tab {
            // Leaving or entering the color editor tab turns its pending preview off
            // or back on, which only the main image viewport can repaint.
            ui.ctx().request_repaint_of(egui::ViewportId::ROOT);
        }
        state.active_tab
    } else {
        ControlsTab::ImageList
    }
}

fn render_annotation_tools_tab(
    ui: &mut egui::Ui,
    image_list: &Arc<Mutex<ImageList>>,
    annotation_tool: &Arc<Mutex<AnnotationTool>>,
    ui_state: &Arc<Mutex<ControlsUiState>>,
    action_queue: &Arc<Mutex<Vec<AppAction>>>,
    ctx: &egui::Context,
) {
    let Ok(mut tool) = annotation_tool.lock() else {
        ui.colored_label(egui::Color32::RED, "annotation tool lock is poisoned");
        return;
    };
    let mode = tool.mode();

    // Transform toolbar.
    ui.horizontal(|ui| {
        if ui
            .add(modifier_tool_button(ICON_ROTATE_LEFT))
            .on_hover_text("Rotate Left (−90°)")
            .clicked()
        {
            push_root_action(ctx, action_queue, AppAction::RotateLeft);
        }
        if ui
            .add(modifier_tool_button(ICON_ROTATE_RIGHT))
            .on_hover_text("Rotate Right (+90°)")
            .clicked()
        {
            push_root_action(ctx, action_queue, AppAction::RotateRight);
        }
        ui.separator();
        // Annotation toolbar — one button per implemented type.
        let is_line = mode == AnnotationMode::AddLine;
        let line_btn = modifier_tool_button(ICON_LINE).selected(is_line);
        if ui.add(line_btn).on_hover_text("Add Line (Shift+L)").clicked() {
            let next_mode = if is_line {
                AnnotationMode::Select
            } else {
                AnnotationMode::AddLine
            };
            push_root_action(ctx, action_queue, AppAction::SetAnnotationMode(next_mode));
        }
        let is_arrow = mode == AnnotationMode::AddArrow;
        if arrow_tool_button(ui, is_arrow)
            .on_hover_text("Add Arrow (Shift+A)")
            .clicked()
        {
            let next_mode = if is_arrow {
                AnnotationMode::Select
            } else {
                AnnotationMode::AddArrow
            };
            push_root_action(ctx, action_queue, AppAction::SetAnnotationMode(next_mode));
        }
        for (annotation_mode, label, tooltip) in [
            (AnnotationMode::AddRectangle, "▭", "Add Rectangle (Shift+R)"),
            (AnnotationMode::AddEllipse, "○", "Add Ellipse (Shift+E)"),
            (AnnotationMode::AddText, "T", "Add Text (Shift+T)"),
        ] {
            let selected = mode == annotation_mode;
            if ui
                .add(
                    egui::Button::new(egui::RichText::new(label).size(MODIFIER_TOOL_ICON_SIZE))
                        .min_size(MODIFIER_TOOL_BUTTON_SIZE)
                        .selected(selected),
                )
                .on_hover_text(tooltip)
                .clicked()
            {
                push_root_action(
                    ctx,
                    action_queue,
                    AppAction::SetAnnotationMode(if selected {
                        AnnotationMode::Select
                    } else {
                        annotation_mode
                    }),
                );
            }
        }
    });

    ui.separator();

    let mut ui_state = ui_state.lock().ok();
    let selected_id = tool.selected_id();
    let selected_element = selected_element_data(image_list, selected_id);
    let selected_kind = selected_element.as_ref().map(AnnotationElement::kind);

    let show_line_panel = matches!(mode, AnnotationMode::AddLine | AnnotationMode::AddArrow)
        || selected_kind == Some(AnnotationKind::Line);
    let show_box_panel = !show_line_panel
        && (matches!(mode, AnnotationMode::AddRectangle | AnnotationMode::AddEllipse)
            || matches!(selected_kind, Some(AnnotationKind::Rectangle | AnnotationKind::Ellipse)));
    let show_text_panel = !show_line_panel
        && !show_box_panel
        && (mode == AnnotationMode::AddText || selected_kind == Some(AnnotationKind::Text));
    if !show_line_panel && !show_box_panel && !show_text_panel {
        if let Some(state) = ui_state.as_deref_mut() {
            flush_annotation_style_edit(image_list, state);
        }
        return;
    }

    // Render the panel for the selected element, or for the defaults of the
    // annotation about to be created. The live-apply and undo choreography
    // below is shared by both panels.
    enum PanelEdit {
        Line(LineStyle),
        Stroke(StrokeStyle),
        Text(TextStyle),
    }
    let (response, edit, editing_selected) = if show_line_panel {
        let selected_style = selected_element
            .as_ref()
            .and_then(AnnotationElement::line_style)
            .copied();
        let mut style = selected_style.unwrap_or_else(|| tool.default_line_style());
        let response = render_line_controls(ui, &mut style, selected_style.is_some(), mode);
        (response, PanelEdit::Line(style), selected_style.is_some())
    } else if show_box_panel {
        let selected = selected_element
            .as_ref()
            .and_then(|element| Some((element.kind(), *element.stroke()?)));
        let mut stroke = selected.map_or_else(|| tool.default_stroke(), |(_, stroke)| stroke);
        let kind = selected.map_or(
            if mode == AnnotationMode::AddEllipse {
                AnnotationKind::Ellipse
            } else {
                AnnotationKind::Rectangle
            },
            |(kind, _)| kind,
        );
        let response = render_box_annotation_controls(ui, &mut stroke, selected.is_some(), kind);
        (response, PanelEdit::Stroke(stroke), selected.is_some())
    } else {
        let selected = selected_element.as_ref().and_then(|element| match element {
            AnnotationElement::Text { style, .. } => Some(style.clone()),
            _ => None,
        });
        let editing_selected = selected.is_some();
        let mut style = selected.unwrap_or_else(|| tool.default_text_style().clone());
        let response = render_text_controls(ui, &mut style, editing_selected);
        (response, PanelEdit::Text(style), editing_selected)
    };

    if response.changed {
        if editing_selected {
            // Capture the pre-edit element before mutating it so the whole
            // slider interaction can be undone as one step.
            if let Some(state) = ui_state.as_deref_mut() {
                begin_annotation_style_edit(image_list, state, selected_id, response.color_popup_open);
            }
            match edit {
                PanelEdit::Line(style) => {
                    apply_selected_line_style_live(image_list, selected_id, style);
                    tool.set_default_stroke(style.stroke);
                }
                PanelEdit::Stroke(stroke) => {
                    apply_selected_stroke_live(image_list, selected_id, stroke);
                    tool.set_default_stroke(stroke);
                }
                PanelEdit::Text(style) => {
                    apply_selected_text_live(image_list, selected_id, style.clone());
                    tool.set_default_text_style(style);
                }
            }
            ctx.request_repaint_of(egui::ViewportId::ROOT);
        } else {
            match edit {
                PanelEdit::Line(style) => tool.set_default_line_style(style),
                PanelEdit::Stroke(stroke) => tool.set_default_stroke(stroke),
                PanelEdit::Text(style) => tool.set_default_text_style(style),
            }
        }
    }
    if editing_selected
        && let Some(state) = ui_state.as_deref_mut()
        && let Some(edit_state) = state.annotation_style_edit.as_mut()
    {
        let color_popup_closed = edit_state.color_popup_was_open && !response.color_popup_open;
        edit_state.color_popup_was_open = response.color_popup_open;
        if response.committed || color_popup_closed {
            flush_annotation_style_edit(image_list, state);
        }
    }
}

fn modifier_tool_button(icon: &'static str) -> egui::Button<'static> {
    egui::Button::new(egui::RichText::new(icon).size(MODIFIER_TOOL_ICON_SIZE)).min_size(MODIFIER_TOOL_BUTTON_SIZE)
}

fn arrow_tool_button(ui: &mut egui::Ui, selected: bool) -> egui::Response {
    let response = ui.add(
        egui::Button::new("")
            .min_size(MODIFIER_TOOL_BUTTON_SIZE)
            .selected(selected),
    );
    let visuals = ui.style().interact_selectable(&response, selected);
    let center = response.rect.center();
    let start = center + egui::vec2(-6.0, 4.0);
    let tip = center + egui::vec2(6.0, -4.0);
    let direction = (tip - start).normalized();
    let perpendicular = egui::vec2(-direction.y, direction.x);
    let head_base = tip - direction * 5.0;
    let color = visuals.fg_stroke.color;
    let stroke_width = visuals.fg_stroke.width.max(1.5);

    ui.painter()
        .line_segment([start, tip], egui::Stroke::new(stroke_width, color));
    ui.painter().add(egui::Shape::convex_polygon(
        vec![tip, head_base + perpendicular * 3.0, head_base - perpendicular * 3.0],
        color,
        egui::Stroke::NONE,
    ));
    response
}

fn render_line_controls(
    ui: &mut egui::Ui,
    style: &mut LineStyle,
    selected: bool,
    mode: AnnotationMode,
) -> StyleControlResponse {
    let mut output = StyleControlResponse::default();

    let header = if selected {
        "Selected line  |  Delete / Backspace to remove"
    } else if mode == AnnotationMode::AddArrow {
        "New arrow style"
    } else {
        "New line style"
    };
    ui.label(egui::RichText::new(header).color(ui.visuals().weak_text_color()));

    render_stroke_controls(ui, "line_stroke_style_grid", &mut style.stroke, &mut output);
    egui::Grid::new("line_endpoint_style_grid")
        .num_columns(2)
        .show(ui, |ui| {
            render_line_endpoint_style_combo(ui, "Start", &mut style.start_style, &mut output);
            render_line_endpoint_style_combo(ui, "End", &mut style.end_style, &mut output);
            render_disabled_line_style_combo(ui, "Stroke", "Solid");
        });

    output
}

fn render_box_annotation_controls(
    ui: &mut egui::Ui,
    stroke: &mut StrokeStyle,
    selected: bool,
    kind: AnnotationKind,
) -> StyleControlResponse {
    let mut output = StyleControlResponse::default();
    let kind_label = match kind {
        AnnotationKind::Rectangle => "rectangle",
        AnnotationKind::Ellipse => "ellipse",
        AnnotationKind::Line => "line",
        AnnotationKind::Text => "text",
    };
    let header = if selected {
        format!("Selected {kind_label}  |  Delete / Backspace to remove")
    } else {
        format!("New {kind_label} style")
    };
    ui.label(egui::RichText::new(header).color(ui.visuals().weak_text_color()));

    render_stroke_controls(ui, "box_annotation_stroke_style_grid", stroke, &mut output);
    output
}

fn render_text_controls(ui: &mut egui::Ui, style: &mut TextStyle, selected: bool) -> StyleControlResponse {
    let mut output = StyleControlResponse::default();
    let header = if selected {
        "Selected text  |  Delete / Backspace to remove"
    } else {
        "New text style"
    };
    ui.label(egui::RichText::new(header).color(ui.visuals().weak_text_color()));

    let text_response = ui.add(
        egui::TextEdit::multiline(&mut style.text)
            .desired_rows(4)
            .desired_width(f32::INFINITY),
    );
    output.changed |= text_response.changed();
    output.committed |= text_response.lost_focus();

    egui::Grid::new("text_annotation_style_grid")
        .num_columns(2)
        .show(ui, |ui| {
            render_style_row(ui, "Color", |ui, width| {
                color_row_controls(ui, width, &mut style.color, &mut output);
            });
            render_style_row(ui, "Font Size", |ui, width| {
                ui.spacing_mut().slider_width = width - LINE_STYLE_WIDTH_VALUE_WIDTH - ui.spacing().item_spacing.x;
                let response = ui.add(egui::Slider::new(&mut style.font_size, 8.0..=96.0).show_value(false));
                ui.label(format!("{:.1}", style.font_size));
                output.changed |= response.changed();
                output.committed |= response.lost_focus() || response.drag_stopped();
            });
        });
    output
}

fn render_stroke_controls(
    ui: &mut egui::Ui,
    grid_id: &'static str,
    stroke: &mut StrokeStyle,
    output: &mut StyleControlResponse,
) {
    egui::Grid::new(grid_id).num_columns(2).show(ui, |ui| {
        render_style_row(ui, "Color", |ui, width| {
            color_row_controls(ui, width, &mut stroke.color, output);
        });

        render_style_row(ui, "Width", |ui, width| {
            ui.spacing_mut().slider_width = width - LINE_STYLE_WIDTH_VALUE_WIDTH - ui.spacing().item_spacing.x;
            let response = ui.add(egui::Slider::new(&mut stroke.width, 1.0..=10.0).show_value(false));
            ui.add_sized(
                [LINE_STYLE_WIDTH_VALUE_WIDTH, ui.spacing().interact_size.y],
                egui::Label::new(format!("{:.1}", stroke.width)),
            );
            output.changed |= response.changed();
            output.committed |= response.drag_stopped() || response.lost_focus();
        });
    });
}

/// Color swatch plus R/G/B drag fields, shared by every style panel.
fn color_row_controls(ui: &mut egui::Ui, width: f32, color: &mut egui::Color32, output: &mut StyleControlResponse) {
    let mut rgb = [color.r() as i32, color.g() as i32, color.b() as i32];
    let response = ui.color_edit_button_srgba(color);
    output.color_popup_open = egui::Popup::is_id_open(ui.ctx(), response.id.with("popup"));
    if response.changed() {
        rgb = [color.r() as i32, color.g() as i32, color.b() as i32];
        output.changed = true;
    }
    if response.drag_stopped()
        || response.lost_focus()
        || (response.changed() && ui.input(|input| input.pointer.any_released()))
    {
        output.committed = true;
    }

    let field_width = ((width - LINE_STYLE_COLOR_SWATCH_WIDTH - ui.spacing().item_spacing.x * 3.0) / 3.0).max(64.0);
    let mut rgb_changed = false;
    for (prefix, value) in ["R ", "G ", "B "].into_iter().zip(rgb.iter_mut()) {
        rgb_changed |= ui
            .add_sized(
                [field_width, ui.spacing().interact_size.y],
                egui::DragValue::new(value).range(0..=255).speed(1).prefix(prefix),
            )
            .changed();
    }
    if rgb_changed {
        *color = egui::Color32::from_rgb(rgb[0] as u8, rgb[1] as u8, rgb[2] as u8);
        output.changed = true;
        output.committed = true;
    }
}

fn render_style_row(ui: &mut egui::Ui, label: &'static str, add_control: impl FnOnce(&mut egui::Ui, f32)) {
    ui.label(label);
    let control_width = ui.available_width();
    ui.horizontal(|ui| add_control(ui, control_width));
    ui.end_row();
}

fn render_disabled_line_style_combo(ui: &mut egui::Ui, label: &'static str, value: &'static str) {
    render_style_row(ui, label, |ui, width| {
        ui.add_enabled_ui(false, |ui| {
            egui::ComboBox::from_id_salt(label)
                .selected_text(value)
                .width(width)
                .show_ui(ui, |_ui| {});
        });
    });
}

fn render_line_endpoint_style_combo(
    ui: &mut egui::Ui,
    label: &'static str,
    style: &mut LineEndpointStyle,
    output: &mut StyleControlResponse,
) {
    render_style_row(ui, label, |ui, width| {
        let before = *style;
        egui::ComboBox::from_id_salt(label)
            .selected_text(style.label())
            .width(width)
            .show_ui(ui, |ui| {
                ui.selectable_value(style, LineEndpointStyle::None, LineEndpointStyle::None.label());
                ui.selectable_value(style, LineEndpointStyle::Arrow, LineEndpointStyle::Arrow.label());
            });
        if *style != before {
            output.changed = true;
            output.committed = true;
        }
    });
}

fn begin_annotation_style_edit(
    image_list: &Arc<Mutex<ImageList>>,
    state: &mut ControlsUiState,
    selected_id: AnnotationId,
    color_popup_open: bool,
) {
    if state
        .annotation_style_edit
        .as_ref()
        .is_none_or(|edit| edit.selected_id != selected_id)
    {
        flush_annotation_style_edit(image_list, state);
        if let Some(before) = selected_element_data(image_list, selected_id) {
            state.annotation_style_edit = Some(AnnotationStyleEditState {
                selected_id,
                before,
                color_popup_was_open: color_popup_open,
            });
        }
    }
}

fn flush_annotation_style_edit(image_list: &Arc<Mutex<ImageList>>, state: &mut ControlsUiState) {
    if let Some(edit) = state.annotation_style_edit.take() {
        for image in visible_modified_images(image_list) {
            if let Ok(mut image) = image.lock()
                && image.annotations().find_by_id(edit.selected_id) != Some(&edit.before)
            {
                image.push_undo_action(crate::modified_image::ImageUndoAction::RestoreElementState {
                    element: edit.before.clone(),
                });
            }
        }
    }
}

fn selected_element_data(image_list: &Arc<Mutex<ImageList>>, selected_id: AnnotationId) -> Option<AnnotationElement> {
    if !selected_id.is_valid() {
        return None;
    }
    visible_modified_images(image_list)
        .into_iter()
        .find_map(|image| image.lock().ok()?.annotations().find_by_id(selected_id).cloned())
}

fn apply_selected_line_style_live(image_list: &Arc<Mutex<ImageList>>, selected_id: AnnotationId, style: LineStyle) {
    for image in visible_modified_images(image_list) {
        if let Ok(mut image) = image.lock() {
            image.update_stroke_style(selected_id, style.stroke);
            image.update_line_endpoint_styles(selected_id, style.start_style, style.end_style);
        }
    }
}

fn apply_selected_stroke_live(image_list: &Arc<Mutex<ImageList>>, selected_id: AnnotationId, stroke: StrokeStyle) {
    for image in visible_modified_images(image_list) {
        if let Ok(mut image) = image.lock() {
            image.update_stroke_style(selected_id, stroke);
        }
    }
}

fn apply_selected_text_live(image_list: &Arc<Mutex<ImageList>>, selected_id: AnnotationId, style: TextStyle) {
    for image in visible_modified_images(image_list) {
        if let Ok(mut image) = image.lock() {
            image.update_text(selected_id, style.clone());
        }
    }
}

fn visible_modified_images(image_list: &Arc<Mutex<ImageList>>) -> Vec<Arc<Mutex<ModifiedImage>>> {
    image_list
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

fn render_image_list_tab(
    ui: &mut egui::Ui,
    ctx: &egui::Context,
    image_list: &Arc<Mutex<ImageList>>,
    cursor_info: &Arc<Mutex<Option<CursorPixelInfo>>>,
    last_auto_scrolled_selected: &Arc<Mutex<Option<usize>>>,
) {
    let cursor = cursor_info.lock().ok().and_then(|info| info.clone());
    let font_size = cursor_overlay_em(ui);
    let overlay_height = cursor.as_ref().map_or(0.0, |_| font_size * 13.5);

    render_filter_row(ui, image_list, ctx);
    ui.add_space(ui.spacing().item_spacing.y);
    let table_height = (ui.available_height() - overlay_height).max(80.0);
    if let Ok(mut images) = image_list.lock() {
        render_image_list(ui, &mut images, last_auto_scrolled_selected, ctx, table_height);
    } else {
        ui.colored_label(egui::Color32::RED, "image list lock is poisoned");
    }

    if let Some(cursor) = cursor {
        render_cursor_overlay(ui, &cursor, overlay_height);
    }
}

fn render_filter_row(ui: &mut egui::Ui, image_list: &Arc<Mutex<ImageList>>, ctx: &egui::Context) {
    if let Ok(mut images) = image_list.lock() {
        let mut filter_text = images.filter_text().to_owned();
        let response = ui.add(
            egui::TextEdit::singleline(&mut filter_text)
                .hint_text("Filter files")
                .desired_width(f32::INFINITY),
        );
        if response.changed() {
            images.set_filter(filter_text);
            ctx.request_repaint_of(egui::ViewportId::ROOT);
        }
    } else {
        ui.colored_label(egui::Color32::RED, "image list lock is poisoned");
    }
}

struct CollectedRow {
    index: usize,
    selected: bool,
    name: String,
    hover_text: Option<String>,
    size_text: String,
    has_changes: bool,
}

fn render_image_list(
    ui: &mut egui::Ui,
    image_list: &mut ImageList,
    last_auto_scrolled_selected: &Arc<Mutex<Option<usize>>>,
    ctx: &egui::Context,
    table_height: f32,
) {
    let row_height = 22.0;
    let header_height = 22.0;
    let size_col_width = 96.0;
    // Floor for the Name column: enough to show ~32 elided characters. Below this
    // the column refuses to shrink further; above this, the cell paints the name
    // manually so it never widens max_used_widths and lets the column shrink freely.
    // Approximate average proportional-char width as 0.55 * font height — good enough,
    // and cheaper than laying out a real galley per row.
    let name_font = egui::TextStyle::Body.resolve(ui.style());
    let approx_char_width = name_font.size * 0.55;
    let name_col_min_width = approx_char_width * 24.0;
    // Collect rows to release the borrow on image_list before the closures.
    let rows: Vec<CollectedRow> = image_list
        .visible_rows()
        .map(|row| CollectedRow {
            index: row.index,
            selected: row.selected,
            name: row.name.to_owned(),
            hover_text: row.display_path.map(|path| path.display().to_string()),
            size_text: row
                .size
                .map(|(w, h)| format!("{w}x{h}"))
                .unwrap_or_else(|| "(?x?)".to_owned()),
            has_changes: row.has_changes,
        })
        .collect();

    let mut pending_select: Option<usize> = None;
    let mut pending_move: Option<(usize, usize)> = None;

    // Capture these before TableBuilder takes &mut ui.
    let painter = ui.painter().clone();
    let pointer_pos = ui.input(|i| i.pointer.hover_pos());

    // Find the selected row's *visible* index (position within `rows`, not the
    // image_list index) so we can drive scroll_to_row in the virtualized table.
    let selected_visible_idx = rows.iter().position(|r| r.selected);
    let scroll_target = match (selected_visible_idx, last_auto_scrolled_selected.lock()) {
        (Some(visible_idx), Ok(mut last)) => {
            let image_idx = rows[visible_idx].index;
            if *last != Some(image_idx) {
                *last = Some(image_idx);
                Some(visible_idx)
            } else {
                None
            }
        }
        _ => None,
    };

    let mut table = TableBuilder::new(ui)
        .sense(egui::Sense::click_and_drag())
        .striped(true)
        .resizable(false)
        .auto_shrink([false, true])
        .cell_layout(egui::Layout::left_to_right(egui::Align::Center))
        .column(Column::remainder().clip(true).at_least(name_col_min_width))
        .column(Column::exact(size_col_width))
        .min_scrolled_height(0.0)
        .max_scroll_height(table_height - header_height);
    if let Some(idx) = scroll_target {
        table = table.scroll_to_row(idx, Some(egui::Align::Center));
    }
    table
        .header(header_height, |mut header| {
            header.col(|ui| {
                ui.strong("Name");
            });
            header.col(|ui| {
                ui.strong("Size");
            });
        })
        .body(|mut body| {
            if scroll_target.is_none() {
                auto_scroll_image_list_drag(body.ui_mut(), ctx, pointer_pos, row_height);
            }

            body.rows(row_height, rows.len(), |mut row| {
                let row_data = &rows[row.index()];
                row.set_selected(row_data.selected);

                row.col(|ui| {
                    let avail = ui.available_width();
                    let (display_name, text_color) = if row_data.has_changes {
                        let name = format!("* {}", row_data.name);
                        let color = egui::Color32::from_rgb(255, 200, 80);
                        (name, color)
                    } else {
                        (row_data.name.clone(), ui.visuals().text_color())
                    };
                    let elided = elide_by_char_count(&display_name, avail, approx_char_width);
                    let cursor = ui.cursor();
                    ui.painter().text(
                        egui::pos2(cursor.left(), cursor.top() + row_height * 0.5),
                        egui::Align2::LEFT_CENTER,
                        elided,
                        name_font.clone(),
                        text_color,
                    );
                });
                row.col(|ui| {
                    ui.add(
                        egui::Label::new(&row_data.size_text)
                            .sense(egui::Sense::empty())
                            .selectable(false)
                            .truncate(),
                    );
                });

                let response = row.response();

                if let Some(ref text) = row_data.hover_text {
                    response.clone().on_hover_text(text);
                }

                if response.clicked() {
                    pending_select = Some(row_data.index);
                }

                response.dnd_set_drag_payload(ImageDragPayload { index: row_data.index });

                if let Some(payload) = response.dnd_hover_payload::<ImageDragPayload>() {
                    if payload.index != row_data.index {
                        let insert_after = pointer_pos.is_some_and(|p| p.y > response.rect.center().y);
                        let y = if insert_after {
                            response.rect.bottom()
                        } else {
                            response.rect.top()
                        };
                        painter.hline(
                            response.rect.left()..=response.rect.right(),
                            y,
                            egui::Stroke::new(2.0, egui::Color32::from_rgb(100, 150, 255)),
                        );
                    }
                }

                if let Some(payload) = response.dnd_release_payload::<ImageDragPayload>() {
                    if payload.index != row_data.index {
                        let insert_after = pointer_pos.is_some_and(|p| p.y > response.rect.center().y);
                        let to = row_data.index + usize::from(insert_after);
                        pending_move = Some((payload.index, to));
                    }
                }
            });
        });

    if let Some((from, to)) = pending_move {
        image_list.move_item(from, to);
        ctx.request_repaint_of(egui::ViewportId::ROOT);
    } else if let Some(index) = pending_select {
        image_list.select_index(index);
        ctx.request_repaint_of(egui::ViewportId::ROOT);
    }
}

fn auto_scroll_image_list_drag(
    ui: &mut egui::Ui,
    ctx: &egui::Context,
    pointer_pos: Option<egui::Pos2>,
    row_height: f32,
) {
    if !egui::DragAndDrop::has_payload_of_type::<ImageDragPayload>(ctx) {
        return;
    }

    let Some(pointer_pos) = pointer_pos else {
        return;
    };

    let viewport = ui.clip_rect();
    if pointer_pos.x < viewport.left() || pointer_pos.x > viewport.right() {
        return;
    }

    let edge_zone = row_height * 1.5;
    let max_delta = row_height;
    let scroll_delta_y = if pointer_pos.y < viewport.top() + edge_zone {
        let strength = ((viewport.top() + edge_zone - pointer_pos.y) / edge_zone).clamp(0.0, 1.0);
        strength * max_delta
    } else if pointer_pos.y > viewport.bottom() - edge_zone {
        let strength = ((pointer_pos.y - (viewport.bottom() - edge_zone)) / edge_zone).clamp(0.0, 1.0);
        -strength * max_delta
    } else {
        0.0
    };

    if scroll_delta_y != 0.0 {
        ui.scroll_with_delta(egui::vec2(0.0, scroll_delta_y));
        ctx.request_repaint();
    }
}

fn render_cursor_overlay(ui: &mut egui::Ui, cursor: &CursorPixelInfo, overlay_height: f32) {
    if overlay_height <= 0.0 {
        return;
    }

    let mono_font = egui::TextStyle::Monospace.resolve(ui.style());
    let em = cursor_overlay_em(ui);
    let overlay_width = em * 21.0;
    let available = ui.available_size();
    let width = overlay_width.min(available.x.max(0.0));
    let (rect, _) = ui.allocate_exact_size(egui::vec2(available.x, overlay_height), egui::Sense::hover());
    let overlay_rect = egui::Rect::from_min_size(
        egui::pos2(rect.center().x - width * 0.5, rect.top()),
        egui::vec2(width, overlay_height),
    );

    let painter = ui.painter();
    painter.rect_filled(overlay_rect, 0.0, egui::Color32::from_rgba_premultiplied(0, 0, 0, 217));

    let origin = overlay_rect.min + egui::vec2(em * 0.25, em * 0.25);
    let square_size = em * 10.0;
    let zoom_rect = egui::Rect::from_min_size(origin, egui::vec2(square_size, square_size));
    render_cursor_zoom_patch(ui, cursor, zoom_rect);

    let swatch_rect = egui::Rect::from_min_size(
        egui::pos2(zoom_rect.right() + ui.spacing().item_spacing.x, zoom_rect.top()),
        egui::vec2(square_size, square_size),
    );
    render_cursor_color_swatch(ui, cursor, swatch_rect, mono_font.clone(), em);

    let closest = closest_color_entries(PixelSRGBA::from_array(cursor.rgba));
    let color_row_y = zoom_rect.bottom() + em * 0.5;
    render_nearest_color_row(ui, origin.x, color_row_y, em, &closest[0], false);
    render_nearest_color_row(
        ui,
        origin.x,
        color_row_y + em * 1.25,
        em,
        &closest[1],
        closest[1].distance - closest[0].distance > 1.0,
    );
}

fn render_cursor_zoom_patch(ui: &mut egui::Ui, cursor: &CursorPixelInfo, rect: egui::Rect) {
    let half_uv = egui::vec2(
        CURSOR_ROI_PIXELS / cursor.image_width as f32 * 0.5,
        CURSOR_ROI_PIXELS / cursor.image_height as f32 * 0.5,
    );
    let uv_min = cursor.uv - half_uv;
    let uv_max = cursor.uv + half_uv;
    // CursorPixelInfo, including the sRGB/Lab/HSV values and nearest-color names
    // beside this patch, is sampled on the CPU from the stored image. The color
    // editor preview exists only in the main image's fragment shader, so letting
    // this callback follow it would change the magnified pixels without changing
    // any of those readouts. Keep the entire cursor overlay internally consistent
    // by deliberately bypassing the preview here.
    let callback = egui_wgpu::Callback::new_paint_callback(
        rect,
        WgpuImageCallback::new(cursor.image_data.clone(), [uv_min.x, uv_min.y], [uv_max.x, uv_max.y])
            .without_color_preview(),
    );
    ui.painter().add(callback);

    let pixel_size = rect.size() / CURSOR_ROI_PIXELS;
    let center_min = rect.min + pixel_size * (CURSOR_ROI_PIXELS as i32 / 2) as f32;
    let center_rect = egui::Rect::from_min_size(center_min, pixel_size);
    ui.painter().rect_stroke(
        center_rect,
        0.0,
        egui::Stroke::new(1.0, egui::Color32::BLACK),
        egui::StrokeKind::Inside,
    );
    ui.painter().rect_stroke(
        center_rect.expand(1.0),
        0.0,
        egui::Stroke::new(1.0, egui::Color32::WHITE),
        egui::StrokeKind::Inside,
    );
}

fn cursor_overlay_em(ui: &egui::Ui) -> f32 {
    ui.text_style_height(&egui::TextStyle::Monospace) * CURSOR_OVERLAY_EM_SCALE
}

fn render_cursor_color_swatch(
    ui: &mut egui::Ui,
    cursor: &CursorPixelInfo,
    rect: egui::Rect,
    mono_font: egui::FontId,
    em: f32,
) {
    let rgba = cursor.rgba;
    let color = egui::Color32::from_rgba_unmultiplied(rgba[0], rgba[1], rgba[2], 255);
    let text_color = if contrast_prefers_black(rgba) {
        egui::Color32::BLACK
    } else {
        egui::Color32::WHITE
    };
    let painter = ui.painter();
    painter.rect_filled(rect, 0.0, color);

    let srgb = PixelSRGBA::from_array(rgba);
    let linear = convert_srgba_to_linear_rgb(srgb);
    let lab = convert_srgba_to_lab(srgb);
    let xyz = convert_srgba_to_xyz(srgb);
    let hsv = srgb.to_hsv().display_hsv();
    let lines = [
        format!("HTML #{:02x}{:02x}{:02x}", rgba[0], rgba[1], rgba[2]),
        format!("sRGB {:3} {:3} {:3}", rgba[0], rgba[1], rgba[2]),
        format!(
            " RGB {:3} {:3} {:3}",
            (linear.r * 255.0).round() as i32,
            (linear.g * 255.0).round() as i32,
            (linear.b * 255.0).round() as i32
        ),
        format!(
            " Lab {:3} {:3} {:3}",
            lab.l.round() as i32,
            lab.a.round() as i32,
            lab.b.round() as i32
        ),
        format!(
            " XYZ {:3} {:3} {:3}",
            xyz.x.round() as i32,
            xyz.y.round() as i32,
            xyz.z.round() as i32
        ),
        format!(" HSV {:3} {:3} {:3}", hsv.0, hsv.1, hsv.2),
    ];

    let mut y = rect.top() + em * 1.5;
    let x = rect.left() + em;
    for line in lines {
        painter.text(
            egui::pos2(x, y),
            egui::Align2::LEFT_TOP,
            line,
            mono_font.clone(),
            text_color,
        );
        y += em * 1.05;
    }
}

fn render_nearest_color_row(
    ui: &mut egui::Ui,
    x: f32,
    y: f32,
    em: f32,
    color: &crate::color_image::ColorMatchingResult,
    disabled: bool,
) {
    let padding = em * 0.5;
    let swatch_rect = egui::Rect::from_min_size(egui::pos2(x, y), egui::vec2(em, em));
    ui.painter().rect_filled(
        swatch_rect,
        0.0,
        egui::Color32::from_rgb(color.entry.r, color.entry.g, color.entry.b),
    );
    let hsv = PixelSRGBA {
        r: color.entry.r,
        g: color.entry.g,
        b: color.entry.b,
        a: 255,
    }
    .to_hsv()
    .display_hsv();
    let text_color = if disabled {
        ui.visuals().weak_text_color()
    } else {
        ui.visuals().text_color()
    };
    let font = egui::TextStyle::Monospace.resolve(ui.style());
    let text_x = swatch_rect.right() + padding;
    let delta_x = x + em * 11.2 - padding;
    let hsv_x = x + em * 14.0 - padding;
    let name = fitted_color_name(
        ui,
        format!("{} ({})", color.entry.class_name, color.entry.color_name),
        &font,
        (delta_x - text_x - padding).max(em),
    );
    ui.painter().text(
        egui::pos2(text_x, y),
        egui::Align2::LEFT_TOP,
        name,
        font.clone(),
        text_color,
    );
    ui.painter().text(
        egui::pos2(delta_x, y),
        egui::Align2::LEFT_TOP,
        format!("ΔE={:2.0}", color.distance),
        font.clone(),
        text_color,
    );
    ui.painter().text(
        egui::pos2(hsv_x, y),
        egui::Align2::LEFT_TOP,
        format!("HSV {:3} {:3} {:3}", hsv.0, hsv.1, hsv.2),
        font,
        text_color,
    );
}

// Cheap elision: estimate fit by char count assuming a fixed average char width.
// Inaccurate for proportional fonts but called per-row-per-frame, so we trade
// pixel-perfect fit for O(1) layout. Cow lets the common "already fits" path
// avoid an allocation.
fn elide_by_char_count(text: &str, max_width: f32, char_width: f32) -> Cow<'_, str> {
    let max_chars = (max_width / char_width).floor().max(0.0) as usize;
    let total = text.chars().count();
    if total <= max_chars {
        return Cow::Borrowed(text);
    }
    if max_chars <= 1 {
        return Cow::Borrowed("…");
    }
    let keep = max_chars - 1; // leave room for the ellipsis
    let mut out: String = text.chars().take(keep).collect();
    out.push('…');
    Cow::Owned(out)
}

fn fitted_color_name(ui: &egui::Ui, mut name: String, font: &egui::FontId, max_width: f32) -> String {
    let painter = ui.painter();
    if painter
        .layout_no_wrap(name.clone(), font.clone(), egui::Color32::WHITE)
        .size()
        .x
        <= max_width
    {
        return name;
    }

    while !name.is_empty() {
        name.pop();
        let candidate = format!("{name}...");
        if painter
            .layout_no_wrap(candidate.clone(), font.clone(), egui::Color32::WHITE)
            .size()
            .x
            <= max_width
        {
            return candidate;
        }
    }
    "...".to_owned()
}

fn contrast_prefers_black(rgba: [u8; 4]) -> bool {
    let luminance = 0.2126 * rgba[0] as f32 + 0.7152 * rgba[1] as f32 + 0.0722 * rgba[2] as f32;
    luminance > 150.0
}

fn push_action(action_queue: &Arc<Mutex<Vec<AppAction>>>, action: AppAction) {
    if let Ok(mut actions) = action_queue.lock() {
        actions.push(action);
    }
}

fn push_root_action(ctx: &egui::Context, action_queue: &Arc<Mutex<Vec<AppAction>>>, action: AppAction) {
    push_action(action_queue, action);
    // Controls run in a separate deferred viewport, while Viewer::update
    // drains this queue only during a root-viewport pass. Requesting a repaint
    // schedules that pass even for non-drawing operations such as clipboard
    // access, file dialogs, or quitting; it is not merely a visual refresh.
    ctx.request_repaint_of(egui::ViewportId::ROOT);
}

fn render_size_footer(
    ui: &mut egui::Ui,
    image_widget_size: &Arc<Mutex<Option<(u32, u32)>>>,
    ui_state: &Arc<Mutex<ControlsUiState>>,
    action_queue: &Arc<Mutex<Vec<AppAction>>>,
    ctx: &egui::Context,
) {
    let widget_size = image_widget_size.lock().ok().and_then(|s| *s);

    if let Ok(mut state) = ui_state.lock() {
        // When neither field is being edited, always sync from the actual widget size.
        if !state.size_being_edited[0] && !state.size_being_edited[1] {
            if let Some((w, h)) = widget_size {
                state.size_texts[0] = w.to_string();
                state.size_texts[1] = h.to_string();
            }
        }

        // Live aspect ratio: derive the non-edited dimension from the edited one.
        // Uses the current widget size as the ratio reference, matching C++ imageRect.size.
        if state.lock_ratio {
            if let Some((ref_w, ref_h)) = widget_size {
                if state.size_being_edited[0] && !state.size_being_edited[1] {
                    if let Ok(w) = state.size_texts[0].parse::<u32>() {
                        let h = (w as f64 * ref_h as f64 / ref_w as f64).round() as u32;
                        state.size_texts[1] = h.to_string();
                    }
                } else if state.size_being_edited[1] && !state.size_being_edited[0] {
                    if let Ok(h) = state.size_texts[1].parse::<u32>() {
                        let w = (h as f64 * ref_w as f64 / ref_h as f64).round() as u32;
                        state.size_texts[0] = w.to_string();
                    }
                }
            }
        }

        ui.horizontal(|ui| {
            let w_resp = size_text_edit(ui, &mut state.size_texts[0]);
            if w_resp.changed() {
                state.size_being_edited[0] = true;
                state.size_being_edited[1] = false;
            }

            ui.checkbox(&mut state.lock_ratio, "");

            let h_resp = size_text_edit(ui, &mut state.size_texts[1]);
            if h_resp.changed() {
                state.size_being_edited[1] = true;
                state.size_being_edited[0] = false;
            }

            // Apply when either field loses focus after being edited (matches ImGui's
            // IsItemDeactivatedAfterEdit — fires on Tab, Enter, or click away).
            let apply = (w_resp.lost_focus() && state.size_being_edited[0])
                || (h_resp.lost_focus() && state.size_being_edited[1]);

            if apply {
                state.size_being_edited = [false, false];
                if let (Ok(w), Ok(h)) = (state.size_texts[0].parse::<u32>(), state.size_texts[1].parse::<u32>()) {
                    if let Ok(mut actions) = action_queue.lock() {
                        actions.push(AppAction::ResizeWindow(WindowResizeAction::Custom {
                            width: w,
                            height: h,
                            lock_ratio: state.lock_ratio,
                        }));
                    }
                    ctx.request_repaint_of(egui::ViewportId::ROOT);
                }
            }
        });
    }
}

fn size_text_edit(ui: &mut egui::Ui, text: &mut String) -> egui::Response {
    let char_width = ui.text_style_height(&egui::TextStyle::Body) * 0.6;
    let desired_width = char_width * 5.0 + ui.spacing().button_padding.x * 2.0;
    ui.scope(|ui| {
        let r = egui::CornerRadius::same(4);
        ui.visuals_mut().widgets.inactive.corner_radius = r;
        ui.visuals_mut().widgets.hovered.corner_radius = r;
        ui.visuals_mut().widgets.active.corner_radius = r;
        ui.add(
            egui::TextEdit::singleline(text)
                .desired_width(desired_width)
                .char_limit(5)
                .horizontal_align(egui::Align::Center),
        )
    })
    .inner
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::color_editor::LevelsAdjustment;

    fn controls_window() -> ControlsWindow {
        ControlsWindow::new(
            Arc::new(Mutex::new(ImageList::new(Vec::new()))),
            Arc::new(Mutex::new(None)),
            Arc::new(Mutex::new(None)),
            Arc::new(Mutex::new(Vec::new())),
            Arc::new(Mutex::new(AnnotationTool::default())),
            Arc::new(Mutex::new(ImageEditorState::default())),
        )
    }

    fn set_pending_levels(window: &ControlsWindow) -> ColorPreview {
        let mut levels = LevelsAdjustment::default();
        levels.luma.input_black = 10;
        window.color_editor_state.lock().unwrap().set_levels_for_test(levels);
        ColorPreview::Levels(levels)
    }

    #[test]
    fn pending_preview_is_reported_only_while_the_color_editor_tab_is_on_screen() {
        let mut window = controls_window();
        let pending = set_pending_levels(&window);

        // The tab has never been opened, so the preview must not reach the image.
        assert_eq!(window.color_preview(), ColorPreview::None);

        window.show_color_editor();
        assert_eq!(window.color_preview(), pending);

        window.ui_state.lock().unwrap().active_tab = ControlsTab::ImageList;
        assert_eq!(window.color_preview(), ColorPreview::None);

        window.ui_state.lock().unwrap().active_tab = ControlsTab::ColorEditor;
        assert_eq!(window.color_preview(), pending);

        // Closing the window must drop the preview without discarding the pending
        // edit, so reopening shows the same adjustment and its Apply/Reset buttons.
        window.toggle();
        assert!(!window.is_enabled());
        assert_eq!(window.color_preview(), ColorPreview::None);
        window.toggle();
        assert_eq!(window.color_preview(), pending);
    }

    #[test]
    fn native_close_request_hides_controls_and_suppresses_pending_preview() {
        let mut window = controls_window();
        let pending = set_pending_levels(&window);
        window.show_color_editor();
        assert_eq!(window.color_preview(), pending);

        window.close_requested.store(true, Ordering::Release);
        window.consume_close_request();

        assert!(!window.is_enabled());
        assert_eq!(window.color_preview(), ColorPreview::None);

        window.toggle();
        assert_eq!(window.color_preview(), pending);

        window.consume_close_request();
        assert!(window.is_enabled());
    }
}
