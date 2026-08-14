//! A thumbnail of the whole image, with the visible region marked on it.
//!
//! It answers the two questions a cropped view raises — how much is hidden,
//! and where you are in it — and stays out of the way otherwise by fading out
//! once the view stops moving.

use std::sync::{Arc, Mutex};

use eframe::egui;
use eframe::egui_wgpu;

use crate::image_view::CellView;
use crate::modified_image::ModifiedImage;
use crate::render::WgpuImageCallback;

/// Tracks when the visible region last moved, so the minimap can show itself
/// while the view is changing and get out of the way once it settles.
#[derive(Default)]
pub struct Minimap {
    last_shown: Option<ShownRegion>,
    last_change_time: Option<f64>,
}

/// What the minimap was last showing. Equal regions on consecutive frames mean
/// the view is holding still, which is what starts the fade.
#[derive(Clone, Copy, PartialEq)]
struct ShownRegion {
    uv_min: egui::Vec2,
    uv_max: egui::Vec2,
}

/// How long the minimap stays fully visible after the view stops moving, and
/// how long it then takes to fade out.
const HOLD: f64 = 0.45;
const FADE: f64 = 0.2;
/// Longest side of the minimap, and how far it sits from the cell corner.
const MAX_SIDE: f32 = 132.0;
const MARGIN: f32 = 12.0;
/// A very long image would otherwise collapse to an unreadable hairline.
const MIN_SIDE: f32 = 10.0;

impl Minimap {
    /// Opacity the minimap should be drawn with this frame, or `None` when it
    /// should not be drawn at all — either nothing is hidden, or the view has
    /// been still long enough for the fade to finish. `None` means no shapes
    /// and no paint callback are submitted, rather than an invisible draw, and
    /// it is also the signal that no repaint needs to be scheduled.
    pub fn opacity_for(&mut self, view: CellView, now: f64) -> Option<f32> {
        if !view.scrollable_axes().any() {
            // Forget the region rather than just declining to draw. The next
            // crop may well be the same one (`s`, `a`, `s` lands right back
            // where it started), and that is a new region as far as the viewer
            // is concerned, so it has to restart the fade instead of comparing
            // equal to a region that finished fading long ago.
            self.last_shown = None;
            self.last_change_time = None;
            return None;
        }

        let region = ShownRegion {
            uv_min: view.uv_min,
            uv_max: view.uv_max,
        };
        if self.last_shown != Some(region) {
            self.last_shown = Some(region);
            self.last_change_time = Some(now);
        }
        let idle = now - self.last_change_time?;
        if idle <= HOLD {
            return Some(1.0);
        }
        let fading = ((idle - HOLD) / FADE) as f32;
        (fading < 1.0).then_some(1.0 - fading)
    }

