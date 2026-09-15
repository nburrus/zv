//! A transient crop selection. Only Apply touches image pixels or undo history.
use std::sync::{Arc, Mutex};

use eframe::egui;

use crate::annotations::WidgetToTextureTransform;
use crate::modified_image::ModifiedImage;
use crate::modifier_ui::{control_row, panel_header};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CropRegion {
    pub min: egui::Vec2,
    pub max: egui::Vec2,
}

/// Pixel edges are half-open: the right/bottom edge may equal the image size.
#[derive(Clone, Copy, Debug, PartialEq, Eq, serde::Serialize)]
pub struct PixelCrop {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}

impl PixelCrop {
    pub fn region(self, size: [u32; 2]) -> CropRegion {
        let size = egui::vec2(size[0] as f32, size[1] as f32);
        CropRegion {
            min: egui::vec2(self.x as f32, self.y as f32) / size,
            max: egui::vec2((self.x + self.width) as f32, (self.y + self.height) as f32) / size,
        }
    }
}

impl CropRegion {
    pub fn pixels(self, [width, height]: [u32; 2]) -> Option<PixelCrop> {
        if width == 0 || height == 0 || !self.min.is_finite() || !self.max.is_finite() {
            return None;
        }
        let min = self.min.min(self.max).clamp(egui::Vec2::ZERO, egui::Vec2::splat(1.0));
        let max = self.min.max(self.max).clamp(egui::Vec2::ZERO, egui::Vec2::splat(1.0));
        let x = (min.x * width as f32).round().min((width - 1) as f32) as u32;
        let y = (min.y * height as f32).round().min((height - 1) as f32) as u32;
        let right = ((max.x * width as f32).round() as u32).clamp(x + 1, width);
        let bottom = ((max.y * height as f32).round() as u32).clamp(y + 1, height);
        Some(PixelCrop {
            x,
            y,
            width: right - x,
            height: bottom - y,
        })
    }
}

#[derive(Clone, Copy)]
enum DragKind {
    Draw,
    Move,
    Resize { x: i8, y: i8 },
}

#[derive(Clone, Copy)]
struct CropDrag {
    kind: DragKind,
    start: egui::Vec2,
    initial: Option<CropRegion>,
    start_widget: egui::Pos2,
}

#[derive(Default)]
pub struct CropTool {
    active: bool,
    region: Option<CropRegion>,
    drag: Option<CropDrag>,
    targets: Vec<(Arc<Mutex<ModifiedImage>>, u64)>,
}

impl CropTool {
    pub fn active(&self) -> bool {
        self.active
    }
    pub fn region(&self) -> Option<CropRegion> {
        self.region
    }
    pub fn is_dragging(&self) -> bool {
        self.drag.is_some()
    }
    pub fn target_count(&self) -> usize {
        self.targets.len()
    }
    pub fn reference_size(&self) -> Option<[u32; 2]> {
        self.targets.first()?.0.lock().ok().map(|image| image.image_size())
    }

    pub fn start(&mut self, images: &[Arc<Mutex<ModifiedImage>>]) {
        self.cancel();
        self.targets = images
            .iter()
            .filter_map(|image| {
                let revision = image.lock().ok()?.display_revision();
                Some((image.clone(), revision))
            })
            .collect();
        self.active = !self.targets.is_empty();
    }

    /// Also guards Apply against a selection change queued in the same frame.
    pub fn validate_targets(&mut self, images: &[Arc<Mutex<ModifiedImage>>]) {
        if self.active
            && (images.len() != self.targets.len()
                || images.iter().zip(&self.targets).any(|(image, (target, revision))| {
                    !Arc::ptr_eq(image, target)
                        || !image.lock().is_ok_and(|image| image.display_revision() == *revision)
                }))
        {
            self.cancel();
        }
    }

    pub fn cancel(&mut self) {
        *self = Self::default();
    }

    pub fn take_region(&mut self) -> Option<CropRegion> {
        if self.drag.is_some() {
            return None;
        }
        let region = self.region?;
        self.cancel();
        Some(region)
    }

