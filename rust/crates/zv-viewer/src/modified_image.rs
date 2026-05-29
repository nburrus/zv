use std::path::{Path, PathBuf};

use eframe::egui_wgpu::wgpu;

use crate::annotations::{AnnotationDocument, AnnotationElement, AnnotationId, AnnotationRenderer, LineAnnotationData};
use crate::image_io::write_rgba_image;
use crate::image_item_data::ImageItemData;

#[derive(Clone, Debug)]
pub enum ImageUndoAction {
    RemoveAnnotation { id: AnnotationId },
    RestoreAnnotation { element: AnnotationElement },
    RestoreLine { id: AnnotationId, data: LineAnnotationData },
}

pub struct ModifiedImage {
    original_data: ImageItemData,
    annotated_data: Option<ImageItemData>,
    annotations: AnnotationDocument,
    actions: Vec<ImageUndoAction>,
    annotations_dirty: bool,
    source_path: Option<PathBuf>,
    display_revision: u64,
}

impl ModifiedImage {
    pub fn new(original_data: ImageItemData, source_path: Option<PathBuf>) -> Self {
        Self {
            original_data,
            annotated_data: None,
            annotations: AnnotationDocument::default(),
            actions: Vec::new(),
            annotations_dirty: false,
            source_path,
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

    #[allow(dead_code)]
    pub fn has_pending_changes(&self) -> bool {
        !self.annotations.is_empty()
    }

    #[allow(dead_code)]
    pub fn can_undo(&self) -> bool {
        !self.actions.is_empty()
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
        }
    }

    pub fn discard_changes(&mut self) {
        if self.annotations.is_empty() {
            return;
        }
        self.annotations.clear();
        self.actions.clear();
        self.annotated_data = None;
        self.annotations_dirty = false;
        self.bump_display_revision();
    }

    pub fn save_changes(&mut self, path: Option<&Path>) -> anyhow::Result<()> {
        if self.annotations.is_empty() {
            return Ok(());
        }
        let output_path = path
            .map(Path::to_path_buf)
            .or_else(|| self.source_path.clone())
            .ok_or_else(|| anyhow::anyhow!("no output path available for image"))?;
        write_rgba_image(&output_path, self.final_data().cpu_data())?;
        let saved = self.final_data().cpu_data().clone();
        self.original_data.set_cpu_data(saved);
        self.source_path = Some(output_path);
        self.annotations.clear();
        self.actions.clear();
        self.annotated_data = None;
        self.annotations_dirty = false;
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
