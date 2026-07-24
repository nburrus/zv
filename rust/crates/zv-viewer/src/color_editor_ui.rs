use std::sync::{Arc, Mutex, Weak};

use eframe::egui;
use egui_phosphor::regular as ph;

use crate::color_editor::{
    ChannelStats, GrayscaleMode, HueShiftParams, ImageColorStats, InvertTarget, LabelColorizeParams, LevelsAdjustment,
    LevelsParams, OneShotOperation, compute_image_color_stats,
};
use crate::image_list::ImageList;
use crate::modified_image::ModifiedImage;
use crate::render::ColorPreview;
use crate::viewer::AppAction;

const HISTOGRAM_HEIGHT: f32 = 88.0;
const HANDLE_RADIUS: f32 = 8.0;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
enum LevelsChannel {
    #[default]
    Luma,
    Red,
    Green,
    Blue,
}

/// Everything that varies per levels channel, so a new channel means one more
/// `CHANNELS` entry instead of another arm in several parallel matches.
struct ChannelInfo {
    channel: LevelsChannel,
    /// Compact label for the channel selector.
    selector_label: &'static str,
    /// Full name, used in tooltips.
    name: &'static str,
    color: egui::Color32,
    stats: fn(&ImageColorStats) -> &ChannelStats,
    levels: fn(&LevelsAdjustment) -> &LevelsParams,
    levels_mut: fn(&mut LevelsAdjustment) -> &mut LevelsParams,
}

/// Indexed by `LevelsChannel as usize`; `channel_table_is_indexed_by_enum_order` guards that.
const CHANNELS: [ChannelInfo; 4] = [
    ChannelInfo {
        channel: LevelsChannel::Luma,
        selector_label: "Luma",
        name: "Luma",
        // Color32::from_white_alpha(230), which is not a const fn.
        color: egui::Color32::from_rgba_premultiplied(230, 230, 230, 230),
        stats: |stats| &stats.luma,
        levels: |levels| &levels.luma,
        levels_mut: |levels| &mut levels.luma,
    },
    ChannelInfo {
        channel: LevelsChannel::Red,
        selector_label: "R",
        name: "Red",
        color: egui::Color32::from_rgb(255, 89, 89),
        stats: |stats| &stats.r,
        levels: |levels| &levels.red,
        levels_mut: |levels| &mut levels.red,
    },
    ChannelInfo {
        channel: LevelsChannel::Green,
        selector_label: "G",
        name: "Green",
        color: egui::Color32::from_rgb(89, 255, 89),
        stats: |stats| &stats.g,
        levels: |levels| &levels.green,
        levels_mut: |levels| &mut levels.green,
    },
    ChannelInfo {
        channel: LevelsChannel::Blue,
        selector_label: "B",
        name: "Blue",
        color: egui::Color32::from_rgb(115, 140, 255),
        stats: |stats| &stats.b,
        levels: |levels| &levels.blue,
        levels_mut: |levels| &mut levels.blue,
    },
];

