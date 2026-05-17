use std::sync::{Arc, Mutex};

use eframe::egui;

use crate::actions::AppAction;
use crate::image_window::CursorPixelInfo;
use crate::shortcuts::{ShortcutViewport, collect_shortcuts};

pub struct ControlsWindow {
    viewport_id: egui::ViewportId,
    cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
    action_queue: Arc<Mutex<Vec<AppAction>>>,
    enabled: bool,
    target_position: Option<egui::Pos2>,
    has_ever_been_shown: bool,
    apply_initial_position_on_show: bool,
    focus_on_show: bool,
}

impl ControlsWindow {
    pub fn new(
        cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
        action_queue: Arc<Mutex<Vec<AppAction>>>,
    ) -> Self {
        Self {
            viewport_id: egui::ViewportId::from_hash_of("zv-controls-window"),
            cursor_info,
            action_queue,
            enabled: false,
            target_position: None,
            has_ever_been_shown: false,
            apply_initial_position_on_show: false,
            focus_on_show: false,
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

    pub fn show(&mut self, ctx: &egui::Context) {
        let cursor_info = self.cursor_info.clone();
        let action_queue = self.action_queue.clone();
        let mut builder = egui::ViewportBuilder::default()
            .with_title("ZV Controls")
            .with_inner_size(egui::vec2(320.0, 120.0))
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

        if self.focus_on_show {
            self.focus_on_show = false;
            ctx.send_viewport_cmd_to(self.viewport_id, egui::ViewportCommand::Focus);
        }
    }
}
