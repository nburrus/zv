use std::path::{Path, PathBuf};

use eframe::egui_wgpu::wgpu;

use crate::annotations::{
    AnnotationDocument, AnnotationElement, AnnotationId, AnnotationRenderer, BoundingBox, LineEndpointStyle,
    StrokeStyle, TextStyle, resize_text_bounds_to_content,
};
use crate::color_image::ImageSRGBA;
use crate::image_io::write_rgba_image;
use crate::image_item_data::ImageItemData;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum RotationDirection {
    Clockwise,
    CounterClockwise,
}

#[derive(Clone, Debug)]
pub enum ImageUndoAction {
    RemoveAnnotation {
        id: AnnotationId,
    },
    /// Re-adds a deleted element to the document.
    RestoreAnnotation {
        element: AnnotationElement,
    },
    /// Overwrites an existing element with an earlier state of itself.
    RestoreElementState {
        element: AnnotationElement,
    },
    ReplaceBaseImage {
        cpu_data: ImageSRGBA,
        annotations: AnnotationDocument,
    },
}

pub struct ModifiedImage {
    original_data: ImageItemData,
    saved_data: Option<ImageSRGBA>,
    annotated_data: Option<ImageItemData>,
    annotations: AnnotationDocument,
    actions: Vec<ImageUndoAction>,
    annotations_dirty: bool,
    source_path: Option<PathBuf>,
    base_dirty: bool,
    display_revision: u64,
}

impl ModifiedImage {
    pub fn new(original_data: ImageItemData, source_path: Option<PathBuf>) -> Self {
        Self {
            original_data,
            saved_data: None,
            annotated_data: None,
            annotations: AnnotationDocument::default(),
            actions: Vec::new(),
            annotations_dirty: false,
            source_path,
            base_dirty: false,
            display_revision: 0,
        }
    }

    pub fn new_unsaved(original_data: ImageItemData) -> Self {
        let mut image = Self::new(original_data, None);
        image.base_dirty = true;
        image
    }

    pub fn final_data(&self) -> &ImageItemData {
        self.annotated_data.as_ref().unwrap_or(&self.original_data)
    }

    pub fn final_data_mut(&mut self) -> &mut ImageItemData {
        self.annotated_data.as_mut().unwrap_or(&mut self.original_data)
    }

    pub fn display_revision(&self) -> u64 {
        self.display_revision
    }

    pub fn pre_annotation_data(&self) -> &ImageItemData {
        &self.original_data
    }

    pub fn annotations(&self) -> &AnnotationDocument {
        &self.annotations
    }

    pub fn image_size(&self) -> [u32; 2] {
        let data = self.original_data.cpu_data();
        [data.width(), data.height()]
    }

    pub fn annotations_mut(&mut self) -> &mut AnnotationDocument {
        &mut self.annotations
    }

    pub fn has_pending_changes(&self) -> bool {
        self.base_dirty || !self.annotations.is_empty()
    }

    #[allow(dead_code)]
    pub fn can_undo(&self) -> bool {
        !self.actions.is_empty()
    }

    pub fn source_path(&self) -> Option<&Path> {
        self.source_path.as_deref()
    }

    pub fn mark_annotations_dirty(&mut self) {
        self.annotations_dirty = true;
    }

    pub fn push_undo_action(&mut self, action: ImageUndoAction) {
        self.actions.push(action);
    }

    pub fn add_element(&mut self, element: AnnotationElement) {
        let id = element.id();
        self.annotations.add_element(element);
        self.mark_annotations_dirty();
        self.push_undo_action(ImageUndoAction::RemoveAnnotation { id });
    }

    pub fn remove_annotation_with_undo(&mut self, id: AnnotationId) {
        if let Some(element) = self.annotations.remove_by_id(id) {
            self.mark_annotations_dirty();
            self.push_undo_action(ImageUndoAction::RestoreAnnotation { element });
        }
    }