impl LevelsChannel {
    fn info(self) -> &'static ChannelInfo {
        &CHANNELS[self as usize]
    }

    fn stats(self, stats: &ImageColorStats) -> &ChannelStats {
        (self.info().stats)(stats)
    }

    fn name(self) -> &'static str {
        self.info().name
    }

    fn color(self) -> egui::Color32 {
        self.info().color
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum LevelsHandle {
    InputBlack,
    Gamma,
    InputWhite,
}

#[derive(Clone, Copy)]
struct LevelsHandlePositions {
    input_black: f32,
    gamma: f32,
    input_white: f32,
}

impl LevelsHandlePositions {
    fn new(params: LevelsParams, rect: egui::Rect) -> Self {
        Self {
            input_black: value_to_x(params.input_black as f32, rect),
            gamma: value_to_x(gamma_midpoint(params), rect),
            input_white: value_to_x(params.input_white as f32, rect),
        }
    }
}

pub struct ColorEditorUiState {
    channel: LevelsChannel,
    levels: LevelsAdjustment,
    histogram_log_scale: bool,
    active_handle: Option<LevelsHandle>,
    hue_degrees: f32,
    label_colorize_seed: u32,
    stats_cache: StatsCache,
}

#[derive(Default)]
struct StatsCache {
    entry: Option<StatsCacheEntry>,
}

struct StatsCacheEntry {
    image: Weak<Mutex<ModifiedImage>>,
    display_revision: u64,
    stats: ImageColorStats,
}

impl StatsCache {
    fn get_or_compute(&mut self, image: &Arc<Mutex<ModifiedImage>>) -> Option<&ImageColorStats> {
        let image_data = image.lock().ok()?;
        let display_revision = image_data.display_revision();
        let cache_hit = self.entry.as_ref().is_some_and(|entry| {
            entry.display_revision == display_revision
                && entry
                    .image
                    .upgrade()
                    .is_some_and(|cached_image| Arc::ptr_eq(&cached_image, image))
        });
        if !cache_hit {
            self.entry = Some(StatsCacheEntry {
                image: Arc::downgrade(image),
                display_revision,
                stats: compute_image_color_stats(image_data.final_data().cpu_data()),
            });
        }
        self.entry.as_ref().map(|entry| &entry.stats)
    }
}

impl Default for ColorEditorUiState {
    fn default() -> Self {
        Self {
            channel: LevelsChannel::default(),
            levels: LevelsAdjustment::default(),
            histogram_log_scale: false,
            active_handle: None,
            hue_degrees: 0.0,
            label_colorize_seed: 1,
            stats_cache: StatsCache::default(),
        }
    }
}

/// Sections holding an edit that is previewed but not yet committed. Only one
/// can be pending at a time; the others stay disabled until it is applied or reset.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum PreviewSection {
    Levels,
    Hue,
}

impl ColorEditorUiState {
    pub(crate) fn color_preview(&self) -> ColorPreview {
        match self.pending_preview() {
            Some(PreviewSection::Levels) => ColorPreview::Levels(self.levels),
            Some(PreviewSection::Hue) => ColorPreview::Hue(HueShiftParams {
                degrees: self.hue_degrees,
            }),
            None => ColorPreview::None,
        }
    }

    fn pending_preview(&self) -> Option<PreviewSection> {
        if !self.levels.is_identity() {
            Some(PreviewSection::Levels)
        } else if self.hue_degrees != 0.0 {
            Some(PreviewSection::Hue)
        } else {
            None
        }
    }

    /// Whether `section` owns the pending edit, i.e. its Apply/Reset would do something.
    fn is_pending(&self, section: PreviewSection) -> bool {
        self.pending_preview() == Some(section)
    }

    /// Whether `section` can be edited: nothing else is previewing.
    fn allows_editing(&self, section: PreviewSection) -> bool {
        self.pending_preview().is_none_or(|pending| pending == section)
    }

    /// One-shot operations commit immediately, so they need a clean slate.
    fn allows_one_shot(&self) -> bool {
        self.pending_preview().is_none()
    }

    fn reset_levels(&mut self) {
        self.levels = LevelsAdjustment::default();
        self.active_handle = None;
    }

    fn reset_hue(&mut self) {
        self.hue_degrees = 0.0;
    }

    fn current_levels(&self) -> LevelsParams {
        *(self.channel.info().levels)(&self.levels)
    }

    fn current_levels_mut(&mut self) -> &mut LevelsParams {
        (self.channel.info().levels_mut)(&mut self.levels)
    }
}

pub fn render_color_editor_tab(
    ui: &mut egui::Ui,
    ctx: &egui::Context,
    image_list: &Arc<Mutex<ImageList>>,
    state: &Arc<Mutex<ColorEditorUiState>>,
    action_queue: &Arc<Mutex<Vec<AppAction>>>,
) {
    let Ok(mut state) = state.lock() else {
        ui.colored_label(egui::Color32::RED, "color editor lock is poisoned");
        return;
    };
    let Some(stats) = active_image_stats(image_list, &mut state) else {
        ui.weak("No loaded image");
        return;
    };
    let preview_before = state.color_preview();

    render_levels_header(ui, &stats, &mut state);
    render_histogram(ui, &stats, &mut state);
    ui.add_space(ui.spacing().item_spacing.y);
    render_levels_buttons(ui, ctx, &mut state, action_queue);
    section_separator(ui);
    render_hue_controls(ui, ctx, &mut state, action_queue);
    section_separator(ui);
    render_one_shot_controls(ui, ctx, &stats, &mut state, action_queue);
    if state.color_preview() != preview_before {
        repaint_both(ctx);
    }
}

