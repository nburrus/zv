use std::sync::{Arc, Mutex};
use std::time::Duration;

use eframe::egui;
use eframe::egui_wgpu;

use crate::annotation_tool::AnnotationTool;
use crate::annotations::WidgetToTextureTransform;
use crate::color_image::PixelSRGBA;
use crate::image_list::SelectedImageView;
use crate::layout::LayoutConfig;
use crate::modified_image::ModifiedImage;
use crate::render::WgpuImageCallback;

#[derive(Clone)]
pub struct CursorPixelInfo {
    pub image_name: String,
    pub x: u32,
    pub y: u32,
    pub image_width: u32,
    pub image_height: u32,
    pub uv: egui::Vec2,
    pub rgba: [u8; 4],
    pub image_data: Arc<Mutex<ModifiedImage>>,
}

impl CursorPixelInfo {
    // Whether both infos describe the same pixel, as far as the controls
    // window display is concerned.
    fn same_pixel(&self, other: &Self) -> bool {
        self.image_name == other.image_name && self.x == other.x && self.y == other.y && self.rgba == other.rgba
    }
}

impl std::fmt::Debug for CursorPixelInfo {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("CursorPixelInfo")
            .field("image_name", &self.image_name)
            .field("x", &self.x)
            .field("y", &self.y)
            .field("image_width", &self.image_width)
            .field("image_height", &self.image_height)
            .field("uv", &self.uv)
            .field("rgba", &self.rgba)
            .finish_non_exhaustive()
    }
}

struct ZoomState {
    // Integer zoom level: 1 = full image, 2 = 2x zoom, etc.
    zoom_factor: u32,
    // Normalized UV coordinate of the zoom center (0..1 each).
    uv_center: egui::Vec2,
}

impl Default for ZoomState {
    fn default() -> Self {
        Self {
            zoom_factor: 1,
            uv_center: egui::vec2(0.5, 0.5),
        }
    }
}

impl ZoomState {
    // Compute clamped uv_min/uv_max for the visible sub-region.
    fn uv_region(&self) -> (egui::Vec2, egui::Vec2) {
        let half = 0.5 / self.zoom_factor as f32;
        let uv0 = egui::vec2(self.uv_center.x - half, self.uv_center.y - half);
        let uv1 = egui::vec2(self.uv_center.x + half, self.uv_center.y + half);

        // Clamp so the ROI stays within the image, shifting both edges together.
        // The two terms are mutually exclusive (only one edge can be out of bounds).
        let dx = f32::max(0.0, -uv0.x) + f32::min(0.0, 1.0 - uv1.x);
        let dy = f32::max(0.0, -uv0.y) + f32::min(0.0, 1.0 - uv1.y);
        (uv0 + egui::vec2(dx, dy), uv1 + egui::vec2(dx, dy))
    }
}

#[derive(Default)]
pub struct ImageWindow {
    zoom: ZoomState,
}

pub struct ImageWindowOutput {
    pub image_rect: Option<egui::Rect>,
    pub secondary_clicked: bool,
    // The cursor pixel info and annotation selection are shared with the
    // controls window, which lives in a separate viewport and stays idle
    // unless explicitly repainted when they change.
    pub shared_state_changed: bool,
}