    pub fn controls(&mut self, ui: &mut egui::Ui) -> bool {
        panel_header(
            ui,
            if self.region.is_some() {
                "Selected crop  |  Enter to apply / Escape to cancel"
            } else {
                "New crop"
            },
        );
        let Some(size) = self.reference_size() else {
            return false;
        };
        let mut changed = false;
        if let Some(mut pixels) = self.region.and_then(|region| region.pixels(size)) {
            ui.add_enabled_ui(!self.is_dragging(), |ui| {
                egui::Grid::new("crop_dimensions").num_columns(2).show(ui, |ui| {
                    for (label, value, range) in [
                        ("X Offset", &mut pixels.x, 0..=size[0] - pixels.width),
                        ("Y Offset", &mut pixels.y, 0..=size[1] - pixels.height),
                    ] {
                        changed |= pixel_control_row(ui, label, value, range);
                    }
                    for (label, value, limit) in [
                        ("Width", &mut pixels.width, size[0] - pixels.x),
                        ("Height", &mut pixels.height, size[1] - pixels.y),
                    ] {
                        changed |= pixel_control_row(ui, label, value, 1..=limit);
                    }
                });
            });
            if changed {
                self.region = Some(pixels.region(size));
            }
        } else {
            ui.label("Drag on the first image to draw a crop.");
        }
        ui.label("Drag inside to move or drag handles to resize.\nHold Shift for a square.");
        if self.targets.len() > 1 {
            ui.label("Pixel values refer to the first image. The same relative crop applies to all visible images.");
        }
        changed
    }

    /// Input is handled only on the reference image, overlays appear on all targets.
    pub fn render(
        &mut self,
        ui: &egui::Ui,
        response: &egui::Response,
        transform: WidgetToTextureTransform,
        reference: bool,
    ) -> bool {
        let before = (self.region, self.is_dragging());
        if reference {
            self.handle_input(response, &transform);
        }
        let painter = ui
            .painter()
            .with_clip_rect(response.rect.intersect(transform.widget_rect));
        if let Some(pixels) = self.region.and_then(|region| region.pixels(transform.image_size)) {
            let region = pixels.region(transform.image_size);
            let rect = egui::Rect::from_min_max(
                transform.texture_to_widget(region.min),
                transform.texture_to_widget(region.max),
            );
            let full = transform.widget_rect;
            let visible = rect.intersect(full);
            let shade = egui::Color32::from_black_alpha(145);
            if visible.is_positive() {
                for outside in [
                    egui::Rect::from_min_max(full.min, egui::pos2(full.right(), visible.top())),
                    egui::Rect::from_min_max(egui::pos2(full.left(), visible.bottom()), full.max),
                    egui::Rect::from_min_max(egui::pos2(full.left(), visible.top()), visible.left_bottom()),
                    egui::Rect::from_min_max(visible.right_top(), egui::pos2(full.right(), visible.bottom())),
                ] {
                    painter.rect_filled(outside, 0.0, shade);
                }
            } else {
                painter.rect_filled(full, 0.0, shade);
            }
            painter.rect_stroke(
                rect,
                0.0,
                egui::Stroke::new(2.0, egui::Color32::GOLD),
                egui::StrokeKind::Inside,
            );
            if reference {
                for (_, _, pos) in handles(rect) {
                    painter.rect_filled(
                        egui::Rect::from_center_size(pos, egui::Vec2::splat(7.0)),
                        0.0,
                        egui::Color32::GOLD,
                    );
                }
            }
        }
        before != (self.region, self.is_dragging())
    }

