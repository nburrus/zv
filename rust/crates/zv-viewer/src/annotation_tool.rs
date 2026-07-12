use std::sync::{Arc, Mutex};

use eframe::egui;

use crate::annotations::{
    AnnotationElement, AnnotationHandle, AnnotationHitPart, AnnotationId, BoundingBox, LineEndpointStyle, LineSegment,
    LineStyle, ShiftConstraint, StrokeStyle, WidgetToTextureTransform, paint_annotation_handles, paint_element_overlay,
};
use crate::modified_image::{ImageUndoAction, ModifiedImage};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AnnotationMode {
    Select,
    AddLine,
    AddArrow,
    AddRectangle,
    AddEllipse,
}

impl AnnotationMode {
    fn shift_constraint(self) -> Option<ShiftConstraint> {
        match self {
            Self::AddLine | Self::AddArrow => Some(ShiftConstraint::SnapTo45Degrees),
            Self::AddRectangle | Self::AddEllipse => Some(ShiftConstraint::Square),
            Self::Select => None,
        }
    }
}

#[derive(Clone)]
struct EditSnapshot {
    image: Arc<Mutex<ModifiedImage>>,
    element: AnnotationElement,
}

#[derive(Clone)]
struct EditDrag {
    id: AnnotationId,
    kind: EditDragKind,
    prev_texture_pos: egui::Vec2,
    moved: bool,
    snapshots: Vec<EditSnapshot>,
    /// For handle drags: the opposite handle, which stays fixed, and the
    /// Shift constraint of the dragged element.
    fixed_anchor: Option<(egui::Vec2, ShiftConstraint)>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum EditDragKind {
    Body,
    Handle(AnnotationHandle),
}

#[derive(Clone, Debug)]
struct CreateDrag {
    start: egui::Vec2,
    current: egui::Vec2,
}

#[derive(Clone)]
pub struct AnnotationTool {
    mode: AnnotationMode,
    selected_id: AnnotationId,
    default_stroke: StrokeStyle,
    default_line_endpoints: [LineEndpointStyle; 2],
    default_arrow_endpoints: [LineEndpointStyle; 2],
    create_drag: Option<CreateDrag>,
    edit_drag: Option<EditDrag>,
}

impl Default for AnnotationTool {
    fn default() -> Self {
        Self {
            mode: AnnotationMode::Select,
            selected_id: AnnotationId::default(),
            default_stroke: StrokeStyle::default(),
            default_line_endpoints: [LineEndpointStyle::None, LineEndpointStyle::None],
            default_arrow_endpoints: [LineEndpointStyle::None, LineEndpointStyle::Arrow],
            create_drag: None,
            edit_drag: None,
        }
    }
}

impl AnnotationTool {
    pub fn mode(&self) -> AnnotationMode {
        self.mode
    }

    pub fn selected_id_is_valid(&self) -> bool {
        self.selected_id.is_valid()
    }

    pub fn selected_id(&self) -> AnnotationId {
        self.selected_id
    }

    pub fn is_creating(&self) -> bool {
        self.create_drag.is_some()
    }

    pub fn is_editing(&self) -> bool {
        self.edit_drag.is_some()
    }

    pub fn set_mode(&mut self, mode: AnnotationMode) {
        if self.mode != mode {
            self.cancel_current_action();
            self.mode = mode;
            // Clearing the selection here is load-bearing: the controls window
            // assumes an Add* mode never coexists with a selected element of a
            // different kind when it picks which style panel to show.
            self.selected_id = AnnotationId::default();
        }
    }

    pub fn default_stroke(&self) -> StrokeStyle {
        self.default_stroke
    }

    pub fn default_line_style(&self) -> LineStyle {
        self.creation_line_style()
    }

    pub fn set_default_line_style(&mut self, style: LineStyle) {
        self.default_stroke = style.stroke;
        let endpoints = [style.start_style, style.end_style];
        if self.mode == AnnotationMode::AddArrow {
            self.default_arrow_endpoints = endpoints;
        } else {
            self.default_line_endpoints = endpoints;
        }
    }

