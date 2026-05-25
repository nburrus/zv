use std::sync::{Arc, Mutex};

use eframe::egui;
use eframe::egui_wgpu;

use crate::color_image::PixelSRGBA;
use crate::image_item_data::ImageItemData;
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
    pub image_data: Arc<Mutex<ImageItemData>>,
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
}

impl ImageWindow {
    pub fn show(
        &mut self,
        ctx: &egui::Context,
        image_name: String,
        image_data: Option<Arc<Mutex<ImageItemData>>>,
        error: Option<&str>,
        cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
    ) -> ImageWindowOutput {
        let mut output = ImageWindowOutput {
            image_rect: None,
            secondary_clicked: false,
        };

        egui::CentralPanel::default()
            .frame(egui::Frame::NONE.fill(egui::Color32::BLACK))
            .show(ctx, |ui| {
                if let Some(error) = error {
                    ui.colored_label(
                        egui::Color32::from_rgb(255, 96, 96),
                        format!("ERROR: could not load {image_name}\n{error}"),
                    );
                    return;
                }

                let Some(image_data) = image_data else {
                    ui.label("Loading...");
                    return;
                };

                let Ok(data) = image_data.lock() else {
                    ui.colored_label(egui::Color32::RED, "ERROR: image data lock is poisoned");
                    return;
                };

                let available_rect = ui.available_rect_before_wrap();
                let rect = available_rect;
                let response = ui.allocate_rect(rect, egui::Sense::click());
                output.image_rect = Some(rect);
                output.secondary_clicked = response.secondary_clicked();

                let (uv_min, uv_max) = self.zoom.uv_region();
                let callback = egui_wgpu::Callback::new_paint_callback(
                    rect,
                    WgpuImageCallback::new(image_data.clone(), [uv_min.x, uv_min.y], [uv_max.x, uv_max.y]),
                );
                ui.painter().add(callback);

                // Pixel under cursor in texture UV space, needed for zoom center.
                let mut mouse_uv_in_texture: Option<egui::Vec2> = None;
                let mut status: Option<StatusBarInfo> = None;

                if let Some(pointer_pos) = response.hover_pos() {
                    // This 0.5 offset is important since the mouse coordinate is an integer.
                    // So when we are in the center of a pixel we'll return 0,0 instead of
                    // 0.5,0.5.
                    let widget_pos = (pointer_pos + egui::vec2(0.5, 0.5)) - rect.min;
                    let uv_window = widget_pos / rect.size();
                    // Map from widget UV space into texture UV space using the current zoom region.
                    let tex_uv = uv_min + uv_window * (uv_max - uv_min);
                    mouse_uv_in_texture = Some(tex_uv);

                    let x = ((tex_uv.x * data.width() as f32).floor() as u32).min(data.width().saturating_sub(1));
                    let y = ((tex_uv.y * data.height() as f32).floor() as u32).min(data.height().saturating_sub(1));
                    if let Some(rgba) = data.pixel_rgba(x, y) {
                        status = Some(StatusBarInfo {
                            image_name: image_name.clone(),
                            x,
                            y,
                            rgba,
                            pointer_pos,
                        });
                        if let Ok(mut info) = cursor_info.lock() {
                            *info = Some(CursorPixelInfo {
                                image_name: image_name.clone(),
                                x,
                                y,
                                image_width: data.width(),
                                image_height: data.height(),
                                uv: tex_uv,
                                rgba,
                                image_data: image_data.clone(),
                            });
                        }
                    }
                } else if let Ok(mut info) = cursor_info.lock() {
                    *info = None;
                }

                // Ctrl+Left click: zoom in, centered on cursor.
                let ctrl = ui.input(|i| i.modifiers.ctrl || i.modifiers.mac_cmd);
                if response.clicked() && ctrl {
                    if let Some(tex_uv) = mouse_uv_in_texture {
                        let min_visible = 16.0 / self.zoom.zoom_factor as f32;
                        if data.width() as f32 > min_visible && data.height() as f32 > min_visible {
                            self.zoom.zoom_factor *= 2;
                            self.zoom.uv_center = tex_uv;
                        }
                    }
                }

                // Ctrl+Right click: zoom out.
                if response.secondary_clicked() && ctrl {
                    if self.zoom.zoom_factor >= 2 {
                        self.zoom.zoom_factor /= 2;
                    }
                    // Don't propagate as a controls-window toggle.
                    output.secondary_clicked = false;
                }

                if let Some(status) = status {
                    paint_status_bar(ui, rect, &status);
                }
            });

        output
    }
}

struct StatusBarInfo {
    image_name: String,
    x: u32,
    y: u32,
    rgba: [u8; 4],
    pointer_pos: egui::Pos2,
}

fn paint_status_bar(ui: &egui::Ui, image_rect: egui::Rect, status: &StatusBarInfo) {
    let painter = ui.painter();
    let font_id = egui::TextStyle::Monospace.resolve(ui.style());
    let font_size = font_id.size;

    let hsv = PixelSRGBA::from_array(status.rgba).to_hsv().display_hsv();
    let line1 = status.image_name.clone();
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