fn section_separator(ui: &mut egui::Ui) {
    let padding = ui.spacing().item_spacing.y;
    ui.add_space(padding);
    ui.separator();
    ui.add_space(padding);
}

fn active_image_stats(image_list: &Arc<Mutex<ImageList>>, state: &mut ColorEditorUiState) -> Option<ImageColorStats> {
    let image = image_list
        .lock()
        .ok()?
        .selected_range_views()
        .into_iter()
        .flatten()
        .find_map(|view| view.data)?;
    stats_for_image(&image, state)
}

fn stats_for_image(image: &Arc<Mutex<ModifiedImage>>, state: &mut ColorEditorUiState) -> Option<ImageColorStats> {
    state.stats_cache.get_or_compute(image).cloned()
}

fn render_levels_header(ui: &mut egui::Ui, stats: &ImageColorStats, state: &mut ColorEditorUiState) {
    ui.horizontal(|ui| {
        for info in &CHANNELS {
            ui.selectable_value(&mut state.channel, info.channel, info.selector_label);
        }
        let channel = state.channel.stats(stats);
        ui.weak(format!(
            "min:{}  max:{}  mean:{:.1}",
            channel.min, channel.max, channel.mean
        ));
        ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
            ui.checkbox(&mut state.histogram_log_scale, "Log");
        });
    });
}

fn render_histogram(ui: &mut egui::Ui, stats: &ImageColorStats, state: &mut ColorEditorUiState) -> egui::Response {
    let desired = egui::vec2(ui.available_width(), HISTOGRAM_HEIGHT);
    let (rect, _) = ui.allocate_exact_size(desired, egui::Sense::hover());
    let response = ui.interact(
        rect.expand2(egui::vec2(HANDLE_RADIUS + 2.0, 0.0)),
        ui.id().with("levels_histogram_interaction"),
        egui::Sense::click_and_drag(),
    );
    let channel = state.channel.stats(stats);
    let color = state.channel.color();
    paint_histogram(ui, rect, channel, state.histogram_log_scale, color);

    let blocked = !state.allows_editing(PreviewSection::Levels);
    let initial_positions = LevelsHandlePositions::new(state.current_levels(), rect);
    let pointer = response.hover_pos().or_else(|| response.interact_pointer_pos());
    let hovered_handle = (!blocked)
        .then(|| pointer.and_then(|pos| nearest_handle(pos, rect, initial_positions)))
        .flatten();
    let primary_down = ui.input(|input| input.pointer.button_down(egui::PointerButton::Primary));
    if !blocked && primary_down && response.is_pointer_button_down_on() && state.active_handle.is_none() {
        state.active_handle = hovered_handle;
    }
    if !blocked
        && response.dragged_by(egui::PointerButton::Primary)
        && let (Some(handle), Some(pos)) = (state.active_handle, response.interact_pointer_pos())
    {
        update_dragged_handle(state, handle, rect, pos.x);
    }

    // Preserve the active handle for this frame's tint/tooltip, then release
    // it through egui's normal click/drag completion signals.
    let active_handle = state.active_handle;
    if response.drag_stopped_by(egui::PointerButton::Primary) || response.clicked_by(egui::PointerButton::Primary) {
        state.active_handle = None;
    }

    paint_levels_overlay(
        ui,
        rect,
        LevelsHandlePositions::new(state.current_levels(), rect),
        color,
        blocked,
        active_handle.or(hovered_handle),
        active_handle,
    );

    if active_handle.is_some() || hovered_handle.is_some() {
        ui.ctx().set_cursor_icon(egui::CursorIcon::ResizeHorizontal);
    }
    if let Some(handle) = active_handle {
        response.show_tooltip_ui(|ui| {
            ui.add(egui::Label::new(handle_tooltip(handle, state.current_levels())).extend());
        });
    } else if let Some(handle) = hovered_handle {
        response.clone().on_hover_ui(|ui| {
            ui.add(egui::Label::new(handle_tooltip(handle, state.current_levels())).extend());
        });
    } else if response.hovered()
        && stats.pixel_count > 0
        && let Some(pos) = pointer
    {
        show_histogram_tooltip(&response, stats, channel, color, state.channel, pos, rect);
    }
    response
}