    pub fn update_stroke_style(&mut self, id: AnnotationId, stroke: StrokeStyle) -> bool {
        let Some(element) = self.annotations.find_by_id_mut(id) else {
            return false;
        };
        let Some(current) = element.stroke_mut() else {
            return false;
        };
        if *current == stroke {
            return false;
        }
        *current = stroke;
        self.mark_annotations_dirty();
        true
    }

    pub fn update_line_endpoint_styles(
        &mut self,
        id: AnnotationId,
        start_style: LineEndpointStyle,
        end_style: LineEndpointStyle,
    ) -> bool {
        let Some(style) = self
            .annotations
            .find_by_id_mut(id)
            .and_then(|element| element.line_style_mut())
        else {
            return false;
        };
        if style.start_style == start_style && style.end_style == end_style {
            return false;
        }
        style.start_style = start_style;
        style.end_style = end_style;
        self.mark_annotations_dirty();
        true
    }

    pub fn update_text(&mut self, id: AnnotationId, replacement: TextStyle) -> bool {
        let size = self.image_size();
        let Some(AnnotationElement::Text { bounds, style, .. }) = self.annotations.find_by_id_mut(id) else {
            return false;
        };
        if *style == replacement {
            return false;
        }
        let reflow = style.text != replacement.text || style.font_size != replacement.font_size;
        *style = replacement;
        if reflow {
            resize_text_bounds_to_content(bounds, style, size);
        }
        self.mark_annotations_dirty();
        true
    }

    pub fn undo_last_change(&mut self) {
        let Some(action) = self.actions.pop() else {
            return;
        };
        match action {
            ImageUndoAction::RemoveAnnotation { id } => {
                self.annotations.remove_by_id(id);
                self.mark_annotations_dirty();
            }
            ImageUndoAction::RestoreAnnotation { element } => {
                self.annotations.add_element(element);
                self.mark_annotations_dirty();
            }
            ImageUndoAction::RestoreElementState { element } => {
                if self.annotations.replace_by_id(element) {
                    self.mark_annotations_dirty();
                }
            }
            ImageUndoAction::ReplaceBaseImage { cpu_data, annotations } => {
                self.base_dirty = self
                    .saved_data
                    .as_ref()
                    .is_some_and(|saved_data| !image_pixels_equal(&cpu_data, saved_data));
                if !self.base_dirty {
                    self.saved_data = None;
                }
                self.original_data = ImageItemData::new(cpu_data);
                self.annotations = annotations;
                self.annotated_data = None;
                self.annotations_dirty = !self.annotations.is_empty();
                self.bump_display_revision();
            }
        }
    }

    pub fn rotate_cw(&mut self) {
        self.rotate(RotationDirection::Clockwise);
    }

    pub fn rotate_ccw(&mut self) {
        self.rotate(RotationDirection::CounterClockwise);
    }

    pub fn apply_base_image_transform(&mut self, transform: impl FnOnce(&ImageSRGBA) -> ImageSRGBA) -> bool {
        let replacement = transform(self.original_data.cpu_data());
        if image_pixels_equal(self.original_data.cpu_data(), &replacement) {
            return false;
        }
        self.replace_base_image(replacement, self.annotations.clone());
        true
    }

    fn rotate(&mut self, direction: RotationDirection) {
        let rotated = rotate_image(self.original_data.cpu_data(), direction);
        let mut rotated_annotations = self.annotations.clone();
        rotate_annotations(&mut rotated_annotations, direction);
        self.replace_base_image(rotated, rotated_annotations);
    }

    fn replace_base_image(&mut self, replacement: ImageSRGBA, replacement_annotations: AnnotationDocument) {
        self.ensure_saved_data_snapshot();
        let snapshot_cpu = self.original_data.cpu_data().clone();
        let snapshot_annotations = self.annotations.clone();
        self.original_data = ImageItemData::new(replacement);
        self.annotations = replacement_annotations;
        self.annotated_data = None;
        self.annotations_dirty = !self.annotations.is_empty();
        self.base_dirty = true;
        self.actions.push(ImageUndoAction::ReplaceBaseImage {
            cpu_data: snapshot_cpu,
            annotations: snapshot_annotations,
        });
        self.bump_display_revision();
    }