    pub fn set_default_stroke(&mut self, stroke: StrokeStyle) {
        self.default_stroke = stroke;
    }

    pub fn delete_selected(&mut self, visible_images: &[Arc<Mutex<ModifiedImage>>]) {
        if !self.selected_id.is_valid() {
            return;
        }
        let id = self.selected_id;
        for image in visible_images {
            if let Ok(mut image) = image.lock() {
                image.remove_annotation_with_undo(id);
            }
        }
        self.selected_id = AnnotationId::default();
    }

    pub fn clear_selection_if_missing(&mut self, visible_images: &[Arc<Mutex<ModifiedImage>>]) {
        if !self.selected_id.is_valid() {
            return;
        }
        let selected_id = self.selected_id;
        let exists = visible_images.iter().any(|image| {
            image
                .lock()
                .ok()
                .is_some_and(|image| image.annotations().find_by_id(selected_id).is_some())
        });
        if !exists {
            self.selected_id = AnnotationId::default();
        }
    }

    pub fn cancel_current_action(&mut self) {
        self.create_drag = None;
        self.edit_drag = None;
    }

    pub fn render_for_image(
        &mut self,
        ui: &egui::Ui,
        response: &egui::Response,
        image: &Arc<Mutex<ModifiedImage>>,
        transform: WidgetToTextureTransform,
        first_valid_image: bool,
        visible_images: &[Arc<Mutex<ModifiedImage>>],
    ) {
        // Process input before painting so keyboard-only modifier changes are
        // reflected in this frame, even when the pointer has not moved.
        if first_valid_image {
            self.handle_input(response, transform, visible_images);
        }

        let painter = ui.painter();
        let mut hover_hit = None;
        if let Ok(image) = image.lock() {
            if self.selected_id.is_valid() {
                if let Some(element) = image.annotations().find_by_id(self.selected_id) {
                    paint_annotation_handles(painter, element, &transform);
                }
            }
            if first_valid_image {
                hover_hit = response.hover_pos().and_then(|pointer_pos| {
                    image
                        .annotations()
                        .hit_test(pointer_pos, &transform, self.selected_id, 6.0, 4.0)
                        .map(|hit| hit.part)
                });
            }
        }

        if let Some(create) = &self.create_drag {
            let preview = self.creation_element(AnnotationId::default(), create);
            paint_element_overlay(painter, &preview, &transform);
        }

        if first_valid_image {
            if let Some(cursor) = cursor_icon_for_state(
                self.mode,
                hover_hit,
                self.create_drag.is_some(),
                self.edit_drag.as_ref().map(|drag| drag.kind),
                shortcut_modifier(response),
            ) {
                if self.create_drag.is_some() || self.edit_drag.is_some() {
                    response.clone().on_hover_and_drag_cursor(cursor);
                } else {
                    response.clone().on_hover_cursor(cursor);
                }
            }
        }
    }