fn paint_histogram(ui: &egui::Ui, rect: egui::Rect, channel: &ChannelStats, log_scale: bool, color: egui::Color32) {
    let painter = ui.painter_at(rect);
    painter.rect_filled(rect, 2.0, egui::Color32::from_rgb(8, 9, 10));
    let peak = channel.histogram.iter().copied().max().unwrap_or(1).max(1) as f32;
    let y_max = if log_scale { (1.0 + peak).log10() } else { peak };
    let bin_width = rect.width() / 256.0;
    for (bin, count) in channel.histogram.iter().copied().enumerate() {
        let value = if log_scale {
            (1.0 + count as f32).log10()
        } else {
            count as f32
        };
        let height = if y_max > 0.0 {
            value / y_max * rect.height()
        } else {
            0.0
        };
        let x0 = rect.left() + bin as f32 * bin_width;
        painter.rect_filled(
            egui::Rect::from_min_max(
                egui::pos2(x0, rect.bottom() - height),
                egui::pos2(x0 + bin_width.max(1.0), rect.bottom()),
            ),
            0.0,
            color,
        );
    }
    for grid_value in [0, 64, 128, 192, 255] {
        let x = value_to_x(grid_value as f32, rect);
        painter.line_segment(
            [egui::pos2(x, rect.top()), egui::pos2(x, rect.bottom())],
            egui::Stroke::new(1.0, egui::Color32::from_white_alpha(45)),
        );
    }
}

fn paint_levels_overlay(
    ui: &egui::Ui,
    rect: egui::Rect,
    positions: LevelsHandlePositions,
    color: egui::Color32,
    blocked: bool,
    visual_handle: Option<LevelsHandle>,
    active_handle: Option<LevelsHandle>,
) {
    let painter = ui.painter_at(rect);
    painter.rect_filled(
        egui::Rect::from_min_max(rect.min, egui::pos2(positions.input_black, rect.bottom())),
        0.0,
        egui::Color32::from_black_alpha(90),
    );
    painter.rect_filled(
        egui::Rect::from_min_max(egui::pos2(positions.input_white, rect.top()), rect.max),
        0.0,
        egui::Color32::from_black_alpha(90),
    );
    let handle_color = if blocked { color.gamma_multiply(0.35) } else { color };
    let gamma_color = if blocked {
        egui::Color32::from_gray(140)
    } else {
        egui::Color32::from_gray(235)
    };
    let accent = ui.visuals().selection.bg_fill;
    let black_color = tint_handle(
        handle_color,
        accent,
        visual_handle == Some(LevelsHandle::InputBlack),
        active_handle == Some(LevelsHandle::InputBlack),
    );
    let gamma_color = tint_handle(
        gamma_color,
        accent,
        visual_handle == Some(LevelsHandle::Gamma),
        active_handle == Some(LevelsHandle::Gamma),
    );
    let white_color = tint_handle(
        handle_color,
        accent,
        visual_handle == Some(LevelsHandle::InputWhite),
        active_handle == Some(LevelsHandle::InputWhite),
    );
    // Use the panel painter for handles so endpoint triangles can extend past
    // the histogram's clip rect at values 0 and 255, matching the C++ UI.
    let handle_painter = ui.painter().clone();
    let guide_stroke = egui::Stroke::new(1.5, egui::Color32::from_white_alpha(190));
    handle_painter.line_segment(
        [
            egui::pos2(positions.input_black, rect.top()),
            egui::pos2(positions.input_black, rect.bottom() - HANDLE_RADIUS),
        ],
        guide_stroke,
    );
    handle_painter.line_segment(
        [
            egui::pos2(positions.input_white, rect.top()),
            egui::pos2(positions.input_white, rect.bottom() - HANDLE_RADIUS),
        ],
        guide_stroke,
    );
    draw_triangle(&handle_painter, positions.input_black, rect.bottom(), black_color);
    draw_diamond(&handle_painter, positions.gamma, rect.bottom(), gamma_color);
    draw_triangle(&handle_painter, positions.input_white, rect.bottom(), white_color);
}