    fn ensure_saved_data_snapshot(&mut self) {
        if !self.base_dirty && self.saved_data.is_none() {
            self.saved_data = Some(self.original_data.cpu_data().clone());
        }
    }

    pub fn discard_changes(&mut self) {
        if !self.has_pending_changes() {
            return;
        }
        if let Some(saved_data) = self.saved_data.take() {
            self.original_data = ImageItemData::new(saved_data);
        }
        self.annotations.clear();
        self.actions.clear();
        self.annotated_data = None;
        self.annotations_dirty = false;
        self.base_dirty = false;
        self.bump_display_revision();
    }

    pub fn save_changes(&mut self, path: Option<&Path>) -> anyhow::Result<()> {
        if !self.has_pending_changes() {
            return Ok(());
        }
        let output_path = path
            .map(Path::to_path_buf)
            .or_else(|| self.source_path.clone())
            .ok_or_else(|| anyhow::anyhow!("no output path available for image"))?;
        write_rgba_image(&output_path, self.final_data().cpu_data())?;
        let saved = self.final_data().cpu_data().clone();
        self.original_data.set_cpu_data(saved);
        self.saved_data = None;
        self.source_path = Some(output_path);
        self.annotations.clear();
        self.actions.clear();
        self.annotated_data = None;
        self.annotations_dirty = false;
        self.base_dirty = false;
        self.bump_display_revision();
        Ok(())
    }

    pub fn update_annotations(
        &mut self,
        renderer: &mut AnnotationRenderer,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
    ) -> bool {
        if !self.annotations_dirty {
            return false;
        }
        if self.annotations.is_empty() {
            let had_annotated_data = self.annotated_data.is_some();
            self.annotated_data = None;
            self.annotations_dirty = false;
            if had_annotated_data {
                self.bump_display_revision();
            }
            return true;
        }
        let Some(annotated_data) = renderer.composite(device, queue, &self.original_data, &self.annotations) else {
            tracing::warn!("failed to composite annotation layer");
            return false;
        };
        self.annotated_data = Some(annotated_data);
        self.annotations_dirty = false;
        self.bump_display_revision();
        true
    }

    fn bump_display_revision(&mut self) {
        self.display_revision = self.display_revision.wrapping_add(1);
    }
}

fn image_pixels_equal(a: &ImageSRGBA, b: &ImageSRGBA) -> bool {
    a.width() == b.width()
        && a.height() == b.height()
        && (0..a.height()).all(|row| {
            let Some(a_row) = a.row_bytes(row) else {
                return false;
            };
            let Some(b_row) = b.row_bytes(row) else {
                return false;
            };
            let tight_len = a.width() as usize * 4;
            a_row[..tight_len] == b_row[..tight_len]
        })
}

fn rotate_annotations(document: &mut AnnotationDocument, direction: RotationDirection) {
    for element in document.elements_mut() {
        match element {
            AnnotationElement::Line { segment, .. } => {
                segment.p1 = rotate_uv(segment.p1, direction);
                segment.p2 = rotate_uv(segment.p2, direction);
            }
            AnnotationElement::Rectangle { bounds, .. }
            | AnnotationElement::Ellipse { bounds, .. }
            | AnnotationElement::Text { bounds, .. } => {
                rotate_bounding_box_uv(bounds, direction);
            }
        }
    }
}

fn rotate_bounding_box_uv(bounds: &mut BoundingBox, direction: RotationDirection) {
    let corners = [
        rotate_uv(bounds.min, direction),
        rotate_uv(eframe::egui::vec2(bounds.max.x, bounds.min.y), direction),
        rotate_uv(eframe::egui::vec2(bounds.min.x, bounds.max.y), direction),
        rotate_uv(bounds.max, direction),
    ];
    bounds.min = corners.iter().copied().reduce(|a, b| a.min(b)).unwrap();
    bounds.max = corners.iter().copied().reduce(|a, b| a.max(b)).unwrap();
}