    fn handle_input(
        &mut self,
        response: &egui::Response,
        transform: WidgetToTextureTransform,
        visible_images: &[Arc<Mutex<ModifiedImage>>],
    ) {
        if shortcut_modifier(response) && self.create_drag.is_none() && self.edit_drag.is_none() {
            return;
        }

        let primary_pressed_pos = primary_pressed_pos(response);
        let primary_down = response
            .ctx
            .input(|input| input.pointer.button_down(egui::PointerButton::Primary));
        let primary_released = response
            .ctx
            .input(|input| input.pointer.button_released(egui::PointerButton::Primary));
        let pointer_pos = response
            .ctx
            .input(|input| input.pointer.interact_pos())
            .or_else(|| response.hover_pos());

        let Some(pointer_pos) = pointer_pos else {
            if primary_released {
                self.finish_create_drag(visible_images);
                self.finish_edit_drag();
            }
            return;
        };
        let texture_pos = transform.widget_to_texture(pointer_pos);
        let shift_down = response.ctx.input(|input| input.modifiers.shift);

        if let Some(pressed_pos) = primary_pressed_pos
            && self.create_drag.is_none()
            && self.edit_drag.is_none()
        {
            let pressed_texture_pos = transform.widget_to_texture(pressed_pos);
            match self.mode {
                AnnotationMode::AddLine
                | AnnotationMode::AddArrow
                | AnnotationMode::AddRectangle
                | AnnotationMode::AddEllipse => {
                    self.selected_id = AnnotationId::default();
                    self.create_drag = Some(CreateDrag {
                        start: pressed_texture_pos,
                        current: pressed_texture_pos,
                    });
                }
                AnnotationMode::Select => {
                    self.start_select_interaction(pressed_pos, pressed_texture_pos, &transform, visible_images);
                }
            }
        }

        if let Some(create) = &mut self.create_drag {
            if primary_down || primary_released {
                create.current = match self.mode.shift_constraint() {
                    Some(constraint) => {
                        constrained_texture_pos(create.start, pointer_pos, constraint, shift_down, &transform)
                    }
                    // Unreachable in practice: a create drag only exists in Add* modes.
                    None => texture_pos,
                };
            }
        }

        if self.edit_drag.is_some() && (primary_down || primary_released) {
            self.update_edit_drag(texture_pos, pointer_pos, shift_down, &transform, visible_images);
        }

        // Annotation compositing happens before the image window is rendered.
        // Any live document edit therefore needs one follow-up frame. Request
        // it while dragging so Shift press/release never depends on mouse input.
        if primary_down && (self.create_drag.is_some() || self.edit_drag.is_some()) {
            response.ctx.request_repaint_of(egui::ViewportId::ROOT);
        }

        if primary_released {
            self.finish_create_drag(visible_images);
            self.finish_edit_drag();
            response.ctx.request_repaint_of(egui::ViewportId::ROOT);
        }
    }

    fn start_select_interaction(
        &mut self,
        widget_pos: egui::Pos2,
        texture_pos: egui::Vec2,
        transform: &WidgetToTextureTransform,
        visible_images: &[Arc<Mutex<ModifiedImage>>],
    ) {
        let Some(first_image) = visible_images.first() else {
            return;
        };
        let hit = first_image.lock().ok().and_then(|image| {
            image
                .annotations()
                .hit_test(widget_pos, transform, self.selected_id, 6.0, 4.0)
        });
        let Some(hit) = hit else {
            self.selected_id = AnnotationId::default();
            return;
        };

        self.selected_id = hit.id;
        let snapshots = capture_snapshots(hit.id, visible_images);
        let (kind, fixed_anchor) = match hit.part {
            AnnotationHitPart::Body => (EditDragKind::Body, None),
            AnnotationHitPart::Handle(handle) => {
                let fixed_anchor = snapshots.first().and_then(|snapshot| {
                    let anchor = snapshot.element.handle_texture_pos(handle.opposite())?;
                    Some((anchor, snapshot.element.shift_constraint()))
                });
                (EditDragKind::Handle(handle), fixed_anchor)
            }
        };
        self.edit_drag = Some(EditDrag {
            id: hit.id,
            kind,
            prev_texture_pos: texture_pos,
            moved: false,
            snapshots,
            fixed_anchor,
        });
    }

    fn finish_create_drag(&mut self, visible_images: &[Arc<Mutex<ModifiedImage>>]) {
        let Some(create) = self.create_drag.take() else {
            return;
        };
        if (create.current - create.start).length_sq() <= 1e-10 {
            return;
        }
        let element = self.creation_element(AnnotationId::next(), &create);
        self.selected_id = element.id();
        for image in visible_images {
            if let Ok(mut image) = image.lock() {
                image.add_element(element.clone());
            }
        }
        self.mode = AnnotationMode::Select;
    }