fn show_histogram_tooltip(
    response: &egui::Response,
    stats: &ImageColorStats,
    channel: &ChannelStats,
    color: egui::Color32,
    levels_channel: LevelsChannel,
    pointer: egui::Pos2,
    rect: egui::Rect,
) {
    let bin = x_to_value(pointer.x, rect) as usize;
    let count = channel.histogram[bin];
    let cumulative: u64 = channel.histogram[..=bin].iter().sum();
    let pct = 100.0 * count as f64 / stats.pixel_count as f64;
    let cumulative_pct = 100.0 * cumulative as f64 / stats.pixel_count as f64;
    response.clone().on_hover_ui(|ui| {
        ui.label(format!("Bin {bin}  [{bin}-{bin}]"));
        ui.separator();
        ui.colored_label(
            color,
            format!(
                "{}  count:{}  {:.2}%  cum {:.2}%",
                levels_channel.name(),
                count,
                pct,
                cumulative_pct
            ),
        );
    });
}

fn render_levels_buttons(
    ui: &mut egui::Ui,
    ctx: &egui::Context,
    state: &mut ColorEditorUiState,
    action_queue: &Arc<Mutex<Vec<AppAction>>>,
) {
    render_control_row(ui, "color_editor_levels_grid", "Level Mapping:", |ui| {
        let enabled = state.is_pending(PreviewSection::Levels);
        if ui.add_enabled(enabled, egui::Button::new("Apply")).clicked() {
            push_action(action_queue, AppAction::ApplyColorLevels(state.levels));
            state.reset_levels();
            repaint_both(ctx);
        }
        if ui.add_enabled(enabled, egui::Button::new("Reset")).clicked() {
            state.reset_levels();
            repaint_both(ctx);
        }
        let help = ui
            .add(
                egui::Label::new(egui::RichText::new(ph::INFO).weak())
                    .selectable(false)
                    .sense(egui::Sense::hover()),
            )
            .on_hover_cursor(egui::CursorIcon::Help);
        if help.hovered() {
            help.show_tooltip_text(
                "Drag the histogram triangles to set black/white input points; the middle diamond adjusts gamma.",
            );
        }
    });
}

fn render_control_row(
    ui: &mut egui::Ui,
    id: &'static str,
    label: &'static str,
    add_controls: impl FnOnce(&mut egui::Ui),
) {
    egui::Grid::new(id).num_columns(2).show(ui, |ui| {
        ui.label(label);
        ui.horizontal(add_controls);
        ui.end_row();
    });
}

fn render_hue_controls(
    ui: &mut egui::Ui,
    ctx: &egui::Context,
    state: &mut ColorEditorUiState,
    action_queue: &Arc<Mutex<Vec<AppAction>>>,
) {
    let blocked = !state.allows_editing(PreviewSection::Hue);
    render_control_row(ui, "color_editor_hue_grid", "Hue:", |ui| {
        ui.add_enabled_ui(!blocked, |ui| {
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                let pending = state.is_pending(PreviewSection::Hue);
                if ui.add_enabled(pending, egui::Button::new("Reset")).clicked() {
                    state.reset_hue();
                    repaint_both(ctx);
                }
                if ui.add_enabled(pending, egui::Button::new("Apply")).clicked() {
                    push_action(
                        action_queue,
                        AppAction::ApplyColorHue(HueShiftParams {
                            degrees: state.hue_degrees,
                        }),
                    );
                    state.reset_hue();
                    repaint_both(ctx);
                }

                // Keep the readout stable without assuming a particular font or scale.
                let value_width = ui
                    .painter()
                    .layout_no_wrap(
                        "360°".into(),
                        egui::TextStyle::Body.resolve(ui.style()),
                        ui.visuals().text_color(),
                    )
                    .size()
                    .x;
                ui.add_sized(
                    [value_width, ui.spacing().interact_size.y],
                    egui::Label::new(format!("{:.0}°", state.hue_degrees)),
                );

                // Trailing controls have now taken their natural widths, so the
                // slider consumes exactly the space left by the current panel.
                ui.spacing_mut().slider_width = ui.available_width().max(1.0);
                ui.add(egui::Slider::new(&mut state.hue_degrees, 0.0..=360.0).show_value(false));
            });
        })
        .response
        .on_disabled_hover_text("Apply or Reset the active levels adjustment first.");
    });
}