    fn handle_input(&mut self, response: &egui::Response, transform: &WidgetToTextureTransform) {
        let (pos, pressed_pos, down, released, shift, shortcut) = response.ctx.input(|input| {
            (
                input.pointer.interact_pos(),
                input.events.iter().find_map(|event| match event {
                    egui::Event::PointerButton {
                        pos,
                        button: egui::PointerButton::Primary,
                        pressed: true,
                        ..
                    } => Some(*pos),
                    _ => None,
                }),
                input.pointer.button_down(egui::PointerButton::Primary),
                input.pointer.button_released(egui::PointerButton::Primary),
                input.modifiers.shift,
                input.modifiers.ctrl || input.modifiers.command || input.modifiers.mac_cmd,
            )
        });
        let Some(pos) = pos else {
            if released {
                self.drag = None;
            }
            return;
        };
        let pointer = transform.widget_to_texture_edge(pos);
        let hit_pos = pressed_pos.unwrap_or(pos);
        let within = response.rect.contains(hit_pos) && transform.widget_rect.contains(hit_pos);
        let hit = self.region.and_then(|region| {
            let rect = egui::Rect::from_min_max(
                transform.texture_to_widget(region.min),
                transform.texture_to_widget(region.max),
            );
            handles(rect)
                .into_iter()
                .find(|(_, _, handle)| handle.distance(hit_pos) <= 7.0)
                .map(|(x, y, _)| DragKind::Resize { x, y })
                .or_else(|| rect.contains(hit_pos).then_some(DragKind::Move))
        });
        if let Some(pressed_pos) = pressed_pos
            && within
            && !shortcut
            && self.drag.is_none()
            && (self.region.is_none() || hit.is_some())
        {
            let kind = hit.unwrap_or(DragKind::Draw);
            self.drag = Some(CropDrag {
                kind,
                start: transform
                    .widget_to_texture_edge(pressed_pos)
                    .clamp(egui::Vec2::ZERO, egui::Vec2::splat(1.0)),
                initial: self.region,
                start_widget: pressed_pos,
            });
            if matches!(kind, DragKind::Draw) {
                self.region = None;
            }
        }
        if let Some(drag) = self.drag {
            if down || released {
                self.region = drag.region_at(pointer, shift, transform.image_size);
                // A click never inserts a one-pixel crop or replaces an existing one.
                if released && matches!(drag.kind, DragKind::Draw) && pos.distance(drag.start_widget) < 3.0 {
                    self.region = drag.initial;
                }
                response.ctx.request_repaint();
            }
            if released {
                self.drag = None;
            }
        }
        if self.drag.is_some() || (within && !shortcut && (self.region.is_none() || hit.is_some())) {
            let kind = self.drag.map(|drag| drag.kind).or(hit).unwrap_or(DragKind::Draw);
            let cursor = match kind {
                DragKind::Draw => egui::CursorIcon::Crosshair,
                DragKind::Move if self.drag.is_some() => egui::CursorIcon::Grabbing,
                DragKind::Move => egui::CursorIcon::Grab,
                DragKind::Resize { x: 0, .. } => egui::CursorIcon::ResizeVertical,
                DragKind::Resize { y: 0, .. } => egui::CursorIcon::ResizeHorizontal,
                DragKind::Resize { x, y } if x == y => egui::CursorIcon::ResizeNwSe,
                DragKind::Resize { .. } => egui::CursorIcon::ResizeNeSw,
            };
            response.ctx.set_cursor_icon(cursor);
        }
    }
}

