use std::sync::{Arc, Mutex};

use eframe::egui;
use eframe::egui_wgpu::{self, wgpu};
use wgpu::util::DeviceExt;

use crate::color_editor::{HueShiftParams, LevelsAdjustment, compile_levels_lut};
use crate::modified_image::ModifiedImage;

const PREVIEW_NONE: u32 = 0;
const PREVIEW_LEVELS: u32 = 1;
const PREVIEW_HUE: u32 = 2;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum ColorPreview {
    #[default]
    None,
    Levels(LevelsAdjustment),
    Hue(HueShiftParams),
}

impl ColorPreview {
    /// A non-finite hue angle would never compare equal to itself, so the renderer would
    /// see a new preview on every frame and keep re-uploading and repainting forever.
    fn sanitized(self) -> Self {
        match self {
            Self::Hue(params) if !params.degrees.is_finite() => Self::None,
            other => other,
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
struct PreviewGpuData {
    uniforms: [u32; 4],
    lut: [[u32; 4]; 256],
}

impl PreviewGpuData {
    fn encode(preview: ColorPreview) -> Self {
        let mut data = Self {
            uniforms: [PREVIEW_NONE, 0, 0, 0],
            lut: std::array::from_fn(|value| {
                let value = value as u32;
                [value, value, value, 255]
            }),
        };
        match preview {
            ColorPreview::None => {}
            ColorPreview::Levels(params) => {
                data.uniforms[0] = PREVIEW_LEVELS;
                let lut = compile_levels_lut(params);
                for (value, encoded) in data.lut.iter_mut().enumerate() {
                    *encoded = [
                        u32::from(lut.r[value]),
                        u32::from(lut.g[value]),
                        u32::from(lut.b[value]),
                        255,
                    ];
                }
            }
            ColorPreview::Hue(params) => {
                data.uniforms[0] = PREVIEW_HUE;
                data.uniforms[1] = params.degrees.to_bits();
            }
        }
        data
    }
}

const IMAGE_SHADER_PREFIX: &str = r#"
struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

struct ZoomUniforms {
    uv_min: vec2<f32>,
    uv_max: vec2<f32>,
};

struct PreviewUniforms {
    kind: u32,
    hue_degrees: f32,
    padding: vec2<u32>,
};

@group(0) @binding(0)
var image_texture: texture_2d<f32>;

@group(0) @binding(1)
var image_sampler: sampler;

@group(0) @binding(2)
var<uniform> zoom: ZoomUniforms;

@group(0) @binding(3)
var<uniform> preview: PreviewUniforms;

// A uniform array (4 KiB, one entry per sRGB code value) rather than a storage
// buffer: storage buffers are unavailable in the fragment stage on downlevel
// GL/WebGL2 adapters, and this easily fits the uniform size limit.
@group(0) @binding(4)
var<uniform> levels_lut: array<vec4<u32>, 256>;

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOut {
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 1.0, -1.0),
        vec2<f32>( 1.0,  1.0),
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 1.0,  1.0),
        vec2<f32>(-1.0,  1.0),
    );

    var base_uvs = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 1.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(0.0, 1.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(0.0, 0.0),
    );

    var out: VertexOut;
    out.position = vec4<f32>(positions[vertex_index], 0.0, 1.0);
    let t = base_uvs[vertex_index];
    out.uv = zoom.uv_min + t * (zoom.uv_max - zoom.uv_min);
    return out;
}
"#;

const IMAGE_SHADER_FRAGMENT: &str = r#"
fn linear_to_srgb_channel(linear: f32) -> f32 {
    if (linear <= 0.0031308) {
        return 12.92 * linear;
    }
    return 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
}

fn linear_to_srgb(linear: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(
        linear_to_srgb_channel(linear.r),
        linear_to_srgb_channel(linear.g),
        linear_to_srgb_channel(linear.b),
    );
}

fn apply_levels(color: vec3<f32>) -> vec3<f32> {
    let indices = vec3<u32>(round(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)) * 255.0));
    return vec3<f32>(
        f32(levels_lut[indices.r].r),
        f32(levels_lut[indices.g].g),
        f32(levels_lut[indices.b].b),
    ) / 255.0;
}

