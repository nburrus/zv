use std::sync::{Arc, Mutex};

use eframe::egui;
use egui_extras::{Column, TableBuilder};

use crate::image_list::ImageList;
use crate::image_window::CursorPixelInfo;
use crate::image_window_geometry::WindowResizeAction;
use crate::shortcuts::{ShortcutViewport, collect_shortcuts};
use crate::viewer::AppAction;

const CONTROLS_WIDTH: f32 = 640.0;
const CONTROLS_HEIGHT: f32 = 420.0;
const CONTROLS_WIDTH_WITH_PADDING: f32 = CONTROLS_WIDTH + 12.0;
const CONTROLS_GAP: f32 = 8.0;
const FOOTER_HEIGHT: f32 = 36.0;

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

#[derive(Debug)]
struct ControlsUiState {
    active_tab: ControlsTab,
    width_text: String,
    height_text: String,
    lock_ratio: bool,
}

impl Default for ControlsUiState {
    fn default() -> Self {
        Self {
            active_tab: ControlsTab::ImageList,
            width_text: String::new(),
            height_text: String::new(),
            lock_ratio: true,
        }
    }
}

pub struct ControlsWindow {
    viewport_id: egui::ViewportId,
    image_list: Arc<Mutex<ImageList>>,
    action_queue: Arc<Mutex<Vec<AppAction>>>,
    ui_state: Arc<Mutex<ControlsUiState>>,
    enabled: bool,
    target_position: Option<egui::Pos2>,
    has_ever_been_shown: bool,
    apply_initial_position_on_show: bool,
    focus_on_show: bool,
    last_auto_scrolled_selected: Arc<Mutex<Option<usize>>>,
}