    /// Draws a thumbnail of the whole image with the visible region outlined
    /// on it, in the corner of the cell. Every cell gets its own, showing its
    /// own image, since they all move together.
    pub fn paint(
        ui: &egui::Ui,
        view: CellView,
        image_data: &Arc<Mutex<ModifiedImage>>,
        image_size: egui::Vec2,
        opacity: f32,
    ) {
        let scale = MAX_SIDE / image_size.x.max(image_size.y);
        let size = (image_size * scale).max(egui::Vec2::splat(MIN_SIDE));
        let rect = egui::Rect::from_min_size(
            egui::pos2(
                view.paint_rect.right() - MARGIN - size.x,
                view.paint_rect.top() + MARGIN,
            ),
            size,
        );
        if !view.paint_rect.contains_rect(rect) {
            return;
        }

        let alpha = |value: f32| (value * opacity * 255.0).round().clamp(0.0, 255.0) as u8;
        let painter = ui.painter().with_clip_rect(view.paint_rect);

        // The thumbnail always shows the whole image, whatever the view shows.
        painter.rect_filled(rect.expand(1.0), 0.0, egui::Color32::from_black_alpha(alpha(0.65)));
        let callback = egui_wgpu::Callback::new_paint_callback(
            rect,
            WgpuImageCallback::new(image_data.clone(), [0.0, 0.0], [1.0, 1.0]).with_opacity(opacity),
        );
        painter.add(callback);

        // Dim what is off screen, and outline what is on screen.
        let visible = egui::Rect::from_min_max(
            rect.min + view.uv_min * rect.size(),
            rect.min + view.uv_max * rect.size(),
        );
        let shade = egui::Color32::from_black_alpha(alpha(0.55));
        for outside in [
            egui::Rect::from_min_max(rect.left_top(), egui::pos2(rect.right(), visible.top())),
            egui::Rect::from_min_max(egui::pos2(rect.left(), visible.bottom()), rect.right_bottom()),
            egui::Rect::from_min_max(egui::pos2(rect.left(), visible.top()), visible.left_bottom()),
            egui::Rect::from_min_max(visible.right_top(), egui::pos2(rect.right(), visible.bottom())),
        ] {
            if outside.is_positive() {
                painter.rect_filled(outside, 0.0, shade);
            }
        }
        painter.rect_stroke(
            visible,
            0.0,
            egui::Stroke::new(1.0, egui::Color32::from_white_alpha(alpha(0.9))),
            egui::StrokeKind::Inside,
        );
        painter.rect_stroke(
            rect,
            0.0,
            egui::Stroke::new(1.0, egui::Color32::from_white_alpha(alpha(0.35))),
            egui::StrokeKind::Outside,
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cell(width: f32, height: f32) -> egui::Rect {
        egui::Rect::from_min_size(egui::pos2(0.0, 0.0), egui::vec2(width, height))
    }

    /// A view of a very tall image, cropped to the cell, as `s` leaves it.
    fn tall_cropped_view(cell_rect: egui::Rect) -> CellView {
        CellView::for_test(cell_rect, egui::vec2(0.0, 0.4), egui::vec2(1.0, 0.45))
    }

    fn assert_near(a: f32, b: f32) {
        assert!((a - b).abs() < 1.0e-4, "expected {a} to be nearly equal to {b}");
    }

    #[test]
    fn it_comes_back_after_the_region_stopped_being_scrollable() {
        let cell_rect = cell(800.0, 600.0);
        let cropped = tall_cropped_view(cell_rect);
        let whole = CellView::for_test(cell_rect, egui::Vec2::ZERO, egui::vec2(1.0, 1.0));
        let mut minimap = Minimap::default();

        assert_eq!(minimap.opacity_for(cropped, 0.0), Some(1.0));
        assert_eq!(minimap.opacity_for(cropped, HOLD + FADE), None);
        // `a` puts the whole image back, so there is nothing to show.
        assert_eq!(minimap.opacity_for(whole, 1.0), None);

        // `s` again lands on the very same region. It is new to the viewer
        // even though it is not new to the minimap, so it has to show again.
        assert_eq!(minimap.opacity_for(cropped, 2.0), Some(1.0));
    }

    #[test]
    fn the_minimap_holds_then_fades_and_restarts_when_the_view_moves() {
        let cell_rect = cell(800.0, 600.0);
        let view = tall_cropped_view(cell_rect);
        let mut minimap = Minimap::default();

        assert_eq!(minimap.opacity_for(view, 0.0), Some(1.0));
        assert_eq!(minimap.opacity_for(view, HOLD), Some(1.0));
        let mid = minimap.opacity_for(view, HOLD + FADE * 0.5).expect("still fading");
        assert_near(mid, 0.5);
        assert_eq!(minimap.opacity_for(view, HOLD + FADE), None);

        // A region that moves again brings it straight back to full opacity,
        // however long it had been idle.
        let moved = CellView::for_test(cell_rect, egui::vec2(0.0, 0.5), egui::vec2(1.0, 0.55));
        assert_eq!(minimap.opacity_for(moved, 10.0), Some(1.0));
    }
}