fn render_one_shot_controls(
    ui: &mut egui::Ui,
    ctx: &egui::Context,
    stats: &ImageColorStats,
    state: &mut ColorEditorUiState,
    action_queue: &Arc<Mutex<Vec<AppAction>>>,
) {
    let blocked = !state.allows_one_shot();
    let response = ui
        .add_enabled_ui(!blocked, |ui| {
            let spacing = ui.spacing().item_spacing.x;
            let column_width = ((ui.available_width() - spacing * 2.0) / 3.0).max(1.0);
            let row_height = ui.spacing().interact_size.y;
            egui::Grid::new("color_editor_one_shot_grid")
                .num_columns(3)
                .min_col_width(column_width)
                .max_col_width(column_width)
                .min_row_height(row_height)
                .spacing(ui.spacing().item_spacing)
                .show(ui, |ui| {
                    for row in [
                        [
                            ("Swap R/B", OneShotOperation::SwapRedBlue),
                            ("Swap R/G", OneShotOperation::SwapRedGreen),
                            ("Swap G/B", OneShotOperation::SwapGreenBlue),
                        ],
                        [
                            ("Invert", OneShotOperation::Invert(InvertTarget::Rgb)),
                            ("Grayscale", OneShotOperation::Grayscale(GrayscaleMode::LumaSrgb)),
                            ("HistEq", OneShotOperation::HistogramEqualization),
                        ],
                    ] {
                        for (label, operation) in row {
                            if ui
                                .add_sized([column_width, row_height], egui::Button::new(label))
                                .clicked()
                            {
                                push_action(action_queue, AppAction::ApplyColorOneShot(operation));
                                repaint_both(ctx);
                            }
                        }
                        ui.end_row();
                    }

                    ui.horizontal_wrapped(|ui| {
                        let label_button = ui
                            .add_enabled_ui(stats.rgb_channels_equal, |ui| ui.button("Label Colorize"))
                            .inner;
                        if label_button.clicked() {
                            push_action(
                                action_queue,
                                AppAction::ApplyColorOneShot(OneShotOperation::LabelColorize(LabelColorizeParams {
                                    seed: state.label_colorize_seed,
                                    background_value: 0,
                                })),
                            );
                            repaint_both(ctx);
                        }
                        label_button.on_disabled_hover_text("Label Colorize applies to grayscale-like label maps.");
                        ui.add(egui::DragValue::new(&mut state.label_colorize_seed).speed(1.0));
                        if ui.small_button("+").clicked() {
                            state.label_colorize_seed = state.label_colorize_seed.saturating_add(1);
                        }
                        ui.label("Seed");
                    });
                    ui.end_row();
                });
        })
        .response;
    response.on_disabled_hover_text("Apply or Reset the active preview first.");
}

fn value_to_x(value: f32, rect: egui::Rect) -> f32 {
    rect.left() + value / 255.0 * rect.width()
}

fn x_to_value(x: f32, rect: egui::Rect) -> i32 {
    (((x - rect.left()) / rect.width()).clamp(0.0, 1.0) * 255.0).round() as i32
}

fn gamma_midpoint(params: LevelsParams) -> f32 {
    params.input_black as f32 + 0.5_f32.powf(params.gamma) * (params.input_white - params.input_black) as f32
}

fn midpoint_to_gamma(input_black: i32, input_white: i32, midpoint: i32) -> f32 {
    let span = (input_white - input_black).max(1) as f32;
    let t = ((midpoint - input_black) as f32 / span).clamp(0.01, 0.99);
    (t.ln() / 0.5_f32.ln()).clamp(0.1, 10.0)
}

fn handle_tooltip(handle: LevelsHandle, params: LevelsParams) -> String {
    match handle {
        LevelsHandle::InputBlack => format!("min: {}", params.input_black),
        LevelsHandle::Gamma => format!("gamma: {:.2}", params.gamma),
        LevelsHandle::InputWhite => format!("max: {}", params.input_white),
    }
}