impl ControlsWindow {
    pub fn new(
        image_list: Arc<Mutex<ImageList>>,
        _cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
        action_queue: Arc<Mutex<Vec<AppAction>>>,
    ) -> Self {
        Self {
            viewport_id: egui::ViewportId::from_hash_of("zv-controls-window"),
            image_list,
            action_queue,
            ui_state: Arc::new(Mutex::new(ControlsUiState::default())),
            enabled: false,
            target_position: None,
            has_ever_been_shown: false,
            apply_initial_position_on_show: false,
            focus_on_show: false,
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
        let last_auto_scrolled_selected = self.last_auto_scrolled_selected.clone();
        let ui_state = self.ui_state.clone();
        let action_queue = self.action_queue.clone();
        let mut builder = egui::ViewportBuilder::default()
            .with_title("zv controls")
            .with_inner_size(egui::vec2(CONTROLS_WIDTH, CONTROLS_HEIGHT))
            .with_resizable(true)
            .with_visible(self.enabled);
        let apply_initial_position_on_show = self.apply_initial_position_on_show;
        self.apply_initial_position_on_show = false;
        if apply_initial_position_on_show {
            if let Some(position) = self.target_position {
                builder = builder.with_position(position);
            }
        }

        ctx.show_viewport_deferred(self.viewport_id, builder, move |ctx, _class| {
            apply_controls_visuals(ctx);
            let new_actions = collect_shortcuts(ctx, ShortcutViewport::Controls);
            if !new_actions.is_empty() {
                if let Ok(mut actions) = action_queue.lock() {
                    actions.extend(new_actions);
                }
                // Viewer::update runs on the root viewport; wake it so queued
                // controls actions are applied even when only controls is focused.
                // ROOT corresponds to the main image viewport/window.
                ctx.request_repaint_of(egui::ViewportId::ROOT);
            }

            egui::TopBottomPanel::top("menu_bar").show(ctx, |ui| {
                egui::MenuBar::new().ui(ui, |ui| {
                    for label in ["File", "Edit", "Tools", "Window", "Help"] {
                        ui.menu_button(label, |_ui| {});
                    }
                });
            });

            egui::CentralPanel::default().show(ctx, |ui| {
                let active_tab = render_tabs(ui, &ui_state);
                ui.separator();
                ui.add_space(ui.spacing().item_spacing.y);
                match active_tab {
                    ControlsTab::ImageList => {
                        render_image_list_tab(
                            ui,
                            ctx,
                            &image_list,
                            &last_auto_scrolled_selected,
                            &ui_state,
                            &action_queue,
                        );
                    }
                    ControlsTab::Modifiers => render_empty_tab(ui, "Modifiers"),
                    ControlsTab::ColorEditor => render_empty_tab(ui, "Color Editor"),
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
        ui.horizontal(|ui| {
            ui.selectable_value(&mut state.active_tab, ControlsTab::ImageList, "Image List");
            ui.selectable_value(&mut state.active_tab, ControlsTab::Modifiers, "Modifiers");
            ui.selectable_value(&mut state.active_tab, ControlsTab::ColorEditor, "Color Editor");
        });
        state.active_tab
    } else {
        ControlsTab::ImageList
    }
}

fn render_empty_tab(ui: &mut egui::Ui, label: &str) {
    ui.add_space(12.0);
    ui.label(egui::RichText::new(label).color(egui::Color32::from_gray(190)));
}

fn render_image_list_tab(
    ui: &mut egui::Ui,
    ctx: &egui::Context,
    image_list: &Arc<Mutex<ImageList>>,
    last_auto_scrolled_selected: &Arc<Mutex<Option<usize>>>,
    ui_state: &Arc<Mutex<ControlsUiState>>,
    action_queue: &Arc<Mutex<Vec<AppAction>>>,
) {
    render_filter_row(ui, image_list, ctx);
    ui.add_space(ui.spacing().item_spacing.y);
    let table_height = (ui.available_height() - FOOTER_HEIGHT).max(80.0);
    if let Ok(mut images) = image_list.lock() {
        render_image_list(ui, &mut images, last_auto_scrolled_selected, ctx, table_height);
    } else {
        ui.colored_label(egui::Color32::RED, "image list lock is poisoned");
    }
    render_size_footer(ui, image_list, ui_state, action_queue, ctx);
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
    // Collect rows to release the borrow on image_list before the closures.
    let rows: Vec<CollectedRow> = image_list
        .visible_rows()
        .map(|row| CollectedRow {
            index: row.index,
            selected: row.selected,
            name: row.name.to_owned(),
            hover_text: row.source_path.map(|p| p.display().to_string()),
            size_text: row
                .size
                .map(|(w, h)| format!("{w}x{h}"))
                .unwrap_or_else(|| "(?x?)".to_owned()),
        })
        .collect();

    let mut pending_select: Option<usize> = None;
    let mut pending_move: Option<(usize, usize)> = None;

    // Capture these before TableBuilder takes &mut ui.
    let painter = ui.painter().clone();
    let pointer_pos = ui.input(|i| i.pointer.hover_pos());

    TableBuilder::new(ui)
        .sense(egui::Sense::click_and_drag())
        .striped(true)
        .resizable(true)
        .cell_layout(egui::Layout::left_to_right(egui::Align::Center))
        .column(Column::remainder())
        .column(Column::initial(size_col_width))
        .min_scrolled_height(0.0)
        .max_scroll_height(table_height - header_height)
        .header(header_height, |mut header| {
            header.col(|ui| {
                ui.strong("Name");
            });
            header.col(|ui| {
                ui.strong("Size");
            });
        })
        .body(|mut body| {
            for row_data in &rows {
                body.row(row_height, |mut row| {
                    row.set_selected(row_data.selected);

                    row.col(|ui| {
                        ui.add(
                            egui::Label::new(&row_data.name)
                                .sense(egui::Sense::empty())
                                .selectable(false),
                        );
                    });
                    row.col(|ui| {
                        ui.add(
                            egui::Label::new(&row_data.size_text)
                                .sense(egui::Sense::empty())
                                .selectable(false),
                        );
                    });

                    let response = row.response();

                    if let Some(ref text) = row_data.hover_text {
                        response.clone().on_hover_text(text);
                    }

                    if row_data.selected {
                        if let Ok(mut last) = last_auto_scrolled_selected.lock() {
                            if *last != Some(row_data.index) {
                                response.scroll_to_me(Some(egui::Align::Center));
                                *last = Some(row_data.index);
                            }
                        }
                    }

                    if response.clicked() {
                        pending_select = Some(row_data.index);
                    }

                    response.dnd_set_drag_payload(ImageDragPayload { index: row_data.index });

                    if let Some(payload) = response.dnd_hover_payload::<ImageDragPayload>() {
                        if payload.index != row_data.index {
                            let insert_after =
                                pointer_pos.is_some_and(|p| p.y > response.rect.center().y);
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
                            let insert_after =
                                pointer_pos.is_some_and(|p| p.y > response.rect.center().y);
                            let to = row_data.index + usize::from(insert_after);
                            pending_move = Some((payload.index, to));
                        }
                    }
                });
            }
        });

    if let Some((from, to)) = pending_move {
        image_list.move_item(from, to);
        ctx.request_repaint_of(egui::ViewportId::ROOT);
    } else if let Some(index) = pending_select {
        image_list.select_index(index);
        ctx.request_repaint_of(egui::ViewportId::ROOT);
    }
}

fn render_size_footer(
    ui: &mut egui::Ui,
    image_list: &Arc<Mutex<ImageList>>,
    ui_state: &Arc<Mutex<ControlsUiState>>,
    action_queue: &Arc<Mutex<Vec<AppAction>>>,
    ctx: &egui::Context,
) {
    let selected_size = image_list.lock().ok().and_then(|images| images.selected_size());
    if let Ok(mut state) = ui_state.lock() {
        if state.width_text.is_empty() || state.height_text.is_empty() {
            if let Some((width, height)) = selected_size {
                state.width_text = width.to_string();
                state.height_text = height.to_string();
            }
        }
        ui.horizontal(|ui| {
            let width_done = size_text_edit(ui, &mut state.width_text);
            let lock_response = ui.add_sized([24.0, 22.0], egui::Checkbox::without_text(&mut state.lock_ratio));
            let height_done = size_text_edit(ui, &mut state.height_text);
            if width_done || height_done || lock_response.changed() {
                if let (Ok(width), Ok(height)) =
                    (state.width_text.parse::<u32>(), state.height_text.parse::<u32>())
                {
                    if let Ok(mut actions) = action_queue.lock() {
                        actions.push(AppAction::ResizeWindow(WindowResizeAction::Custom {
                            width,
                            height,
                            lock_ratio: state.lock_ratio,
                        }));
                    }
                    ctx.request_repaint_of(egui::ViewportId::ROOT);
                }
            }
        });
    }
}

fn size_text_edit(ui: &mut egui::Ui, text: &mut String) -> bool {
    let response = ui.add_sized(
        [48.0, 22.0],
        egui::TextEdit::singleline(text)
            .frame(true)
            .char_limit(5)
            .horizontal_align(egui::Align::Center),
    );
    response.lost_focus() && ui.input(|input| input.key_pressed(egui::Key::Enter))
}