impl ImageWindow {
    pub fn show(
        &mut self,
        ctx: &egui::Context,
        layout: LayoutConfig,
        images: Vec<Option<SelectedImageView>>,
        cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
        annotation_tool: Arc<Mutex<AnnotationTool>>,
    ) -> ImageWindowOutput {
        let mut output = ImageWindowOutput {
            image_rect: None,
            secondary_clicked: false,
            shared_state_changed: false,
        };

        egui::CentralPanel::default()
            .frame(egui::Frame::NONE.fill(egui::Color32::BLACK))
            .show(ctx, |ui| {
                let available_rect = ui.available_rect_before_wrap();
                output.image_rect = Some(available_rect);

                if images.is_empty() {
                    ui.label("Loading...");
                    return;
                }

                let (uv_min, uv_max) = self.zoom.uv_region();
                let cell_rects = layout_cell_rects(available_rect, layout, 1.0);
                let ctrl = ui.input(|i| i.modifiers.ctrl || i.modifiers.mac_cmd);
                let mut hovered: Option<HoveredImage> = None;
                let visible_images = images
                    .iter()
                    .filter_map(|image| image.as_ref()?.data.clone())
                    .collect::<Vec<_>>();
                let first_valid_index = images
                    .iter()
                    .position(|image| image.as_ref().and_then(|image| image.data.as_ref()).is_some());

                for (index, cell_rect) in cell_rects.iter().copied().enumerate() {
                    let response = ui.allocate_rect(cell_rect, egui::Sense::click_and_drag());
                    if response.secondary_clicked() {
                        output.secondary_clicked = true;
                    }

                    let Some(Some(image)) = images.get(index) else {
                        continue;
                    };

                    if let Some(error) = image.error.as_ref() {
                        paint_error(ui, cell_rect, &image.name, error);
                        continue;
                    }

                    let Some(image_data) = image.data.as_ref() else {
                        paint_loading(ui, cell_rect);
                        continue;
                    };

                    let callback = egui_wgpu::Callback::new_paint_callback(
                        cell_rect,
                        WgpuImageCallback::new(image_data.clone(), [uv_min.x, uv_min.y], [uv_max.x, uv_max.y]),
                    );
                    ui.painter().add(callback);

                    if let Ok(mut tool) = annotation_tool.lock() {
                        let image_size = image_data
                            .lock()
                            .ok()
                            .map(|image| [image.final_data().width(), image.final_data().height()])
                            .unwrap_or([1, 1]);
                        let annotation_output = tool.render_for_image(
                            ui,
                            &response,
                            image_data,
                            WidgetToTextureTransform {
                                widget_rect: cell_rect,
                                uv_min,
                                uv_max,
                                image_size,
                            },
                            first_valid_index == Some(index),
                            &visible_images,
                        );
                        output.shared_state_changed |= annotation_output.selection_changed;
                    }

                    let Some(pointer_pos) = response.hover_pos() else {
                        continue;
                    };

                    let Some(sample) = sample_image_at_pointer(image_data, cell_rect, pointer_pos, uv_min, uv_max)
                    else {
                        continue;
                    };

                    if ctrl && response.clicked() {
                        let min_visible = 16.0 / self.zoom.zoom_factor as f32;
                        if sample.image_width as f32 > min_visible && sample.image_height as f32 > min_visible {
                            self.zoom.zoom_factor *= 2;
                            self.zoom.uv_center = sample.uv;
                        }
                    }

                    if ctrl && response.secondary_clicked() {
                        if self.zoom.zoom_factor >= 2 {
                            self.zoom.zoom_factor /= 2;
                        }
                        output.secondary_clicked = false;
                    }

                    hovered = Some(HoveredImage {
                        slot_index: index,
                        image_name: image.name.clone(),
                        rect: cell_rect,
                        pointer_pos,
                        sample,
                        image_data: image_data.clone(),
                    });
                }

                if let Some(hovered) = hovered {
                    if let Ok(mut info) = cursor_info.lock() {
                        let new_info = CursorPixelInfo {
                            image_name: hovered.image_name.clone(),
                            x: hovered.sample.x,
                            y: hovered.sample.y,
                            image_width: hovered.sample.image_width,
                            image_height: hovered.sample.image_height,
                            uv: hovered.sample.uv,
                            rgba: hovered.sample.rgba,
                            image_data: hovered.image_data.clone(),
                        };
                        output.shared_state_changed |= !info.as_ref().is_some_and(|old| old.same_pixel(&new_info));
                        *info = Some(new_info);
                    }
                    paint_synced_cursor(
                        ui,
                        &images,
                        &cell_rects,
                        hovered.slot_index,
                        hovered.sample.uv,
                        uv_min,
                        uv_max,
                    );
                    paint_synced_status_bars(ui, &images, &cell_rects, &hovered);
                } else if let Ok(mut info) = cursor_info.lock() {
                    output.shared_state_changed |= info.take().is_some();
                }
            });

        output
    }
}

#[derive(Clone)]
struct ImageSample {
    x: u32,
    y: u32,
    image_width: u32,
    image_height: u32,
    uv: egui::Vec2,
    rgba: [u8; 4],
}

struct HoveredImage {
    slot_index: usize,
    image_name: String,
    rect: egui::Rect,
    pointer_pos: egui::Pos2,
    sample: ImageSample,
    image_data: Arc<Mutex<ModifiedImage>>,
}

struct StatusBarInfo<'a> {
    image_name: &'a str,
    x: u32,
    y: u32,
    rgba: [u8; 4],
    pointer_pos: egui::Pos2,
}

