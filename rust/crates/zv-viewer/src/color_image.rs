use std::marker::PhantomData;

use eframe::egui_wgpu::wgpu;

#[allow(dead_code)]
pub trait PixelFormat: Copy + 'static {
    type Pixel: bytemuck::Pod + Copy + Default;

    const NAME: &'static str;
    const CHANNELS: usize;
    const BYTES_PER_PIXEL: usize;
    const WGPU_FORMAT: wgpu::TextureFormat;
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, bytemuck::Pod, bytemuck::Zeroable)]
pub struct PixelSRGBA {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

impl PixelSRGBA {
    pub fn as_array(self) -> [u8; 4] {
        [self.r, self.g, self.b, self.a]
    }
}

#[derive(Clone, Copy, Debug)]
pub enum Srgba8Format {}

impl PixelFormat for Srgba8Format {
    type Pixel = PixelSRGBA;

    const NAME: &'static str = "sRGBA8";
    const CHANNELS: usize = 4;
    const BYTES_PER_PIXEL: usize = std::mem::size_of::<PixelSRGBA>();
    // Image files decoded into ImageSRGBA contain display-referred sRGB bytes.
    // Upload them as an sRGB texture so hardware texture filtering happens in
    // linear light. The final encode is handled by the image render shader
    // because egui-wgpu normally renders into a gamma-space Rgba/Bgra8Unorm
    // surface instead of an sRGB surface.
    const WGPU_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba8UnormSrgb;
}

pub type ImageSRGBA = Image<Srgba8Format>;

#[derive(Clone, Debug)]
pub struct Image<F: PixelFormat> {
    width: u32,
    height: u32,
    bytes_per_row: usize,
    bytes: Vec<u8>,
    _format: PhantomData<F>,
}

impl<F: PixelFormat> Image<F> {
    pub const ROW_ALIGNMENT: usize = 256;

    pub fn new(width: u32, height: u32) -> Self {
        assert_eq!(
            F::BYTES_PER_PIXEL,
            std::mem::size_of::<F::Pixel>(),
            "PixelFormat::BYTES_PER_PIXEL must match the typed pixel size",
        );

        let bytes_per_row = aligned_bytes_per_row::<F>(width);
        let bytes = vec![0; bytes_per_row * height as usize];
        Self {
            width,
            height,
            bytes_per_row,
            bytes,
            _format: PhantomData,
        }
    }

    pub fn from_tightly_packed_bytes(width: u32, height: u32, input: &[u8]) -> Self {
        let tight_bytes_per_row = width as usize * F::BYTES_PER_PIXEL;
        assert_eq!(input.len(), tight_bytes_per_row * height as usize);

        let mut image = Self::new(width, height);
        for row in 0..height as usize {
            let src_start = row * tight_bytes_per_row;
            let dst_start = row * image.bytes_per_row;
            image.bytes[dst_start..dst_start + tight_bytes_per_row]
                .copy_from_slice(&input[src_start..src_start + tight_bytes_per_row]);
        }
        image
    }

    pub fn width(&self) -> u32 {
        self.width
    }

    pub fn height(&self) -> u32 {
        self.height
    }

    #[allow(dead_code)]
    pub fn bytes_per_pixel(&self) -> usize {
        F::BYTES_PER_PIXEL
    }

    pub fn bytes_per_row(&self) -> usize {
        self.bytes_per_row
    }

    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }

    pub fn row_bytes(&self, row: u32) -> Option<&[u8]> {
        if row >= self.height {
            return None;
        }

        let start = row as usize * self.bytes_per_row;
        Some(&self.bytes[start..start + self.bytes_per_row])
    }

    pub fn row_bytes_mut(&mut self, row: u32) -> Option<&mut [u8]> {
        if row >= self.height {
            return None;
        }

        let start = row as usize * self.bytes_per_row;
        Some(&mut self.bytes[start..start + self.bytes_per_row])
    }

    pub fn row(&self, row: u32) -> Option<&[F::Pixel]> {
        let row = self.row_bytes(row)?;
        let tight_len = self.width as usize * F::BYTES_PER_PIXEL;
        Some(bytemuck::cast_slice(&row[..tight_len]))
    }

    pub fn row_mut(&mut self, row: u32) -> Option<&mut [F::Pixel]> {
        let width = self.width as usize;
        let row = self.row_bytes_mut(row)?;
        let tight_len = width * F::BYTES_PER_PIXEL;
        Some(bytemuck::cast_slice_mut(&mut row[..tight_len]))
    }

    pub fn pixel(&self, x: u32, y: u32) -> Option<F::Pixel> {
        self.row(y)?.get(x as usize).copied()
    }
}

fn aligned_bytes_per_row<F: PixelFormat>(width: u32) -> usize {
    let tight = width as usize * F::BYTES_PER_PIXEL;
    tight.next_multiple_of(Image::<F>::ROW_ALIGNMENT)
}
