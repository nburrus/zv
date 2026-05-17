use std::sync::{Arc, Mutex};

use eframe::egui;
use eframe::egui_wgpu;

use crate::image::ImageItemData;
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

                let image_size = egui::vec2(data.width() as f32, data.height() as f32);
                let available = ui.available_size();
                let scale = (available.x / image_size.x)
                    .min(available.y / image_size.y)
                    .min(1.0)
                    .max(0.01);
                let display_size = image_size * scale;

                ui.vertical_centered(|ui| {
                    ui.label(format!(
                        "{image_name}  {} x {}  stride {} bytes",
                        data.width(),
                        data.height(),
                        data.bytes_per_row(),
                    ));
                    ui.add_space(4.0);
                    let (rect, response) =
                        ui.allocate_exact_size(display_size, egui::Sense::click());
                    output.image_rect = Some(rect);
                    output.secondary_clicked = response.secondary_clicked();

                    let callback = egui_wgpu::Callback::new_paint_callback(
                        rect,
                        WgpuImageCallback::new(image_data.clone()),
                    );
                    ui.painter().add(callback);

                    if let Some(pointer_pos) = response.hover_pos() {
                        let uv = (pointer_pos - rect.min) / rect.size();
                        let x = (uv.x * data.width() as f32).floor() as u32;
                        let y = (uv.y * data.height() as f32).floor() as u32;
                        if let Some(rgba) = data.pixel_rgba(x, y) {
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
                });
            });

        output
    }
}