fn nearest_handle(pos: egui::Pos2, rect: egui::Rect, positions: LevelsHandlePositions) -> Option<LevelsHandle> {
    if (pos.y - (rect.bottom() - HANDLE_RADIUS * 0.5)).abs() > HANDLE_RADIUS + 2.0 {
        return None;
    }
    if (pos.x - positions.input_black).abs() <= HANDLE_RADIUS + 2.0 {
        Some(LevelsHandle::InputBlack)
    } else if (pos.x - positions.gamma).abs() <= HANDLE_RADIUS + 2.0 {
        Some(LevelsHandle::Gamma)
    } else if (pos.x - positions.input_white).abs() <= HANDLE_RADIUS + 2.0 {
        Some(LevelsHandle::InputWhite)
    } else {
        None
    }
}

fn update_dragged_handle(state: &mut ColorEditorUiState, handle: LevelsHandle, rect: egui::Rect, pointer_x: f32) {
    let value = x_to_value(pointer_x, rect);
    let params = state.current_levels_mut();
    match handle {
        LevelsHandle::InputBlack => params.input_black = value.min(params.input_white - 1),
        LevelsHandle::InputWhite => params.input_white = value.max(params.input_black + 1),
        LevelsHandle::Gamma => params.gamma = midpoint_to_gamma(params.input_black, params.input_white, value),
    }
}

fn tint_handle(base: egui::Color32, accent: egui::Color32, highlighted: bool, active: bool) -> egui::Color32 {
    if !highlighted {
        return base;
    }
    base.blend(egui::Color32::from_rgba_unmultiplied(
        accent.r(),
        accent.g(),
        accent.b(),
        if active { 120 } else { 72 },
    ))
}

fn draw_triangle(painter: &egui::Painter, x: f32, bottom: f32, color: egui::Color32) {
    painter.add(egui::Shape::convex_polygon(
        vec![
            egui::pos2(x, bottom - HANDLE_RADIUS),
            egui::pos2(x - HANDLE_RADIUS, bottom),
            egui::pos2(x + HANDLE_RADIUS, bottom),
        ],
        color,
        egui::Stroke::new(1.0, egui::Color32::from_gray(20)),
    ));
}

fn draw_diamond(painter: &egui::Painter, x: f32, bottom: f32, color: egui::Color32) {
    painter.add(egui::Shape::convex_polygon(
        vec![
            egui::pos2(x, bottom - HANDLE_RADIUS),
            egui::pos2(x + HANDLE_RADIUS, bottom - HANDLE_RADIUS * 0.5),
            egui::pos2(x, bottom),
            egui::pos2(x - HANDLE_RADIUS, bottom - HANDLE_RADIUS * 0.5),
        ],
        color,
        egui::Stroke::new(1.0, egui::Color32::from_gray(20)),
    ));
}

fn push_action(action_queue: &Arc<Mutex<Vec<AppAction>>>, action: AppAction) {
    if let Ok(mut actions) = action_queue.lock() {
        actions.push(action);
    }
}