fn rotate_uv(uv: eframe::egui::Vec2, direction: RotationDirection) -> eframe::egui::Vec2 {
    match direction {
        RotationDirection::Clockwise => eframe::egui::vec2(1.0 - uv.y, uv.x),
        RotationDirection::CounterClockwise => eframe::egui::vec2(uv.y, 1.0 - uv.x),
    }
}

fn rotate_image(src: &ImageSRGBA, direction: RotationDirection) -> ImageSRGBA {
    let in_w = src.width() as usize;
    let in_h = src.height() as usize;
    let out_w = in_h;
    let out_h = in_w;
    let mut out = ImageSRGBA::new(out_w as u32, out_h as u32);
    let src_bpr = src.bytes_per_row();
    let src_bytes = src.bytes();
    for out_r in 0..out_h {
        let row = out.row_mut(out_r as u32).expect("valid row");
        for out_c in 0..out_w {
            let (in_r, in_c) = match direction {
                RotationDirection::Clockwise => (in_h - out_c - 1, out_r),
                RotationDirection::CounterClockwise => (out_c, in_w - out_r - 1),
            };
            let src_off = in_r * src_bpr + in_c * 4;
            row[out_c] = crate::color_image::PixelSRGBA {
                r: src_bytes[src_off],
                g: src_bytes[src_off + 1],
                b: src_bytes[src_off + 2],
                a: src_bytes[src_off + 3],
            };
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::annotations::{LineSegment, LineStyle};
    use crate::color_editor::{OneShotOperation, apply_one_shot};
    use crate::color_image::{ImageSRGBA, PixelSRGBA};
    use crate::image_io::load_rgba_image;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn add_default_line(modified: &mut ModifiedImage, id: AnnotationId) {
        modified.add_element(AnnotationElement::Line {
            id,
            segment: LineSegment::default(),
            style: LineStyle::default(),
        });
    }

    fn image() -> ImageItemData {
        image_with_color(PixelSRGBA {
            r: 1,
            g: 2,
            b: 3,
            a: 255,
        })
    }

    fn image_with_color(color: PixelSRGBA) -> ImageItemData {
        let mut image = ImageSRGBA::new(2, 2);
        for row in 0..2 {
            image.row_mut(row).unwrap().fill(color);
        }
        ImageItemData::new(image)
    }

    fn first_pixel(data: &ImageItemData) -> [u8; 4] {
        data.pixel_rgba(0, 0).unwrap()
    }

    fn temp_png_path(name: &str) -> PathBuf {
        let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        std::env::temp_dir().join(format!("zv-{name}-{}-{stamp}.png", std::process::id()))
    }

    #[test]
    fn undo_line_creation_removes_annotation() {
        let mut modified = ModifiedImage::new(image(), None);
        let id = AnnotationId::next();
        add_default_line(&mut modified, id);
        assert!(!modified.annotations().is_empty());

        modified.undo_last_change();
        assert!(modified.annotations().is_empty());
    }

    #[test]
    fn live_rectangle_style_updates_can_be_committed_as_one_undo_step() {
        let mut modified = ModifiedImage::new(image(), None);
        let id = AnnotationId::next();
        let before_bounds = BoundingBox::default();
        let before_stroke = StrokeStyle::default();
        modified.add_element(AnnotationElement::Rectangle {
            id,
            bounds: before_bounds,
            stroke: before_stroke,
        });

        modified.update_stroke_style(
            id,
            StrokeStyle {
                color: eframe::egui::Color32::RED,
                width: 7.0,
            },
        );
        modified.push_undo_action(ImageUndoAction::RestoreElementState {
            element: AnnotationElement::Rectangle {
                id,
                bounds: before_bounds,
                stroke: before_stroke,
            },
        });
        modified.undo_last_change();

        let AnnotationElement::Rectangle { bounds, stroke, .. } = modified.annotations().find_by_id(id).unwrap() else {
            panic!("expected rectangle");
        };
        assert_eq!(*bounds, before_bounds);
        assert_eq!(*stroke, before_stroke);
    }

    #[test]
    fn live_line_style_updates_can_be_committed_as_one_undo_step() {
        let mut modified = ModifiedImage::new(image(), None);
        let id = AnnotationId::next();
        let before = LineStyle::default();
        modified.add_element(AnnotationElement::Line {
            id,
            segment: LineSegment::default(),
            style: before,
        });
        modified.actions.clear();

        modified.update_stroke_style(
            id,
            StrokeStyle {
                color: eframe::egui::Color32::RED,
                width: 7.0,
            },
        );
        modified.update_line_endpoint_styles(id, LineEndpointStyle::None, LineEndpointStyle::Arrow);
        modified.update_stroke_style(
            id,
            StrokeStyle {
                color: eframe::egui::Color32::GREEN,
                width: 12.0,
            },
        );
        modified.update_line_endpoint_styles(id, LineEndpointStyle::Arrow, LineEndpointStyle::None);

        assert!(!modified.can_undo());
        modified.push_undo_action(ImageUndoAction::RestoreElementState {
            element: AnnotationElement::Line {
                id,
                segment: LineSegment::default(),
                style: before,
            },
        });
        assert!(modified.can_undo());

        modified.undo_last_change();

        let style = modified.annotations().find_by_id(id).unwrap().line_style().unwrap();
        assert_eq!(*style, before);
        assert!(!modified.can_undo());
    }

    #[test]
    fn discard_clears_annotations_and_undo() {
        let mut modified = ModifiedImage::new(image(), None);
        add_default_line(&mut modified, AnnotationId::next());
        modified.discard_changes();
        assert!(modified.annotations().is_empty());
        assert!(!modified.can_undo());
    }

    #[test]
    fn save_without_annotations_is_noop() {
        let mut modified = ModifiedImage::new(image(), None);
        let revision = modified.display_revision();
        modified.save_changes(None).unwrap();
        assert_eq!(modified.display_revision(), revision);
        assert!(modified.annotations().is_empty());
        assert!(!modified.can_undo());
    }

    #[test]
    fn unsaved_image_requires_save_and_can_be_written() {
        let output = std::env::temp_dir().join(format!("zv-unsaved-image-{}.png", std::process::id()));
        let mut modified = ModifiedImage::new_unsaved(image());

        assert!(modified.has_pending_changes());
        modified.save_changes(Some(&output)).unwrap();

        assert!(output.is_file());
        assert!(!modified.has_pending_changes());
        let _ = std::fs::remove_file(output);
    }

    #[test]
    fn rotation_counts_as_pending_change_and_discard_restores_saved_pixels() {
        let mut modified = ModifiedImage::new(image(), None);
        let original_pixel = first_pixel(modified.final_data());
        assert!(modified.saved_data.is_none());

        modified.rotate_cw();
        assert!(modified.has_pending_changes());
        assert!(modified.saved_data.is_some());

        modified.discard_changes();
        assert!(!modified.has_pending_changes());
        assert!(modified.saved_data.is_none());
        assert_eq!(modified.final_data().width(), 2);
        assert_eq!(modified.final_data().height(), 2);
        assert_eq!(first_pixel(modified.final_data()), original_pixel);
    }

    #[test]
    fn rotation_transforms_annotations_instead_of_clearing_them() {
        let mut modified = ModifiedImage::new(image(), None);
        let id = AnnotationId::next();
        modified.add_element(AnnotationElement::Line {
            id,
            segment: LineSegment {
                p1: eframe::egui::vec2(0.25, 0.5),
                p2: eframe::egui::vec2(0.75, 0.25),
            },
            style: LineStyle::default(),
        });

        modified.rotate_cw();

        let AnnotationElement::Line { segment, .. } = modified.annotations().find_by_id(id).unwrap() else {
            panic!("expected line");
        };
        assert_eq!(segment.p1, eframe::egui::vec2(0.5, 0.25));
        assert_eq!(segment.p2, eframe::egui::vec2(0.75, 0.75));
    }

    #[test]
    fn undo_rotation_drops_lazy_saved_snapshot_after_returning_to_clean_base() {
        let mut modified = ModifiedImage::new(image(), None);
        modified.rotate_cw();
        assert!(modified.saved_data.is_some());

        modified.undo_last_change();

        assert!(!modified.has_pending_changes());
        assert!(modified.saved_data.is_none());
    }

    #[test]
    fn base_image_transform_commits_pre_annotation_pixels_and_preserves_annotations() {
        let original = PixelSRGBA {
            r: 10,
            g: 20,
            b: 30,
            a: 77,
        };
        let annotated = PixelSRGBA {
            r: 200,
            g: 180,
            b: 40,
            a: 255,
        };
        let mut modified = ModifiedImage::new(image_with_color(original), None);
        let annotation_id = AnnotationId::next();
        add_default_line(&mut modified, annotation_id);
        modified.actions.clear();
        modified.annotated_data = Some(image_with_color(annotated));
        let revision = modified.display_revision();

        modified.apply_base_image_transform(|image| apply_one_shot(image, OneShotOperation::Invert));

        assert_eq!(first_pixel(modified.pre_annotation_data()), [245, 235, 225, 77]);
        assert!(modified.annotated_data.is_none());
        assert!(modified.annotations().find_by_id(annotation_id).is_some());
        assert!(modified.annotations_dirty);
        assert!(modified.base_dirty);
        assert!(modified.saved_data.is_some());
        assert_eq!(modified.display_revision(), revision + 1);
        assert_eq!(modified.actions.len(), 1);
        assert!(matches!(
            modified.actions.last(),
            Some(ImageUndoAction::ReplaceBaseImage { .. })
        ));
    }

    #[test]
    fn undo_base_image_transform_restores_pixels_annotations_and_revision() {
        let original = PixelSRGBA {
            r: 10,
            g: 20,
            b: 30,
            a: 77,
        };
        let mut modified = ModifiedImage::new(image_with_color(original), None);
        let annotation_id = AnnotationId::next();
        add_default_line(&mut modified, annotation_id);
        modified.actions.clear();
        let revision = modified.display_revision();
        modified.apply_base_image_transform(|image| apply_one_shot(image, OneShotOperation::Grayscale));

        modified.undo_last_change();

        assert_eq!(first_pixel(modified.pre_annotation_data()), original.as_array());
        assert!(modified.annotations().find_by_id(annotation_id).is_some());
        assert!(!modified.base_dirty);
        assert!(modified.saved_data.is_none());
        assert!(!modified.can_undo());
        assert_eq!(modified.display_revision(), revision + 2);
    }

    #[test]
    fn repeated_base_image_transforms_are_independent_undo_steps() {
        let original = PixelSRGBA {
            r: 10,
            g: 20,
            b: 30,
            a: 77,
        };
        let mut modified = ModifiedImage::new(image_with_color(original), None);
        let revision = modified.display_revision();

        modified.apply_base_image_transform(|image| apply_one_shot(image, OneShotOperation::Invert));
        modified.apply_base_image_transform(|image| apply_one_shot(image, OneShotOperation::SwapRedGreen));

        assert_eq!(first_pixel(modified.pre_annotation_data()), [235, 245, 225, 77]);
        assert_eq!(modified.actions.len(), 2);
        assert_eq!(modified.display_revision(), revision + 2);

        modified.undo_last_change();
        assert_eq!(first_pixel(modified.pre_annotation_data()), [245, 235, 225, 77]);
        assert!(modified.has_pending_changes());
        modified.undo_last_change();
        assert_eq!(first_pixel(modified.pre_annotation_data()), original.as_array());
        assert!(!modified.has_pending_changes());
        assert_eq!(modified.display_revision(), revision + 4);
    }

    #[test]
    fn base_image_transform_participates_in_save_and_discard_state() {
        let original = PixelSRGBA {
            r: 10,
            g: 20,
            b: 30,
            a: 77,
        };
        let output_path = temp_png_path("base-transform-save");
        let mut modified = ModifiedImage::new(image_with_color(original), None);
        modified.apply_base_image_transform(|image| apply_one_shot(image, OneShotOperation::Invert));
        modified.discard_changes();
        assert_eq!(first_pixel(modified.pre_annotation_data()), original.as_array());
        assert!(!modified.has_pending_changes());

        modified.apply_base_image_transform(|image| apply_one_shot(image, OneShotOperation::Invert));
        modified.save_changes(Some(&output_path)).unwrap();

        assert_eq!(first_pixel(modified.pre_annotation_data()), [245, 235, 225, 77]);
        assert!(!modified.has_pending_changes());
        assert!(modified.saved_data.is_none());
        assert!(!modified.can_undo());
        assert_eq!(
            load_rgba_image(&output_path).unwrap().pixel(0, 0).unwrap().as_array(),
            [245, 235, 225, 77]
        );

        let _ = std::fs::remove_file(output_path);
    }

    #[test]
    fn discard_restores_original_display_data_and_bumps_revision() {
        let original = PixelSRGBA {
            r: 1,
            g: 2,
            b: 3,
            a: 255,
        };
        let annotated = PixelSRGBA {
            r: 20,
            g: 30,
            b: 40,
            a: 255,
        };
        let mut modified = ModifiedImage::new(image_with_color(original), None);
        add_default_line(&mut modified, AnnotationId::next());
        modified.annotated_data = Some(image_with_color(annotated));

        let revision = modified.display_revision();
        assert_eq!(first_pixel(modified.final_data()), annotated.as_array());

        modified.discard_changes();

        assert_eq!(first_pixel(modified.final_data()), original.as_array());
        assert!(modified.annotated_data.is_none());
        assert!(modified.annotations().is_empty());
        assert!(!modified.can_undo());
        assert_eq!(modified.display_revision(), revision + 1);
    }

    #[test]
    fn save_rebases_original_to_annotated_data_and_clears_edit_state() {
        let original = PixelSRGBA {
            r: 1,
            g: 2,
            b: 3,
            a: 255,
        };
        let annotated = PixelSRGBA {
            r: 200,
            g: 180,
            b: 40,
            a: 255,
        };
        let output_path = temp_png_path("save-rebases");
        let mut modified = ModifiedImage::new(image_with_color(original), None);
        add_default_line(&mut modified, AnnotationId::next());
        modified.annotated_data = Some(image_with_color(annotated));

        let revision = modified.display_revision();
        modified.save_changes(Some(&output_path)).unwrap();

        assert_eq!(first_pixel(modified.pre_annotation_data()), annotated.as_array());
        assert_eq!(first_pixel(modified.final_data()), annotated.as_array());
        assert!(modified.annotated_data.is_none());
        assert!(modified.annotations().is_empty());
        assert!(!modified.can_undo());
        assert_eq!(modified.display_revision(), revision + 1);
        assert_eq!(
            load_rgba_image(&output_path).unwrap().pixel(0, 0).unwrap().as_array(),
            annotated.as_array()
        );

        let _ = std::fs::remove_file(output_path);
    }

    #[test]
    fn repeated_save_after_successful_save_is_noop() {
        let annotated = PixelSRGBA {
            r: 90,
            g: 100,
            b: 110,
            a: 255,
        };
        let output_path = temp_png_path("repeated-save");
        let mut modified = ModifiedImage::new(image(), None);
        add_default_line(&mut modified, AnnotationId::next());
        modified.annotated_data = Some(image_with_color(annotated));
        modified.save_changes(Some(&output_path)).unwrap();

        let revision = modified.display_revision();
        let before = std::fs::read(&output_path).unwrap();
        modified.save_changes(None).unwrap();
        let after = std::fs::read(&output_path).unwrap();

        assert_eq!(modified.display_revision(), revision);
        assert_eq!(before, after);

        let _ = std::fs::remove_file(output_path);
    }
}