fn hue_shift(color: vec3<f32>, degrees: f32) -> vec3<f32> {
    let maximum = max(color.r, max(color.g, color.b));
    let minimum = min(color.r, min(color.g, color.b));
    let chroma = maximum - minimum;
    if (chroma == 0.0) {
        return color;
    }

    var hue: f32;
    if (maximum == color.r) {
        hue = 60.0 * ((color.g - color.b) / chroma);
    } else if (maximum == color.g) {
        hue = 60.0 * (((color.b - color.r) / chroma) + 2.0);
    } else {
        hue = 60.0 * (((color.r - color.g) / chroma) + 4.0);
    }
    hue = hue + degrees;
    hue = hue - floor(hue / 360.0) * 360.0;

    let hue_sector = hue / 60.0;
    let x = chroma * (1.0 - abs(hue_sector - 2.0 * floor(hue_sector / 2.0) - 1.0));
    var shifted: vec3<f32>;
    if (hue < 60.0) {
        shifted = vec3<f32>(chroma, x, 0.0);
    } else if (hue < 120.0) {
        shifted = vec3<f32>(x, chroma, 0.0);
    } else if (hue < 180.0) {
        shifted = vec3<f32>(0.0, chroma, x);
    } else if (hue < 240.0) {
        shifted = vec3<f32>(0.0, x, chroma);
    } else if (hue < 300.0) {
        shifted = vec3<f32>(x, 0.0, chroma);
    } else {
        shifted = vec3<f32>(chroma, 0.0, x);
    }
    return shifted + vec3<f32>(maximum - chroma);
}

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    // egui-wgpu prefers Rgba/Bgra8Unorm swapchain formats for UI rendering.
    // Those targets are "gamma-space": the value written by the shader is
    // stored directly as the display code value, with no hardware sRGB encode.
    //
    // Our image texture is still Rgba8UnormSrgb so sampling/filtering happens
    // in linear light. Convert the sampled linear color back to sRGB here so a
    // 1:1 texel round-trips to the original image byte on egui's usual target.
    let sampled = textureSample(image_texture, image_sampler, in.uv);
    var color = linear_to_srgb(sampled.rgb);
    if (preview.kind == 1u) {
        color = apply_levels(color);
    } else if (preview.kind == 2u) {
        color = hue_shift(color, preview.hue_degrees);
    }
    return vec4<f32>(color, sampled.a);
}
"#;

pub struct WgpuImageRenderer {
    pipeline: wgpu::RenderPipeline,
    bind_group_layout: wgpu::BindGroupLayout,
    sampler: wgpu::Sampler,
    preview_uniform_buffer: wgpu::Buffer,
    preview_off_uniform_buffer: wgpu::Buffer,
    preview_lut_buffer: wgpu::Buffer,
    preview: ColorPreview,
}

