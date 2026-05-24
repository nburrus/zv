use std::sync::{Arc, Mutex};

use eframe::egui;
use eframe::egui_wgpu;

use crate::image_item_data::ImageItemData;
use crate::render::WgpuImageCallback;

#[derive(Clone, Debug)]
pub struct CursorPixelInfo {
    pub image_name: String,
    pub x: u32,
    pub y: u32,
    pub rgba: [u8; 4],
}

#[derive(Default)]
pub struct ImageWindow;

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

                let callback = egui_wgpu::Callback::new_paint_callback(
                    rect,
                    WgpuImageCallback::new(image_data.clone()),
                );
                ui.painter().add(callback);

                let mut status = format!(
                    "{}  {} x {}",
                    image_name,
                    data.width(),
                    data.height(),
                );
                if let Some(pointer_pos) = response.hover_pos() {
                    // This 0.5 offset is important since the mouse coordinate is an integer.
                    // So when we are in the center of a pixel we'll return 0,0 instead of
                    // 0.5,0.5.
                    let image_pos = (pointer_pos + egui::vec2(0.5, 0.5)) - rect.min;
                    let uv = image_pos / rect.size();
                    let x = (uv.x * data.width() as f32).floor() as u32;
                    let y = (uv.y * data.height() as f32).floor() as u32;
                    if let Some(rgba) = data.pixel_rgba(x, y) {
                        status = format!(
                            "{status}    {x:4}, {y:4}  sRGBA {:3} {:3} {:3} {:3}",
                            rgba[0], rgba[1], rgba[2], rgba[3],
                        );
                        if let Ok(mut info) = cursor_info.lock() {
                            *info = Some(CursorPixelInfo {
                                image_name: image_name.clone(),
                                x,
                                y,
                                rgba,
                            });
                        }
                    }
                } else if let Ok(mut info) = cursor_info.lock() {
                    *info = None;
                }

                paint_status_overlay(ui, rect, &status);
            });

        output
    }
}

fn paint_status_overlay(ui: &egui::Ui, image_rect: egui::Rect, text: &str) {
    let painter = ui.painter();
    let font_id = egui::TextStyle::Monospace.resolve(ui.style());
    let galley = painter.layout_no_wrap(text.to_owned(), font_id, egui::Color32::WHITE);
    let padding = egui::vec2(8.0, 5.0);
    let overlay_size = galley.size() + padding * 2.0;
    let overlay_rect = egui::Rect::from_min_size(
        image_rect.left_bottom() + egui::vec2(8.0, -overlay_size.y - 8.0),
        overlay_size,
    )
    .intersect(image_rect);

    painter.rect_filled(
        overlay_rect,
        0.0,
        egui::Color32::from_rgba_premultiplied(0, 0, 0, 190),
    );
    painter.galley(overlay_rect.min + padding, galley, egui::Color32::WHITE);
}
