use eframe::egui_wgpu::wgpu;

use crate::color_image::RgbaImage;

pub struct ImageItemData {
    cpu_data: RgbaImage,
    texture_data: Option<WgpuImageTexture>,
}

impl ImageItemData {
    pub fn new(cpu_data: RgbaImage) -> Self {
        Self {
            cpu_data,
            texture_data: None,
        }
    }

    pub fn width(&self) -> u32 {
        self.cpu_data.width()
    }

    pub fn height(&self) -> u32 {
        self.cpu_data.height()
    }

    pub fn bytes_per_row(&self) -> usize {
        self.cpu_data.bytes_per_row()
    }

    pub fn pixel_rgba(&self, x: u32, y: u32) -> Option<[u8; 4]> {
        self.cpu_data.pixel_rgba(x, y)
    }

    pub fn ensure_uploaded_to_gpu(
        &mut self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        bind_group_layout: &wgpu::BindGroupLayout,
        sampler: &wgpu::Sampler,
    ) {
        if self.texture_data.is_some() {
            return;
        }

        let size = wgpu::Extent3d {
            width: self.cpu_data.width(),
            height: self.cpu_data.height(),
            depth_or_array_layers: 1,
        };
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("zv image item texture"),
            size,
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::TEXTURE_BINDING | wgpu::TextureUsages::COPY_DST,
            view_formats: &[wgpu::TextureFormat::Rgba8Unorm],
        });

        queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture: &texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            self.cpu_data.pixels(),
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(self.cpu_data.bytes_per_row() as u32),
                rows_per_image: Some(self.cpu_data.height()),
            },
            size,
        );

        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        let bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("zv image item bind group"),
            layout: bind_group_layout,
            entries: &[
                wgpu::BindGroupEntry {
                    binding: 0,
                    resource: wgpu::BindingResource::TextureView(&view),
                },
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::Sampler(sampler),
                },
            ],
        });

        self.texture_data = Some(WgpuImageTexture {
            _texture: texture,
            _view: view,
            bind_group,
        });
    }

    pub fn gpu_bind_group(&self) -> Option<&wgpu::BindGroup> {
        self.texture_data
            .as_ref()
            .map(|texture_data| &texture_data.bind_group)
    }
}

pub struct WgpuImageTexture {
    _texture: wgpu::Texture,
    _view: wgpu::TextureView,
    bind_group: wgpu::BindGroup,
}
