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

pub fn pixel_control_row(
    ui: &mut egui::Ui,
    label: &'static str,
    value: &mut u32,
    range: std::ops::RangeInclusive<u32>,
) -> bool {
    pixel_control_row_with_slider_range(ui, label, value, range.clone(), range)
}

/// A focused slider range with a separate range for exact numeric entry.
pub fn pixel_control_row_with_slider_range(
    ui: &mut egui::Ui,
    label: &'static str,
    value: &mut u32,
    slider_range: std::ops::RangeInclusive<u32>,
    range: std::ops::RangeInclusive<u32>,
) -> bool {
    let clamping = if slider_range == range {
        egui::SliderClamping::Always
    } else {
        egui::SliderClamping::Edits
    };
    let mut changed = false;
    control_row(ui, label, |ui, width| {
        const VALUE_WIDTH: f32 = 72.0;
        ui.spacing_mut().slider_width = (width - VALUE_WIDTH - ui.spacing().item_spacing.x).max(1.0);
        changed |= ui
            .add(
                egui::Slider::new(value, slider_range)
                    .clamping(clamping)
                    .show_value(false),
            )
            .changed();
        changed |= ui
            .add_sized(
                [VALUE_WIDTH, ui.spacing().interact_size.y],
                egui::DragValue::new(value).range(range).speed(1).suffix(" px"),
            )
            .changed();
    });
    changed
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exact_dimensions_outside_slider_range_survive_repainting() {
        let ctx = egui::Context::default();
        let mut value = 4096;
        for _ in 0..3 {
            let _ = ctx.run(egui::RawInput::default(), |ctx| {
                egui::CentralPanel::default().show(ctx, |ui| {
                    egui::Grid::new("size").show(ui, |ui| {
                        assert!(!pixel_control_row_with_slider_range(
                            ui,
                            "Width",
                            &mut value,
                            1..=1200,
                            1..=16384
                        ));
                    });
                });
            });
            assert_eq!(value, 4096);
        }
    }
}
