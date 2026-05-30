use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc;

use eframe::egui;
use eframe::egui_wgpu::{self, wgpu};

use crate::color_image::ImageSRGBA;
use crate::image_item_data::ImageItemData;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Hash)]
pub struct AnnotationId(u64);

impl AnnotationId {
    pub fn next() -> Self {
        static NEXT_ID: AtomicU64 = AtomicU64::new(1);
        Self(NEXT_ID.fetch_add(1, Ordering::Relaxed))
    }

    pub fn is_valid(self) -> bool {
        self.0 != 0
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LineAnnotationData {
    pub p1: egui::Vec2,
    pub p2: egui::Vec2,
    pub color: egui::Color32,
    pub stroke_width: f32,
}

impl Default for LineAnnotationData {
    fn default() -> Self {
        Self {
            p1: egui::vec2(0.1, 0.1),
            p2: egui::vec2(0.5, 0.5),
            color: egui::Color32::YELLOW,
            stroke_width: 2.0,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum AnnotationElement {
    Line { id: AnnotationId, data: LineAnnotationData },
}

impl AnnotationElement {
    pub fn id(&self) -> AnnotationId {
        match self {
            Self::Line { id, .. } => *id,
        }
    }

    pub fn line(&self) -> Option<&LineAnnotationData> {
        match self {
            Self::Line { data, .. } => Some(data),
        }
    }

    pub fn line_mut(&mut self) -> Option<&mut LineAnnotationData> {
        match self {
            Self::Line { data, .. } => Some(data),
        }
    }

    pub fn handle_texture_pos(&self, handle_idx: usize) -> Option<egui::Vec2> {
        match self {
            Self::Line { data, .. } => match handle_idx {
                0 => Some(data.p1),
                1 => Some(data.p2),
                _ => None,
            },
        }
    }

    pub fn move_by(&mut self, delta: egui::Vec2) {
        match self {
            Self::Line { data, .. } => {
                data.p1 += delta;
                data.p2 += delta;
            }
        }
    }

    pub fn move_handle_to(&mut self, handle_idx: usize, texture_pos: egui::Vec2) {
        match self {
            Self::Line { data, .. } => match handle_idx {
                0 => data.p1 = texture_pos,
                1 => data.p2 = texture_pos,
                _ => {}
            },
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AnnotationHitPart {
    Body,
    Handle,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AnnotationHitResult {
    pub id: AnnotationId,
    pub part: AnnotationHitPart,
    pub handle_idx: Option<usize>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct AnnotationDocument {
    elements: Vec<AnnotationElement>,
}

impl AnnotationDocument {
    pub fn add_line(&mut self, id: AnnotationId, data: LineAnnotationData) {
        self.elements.push(AnnotationElement::Line { id, data });
    }

    pub fn remove_by_id(&mut self, id: AnnotationId) -> Option<AnnotationElement> {
        let index = self.elements.iter().position(|element| element.id() == id)?;
        Some(self.elements.remove(index))
    }

    pub fn find_by_id(&self, id: AnnotationId) -> Option<&AnnotationElement> {
        self.elements.iter().find(|element| element.id() == id)
    }

    pub fn find_by_id_mut(&mut self, id: AnnotationId) -> Option<&mut AnnotationElement> {
        self.elements.iter_mut().find(|element| element.id() == id)
    }

    pub fn elements(&self) -> &[AnnotationElement] {
        &self.elements
    }

    pub fn elements_mut(&mut self) -> &mut [AnnotationElement] {
        &mut self.elements
    }

    pub fn is_empty(&self) -> bool {
        self.elements.is_empty()
    }

    pub fn clear(&mut self) {
        self.elements.clear();
    }

    pub fn hit_test(
        &self,
        widget_pos: egui::Pos2,
        transform: &WidgetToTextureTransform,
        selected_id: AnnotationId,
        handle_radius_px: f32,
        body_tolerance_px: f32,
    ) -> Option<AnnotationHitResult> {
        if selected_id.is_valid() {
            if let Some(element) = self.find_by_id(selected_id) {
                if let Some(handle_idx) = hit_handle(element, widget_pos, transform, handle_radius_px) {
                    return Some(AnnotationHitResult {
                        id: selected_id,
                        part: AnnotationHitPart::Handle,
                        handle_idx: Some(handle_idx),
                    });
                }
            }
        }

        for element in self.elements.iter().rev() {
            if let Some(handle_idx) = hit_handle(element, widget_pos, transform, handle_radius_px) {
                return Some(AnnotationHitResult {
                    id: element.id(),
                    part: AnnotationHitPart::Handle,
                    handle_idx: Some(handle_idx),
                });
            }

            let body_hit = match element {
                AnnotationElement::Line { data, .. } => {
                    let p1 = transform.texture_to_widget(data.p1);
                    let p2 = transform.texture_to_widget(data.p2);
                    distance_to_segment(widget_pos, p1, p2)
                        <= body_tolerance_px + transform.stroke_hit_tolerance_widget(data.stroke_width)
                }
            };
            if body_hit {
                return Some(AnnotationHitResult {
                    id: element.id(),
                    part: AnnotationHitPart::Body,
                    handle_idx: None,
                });
            }
        }

        None
    }
}

#[derive(Clone, Copy, Debug)]
pub struct WidgetToTextureTransform {
    pub widget_rect: egui::Rect,
    pub uv_min: egui::Vec2,
    pub uv_max: egui::Vec2,
    pub image_size: [u32; 2],
}

impl WidgetToTextureTransform {
    pub fn widget_to_texture(&self, pos: egui::Pos2) -> egui::Vec2 {
        let widget_pos = (pos + egui::vec2(0.5, 0.5)) - self.widget_rect.min;
        let uv_window = widget_pos / self.widget_rect.size();
        self.uv_min + uv_window * (self.uv_max - self.uv_min)
    }

    pub fn texture_to_widget(&self, uv: egui::Vec2) -> egui::Pos2 {
        let uv_window = (uv - self.uv_min) / (self.uv_max - self.uv_min);
        self.widget_rect.min + uv_window * self.widget_rect.size()
    }

    pub fn image_pixel_to_widget_scale(&self) -> egui::Vec2 {
        let visible_texels = (self.uv_max - self.uv_min)
            * egui::vec2(self.image_size[0].max(1) as f32, self.image_size[1].max(1) as f32);
        self.widget_rect.size() / visible_texels
    }

    pub fn line_stroke_width_widget(&self, stroke_width: f32) -> f32 {
        (stroke_width * self.image_pixel_to_widget_scale().x).max(1.0)
    }

    pub fn stroke_hit_tolerance_widget(&self, stroke_width: f32) -> f32 {
        let scale = self.image_pixel_to_widget_scale();
        stroke_width * 0.5 * scale.x.max(scale.y)
    }
}

pub fn line_shapes_for_image(document: &AnnotationDocument, width: u32, height: u32) -> Vec<egui::Shape> {
    let mut shapes = Vec::new();
    for element in document.elements() {
        match element {
            AnnotationElement::Line { data, .. } => {
                let p1 = egui::pos2(data.p1.x * width as f32, data.p1.y * height as f32);
                let p2 = egui::pos2(data.p2.x * width as f32, data.p2.y * height as f32);
                shapes.push(egui::Shape::line_segment(
                    [p1, p2],
                    egui::Stroke::new(data.stroke_width.max(1.0), data.color),
                ));
            }
        }
    }
    shapes
}

pub fn paint_line_overlay(painter: &egui::Painter, data: &LineAnnotationData, transform: &WidgetToTextureTransform) {
    painter.line_segment(
        [
            transform.texture_to_widget(data.p1),
            transform.texture_to_widget(data.p2),
        ],
        egui::Stroke::new(transform.line_stroke_width_widget(data.stroke_width), data.color),
    );
}

pub fn paint_line_handles(painter: &egui::Painter, element: &AnnotationElement, transform: &WidgetToTextureTransform) {
    for handle_idx in 0..2 {
        let Some(handle_pos) = element.handle_texture_pos(handle_idx) else {
            continue;
        };
        let center = transform.texture_to_widget(handle_pos);
        painter.circle_filled(center, 6.0, egui::Color32::from_white_alpha(220));
        painter.circle_stroke(
            center,
            6.0,
            egui::Stroke::new(1.5, egui::Color32::from_black_alpha(220)),
        );
    }
}

pub struct AnnotationRenderer {
    renderer: egui_wgpu::Renderer,
    white_texture_uploaded: bool,
    composite_resources: Option<AnnotationCompositeResources>,
}

struct AnnotationCompositeResources {
    size: wgpu::Extent3d,
    texture: wgpu::Texture,
    view: wgpu::TextureView,
    readback_buffer: wgpu::Buffer,
    padded_bytes_per_row: usize,
}

impl AnnotationRenderer {
    pub fn new(device: &wgpu::Device) -> Self {
        Self {
            renderer: egui_wgpu::Renderer::new(
                device,
                wgpu::TextureFormat::Rgba8Unorm,
                egui_wgpu::RendererOptions::default(),
            ),
            white_texture_uploaded: false,
            composite_resources: None,
        }
    }

    pub fn composite(
        &mut self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        input: &ImageItemData,
        document: &AnnotationDocument,
    ) -> Option<ImageItemData> {
        let base = input.cpu_data();
        if base.width() == 0 || base.height() == 0 {
            return None;
        }

        let width = base.width();
        let height = base.height();
        self.ensure_composite_resources(device, width, height);
        let (texture, view, readback_buffer, size, padded_bytes_per_row) = {
            let resources = self.composite_resources.as_ref()?;
            resources.upload_base_image(queue, base);
            (
                resources.texture.clone(),
                resources.view.clone(),
                resources.readback_buffer.clone(),
                resources.size,
                resources.padded_bytes_per_row,
            )
        };

        let shapes = line_shapes_for_image(document, width, height)
            .into_iter()
            .map(|shape| egui::epaint::ClippedShape {
                clip_rect: egui::Rect::from_min_size(egui::Pos2::ZERO, egui::vec2(width as f32, height as f32)),
                shape,
            })
            .collect::<Vec<_>>();
        let mut tessellator =
            egui::epaint::Tessellator::new(1.0, egui::epaint::TessellationOptions::default(), [1, 1], Vec::new());
        let paint_jobs = tessellator.tessellate_shapes(shapes);
        let screen_descriptor = egui_wgpu::ScreenDescriptor {
            size_in_pixels: [width, height],
            pixels_per_point: 1.0,
        };
        self.ensure_white_texture(device, queue);

        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("zv annotation composite encoder"),
        });
        self.renderer
            .update_buffers(device, queue, &mut encoder, &paint_jobs, &screen_descriptor);
        {
            let mut render_pass = encoder
                .begin_render_pass(&wgpu::RenderPassDescriptor {
                    label: Some("zv annotation composite pass"),
                    color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                        view: &view,
                        depth_slice: None,
                        resolve_target: None,
                        ops: wgpu::Operations {
                            load: wgpu::LoadOp::Load,
                            store: wgpu::StoreOp::Store,
                        },
                    })],
                    depth_stencil_attachment: None,
                    timestamp_writes: None,
                    occlusion_query_set: None,
                })
                .forget_lifetime();
            self.renderer.render(&mut render_pass, &paint_jobs, &screen_descriptor);
        }

        encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: &texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &readback_buffer,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(padded_bytes_per_row as u32),
                    rows_per_image: Some(height),
                },
            },
            size,
        );
        let submission = queue.submit(Some(encoder.finish()));

        let slice = readback_buffer.slice(..);
        let (sender, receiver) = mpsc::channel();
        slice.map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::PollType::Wait {
            submission_index: Some(submission),
            timeout: None,
        });
        if receiver.recv().ok()?.is_err() {
            return None;
        }

        let mapped = slice.get_mapped_range();
        let mut tight = vec![0; width as usize * height as usize * 4];
        let tight_bytes_per_row = width as usize * 4;
        for row in 0..height as usize {
            let src = row * padded_bytes_per_row;
            let dst = row * tight_bytes_per_row;
            tight[dst..dst + tight_bytes_per_row].copy_from_slice(&mapped[src..src + tight_bytes_per_row]);
        }
        drop(mapped);
        readback_buffer.unmap();

        Some(ImageItemData::new(ImageSRGBA::from_tightly_packed_bytes(
            width, height, &tight,
        )))
    }

    fn ensure_white_texture(&mut self, device: &wgpu::Device, queue: &wgpu::Queue) {
        if self.white_texture_uploaded {
            return;
        }
        let image = egui::epaint::ColorImage::filled([1, 1], egui::Color32::WHITE);
        let delta = egui::epaint::ImageDelta::full(image, egui::TextureOptions::LINEAR);
        self.renderer
            .update_texture(device, queue, egui::TextureId::default(), &delta);
        self.white_texture_uploaded = true;
    }

    fn ensure_composite_resources(&mut self, device: &wgpu::Device, width: u32, height: u32) {
        let needs_reallocate = self
            .composite_resources
            .as_ref()
            .is_none_or(|resources| resources.size.width != width || resources.size.height != height);
        if !needs_reallocate {
            return;
        }

        let size = wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        };
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("zv annotation composite texture"),
            size,
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT
                | wgpu::TextureUsages::COPY_SRC
                | wgpu::TextureUsages::COPY_DST,
            view_formats: &[wgpu::TextureFormat::Rgba8Unorm],
        });
        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        let padded_bytes_per_row = (width as usize * 4).next_multiple_of(wgpu::COPY_BYTES_PER_ROW_ALIGNMENT as usize);
        let readback_buffer_size = padded_bytes_per_row * height as usize;
        let readback_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("zv annotation composite readback"),
            size: readback_buffer_size as u64,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        self.composite_resources = Some(AnnotationCompositeResources {
            size,
            texture,
            view,
            readback_buffer,
            padded_bytes_per_row,
        });
    }
}