fn layout_cell_rects(rect: egui::Rect, layout: LayoutConfig, padding: f32) -> Vec<egui::Rect> {
    let rows = layout.rows.max(1);
    let cols = layout.cols.max(1);
    let content_width = (rect.width() - (cols.saturating_sub(1) as f32 * padding)).max(1.0);
    let content_height = (rect.height() - (rows.saturating_sub(1) as f32 * padding)).max(1.0);
    let cell_width = content_width / cols as f32;
    let cell_height = content_height / rows as f32;

    let mut rects = Vec::with_capacity(rows * cols);
    for row in 0..rows {
        for col in 0..cols {
            let min = egui::pos2(
                rect.left() + col as f32 * (cell_width + padding),
                rect.top() + row as f32 * (cell_height + padding),
            );
            rects.push(egui::Rect::from_min_size(min, egui::vec2(cell_width, cell_height)));
        }
    }
    rects
}

fn sample_image_at_pointer(
    image_data: &Arc<Mutex<ModifiedImage>>,
    rect: egui::Rect,
    pointer_pos: egui::Pos2,
    uv_min: egui::Vec2,
    uv_max: egui::Vec2,
) -> Option<ImageSample> {
    let Ok(data) = image_data.lock() else {
        return None;
    };
    let data = data.final_data();
    if data.width() == 0 || data.height() == 0 {
        return None;
    }

    let widget_pos = (pointer_pos + egui::vec2(0.5, 0.5)) - rect.min;
    let uv_window = widget_pos / rect.size();
    let tex_uv = uv_min + uv_window * (uv_max - uv_min);
    if !(0.0..=1.0).contains(&tex_uv.x) || !(0.0..=1.0).contains(&tex_uv.y) {
        return None;
    }

    sample_image_at_uv(data, tex_uv)
}

fn sample_image_at_uv(data: &crate::image_item_data::ImageItemData, uv: egui::Vec2) -> Option<ImageSample> {
    if data.width() == 0 || data.height() == 0 {
        return None;
    }
    let x = ((uv.x * data.width() as f32).floor() as u32).min(data.width().saturating_sub(1));
    let y = ((uv.y * data.height() as f32).floor() as u32).min(data.height().saturating_sub(1));
    let rgba = data.pixel_rgba(x, y)?;
    Some(ImageSample {
        x,
        y,
        image_width: data.width(),
        image_height: data.height(),
        uv,
        rgba,
    })
}

fn paint_loading(ui: &egui::Ui, rect: egui::Rect) {
    const REPAINT_INTERVAL: Duration = Duration::from_millis(100);
    const SPINNER_RADIUS: f32 = 9.0;
    const SPINNER_SEGMENTS: usize = 18;

    ui.ctx().request_repaint_after(REPAINT_INTERVAL);

    let spinner_center = rect.center() - egui::vec2(0.0, 13.0);
    let start_angle = ui.input(|input| input.time as f32) * std::f32::consts::TAU;
    let sweep = std::f32::consts::TAU * 0.72;
    let points = (0..=SPINNER_SEGMENTS)
        .map(|index| {
            let angle = start_angle + sweep * index as f32 / SPINNER_SEGMENTS as f32;
            spinner_center + SPINNER_RADIUS * egui::vec2(angle.cos(), angle.sin())
        })
        .collect();
    ui.painter()
        .add(egui::Shape::line(points, egui::Stroke::new(2.5, egui::Color32::WHITE)));

    ui.painter().text(
        rect.center() + egui::vec2(0.0, 13.0),
        egui::Align2::CENTER_CENTER,
        "Loading image…",
        egui::TextStyle::Body.resolve(ui.style()),
        ui.visuals().weak_text_color(),
    );
}

fn paint_error(ui: &egui::Ui, rect: egui::Rect, image_name: &str, error: &str) {
    let text = format!("ERROR: could not load {image_name}\n{error}");
    ui.painter().text(
        rect.left_top() + egui::vec2(8.0, 8.0),
        egui::Align2::LEFT_TOP,
        text,
        egui::TextStyle::Body.resolve(ui.style()),
        egui::Color32::from_rgb(255, 96, 96),
    );
}