    fn creation_element(&self, id: AnnotationId, create: &CreateDrag) -> AnnotationElement {
        match self.mode {
            AnnotationMode::AddRectangle | AnnotationMode::AddEllipse => {
                let bounds = BoundingBox {
                    min: create.start.min(create.current),
                    max: create.start.max(create.current),
                };
                let stroke = self.default_stroke;
                if self.mode == AnnotationMode::AddEllipse {
                    AnnotationElement::Ellipse { id, bounds, stroke }
                } else {
                    AnnotationElement::Rectangle { id, bounds, stroke }
                }
            }
            AnnotationMode::Select | AnnotationMode::AddLine | AnnotationMode::AddArrow => AnnotationElement::Line {
                id,
                segment: LineSegment {
                    p1: create.start,
                    p2: create.current,
                },
                style: self.creation_line_style(),
            },
        }
    }

    fn update_edit_drag(
        &mut self,
        texture_pos: egui::Vec2,
        widget_pos: egui::Pos2,
        shift_down: bool,
        transform: &WidgetToTextureTransform,
        visible_images: &[Arc<Mutex<ModifiedImage>>],
    ) {
        let Some(edit) = &mut self.edit_drag else {
            return;
        };
        match edit.kind {
            EditDragKind::Body => {
                let delta = texture_pos - edit.prev_texture_pos;
                if delta.length_sq() <= 1e-12 {
                    return;
                }
                for image in visible_images {
                    if let Ok(mut image) = image.lock() {
                        if let Some(element) = image.annotations_mut().find_by_id_mut(edit.id) {
                            element.move_by(delta);
                            image.mark_annotations_dirty();
                        }
                    }
                }
                edit.prev_texture_pos = texture_pos;
                edit.moved = true;
            }
            EditDragKind::Handle(handle) => {
                let texture_pos = edit.fixed_anchor.map_or(texture_pos, |(anchor, constraint)| {
                    constrained_texture_pos(anchor, widget_pos, constraint, shift_down, transform)
                });
                if (texture_pos - edit.prev_texture_pos).length_sq() <= 1e-12 {
                    return;
                }
                for image in visible_images {
                    if let Ok(mut image) = image.lock() {
                        if let Some(element) = image.annotations_mut().find_by_id_mut(edit.id) {
                            if let Some((anchor, _)) = edit.fixed_anchor {
                                element.move_handle_with_anchor(handle, texture_pos, anchor);
                            } else {
                                element.move_handle_to(handle, texture_pos);
                            }
                            image.mark_annotations_dirty();
                        }
                    }
                }
                edit.prev_texture_pos = texture_pos;
                edit.moved = true;
            }
        }
    }

    fn finish_edit_drag(&mut self) {
        let Some(edit) = self.edit_drag.take() else {
            return;
        };
        if !edit.moved {
            return;
        }
        for snapshot in edit.snapshots {
            if let Ok(mut image) = snapshot.image.lock() {
                image.push_undo_action(ImageUndoAction::RestoreElementState {
                    element: snapshot.element,
                });
            }
        }
    }