impl WgpuImageRenderer {
    pub fn new(device: &wgpu::Device, target_format: wgpu::TextureFormat) -> Self {
        // egui-wgpu intentionally prefers non-sRGB Rgba/Bgra8Unorm surfaces
        // for gamma-space UI rendering. ZV's image renderer assumes that same
        // target contract and explicitly writes sRGB display code values.
        assert!(
            !target_format.is_srgb(),
            "zv image renderer expects egui's non-sRGB gamma-space target, got {target_format:?}",
        );
        let shader_source = format!("{IMAGE_SHADER_PREFIX}\n{IMAGE_SHADER_FRAGMENT}");
        tracing::info!(?target_format, "creating image renderer");

        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("zv image shader"),
            source: wgpu::ShaderSource::Wgsl(shader_source.into()),
        });

        let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("zv image bind group layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::VERTEX,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 3,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 4,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: None,
                    },
                    count: None,
                },
            ],
        });

        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("zv image pipeline layout"),
            bind_group_layouts: &[&bind_group_layout],
            push_constant_ranges: &[],
        });

        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("zv image pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                buffers: &[],
                compilation_options: wgpu::PipelineCompilationOptions::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                targets: &[Some(wgpu::ColorTargetState {
                    format: target_format,
                    blend: Some(wgpu::BlendState::ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: wgpu::PipelineCompilationOptions::default(),
            }),
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                ..Default::default()
            },
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview: None,
            cache: None,
        });

        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("zv image display sampler"),
            mag_filter: wgpu::FilterMode::Nearest,
            min_filter: wgpu::FilterMode::Linear,
            mipmap_filter: wgpu::FilterMode::Linear,
            address_mode_u: wgpu::AddressMode::ClampToEdge,
            address_mode_v: wgpu::AddressMode::ClampToEdge,
            ..Default::default()
        });
        let initial_preview = PreviewGpuData::encode(ColorPreview::None);
        let preview_uniform_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("zv color preview uniform buffer"),
            contents: bytemuck::cast_slice(&initial_preview.uniforms),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });
        let preview_lut_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("zv color preview levels LUT buffer"),
            contents: bytemuck::cast_slice(&initial_preview.lut),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });
        // Callbacks that must show the unmodified image bind this instead of the live
        // preview uniform. It never changes, so no LUT counterpart is needed: the shader
        // ignores the LUT entirely when the preview kind is "none".
        let preview_off_uniform_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("zv color preview disabled uniform buffer"),
            contents: bytemuck::cast_slice(&initial_preview.uniforms),
            usage: wgpu::BufferUsages::UNIFORM,
        });

        Self {
            pipeline,
            bind_group_layout,
            sampler,
            preview_uniform_buffer,
            preview_off_uniform_buffer,
            preview_lut_buffer,
            preview: ColorPreview::None,
        }
    }

    // The preview buffers live here for the renderer's whole lifetime and are updated
    // in place, so the per-callback bind groups referencing them never go stale.
    pub fn set_color_preview(&mut self, queue: &wgpu::Queue, preview: ColorPreview) -> bool {
        let preview = preview.sanitized();
        if self.preview == preview {
            return false;
        }
        let data = PreviewGpuData::encode(preview);
        queue.write_buffer(&self.preview_uniform_buffer, 0, bytemuck::cast_slice(&data.uniforms));
        if matches!(preview, ColorPreview::Levels(_)) {
            queue.write_buffer(&self.preview_lut_buffer, 0, bytemuck::cast_slice(&data.lut));
        }
        self.preview = preview;
        true
    }

    fn create_callback_resources(
        &self,
        device: &wgpu::Device,
        texture_view: &wgpu::TextureView,
        color_preview: CallbackColorPreview,
    ) -> WgpuImageCallbackResources {
        // Each paint callback owns its own tiny UV uniform so two callbacks can
        // render different regions of the same shared texture in one frame.
        let zoom_uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("zv image callback zoom uniform buffer"),
            size: std::mem::size_of::<[f32; 4]>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("zv image callback bind group"),
            layout: &self.bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(texture_view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::Sampler(&self.sampler),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: zoom_uniform_buffer.as_entire_binding(),
                },
                wgpu::BindGroupEntry {
                    binding: 3,
                    resource: match color_preview {
                        CallbackColorPreview::Follow => self.preview_uniform_buffer.as_entire_binding(),
                        CallbackColorPreview::Ignore => self.preview_off_uniform_buffer.as_entire_binding(),
                    },
                },
                wgpu::BindGroupEntry {
                    binding: 4,
                    resource: self.preview_lut_buffer.as_entire_binding(),
                },
            ],
        });

        WgpuImageCallbackResources {
            zoom_uniform_buffer,
            bind_group,
            image_revision: 0,
        }
    }

    fn write_uv_uniform(
        &self,
        queue: &wgpu::Queue,
        resources: &WgpuImageCallbackResources,
        uv_min: [f32; 2],
        uv_max: [f32; 2],
    ) {
        let zoom_data: [f32; 4] = [uv_min[0], uv_min[1], uv_max[0], uv_max[1]];
        queue.write_buffer(&resources.zoom_uniform_buffer, 0, bytemuck::cast_slice(&zoom_data));
    }

    fn paint_bind_group(&self, bind_group: &wgpu::BindGroup, render_pass: &mut wgpu::RenderPass<'static>) {
        render_pass.set_pipeline(&self.pipeline);
        render_pass.set_bind_group(0, bind_group, &[]);
        render_pass.draw(0..6, 0..1);
    }
}

struct WgpuImageCallbackResources {
    zoom_uniform_buffer: wgpu::Buffer,
    bind_group: wgpu::BindGroup,
    image_revision: u64,
}

/// Whether a callback shows the pending color-editor preview or the image as stored.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CallbackColorPreview {
    Follow,
    Ignore,
}

#[derive(Clone)]
pub struct WgpuImageCallback {
    image_data: Arc<Mutex<ModifiedImage>>,
    uv_min: [f32; 2],
    uv_max: [f32; 2],
    color_preview: CallbackColorPreview,
    resources: Arc<Mutex<Option<WgpuImageCallbackResources>>>,
}

impl WgpuImageCallback {
    pub fn new(image_data: Arc<Mutex<ModifiedImage>>, uv_min: [f32; 2], uv_max: [f32; 2]) -> Self {
        Self {
            image_data,
            uv_min,
            uv_max,
            color_preview: CallbackColorPreview::Follow,
            resources: Arc::new(Mutex::new(None)),
        }
    }

