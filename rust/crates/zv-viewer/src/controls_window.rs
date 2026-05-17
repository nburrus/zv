use std::sync::{Arc, Mutex};

use eframe::egui;

use crate::image_window::CursorPixelInfo;

pub struct ControlsWindow {
    viewport_id: egui::ViewportId,
    cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
    enabled: bool,
    target_position: Option<egui::Pos2>,
}

impl ControlsWindow {
    pub fn new(cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>) -> Self {
        Self {
            viewport_id: egui::ViewportId::from_hash_of("zv-controls-window"),
            cursor_info,
            enabled: false,
            target_position: None,
        }
    }

    pub fn toggle(&mut self) {
        self.enabled = !self.enabled;
    }

    pub fn set_target_position(&mut self, position: Option<egui::Pos2>) {
        self.target_position = position;
    }

    pub fn show(&mut self, ctx: &egui::Context) {
        let cursor_info = self.cursor_info.clone();
        let mut builder = egui::ViewportBuilder::default()
            .with_title("ZV Controls")
            .with_inner_size(egui::vec2(320.0, 120.0))
            .with_resizable(true)
            .with_visible(self.enabled);
        if let Some(position) = self.target_position {
            builder = builder.with_position(position);
        }

        ctx.show_viewport_deferred(self.viewport_id, builder, move |ctx, _class| {
            let close_requested = ctx.input(|input| input.key_pressed(egui::Key::Q));
            if close_requested {
                ctx.send_viewport_cmd_to(egui::ViewportId::ROOT, egui::ViewportCommand::Close);
            }

            egui::CentralPanel::default().show(ctx, |ui| {
                ui.heading("Controls");
                ui.separator();

                let info = cursor_info.lock().ok().and_then(|info| info.clone());
                if let Some(info) = info {
                    ui.label(format!("Image: {}", info.image_name));
                    ui.label(format!("Pixel: {}, {}", info.x, info.y));
                    ui.label(format!(
                        "sRGBA: {} {} {} {}",
                        info.rgba[0], info.rgba[1], info.rgba[2], info.rgba[3]
                    ));
                } else {
                    ui.label("Move the mouse over the image.");
                }
            });
        });
    }
}
