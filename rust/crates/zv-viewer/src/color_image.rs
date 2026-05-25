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

pub fn mip_level_count(width: u32, height: u32) -> u32 {
    u32::BITS - width.max(height).leading_zeros()
}

pub fn downsample_2x_srgba(source: &ImageSRGBA) -> ImageSRGBA {
    let src_w = source.width() as usize;
    let src_h = source.height() as usize;
    let dst_w = (src_w / 2).max(1);
    let dst_h = (src_h / 2).max(1);
    let mut target = ImageSRGBA::new(dst_w as u32, dst_h as u32);

    // safe_dst_{w,h}: iterations where x0+1 / y0+1 are guaranteed in-bounds.
    // The tails (<=1 row or column) only fire for 1-pixel source dimensions.
    let safe_dst_w = src_w / 2;
    let safe_dst_h = src_h / 2;

    // Main y loop: y1 = y0+1 is always in-bounds.
    for dst_y in 0..safe_dst_h {
        let y0 = dst_y * 2;
        let row0: &[u32] = bytemuck::cast_slice(&source.row_bytes(y0 as u32).unwrap()[..src_w * 4]);
        let row1: &[u32] = bytemuck::cast_slice(&source.row_bytes((y0 + 1) as u32).unwrap()[..src_w * 4]);
        let dst_bytes = target.row_bytes_mut(dst_y as u32).unwrap();
        let dst_row: &mut [u32] = bytemuck::cast_slice_mut(&mut dst_bytes[..dst_w * 4]);
        // Safe x loop: pre-sliced so LLVM can prove all accesses in-bounds and
        // vectorize with LD2 / LD2.4S on NEON, or 128-bit loads on x86.
        downsample_row_chunks(
            &row0[..safe_dst_w * 2],
            &row1[..safe_dst_w * 2],
            &mut dst_row[..safe_dst_w],
        );
        // Tail x: at most one pixel (only when src_w == 1).
        for dst_x in safe_dst_w..dst_w {
            let x0 = dst_x * 2;
            let x1 = (x0 + 1).min(src_w - 1);
            dst_row[dst_x] = avg4_packed_rgba(row0[x0], row0[x1], row1[x0], row1[x1]);
        }
    }
    // Tail y: at most one row (only when src_h == 1).
    for dst_y in safe_dst_h..dst_h {
        let y0 = dst_y * 2;
        let y1 = (y0 + 1).min(src_h - 1);
        let row0: &[u32] = bytemuck::cast_slice(&source.row_bytes(y0 as u32).unwrap()[..src_w * 4]);
        let row1: &[u32] = bytemuck::cast_slice(&source.row_bytes(y1 as u32).unwrap()[..src_w * 4]);
        let dst_bytes = target.row_bytes_mut(dst_y as u32).unwrap();
        let dst_row: &mut [u32] = bytemuck::cast_slice_mut(&mut dst_bytes[..dst_w * 4]);
        downsample_row_chunks(
            &row0[..safe_dst_w * 2],
            &row1[..safe_dst_w * 2],
            &mut dst_row[..safe_dst_w],
        );
        for dst_x in safe_dst_w..dst_w {
            let x0 = dst_x * 2;
            let x1 = (x0 + 1).min(src_w - 1);
            dst_row[dst_x] = avg4_packed_rgba(row0[x0], row0[x1], row1[x0], row1[x1]);
        }
    }

    target
}

/// Average one output row. `row0` and `row1` must each have exactly `2 * dst.len()` elements.
///
/// Pseudocode per output pixel i:
/// ```text
///   dst[i] = avg4( row0[2i], row0[2i+1],   // top-left,  top-right
///                  row1[2i], row1[2i+1] )   // bot-left,  bot-right
/// ```
/// `as_chunks::<2>()` gives the compiler array references of known size 2, so
/// `s[0]`/`s[1]` are bounds-check-free and LLVM emits `LD2.4S` on NEON
/// (4 output pixels per iteration) with opt-level >= 2.
#[inline]
fn downsample_row_chunks(row0: &[u32], row1: &[u32], dst: &mut [u32]) {
    let (s0, _) = row0.as_chunks::<2>();
    let (s1, _) = row1.as_chunks::<2>();
    for ((d, a), b) in dst.iter_mut().zip(s0).zip(s1) {
        *d = avg4_packed_rgba(a[0], a[1], b[0], b[1]);
    }
}

/// Average four RGBA pixels packed as u32 using SWAR (SIMD-within-a-register).
///
/// Pseudocode (per channel):
/// ```text
///   out.R = (a.R + b.R + c.R + d.R + 2) >> 2    // rounded divide-by-4
///   out.G = ...   out.B = ...   out.A = ...
/// ```
/// Implementation uses two 16-bit lanes packed in a u32 via the `0x00FF00FF` mask,
/// so channels {R,B} and {G,A} are summed in parallel without inter-channel overflow
/// (max per-channel sum 4x255+2 = 1022, fits in 10 bits; lanes are 16 bits apart).
#[inline(always)]
fn avg4_packed_rgba(a: u32, b: u32, c: u32, d: u32) -> u32 {
    const MASK: u32 = 0x00FF_00FF;
    // Lane 0 = R (bits 0-7), Lane 1 = B (bits 16-23)
    let lo = (a & MASK) + (b & MASK) + (c & MASK) + (d & MASK) + 0x0002_0002;
    // Lane 0 = G (bits 0-7), Lane 1 = A (bits 16-23), shifted from odd byte positions
    let hi = ((a >> 8) & MASK) + ((b >> 8) & MASK) + ((c >> 8) & MASK) + ((d >> 8) & MASK) + 0x0002_0002;
    ((lo >> 2) & MASK) | (((hi >> 2) & MASK) << 8)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mip_level_count_includes_base_level_and_1x1() {
        assert_eq!(mip_level_count(1, 1), 1);
        assert_eq!(mip_level_count(2, 1), 2);
        assert_eq!(mip_level_count(3, 2), 2);
        assert_eq!(mip_level_count(4, 4), 3);
        assert_eq!(mip_level_count(640, 480), 10);
    }

    #[test]
    fn downsample_handles_odd_dimensions() {
        // 3x1 -> 1x1 (floor(3/2)=1): strict 2x2 averages pixels (0,0) and (1,0);
        // y1 is clamped to y0 (single row), x=2 is outside the window and dropped.
        let mut source = ImageSRGBA::new(3, 1);
        source.row_mut(0).unwrap()[0] = PixelSRGBA {
            r: 200,
            g: 0,
            b: 0,
            a: 255,
        };
        source.row_mut(0).unwrap()[1] = PixelSRGBA {
            r: 100,
            g: 0,
            b: 0,
            a: 255,
        };
        source.row_mut(0).unwrap()[2] = PixelSRGBA {
            r: 0,
            g: 0,
            b: 255,
            a: 255,
        };

        let mip = downsample_2x_srgba(&source);
        assert_eq!(mip.width(), 1);
        assert_eq!(mip.height(), 1);
        // (200+100+200+100+2)/4 = 150; blue pixel at x=2 not included
        let p = mip.pixel(0, 0).unwrap();
        assert_eq!(p.r, 150);
        assert_eq!(p.b, 0);
        assert_eq!(p.a, 255);
    }
}