fn paint_synced_cursor(
    ui: &egui::Ui,
    images: &[Option<SelectedImageView>],
    rects: &[egui::Rect],
    hovered_index: usize,
    uv: egui::Vec2,
    uv_min: egui::Vec2,
    uv_max: egui::Vec2,
) {
    let uv_window = (uv - uv_min) / (uv_max - uv_min);
    for (index, image) in images.iter().enumerate() {
        if index == hovered_index || image.as_ref().and_then(|image| image.data.as_ref()).is_none() {
            continue;
        }
        let Some(rect) = rects.get(index).copied() else {
            continue;
        };
        let center = rect.min + uv_window * rect.size();
        ui.painter().circle_stroke(
            center,
            5.0,
            egui::Stroke::new(1.0, egui::Color32::from_black_alpha(180)),
        );
        ui.painter().circle_stroke(
            center,
            4.0,
            egui::Stroke::new(2.0, egui::Color32::from_white_alpha(180)),
        );
        ui.painter().circle_stroke(
            center,
            3.0,
            egui::Stroke::new(1.0, egui::Color32::from_black_alpha(180)),
        );
    }
}

fn paint_synced_status_bars(
    ui: &egui::Ui,
    images: &[Option<SelectedImageView>],
    rects: &[egui::Rect],
    hovered: &HoveredImage,
) {
    for (index, image) in images.iter().enumerate() {
        let Some(image) = image else {
            continue;
        };
        let Some(image_data) = image.data.as_ref() else {
            continue;
        };
        let Some(rect) = rects.get(index).copied() else {
            continue;
        };
        let Ok(data) = image_data.lock() else {
            continue;
        };
        let Some(sample) = sample_image_at_uv(data.final_data(), hovered.sample.uv) else {
            continue;
        };
        let pointer_pos = if index == hovered.slot_index {
            hovered.pointer_pos
        } else {
            rect.min + (hovered.pointer_pos - hovered.rect.min)
        };
        paint_status_bar(
            ui,
            rect,
            &StatusBarInfo {
                image_name: &image.name,
                x: sample.x,
                y: sample.y,
                rgba: sample.rgba,
                pointer_pos,
            },
        );
    }
}

fn paint_status_bar(ui: &egui::Ui, image_rect: egui::Rect, status: &StatusBarInfo<'_>) {
    let painter = ui.painter();
    let font_id = egui::TextStyle::Monospace.resolve(ui.style());
    let font_size = font_id.size;

    let hsv = PixelSRGBA::from_array(status.rgba).to_hsv().display_hsv();
    let line1 = status.image_name.to_owned();
    let line2 = format!(
        "{:4}, {:4} (sRGBA {:3} {:3} {:3} {:3}) (HSV {:3} {:3} {:3})",
        status.x, status.y, status.rgba[0], status.rgba[1], status.rgba[2], status.rgba[3], hsv.0, hsv.1, hsv.2,
    );

    let line1_galley = painter.layout_no_wrap(line1, font_id.clone(), egui::Color32::WHITE);
    let line2_galley = painter.layout_no_wrap(line2, font_id, egui::Color32::WHITE);
    let line1_height = line1_galley.size().y;
    let line2_height = line2_galley.size().y;
    let line_gap = 0.0;
    let text_height = line1_height + line_gap + line2_height;
    let top_bar_height = (font_size * 2.2).max(text_height + font_size * 0.35);
    let bottom_bar_height = (font_size * 2.55).max(text_height + font_size * 0.55);

    let mouse_y_in_widget = status.pointer_pos.y - image_rect.top();
    let show_on_bottom = (image_rect.height() - mouse_y_in_widget) > top_bar_height;
    let bar_height = if show_on_bottom {
        bottom_bar_height
    } else {
        top_bar_height
    };

    let bar_rect = if show_on_bottom {
        egui::Rect::from_min_max(
            egui::pos2(image_rect.left(), image_rect.bottom() - bar_height),
            image_rect.right_bottom(),
        )
    } else {
        egui::Rect::from_min_max(
            image_rect.left_top(),
            egui::pos2(image_rect.right(), image_rect.top() + bar_height),
        )
    };

    let text_x = image_rect.left() + font_size * 0.5;
    let text_y = bar_rect.top() + (bar_rect.height() - text_height) * 0.5;
    let clip_rect = bar_rect;

    painter.rect_filled(bar_rect, 0.0, egui::Color32::from_rgba_premultiplied(0, 0, 0, 127));
    let clipped = painter.with_clip_rect(clip_rect);
    clipped.galley(egui::pos2(text_x, text_y), line1_galley, egui::Color32::WHITE);
    clipped.galley(
        egui::pos2(text_x, text_y + line1_height + line_gap),
        line2_galley,
        egui::Color32::WHITE,
    );
}