fn pixel_control_row(
    ui: &mut egui::Ui,
    label: &'static str,
    value: &mut u32,
    range: std::ops::RangeInclusive<u32>,
) -> bool {
    let mut changed = false;
    control_row(ui, label, |ui, width| {
        const VALUE_WIDTH: f32 = 72.0;
        ui.spacing_mut().slider_width = (width - VALUE_WIDTH - ui.spacing().item_spacing.x).max(1.0);
        changed |= ui
            .add(egui::Slider::new(value, range.clone()).show_value(false))
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

fn handles(rect: egui::Rect) -> [(i8, i8, egui::Pos2); 8] {
    [
        (-1, -1, rect.left_top()),
        (0, -1, rect.center_top()),
        (1, -1, rect.right_top()),
        (-1, 0, rect.left_center()),
        (1, 0, rect.right_center()),
        (-1, 1, rect.left_bottom()),
        (0, 1, rect.center_bottom()),
        (1, 1, rect.right_bottom()),
    ]
}

impl CropDrag {
    fn region_at(self, pointer: egui::Vec2, square: bool, size: [u32; 2]) -> Option<CropRegion> {
        let extent = egui::vec2(size[0] as f32, size[1] as f32);
        let pointer = (pointer * extent).round();
        let start = (self.start * extent).round();
        let (mut anchor, mut moving) = match self.kind {
            DragKind::Draw => (start, pointer),
            DragKind::Move => {
                let initial = self.initial?;
                let min = (initial.min * extent).round();
                let max = (initial.max * extent).round();
                let delta = (pointer - start).clamp(-min, extent - max);
                return Some(CropRegion {
                    min: (min + delta) / extent,
                    max: (max + delta) / extent,
                });
            }
            DragKind::Resize { x, y } => {
                let initial = self.initial?;
                let min = (initial.min * extent).round();
                let max = (initial.max * extent).round();
                let anchor = egui::vec2(if x < 0 { max.x } else { min.x }, if y < 0 { max.y } else { min.y });
                let moving = egui::vec2(
                    if x == 0 { max.x } else { pointer.x },
                    if y == 0 { max.y } else { pointer.y },
                );
                (anchor, moving)
            }
        };
        anchor = anchor.clamp(egui::Vec2::ZERO, extent);
        if square && !matches!(self.kind, DragKind::Resize { x: 0, .. } | DragKind::Resize { y: 0, .. }) {
            let delta = moving - anchor;
            let direction = egui::vec2(
                if delta.x < 0.0 { -1.0 } else { 1.0 },
                if delta.y < 0.0 { -1.0 } else { 1.0 },
            );
            let room = egui::vec2(
                if direction.x < 0.0 {
                    anchor.x
                } else {
                    extent.x - anchor.x
                },
                if direction.y < 0.0 {
                    anchor.y
                } else {
                    extent.y - anchor.y
                },
            );
            let side = delta.x.abs().max(delta.y.abs()).min(room.x).min(room.y);
            moving = anchor + direction * side;
        }
        moving = moving.clamp(egui::Vec2::ZERO, extent);
        let min = anchor.min(moving);
        let max = anchor.max(moving);
        if max.x == min.x || max.y == min.y {
            return self.initial.filter(|_| !matches!(self.kind, DragKind::Draw));
        }
        Some(CropRegion {
            min: min / extent,
            max: max / extent,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{color_image::ImageSRGBA, image_item_data::ImageItemData};

    fn image(size: [u32; 2]) -> Arc<Mutex<ModifiedImage>> {
        Arc::new(Mutex::new(ModifiedImage::new(
            ImageItemData::new(ImageSRGBA::new(size[0], size[1])),
            None,
        )))
    }

    #[test]
    fn pixel_edges_round_trip_including_last_row_and_column() {
        for size in [[1, 1], [1, 9], [17, 1], [37, 23], [4096, 3072]] {
            for x in [0, size[0] / 2, size[0] - 1] {
                for y in [0, size[1] / 2, size[1] - 1] {
                    for [width, height] in [[1, 1], [size[0] - x, size[1] - y]] {
                        let crop = PixelCrop { x, y, width, height };
                        assert_eq!(crop.region(size).pixels(size), Some(crop));
                    }
                }
            }
        }
    }

    #[test]
    fn relative_regions_remain_nonempty_and_in_bounds_on_every_target() {
        for a in [-2.0, 0.0, 0.01, 0.5, 0.99, 1.0, 3.0] {
            for b in [-1.0, 0.0, 0.51, 1.0, 2.0] {
                let region = CropRegion {
                    min: egui::vec2(a, b),
                    max: egui::vec2(b, a),
                };
                for size in [[1, 1], [2, 17], [91, 3]] {
                    let crop = region.pixels(size).unwrap();
                    assert!(crop.width > 0 && crop.height > 0);
                    assert!(crop.x + crop.width <= size[0] && crop.y + crop.height <= size[1]);
                }
            }
        }
        let region = CropRegion {
            min: egui::Vec2::ZERO,
            max: egui::Vec2::splat(f32::NAN),
        };
        assert!(region.pixels([10, 10]).is_none());
    }

    #[test]
    fn square_drag_preserves_direction_and_square_at_image_boundaries() {
        let start = egui::vec2(0.4, 0.6);
        let drag = CropDrag {
            kind: DragKind::Draw,
            start,
            initial: None,
            start_widget: egui::Pos2::ZERO,
        };
        for pointer in [
            egui::vec2(-1.0, -1.0),
            egui::vec2(2.0, -1.0),
            egui::vec2(-1.0, 2.0),
            egui::vec2(2.0, 2.0),
        ] {
            let region = drag.region_at(pointer, true, [200, 100]).unwrap();
            let crop = region.pixels([200, 100]).unwrap();
            assert_eq!(crop.width, crop.height);
            assert_eq!(
                if pointer.x < start.x {
                    region.max.x
                } else {
                    region.min.x
                },
                start.x
            );
            assert_eq!(
                if pointer.y < start.y {
                    region.max.y
                } else {
                    region.min.y
                },
                start.y
            );
            assert!(crop.x + crop.width <= 200 && crop.y + crop.height <= 100);
        }
    }

    #[test]
    fn moving_clamps_translation_without_shrinking_the_crop() {
        let initial = PixelCrop {
            x: 20,
            y: 30,
            width: 40,
            height: 20,
        }
        .region([100, 100]);
        let drag = CropDrag {
            kind: DragKind::Move,
            start: egui::vec2(0.4, 0.4),
            initial: Some(initial),
            start_widget: egui::Pos2::ZERO,
        };
        for pointer in [egui::Vec2::splat(-2.0), egui::Vec2::splat(2.0)] {
            let crop = drag
                .region_at(pointer, false, [100, 100])
                .unwrap()
                .pixels([100, 100])
                .unwrap();
            assert_eq!([crop.width, crop.height], [40, 20]);
            assert_eq!([crop.x, crop.y], if pointer.x < 0.0 { [0, 0] } else { [60, 80] });
        }
    }

    #[test]
    fn edge_resize_preserves_other_axis_and_survives_crossing_anchor() {
        let initial = PixelCrop {
            x: 20,
            y: 30,
            width: 40,
            height: 20,
        }
        .region([100, 100]);
        for (x, y) in [(-1, 0), (1, 0), (0, -1), (0, 1)] {
            let drag = CropDrag {
                kind: DragKind::Resize { x, y },
                start: egui::Vec2::ZERO,
                initial: Some(initial),
                start_widget: egui::Pos2::ZERO,
            };
            for pointer in [egui::Vec2::splat(0.1), egui::Vec2::splat(0.9)] {
                let crop = drag
                    .region_at(pointer, true, [100, 100])
                    .unwrap()
                    .pixels([100, 100])
                    .unwrap();
                if x == 0 {
                    assert_eq!([crop.x, crop.width], [20, 40]);
                }
                if y == 0 {
                    assert_eq!([crop.y, crop.height], [30, 20]);
                }
            }
        }
    }

    #[test]
    fn changed_target_order_or_pixels_invalidates_pending_crop() {
        let a = image([100, 100]);
        let b = image([100, 100]);
        let mut tool = CropTool::default();
        tool.start(&[a.clone(), b.clone()]);
        tool.validate_targets(&[b.clone(), a.clone()]);
        assert!(!tool.active());
        tool.start(&[a.clone(), b.clone()]);
        a.lock().unwrap().rotate_cw();
        tool.validate_targets(&[a, b]);
        assert!(!tool.active());
    }

    fn input(tool: &mut CropTool, ctx: &egui::Context, events: Vec<egui::Event>, shift: bool) -> bool {
        let mut changed = false;
        let rect = egui::Rect::from_min_size(egui::Pos2::ZERO, egui::vec2(400.0, 200.0));
        let _ = ctx.run(
            egui::RawInput {
                screen_rect: Some(rect),
                events,
                modifiers: if shift {
                    egui::Modifiers::SHIFT
                } else {
                    egui::Modifiers::NONE
                },
                ..Default::default()
            },
            |ctx| {
                egui::CentralPanel::default().show(ctx, |ui| {
                    let response = ui.allocate_rect(rect, egui::Sense::click_and_drag());
                    // A zoomed view of a non-square image.
                    changed |= tool.render(
                        ui,
                        &response,
                        WidgetToTextureTransform {
                            widget_rect: rect,
                            uv_min: egui::vec2(0.25, 0.25),
                            uv_max: egui::vec2(0.75, 0.75),
                            image_size: [800, 400],
                        },
                        true,
                    );
                });
            },
        );
        changed
    }

    fn button(pos: egui::Pos2, pressed: bool) -> egui::Event {
        egui::Event::PointerButton {
            pos,
            button: egui::PointerButton::Primary,
            pressed,
            modifiers: egui::Modifiers::NONE,
        }
    }

    #[test]
    fn zoomed_drawing_stays_temporary_and_shift_updates_without_mouse_motion() {
        let target = image([800, 400]);
        let mut tool = CropTool::default();
        tool.start(std::slice::from_ref(&target));
        let ctx = egui::Context::default();
        let start = egui::pos2(80.0, 40.0);
        let end = egui::pos2(240.0, 120.0);
        input(
            &mut tool,
            &ctx,
            vec![egui::Event::PointerMoved(start), button(start, true)],
            false,
        );
        input(&mut tool, &ctx, vec![egui::Event::PointerMoved(end)], false);
        assert_eq!(
            tool.region.unwrap().pixels([800, 400]).unwrap(),
            PixelCrop {
                x: 280,
                y: 140,
                width: 160,
                height: 80
            }
        );
        assert!(tool.take_region().is_none()); // Enter during a drag cannot commit it.
        input(&mut tool, &ctx, vec![], true);
        let square = tool.region.unwrap().pixels([800, 400]).unwrap();
        assert_eq!([square.width, square.height], [160, 160]);
        input(&mut tool, &ctx, vec![button(end, false)], false);
        assert!(!tool.is_dragging());
        assert!(tool.active());
        assert!(!target.lock().unwrap().has_pending_changes());
        assert!(!target.lock().unwrap().can_undo());
        assert!(tool.take_region().is_some());
        assert!(!tool.active());
    }

    #[test]
    fn press_and_release_in_one_frame_use_the_press_position_as_anchor() {
        let mut tool = CropTool::default();
        tool.start(&[image([800, 400])]);
        let ctx = egui::Context::default();
        let start = egui::pos2(80.0, 40.0);
        let end = egui::pos2(240.0, 120.0);
        input(
            &mut tool,
            &ctx,
            vec![
                egui::Event::PointerMoved(start),
                button(start, true),
                egui::Event::PointerMoved(end),
                button(end, false),
            ],
            false,
        );
        assert_eq!(
            tool.region.unwrap().pixels([800, 400]).unwrap(),
            PixelCrop {
                x: 280,
                y: 140,
                width: 160,
                height: 80
            }
        );
        assert!(!tool.is_dragging());
    }

    #[test]
    fn release_without_motion_notifies_controls_that_crop_can_be_applied() {
        let mut tool = CropTool::default();
        tool.start(&[image([800, 400])]);
        let ctx = egui::Context::default();
        let start = egui::pos2(80.0, 40.0);
        let end = egui::pos2(240.0, 120.0);
        input(
            &mut tool,
            &ctx,
            vec![egui::Event::PointerMoved(start), button(start, true)],
            false,
        );
        input(&mut tool, &ctx, vec![egui::Event::PointerMoved(end)], false);
        let before = tool.region;
        assert!(input(&mut tool, &ctx, vec![button(end, false)], false));
        assert_eq!(tool.region, before);
        assert!(!tool.is_dragging());
    }

    #[test]
    fn plain_click_does_not_insert_a_crop() {
        let mut tool = CropTool::default();
        tool.start(&[image([800, 400])]);
        let ctx = egui::Context::default();
        let pos = egui::pos2(100.0, 80.0);
        input(
            &mut tool,
            &ctx,
            vec![egui::Event::PointerMoved(pos), button(pos, true)],
            false,
        );
        input(&mut tool, &ctx, vec![button(pos, false)], false);
        assert!(tool.region.is_none() && !tool.is_dragging());
    }

    #[test]
    fn dragging_outside_a_pending_crop_does_not_replace_it() {
        let mut tool = CropTool::default();
        tool.start(&[image([800, 400])]);
        let ctx = egui::Context::default();
        let start = egui::pos2(80.0, 40.0);
        let end = egui::pos2(240.0, 120.0);
        input(
            &mut tool,
            &ctx,
            vec![egui::Event::PointerMoved(start), button(start, true)],
            false,
        );
        input(&mut tool, &ctx, vec![egui::Event::PointerMoved(end)], false);
        input(&mut tool, &ctx, vec![button(end, false)], false);
        let pending = tool.region;

        let miss = egui::pos2(320.0, 160.0);
        input(
            &mut tool,
            &ctx,
            vec![egui::Event::PointerMoved(miss), button(miss, true)],
            false,
        );
        input(
            &mut tool,
            &ctx,
            vec![egui::Event::PointerMoved(egui::pos2(360.0, 180.0))],
            false,
        );

        assert_eq!(tool.region, pending);
        assert!(!tool.is_dragging());
    }
}