    fn creation_line_style(&self) -> LineStyle {
        let [start_style, end_style] = if self.mode == AnnotationMode::AddArrow {
            self.default_arrow_endpoints
        } else {
            self.default_line_endpoints
        };
        LineStyle {
            stroke: self.default_stroke,
            start_style,
            end_style,
        }
    }
}

fn constrained_texture_pos(
    anchor_texture: egui::Vec2,
    pointer_widget: egui::Pos2,
    constraint: ShiftConstraint,
    shift_down: bool,
    transform: &WidgetToTextureTransform,
) -> egui::Vec2 {
    if !shift_down {
        return transform.widget_to_texture(pointer_widget);
    }
    let anchor_widget = transform.texture_to_widget(anchor_texture);
    let delta = pointer_widget - anchor_widget;
    let constrained_delta = match constraint {
        ShiftConstraint::SnapTo45Degrees => snap_to_45_degrees(delta),
        ShiftConstraint::Square => constrain_to_square(delta),
    };
    transform.widget_to_texture(anchor_widget + constrained_delta)
}

fn snap_to_45_degrees(delta: egui::Vec2) -> egui::Vec2 {
    if delta.length_sq() <= f32::EPSILON {
        return delta;
    }
    let step = std::f32::consts::FRAC_PI_4;
    let angle = (delta.y.atan2(delta.x) / step).round() * step;
    egui::Vec2::angled(angle) * delta.length()
}

fn constrain_to_square(delta: egui::Vec2) -> egui::Vec2 {
    let extent = delta.x.abs().max(delta.y.abs());
    egui::vec2(delta.x.signum() * extent, delta.y.signum() * extent)
}

fn shortcut_modifier(response: &egui::Response) -> bool {
    response
        .ctx
        .input(|input| input.modifiers.ctrl || input.modifiers.command || input.modifiers.mac_cmd)
}

fn cursor_icon_for_state(
    mode: AnnotationMode,
    hover_hit: Option<AnnotationHitPart>,
    is_creating: bool,
    edit_drag_kind: Option<EditDragKind>,
    shortcut_modifier: bool,
) -> Option<egui::CursorIcon> {
    if edit_drag_kind.is_some() {
        return Some(egui::CursorIcon::Grabbing);
    }
    if is_creating {
        return Some(egui::CursorIcon::Crosshair);
    }
    if shortcut_modifier {
        return None;
    }
    match mode {
        AnnotationMode::AddLine
        | AnnotationMode::AddArrow
        | AnnotationMode::AddRectangle
        | AnnotationMode::AddEllipse => Some(egui::CursorIcon::Crosshair),
        AnnotationMode::Select if hover_hit.is_some() => Some(egui::CursorIcon::Grab),
        AnnotationMode::Select => None,
    }
}

fn primary_pressed_pos(response: &egui::Response) -> Option<egui::Pos2> {
    response
        .ctx
        .input(|input| {
            input
                .pointer
                .button_pressed(egui::PointerButton::Primary)
                .then(|| input.pointer.interact_pos())
                .flatten()
        })
        .filter(|pos| response.rect.contains(*pos))
}

fn capture_snapshots(id: AnnotationId, visible_images: &[Arc<Mutex<ModifiedImage>>]) -> Vec<EditSnapshot> {
    visible_images
        .iter()
        .filter_map(|image| {
            let element = image.lock().ok()?.annotations().find_by_id(id)?.clone();
            Some(EditSnapshot {
                image: image.clone(),
                element,
            })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn line_constraint_snaps_to_nearest_45_degrees() {
        let horizontal = snap_to_45_degrees(egui::vec2(10.0, 1.0));
        assert!(horizontal.y.abs() < 1e-4);

        let diagonal = snap_to_45_degrees(egui::vec2(8.0, 6.0));
        assert!((diagonal.x.abs() - diagonal.y.abs()).abs() < 1e-4);
    }

    #[test]
    fn rectangle_and_ellipse_constraint_preserves_drag_direction() {
        assert_eq!(constrain_to_square(egui::vec2(-4.0, 9.0)), egui::vec2(-9.0, 9.0));
        assert_eq!(constrain_to_square(egui::vec2(7.0, -2.0)), egui::vec2(7.0, -7.0));
    }

    #[test]
    fn edited_stroke_becomes_the_default_for_every_annotation_tool() {
        let mut tool = AnnotationTool::default();
        let stroke = StrokeStyle {
            color: egui::Color32::RED,
            width: 6.0,
        };
        tool.set_default_stroke(stroke);
        assert_eq!(tool.default_line_style().stroke, stroke);
        assert_eq!(tool.default_stroke, stroke);
        tool.set_mode(AnnotationMode::AddArrow);
        assert_eq!(tool.default_line_style().stroke, stroke);
    }
}