impl AnnotationCompositeResources {
    fn upload_base_image(&self, queue: &wgpu::Queue, base: &ImageSRGBA) {
        queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture: &self.texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            base.bytes(),
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(base.bytes_per_row() as u32),
                rows_per_image: Some(base.height()),
            },
            self.size,
        );
    }
}

fn hit_handle(
    element: &AnnotationElement,
    widget_pos: egui::Pos2,
    transform: &WidgetToTextureTransform,
    handle_radius_px: f32,
) -> Option<usize> {
    (0..2).find(|&handle_idx| {
        element
            .handle_texture_pos(handle_idx)
            .is_some_and(|pos| transform.texture_to_widget(pos).distance(widget_pos) <= handle_radius_px)
    })
}

fn distance_to_segment(p: egui::Pos2, a: egui::Pos2, b: egui::Pos2) -> f32 {
    let ab = b - a;
    let len_sq = ab.length_sq();
    if len_sq <= f32::EPSILON {
        return p.distance(a);
    }
    let ap = p - a;
    let t = (ap.dot(ab) / len_sq).clamp(0.0, 1.0);
    p.distance(a + ab * t)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn transform() -> WidgetToTextureTransform {
        WidgetToTextureTransform {
            widget_rect: egui::Rect::from_min_size(egui::Pos2::ZERO, egui::vec2(100.0, 100.0)),
            uv_min: egui::Vec2::ZERO,
            uv_max: egui::vec2(1.0, 1.0),
            image_size: [100, 100],
        }
    }

    #[test]
    fn line_handles_are_texture_endpoints() {
        let element = AnnotationElement::Line {
            id: AnnotationId(1),
            data: LineAnnotationData {
                p1: egui::vec2(0.25, 0.5),
                p2: egui::vec2(0.75, 0.5),
                ..Default::default()
            },
        };
        assert_eq!(element.handle_texture_pos(0), Some(egui::vec2(0.25, 0.5)));
        assert_eq!(element.handle_texture_pos(1), Some(egui::vec2(0.75, 0.5)));
        assert_eq!(element.handle_texture_pos(2), None);
    }

    #[test]
    fn line_hit_test_prefers_selected_handle() {
        let mut document = AnnotationDocument::default();
        let id = AnnotationId(3);
        document.add_line(
            id,
            LineAnnotationData {
                p1: egui::vec2(0.2, 0.2),
                p2: egui::vec2(0.8, 0.2),
                ..Default::default()
            },
        );

        let hit = document
            .hit_test(egui::pos2(20.0, 20.0), &transform(), id, 6.0, 4.0)
            .expect("handle hit");
        assert_eq!(hit.part, AnnotationHitPart::Handle);
        assert_eq!(hit.handle_idx, Some(0));
    }

    #[test]
    fn line_body_can_be_moved() {
        let mut element = AnnotationElement::Line {
            id: AnnotationId(7),
            data: LineAnnotationData {
                p1: egui::vec2(0.1, 0.2),
                p2: egui::vec2(0.3, 0.4),
                ..Default::default()
            },
        };
        element.move_by(egui::vec2(0.1, -0.1));
        let data = element.line().unwrap();
        assert_eq!(data.p1, egui::vec2(0.2, 0.1));
        assert_eq!(data.p2, egui::vec2(0.4, 0.3));
    }

    #[test]
    fn line_hit_test_scales_stroke_width_to_widget_pixels() {
        let mut document = AnnotationDocument::default();
        let id = AnnotationId(9);
        document.add_line(
            id,
            LineAnnotationData {
                p1: egui::vec2(0.1, 0.5),
                p2: egui::vec2(0.9, 0.5),
                stroke_width: 10.0,
                ..Default::default()
            },
        );
        let transform = WidgetToTextureTransform {
            widget_rect: egui::Rect::from_min_size(egui::Pos2::ZERO, egui::vec2(200.0, 200.0)),
            uv_min: egui::Vec2::ZERO,
            uv_max: egui::vec2(1.0, 1.0),
            image_size: [100, 100],
        };

        let hit = document
            .hit_test(egui::pos2(100.0, 113.0), &transform, AnnotationId::default(), 6.0, 4.0)
            .expect("scaled stroke hit");
        assert_eq!(hit.id, id);
        assert_eq!(hit.part, AnnotationHitPart::Body);
    }
}
