//! Shared typography and row layout for modifier panels.
use eframe::egui;

pub fn panel_header(ui: &mut egui::Ui, text: impl Into<String>) {
    ui.label(egui::RichText::new(text).color(ui.visuals().weak_text_color()));
}

pub fn control_row(ui: &mut egui::Ui, label: &'static str, add_control: impl FnOnce(&mut egui::Ui, f32)) {
    ui.label(label);
    let control_width = ui.available_width();
    ui.horizontal(|ui| add_control(ui, control_width));
    ui.end_row();
}