fn repaint_both(ctx: &egui::Context) {
    ctx.request_repaint_of(egui::ViewportId::ROOT);
    ctx.request_repaint();
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::color_image::{ImageSRGBA, PixelSRGBA};
    use crate::image_item_data::ImageItemData;

    fn image(pixel: PixelSRGBA) -> Arc<Mutex<ModifiedImage>> {
        Arc::new(Mutex::new(ModifiedImage::new(
            ImageItemData::new(ImageSRGBA::from_tightly_packed_bytes(1, 1, &pixel.as_array())),
            None,
        )))
    }

    fn histogram_frame(
        ctx: &egui::Context,
        state: &mut ColorEditorUiState,
        stats: &ImageColorStats,
        events: Vec<egui::Event>,
    ) -> egui::Response {
        let mut response = None;
        let raw_input = egui::RawInput {
            screen_rect: Some(egui::Rect::from_min_size(egui::Pos2::ZERO, egui::vec2(640.0, 240.0))),
            events,
            focused: true,
            ..Default::default()
        };
        let _ = ctx.run(raw_input, |ctx| {
            egui::CentralPanel::default().show(ctx, |ui| {
                response = Some(render_histogram(ui, stats, state));
            });
        });
        response.unwrap()
    }

    #[test]
    fn stats_cache_tracks_exact_image_revision_without_retaining_images() {
        let first = image(PixelSRGBA::from_array([12, 12, 12, 255]));
        let second = image(PixelSRGBA::from_array([220, 220, 220, 255]));
        let mut state = ColorEditorUiState::default();

        assert_eq!(stats_for_image(&first, &mut state).unwrap().luma.mean, 12.0);
        let first_cache_reference = state.stats_cache.entry.as_ref().unwrap().image.clone();
        first
            .lock()
            .unwrap()
            .apply_base_image_transform(|_| ImageSRGBA::from_tightly_packed_bytes(1, 1, &[24, 24, 24, 255]));
        assert_eq!(stats_for_image(&first, &mut state).unwrap().luma.mean, 24.0);

        drop(first);
        assert!(first_cache_reference.upgrade().is_none());
        assert_eq!(stats_for_image(&second, &mut state).unwrap().luma.mean, 220.0);
    }

    #[test]
    fn pending_previews_block_incompatible_sections() {
        let mut state = ColorEditorUiState::default();
        assert_eq!(state.pending_preview(), None);
        assert_eq!(state.color_preview(), ColorPreview::None);
        assert!(state.allows_editing(PreviewSection::Levels));
        assert!(state.allows_editing(PreviewSection::Hue));
        assert!(state.allows_one_shot());

        state.levels.luma.input_black = 10;
        assert!(state.is_pending(PreviewSection::Levels));
        assert_eq!(state.color_preview(), ColorPreview::Levels(state.levels));
        assert!(state.allows_editing(PreviewSection::Levels));
        assert!(!state.allows_editing(PreviewSection::Hue));
        assert!(!state.allows_one_shot());
        state.reset_levels();

        state.hue_degrees = 90.0;
        assert!(state.is_pending(PreviewSection::Hue));
        assert_eq!(
            state.color_preview(),
            ColorPreview::Hue(HueShiftParams { degrees: 90.0 })
        );
        assert!(state.allows_editing(PreviewSection::Hue));
        assert!(!state.allows_editing(PreviewSection::Levels));
        assert!(!state.allows_one_shot());
        state.reset_hue();
        assert_eq!(state.pending_preview(), None);
        assert_eq!(state.color_preview(), ColorPreview::None);
    }

    #[test]
    fn channel_table_is_indexed_by_enum_order() {
        for (index, info) in CHANNELS.iter().enumerate() {
            assert_eq!(info.channel as usize, index, "{} is out of order", info.name);
            assert_eq!(info.channel.info().name, info.name);
        }
    }

    #[test]
    fn endpoint_handle_is_captured_on_press_and_dragged_with_egui_response() {
        let ctx = egui::Context::default();
        let stats = ImageColorStats {
            pixel_count: 1,
            ..Default::default()
        };
        let mut state = ColorEditorUiState::default();
        let response = histogram_frame(&ctx, &mut state, &stats, Vec::new());
        let start = egui::pos2(response.rect.left() + 1.0, response.rect.bottom() - HANDLE_RADIUS * 0.5);
        let target = egui::pos2(response.rect.left() + 100.0, start.y);

        histogram_frame(
            &ctx,
            &mut state,
            &stats,
            vec![
                egui::Event::PointerMoved(start),
                egui::Event::PointerButton {
                    pos: start,
                    button: egui::PointerButton::Primary,
                    pressed: true,
                    modifiers: egui::Modifiers::NONE,
                },
            ],
        );
        assert_eq!(state.active_handle, Some(LevelsHandle::InputBlack));

        histogram_frame(&ctx, &mut state, &stats, vec![egui::Event::PointerMoved(target)]);
        assert!(state.levels.luma.input_black > 0);

        histogram_frame(
            &ctx,
            &mut state,
            &stats,
            vec![egui::Event::PointerButton {
                pos: target,
                button: egui::PointerButton::Primary,
                pressed: false,
                modifiers: egui::Modifiers::NONE,
            }],
        );
        assert_eq!(state.active_handle, None);
    }
}
