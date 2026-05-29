use std::sync::{Arc, Mutex};

use eframe::egui;

use crate::annotations::{
    AnnotationHitPart, AnnotationId, LineAnnotationData, WidgetToTextureTransform, paint_line_handles,
    paint_line_overlay,
};
use crate::modified_image::{ImageUndoAction, ModifiedImage};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AnnotationMode {
    Select,
    AddLine,
}

#[derive(Clone)]
struct EditSnapshot {
    image: Arc<Mutex<ModifiedImage>>,
    id: AnnotationId,
    data: LineAnnotationData,
}

#[derive(Clone)]
struct EditDrag {
    id: AnnotationId,
    kind: EditDragKind,
    prev_texture_pos: egui::Vec2,
    moved: bool,
    snapshots: Vec<EditSnapshot>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum EditDragKind {
    Body,
    Handle(usize),
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
    default_line: LineAnnotationData,
    create_drag: Option<CreateDrag>,
    edit_drag: Option<EditDrag>,
}

impl Default for AnnotationTool {
    fn default() -> Self {
        Self {
            mode: AnnotationMode::Select,
            selected_id: AnnotationId::default(),
            default_line: LineAnnotationData::default(),
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
            self.selected_id = AnnotationId::default();
        }
    }

    pub fn default_line_mut(&mut self) -> &mut LineAnnotationData {
        &mut self.default_line
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
        let painter = ui.painter();
        if let Ok(image) = image.lock() {
            if self.selected_id.is_valid() {
                if let Some(element) = image.annotations().find_by_id(self.selected_id) {
                    paint_line_handles(painter, element, &transform);
                }
            }
        }

        if let Some(create) = &self.create_drag {
            let mut data = self.default_line;
            data.p1 = create.start;
            data.p2 = create.current;
            paint_line_overlay(painter, &data, &transform);
        }

        if first_valid_image {
            self.handle_input(response, transform, visible_images);
        }
    }

    fn handle_input(
        &mut self,
        response: &egui::Response,
        transform: WidgetToTextureTransform,
        visible_images: &[Arc<Mutex<ModifiedImage>>],
    ) {
        let shortcut_modifier = response
            .ctx
            .input(|input| input.modifiers.ctrl || input.modifiers.command || input.modifiers.mac_cmd);
        if shortcut_modifier && self.create_drag.is_none() && self.edit_drag.is_none() {
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

        if let Some(pressed_pos) = primary_pressed_pos
            && self.create_drag.is_none()
            && self.edit_drag.is_none()
        {
            let pressed_texture_pos = transform.widget_to_texture(pressed_pos);
            match self.mode {
                AnnotationMode::AddLine => {
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
                create.current = texture_pos;
            }
        }

        if self.edit_drag.is_some() && (primary_down || primary_released) {
            self.update_edit_drag(texture_pos, visible_images);
        }

        if primary_released {
            self.finish_create_drag(visible_images);
            self.finish_edit_drag();
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
        let snapshots = capture_line_snapshots(hit.id, visible_images);
        let kind = match hit.part {
            AnnotationHitPart::Body => EditDragKind::Body,
            AnnotationHitPart::Handle => EditDragKind::Handle(hit.handle_idx.unwrap_or(0)),
        };
        self.edit_drag = Some(EditDrag {
            id: hit.id,
            kind,
            prev_texture_pos: texture_pos,
            moved: false,
            snapshots,
        });
    }

    fn finish_create_drag(&mut self, visible_images: &[Arc<Mutex<ModifiedImage>>]) {
        let Some(create) = self.create_drag.take() else {
            return;
        };
        if (create.current - create.start).length_sq() <= 1e-10 {
            return;
        }
        let id = AnnotationId::next();
        let mut data = self.default_line;
        data.p1 = create.start;
        data.p2 = create.current;
        for image in visible_images {
            if let Ok(mut image) = image.lock() {
                image.add_line(id, data);
            }
        }
        self.selected_id = id;
    }

    fn update_edit_drag(&mut self, texture_pos: egui::Vec2, visible_images: &[Arc<Mutex<ModifiedImage>>]) {
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
            EditDragKind::Handle(handle_idx) => {
                if (texture_pos - edit.prev_texture_pos).length_sq() <= 1e-12 {
                    return;
                }
                for image in visible_images {
                    if let Ok(mut image) = image.lock() {
                        if let Some(element) = image.annotations_mut().find_by_id_mut(edit.id) {
                            element.move_handle_to(handle_idx, texture_pos);
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
                image.push_undo_action(ImageUndoAction::RestoreLine {
                    id: snapshot.id,
                    data: snapshot.data,
                });
            }
        }
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

fn capture_line_snapshots(id: AnnotationId, visible_images: &[Arc<Mutex<ModifiedImage>>]) -> Vec<EditSnapshot> {
    visible_images
        .iter()
        .filter_map(|image| {
            let data = image.lock().ok().and_then(|image| {
                image
                    .annotations()
                    .find_by_id(id)
                    .and_then(|element| element.line().copied())
            })?;
            Some(EditSnapshot {
                image: image.clone(),
                id,
                data,
            })
        })
        .collect()
}