    /// Renders the stored pixels even while the shared color-editor preview is
    /// pending. Auxiliary callbacks use this when their surrounding metadata was
    /// computed on the CPU from the unpreviewed image; applying the GPU-only preview
    /// to just the callback would make the pixels disagree with that metadata.
    pub fn without_color_preview(mut self) -> Self {
        self.color_preview = CallbackColorPreview::Ignore;
        self
    }
}

impl egui_wgpu::CallbackTrait for WgpuImageCallback {
    fn prepare(
        &self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        _screen_descriptor: &egui_wgpu::ScreenDescriptor,
        _egui_encoder: &mut wgpu::CommandEncoder,
        callback_resources: &mut egui_wgpu::CallbackResources,
    ) -> Vec<wgpu::CommandBuffer> {
        let Ok(mut image_data) = self.image_data.lock() else {
            tracing::warn!("image data lock is poisoned");
            return Vec::new();
        };

        let image_revision = image_data.display_revision();
        let final_data = image_data.final_data_mut();
        final_data.ensure_uploaded_to_gpu(device, queue);
        let Some(texture_view) = final_data.gpu_texture_view() else {
            return Vec::new();
        };

        let Some(renderer) = callback_resources.get::<WgpuImageRenderer>() else {
            tracing::warn!("missing WgpuImageRenderer callback resource");
            return Vec::new();
        };

        let Ok(mut resources) = self.resources.lock() else {
            tracing::warn!("image callback resources lock is poisoned");
            return Vec::new();
        };
        if resources
            .as_ref()
            .is_none_or(|resources| resources.image_revision != image_revision)
        {
            let mut new_resources = renderer.create_callback_resources(device, texture_view, self.color_preview);
            new_resources.image_revision = image_revision;
            *resources = Some(new_resources);
        }
        if let Some(resources) = resources.as_ref() {
            renderer.write_uv_uniform(queue, resources, self.uv_min, self.uv_max);
        }

        Vec::new()
    }

    fn paint(
        &self,
        _info: egui::PaintCallbackInfo,
        render_pass: &mut wgpu::RenderPass<'static>,
        callback_resources: &egui_wgpu::CallbackResources,
    ) {
        let Some(renderer) = callback_resources.get::<WgpuImageRenderer>() else {
            return;
        };

        let Ok(resources) = self.resources.lock() else {
            return;
        };
        let Some(resources) = resources.as_ref() else {
            return;
        };

        renderer.paint_bind_group(&resources.bind_group, render_pass);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::color_editor::LevelsParams;

    #[test]
    fn identity_preview_encoding_uses_identity_lut() {
        let data = PreviewGpuData::encode(ColorPreview::None);
        assert_eq!(data.uniforms, [PREVIEW_NONE, 0, 0, 0]);
        assert_eq!(data.lut[0], [0, 0, 0, 255]);
        assert_eq!(data.lut[127], [127, 127, 127, 255]);
        assert_eq!(data.lut[255], [255, 255, 255, 255]);
    }

    #[test]
    fn levels_preview_encoding_uses_compiled_replacement_luts() {
        let params = LevelsAdjustment {
            luma: LevelsParams {
                output_black: 10,
                output_white: 200,
                ..Default::default()
            },
            red: LevelsParams {
                input_black: 127,
                ..Default::default()
            },
            ..Default::default()
        };
        let data = PreviewGpuData::encode(ColorPreview::Levels(params));
        assert_eq!(data.uniforms[0], PREVIEW_LEVELS);
        assert_eq!((data.lut[0][1], data.lut[255][1]), (10, 200));
        assert_eq!((data.lut[127][0], data.lut[255][0]), (0, 255));
    }

    #[test]
    fn non_finite_hue_angles_sanitize_to_no_preview() {
        for degrees in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY] {
            assert_eq!(
                ColorPreview::Hue(HueShiftParams { degrees }).sanitized(),
                ColorPreview::None
            );
        }
        let finite = ColorPreview::Hue(HueShiftParams { degrees: 90.0 });
        assert_eq!(finite.sanitized(), finite);
    }

    #[test]
    fn hue_preview_encoding_preserves_angle_bits() {
        let data = PreviewGpuData::encode(ColorPreview::Hue(HueShiftParams { degrees: 123.5 }));
        assert_eq!(data.uniforms[0], PREVIEW_HUE);
        assert_eq!(f32::from_bits(data.uniforms[1]), 123.5);
    }
}
