use std::sync::{Arc, Mutex};

use eframe::egui;
use eframe::egui_wgpu::{self, wgpu};

use crate::image_item_data::ImageItemData;

const IMAGE_SHADER_PREFIX: &str = r#"
struct VertexOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

struct ZoomUniforms {
    uv_min: vec2<f32>,
    uv_max: vec2<f32>,
};

@group(0) @binding(0)
var image_texture: texture_2d<f32>;

@group(0) @binding(1)
var image_sampler: sampler;

@group(0) @binding(2)
var<uniform> zoom: ZoomUniforms;

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

@fragment
fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {
    // egui-wgpu prefers Rgba/Bgra8Unorm swapchain formats for UI rendering.
    // Those targets are "gamma-space": the value written by the shader is
    // stored directly as the display code value, with no hardware sRGB encode.
    //
    // Our image texture is still Rgba8UnormSrgb so sampling/filtering happens
    // in linear light. Convert the sampled linear color back to sRGB here so a
    // 1:1 texel round-trips to the original image byte on egui's usual target.
    let color = textureSample(image_texture, image_sampler, in.uv);
    return vec4<f32>(linear_to_srgb(color.rgb), color.a);
}
"#;

pub struct WgpuImageRenderer {
    pipeline: wgpu::RenderPipeline,
    bind_group_layout: wgpu::BindGroupLayout,
    sampler: wgpu::Sampler,
    zoom_uniform_buffer: wgpu::Buffer,
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

        // Default zoom: show full image (uv_min=0,0  uv_max=1,1).
        let zoom_uniform_data: [f32; 4] = [0.0, 0.0, 1.0, 1.0];
        let zoom_uniform_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("zv zoom uniform buffer"),
            size: std::mem::size_of::<[f32; 4]>() as u64,
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
            mapped_at_creation: true,
        });
        zoom_uniform_buffer
            .slice(..)
            .get_mapped_range_mut()
            .copy_from_slice(bytemuck::cast_slice(&zoom_uniform_data));
        zoom_uniform_buffer.unmap();

        Self {
            pipeline,
            bind_group_layout,
            sampler,
            zoom_uniform_buffer,
        }
    }

    fn prepare_image_data(
        &self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        image_data: &mut ImageItemData,
        uv_min: [f32; 2],
        uv_max: [f32; 2],
    ) {
        image_data.ensure_uploaded_to_gpu(
            device,
            queue,
            &self.bind_group_layout,
            &self.sampler,
            &self.zoom_uniform_buffer,
        );
        let zoom_data: [f32; 4] = [uv_min[0], uv_min[1], uv_max[0], uv_max[1]];
        queue.write_buffer(
            &self.zoom_uniform_buffer,
            0,
            bytemuck::cast_slice(&zoom_data),
        );
    }

    fn paint_image_data(
        &self,
        image_data: &ImageItemData,
        render_pass: &mut wgpu::RenderPass<'static>,
    ) {
        let Some(bind_group) = image_data.gpu_bind_group() else {
            return;
        };
        render_pass.set_pipeline(&self.pipeline);
        render_pass.set_bind_group(0, bind_group, &[]);
        render_pass.draw(0..6, 0..1);
    }
}

#[derive(Clone)]
pub struct WgpuImageCallback {
    image_data: Arc<Mutex<ImageItemData>>,
    uv_min: [f32; 2],
    uv_max: [f32; 2],
}

impl WgpuImageCallback {
    pub fn new(image_data: Arc<Mutex<ImageItemData>>, uv_min: [f32; 2], uv_max: [f32; 2]) -> Self {
        Self {
            image_data,
            uv_min,
            uv_max,
        }
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
        let Some(renderer) = callback_resources.get_mut::<WgpuImageRenderer>() else {
            tracing::warn!("missing WgpuImageRenderer callback resource");
            return Vec::new();
        };

        let Ok(mut image_data) = self.image_data.lock() else {
            tracing::warn!("image data lock is poisoned");
            return Vec::new();
        };

        renderer.prepare_image_data(device, queue, &mut image_data, self.uv_min, self.uv_max);
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

        let Ok(image_data) = self.image_data.lock() else {
            return;
        };

        renderer.paint_image_data(&image_data, render_pass);
    }
}
