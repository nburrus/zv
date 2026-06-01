use std::path::{Path, PathBuf};

use eframe::egui_wgpu::wgpu;

use crate::annotations::{AnnotationDocument, AnnotationElement, AnnotationId, AnnotationRenderer, LineAnnotationData};
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
    RestoreAnnotation {
        element: AnnotationElement,
    },
    RestoreLine {
        id: AnnotationId,
        data: LineAnnotationData,
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

    pub fn final_data(&self) -> &ImageItemData {
        self.annotated_data.as_ref().unwrap_or(&self.original_data)
    }

    pub fn final_data_mut(&mut self) -> &mut ImageItemData {
        self.annotated_data.as_mut().unwrap_or(&mut self.original_data)
    }

    pub fn display_revision(&self) -> u64 {
        self.display_revision
    }

    #[allow(dead_code)]
    pub fn pre_annotation_data(&self) -> &ImageItemData {
        &self.original_data
    }

    pub fn annotations(&self) -> &AnnotationDocument {
        &self.annotations
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

    pub fn add_line(&mut self, id: AnnotationId, data: LineAnnotationData) {
        self.annotations.add_line(id, data);
        self.mark_annotations_dirty();
        self.push_undo_action(ImageUndoAction::RemoveAnnotation { id });
    }

    pub fn remove_annotation_with_undo(&mut self, id: AnnotationId) {
        if let Some(element) = self.annotations.remove_by_id(id) {
            self.mark_annotations_dirty();
            self.push_undo_action(ImageUndoAction::RestoreAnnotation { element });
        }
    }

    pub fn update_line_style(&mut self, id: AnnotationId, color: eframe::egui::Color32, stroke_width: f32) -> Option<LineAnnotationData> {
        let Some(element) = self.annotations.find_by_id_mut(id) else {
            return None;
        };
        let Some(line) = element.line_mut() else {
            return None;
        };
        if line.color == color && (line.stroke_width - stroke_width).abs() <= f32::EPSILON {
            return None;
        }
        let previous = *line;
        line.color = color;
        line.stroke_width = stroke_width;
        self.mark_annotations_dirty();
        Some(previous)
    }

    pub fn push_line_style_undo(&mut self, id: AnnotationId, previous: LineAnnotationData) {
        let Some(element) = self.annotations.find_by_id(id) else {
            return;
        };
        let Some(line) = element.line() else {
            return;
        };
        if *line != previous {
            self.push_undo_action(ImageUndoAction::RestoreLine { id, data: previous });
        }
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
                match element {
                    AnnotationElement::Line { id, data } => self.annotations.add_line(id, data),
                }
                self.mark_annotations_dirty();
            }
            ImageUndoAction::RestoreLine { id, data } => {
                if let Some(element) = self.annotations.find_by_id_mut(id) {
                    if let Some(line) = element.line_mut() {
                        *line = data;
                        self.mark_annotations_dirty();
                    }
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

    fn rotate(&mut self, direction: RotationDirection) {
        self.ensure_saved_data_snapshot();
        let snapshot_cpu = self.original_data.cpu_data().clone();
        let snapshot_annotations = self.annotations.clone();
        let rotated = rotate_image(self.original_data.cpu_data(), direction);
        self.original_data = rotated;
        rotate_annotations(&mut self.annotations, direction);
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
            AnnotationElement::Line { data, .. } => {
                data.p1 = rotate_uv(data.p1, direction);
                data.p2 = rotate_uv(data.p2, direction);
            }
        }
    }
}

fn rotate_uv(uv: eframe::egui::Vec2, direction: RotationDirection) -> eframe::egui::Vec2 {
    match direction {
        RotationDirection::Clockwise => eframe::egui::vec2(1.0 - uv.y, uv.x),
        RotationDirection::CounterClockwise => eframe::egui::vec2(uv.y, 1.0 - uv.x),
    }
}

fn rotate_image(src: &ImageSRGBA, direction: RotationDirection) -> ImageItemData {
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
    ImageItemData::new(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::color_image::{ImageSRGBA, PixelSRGBA};
    use crate::image_io::load_rgba_image;
    use std::time::{SystemTime, UNIX_EPOCH};

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
        std::env::temp_dir().join(format!("zv-viewer-{name}-{}-{stamp}.png", std::process::id()))
    }

    #[test]
    fn undo_line_creation_removes_annotation() {
        let mut modified = ModifiedImage::new(image(), None);
        let id = AnnotationId::next();
        modified.add_line(id, LineAnnotationData::default());
        assert!(!modified.annotations().is_empty());

        modified.undo_last_change();
        assert!(modified.annotations().is_empty());
    }

    #[test]
    fn live_line_style_updates_can_be_committed_as_one_undo_step() {
        let mut modified = ModifiedImage::new(image(), None);
        let id = AnnotationId::next();
        let before = LineAnnotationData::default();
        modified.add_line(id, before);
        modified.actions.clear();

        modified.update_line_style(id, eframe::egui::Color32::RED, 7.0);
        modified.update_line_style(id, eframe::egui::Color32::GREEN, 12.0);

        assert!(!modified.can_undo());
        modified.push_line_style_undo(id, before);
        assert!(modified.can_undo());

        modified.undo_last_change();

        let line = modified.annotations().find_by_id(id).unwrap().line().unwrap();
        assert_eq!(*line, before);
        assert!(!modified.can_undo());
    }

    #[test]
    fn discard_clears_annotations_and_undo() {
        let mut modified = ModifiedImage::new(image(), None);
        modified.add_line(AnnotationId::next(), LineAnnotationData::default());
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
        modified.add_line(
            id,
            LineAnnotationData {
                p1: eframe::egui::vec2(0.25, 0.5),
                p2: eframe::egui::vec2(0.75, 0.25),
                ..LineAnnotationData::default()
            },
        );

        modified.rotate_cw();

        let line = modified.annotations().find_by_id(id).unwrap().line().unwrap();
        assert_eq!(line.p1, eframe::egui::vec2(0.5, 0.25));
        assert_eq!(line.p2, eframe::egui::vec2(0.75, 0.75));
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
        modified.add_line(AnnotationId::next(), LineAnnotationData::default());
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
        modified.add_line(AnnotationId::next(), LineAnnotationData::default());
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
        modified.add_line(AnnotationId::next(), LineAnnotationData::default());
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
