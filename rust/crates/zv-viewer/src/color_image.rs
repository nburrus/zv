use std::marker::PhantomData;
use std::sync::OnceLock;

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

    pub fn from_array(rgba: [u8; 4]) -> Self {
        Self {
            r: rgba[0],
            g: rgba[1],
            b: rgba[2],
            a: rgba[3],
        }
    }

    pub fn to_hsv(self) -> PixelHSV {
        convert_srgba_to_hsv(self)
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PixelHSV {
    /// Hue normalized to [0, 1].
    pub h: f32,
    /// Saturation normalized to [0, 1].
    pub s: f32,
    /// Value in display-code units, [0, 255].
    pub v: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PixelLinearRGB {
    pub r: f64,
    pub g: f64,
    pub b: f64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PixelXYZ {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PixelLab {
    pub l: f64,
    pub a: f64,
    pub b: f64,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ColorEntry {
    pub class_name: String,
    pub color_name: String,
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ColorMatchingResult {
    pub entry: ColorEntry,
    pub distance: f64,
}

impl PixelHSV {
    pub fn display_hsv(self) -> (i32, i32, i32) {
        (
            (self.h * 360.0).round() as i32,
            (self.s * 100.0).round() as i32,
            (self.v * 100.0 / 255.0).round() as i32,
        )
    }
}

pub fn convert_srgba_to_hsv(srgba: PixelSRGBA) -> PixelHSV {
    // HSV is usually computed from raw RGB code values, not linear RGB.
    // This follows libzv/ColorConversion.cpp::convertToHSV.
    let mut r = srgba.r as f32;
    let mut g = srgba.g as f32;
    let mut b = srgba.b as f32;

    let mut k = 0.0;
    if g < b {
        std::mem::swap(&mut g, &mut b);
        k = -1.0;
    }
    if r < g {
        std::mem::swap(&mut r, &mut g);
        k = -2.0 / 6.0 - k;
    }

    let chroma = r - g.min(b);
    PixelHSV {
        h: (k + (g - b) / (6.0 * chroma + 1e-20)).abs(),
        s: chroma / (r + 1e-20),
        v: r,
    }
}

pub fn convert_srgba_to_linear_rgb(srgba: PixelSRGBA) -> PixelLinearRGB {
    PixelLinearRGB {
        r: srgb_to_linear(srgba.r as f64 / 255.0),
        g: srgb_to_linear(srgba.g as f64 / 255.0),
        b: srgb_to_linear(srgba.b as f64 / 255.0),
    }
}

pub fn convert_srgba_to_xyz(srgba: PixelSRGBA) -> PixelXYZ {
    let rgb = convert_srgba_to_linear_rgb(srgba);
    let r = rgb.r * 100.0;
    let g = rgb.g * 100.0;
    let b = rgb.b * 100.0;

    PixelXYZ {
        x: r * 0.4124564 + g * 0.3575761 + b * 0.1804375,
        y: r * 0.2126729 + g * 0.7151522 + b * 0.0721750,
        z: r * 0.0193339 + g * 0.1191920 + b * 0.9503041,
    }
}

pub fn convert_srgba_to_lab(srgba: PixelSRGBA) -> PixelLab {
    let xyz = convert_srgba_to_xyz(srgba);
    let mut x = xyz.x / 95.047;
    let mut y = xyz.y / 100.0;
    let mut z = xyz.z / 108.883;

    x = if x > 0.008856 {
        x.cbrt()
    } else {
        7.787 * x + 16.0 / 116.0
    };
    y = if y > 0.008856 {
        y.cbrt()
    } else {
        7.787 * y + 16.0 / 116.0
    };
    z = if z > 0.008856 {
        z.cbrt()
    } else {
        7.787 * z + 16.0 / 116.0
    };

    PixelLab {
        l: 116.0 * y - 16.0,
        a: 500.0 * (x - y),
        b: 200.0 * (y - z),
    }
}

pub fn color_distance_cie2000(lab_1: PixelLab, lab_2: PixelLab) -> f64 {
    let eps = 1e-5;
    let mut c1 = (lab_1.a * lab_1.a + lab_1.b * lab_1.b).sqrt();
    let mut c2 = (lab_2.a * lab_2.a + lab_2.b * lab_2.b).sqrt();
    let mut mean_c = (c1 + c2) / 2.0;
    let mut mean_c7 = mean_c.powi(7);

    let g = 0.5 * (1.0 - (mean_c7 / (mean_c7 + 6103515625.0)).sqrt());
    let a1p = lab_1.a * (1.0 + g);
    let a2p = lab_2.a * (1.0 + g);

    c1 = (a1p * a1p + lab_1.b * lab_1.b).sqrt();
    c2 = (a2p * a2p + lab_2.b * lab_2.b).sqrt();
    let h1 = (lab_1.b.atan2(a1p) + std::f64::consts::TAU) % std::f64::consts::TAU;
    let h2 = (lab_2.b.atan2(a2p) + std::f64::consts::TAU) % std::f64::consts::TAU;

    let delta_l = lab_2.l - lab_1.l;
    let delta_c = c2 - c1;
    let delta_h_angle = if c1 * c2 < eps {
        0.0
    } else if (h2 - h1).abs() <= std::f64::consts::PI {
        h2 - h1
    } else if h2 > h1 {
        h2 - h1 - std::f64::consts::TAU
    } else {
        h2 - h1 + std::f64::consts::TAU
    };
    let delta_h = 2.0 * (c1 * c2).sqrt() * (delta_h_angle / 2.0).sin();

    let mean_l = (lab_1.l + lab_2.l) / 2.0;
    mean_c = (c1 + c2) / 2.0;
    mean_c7 = mean_c.powi(7);
    let mean_h = if c1 * c2 < eps {
        h1 + h2
    } else if (h1 - h2).abs() <= std::f64::consts::PI + eps {
        (h1 + h2) / 2.0
    } else if h1 + h2 < std::f64::consts::TAU {
        (h1 + h2 + std::f64::consts::TAU) / 2.0
    } else {
        (h1 + h2 - std::f64::consts::TAU) / 2.0
    };

    let deg2rad = std::f64::consts::PI / 180.0;
    let rad2deg = 180.0 / std::f64::consts::PI;
    let t = 1.0 - 0.17 * (mean_h - deg2rad * 30.0).cos()
        + 0.24 * (2.0 * mean_h).cos()
        + 0.32 * (3.0 * mean_h + deg2rad * 6.0).cos()
        - 0.2 * (4.0 * mean_h - deg2rad * 63.0).cos();
    let sl = 1.0 + (0.015 * (mean_l - 50.0).powi(2)) / (20.0 + (mean_l - 50.0).powi(2)).sqrt();
    let sc = 1.0 + 0.045 * mean_c;
    let sh = 1.0 + 0.015 * mean_c * t;
    let rc = 2.0 * (mean_c7 / (mean_c7 + 6103515625.0)).sqrt();
    let rt = -(deg2rad * (60.0 * (-((rad2deg * mean_h - 275.0) / 25.0).powi(2)).exp())).sin() * rc;

    ((delta_l / sl).powi(2) + (delta_c / sc).powi(2) + (delta_h / sh).powi(2) + rt * (delta_c / sc) * (delta_h / sh))
        .sqrt()
}

pub fn closest_color_entries(srgba: PixelSRGBA) -> [ColorMatchingResult; 2] {
    let target_lab = convert_srgba_to_lab(srgba);
    let mut best: [Option<ColorMatchingResult>; 2] = [None, None];
    for entry in color_entries() {
        let entry_lab = convert_srgba_to_lab(PixelSRGBA {
            r: entry.r,
            g: entry.g,
            b: entry.b,
            a: 255,
        });
        let distance = color_distance_cie2000(target_lab, entry_lab);
        let result = ColorMatchingResult {
            entry: entry.clone(),
            distance,
        };
        if best[0].as_ref().is_none_or(|current| distance < current.distance) {
            best[1] = best[0].take();
            best[0] = Some(result);
        } else if best[1].as_ref().is_none_or(|current| distance < current.distance) {
            best[1] = Some(result);
        }
    }

    [
        best[0].clone().expect("color table must contain at least two entries"),
        best[1].clone().expect("color table must contain at least two entries"),
    ]
}

fn srgb_to_linear(x: f64) -> f64 {
    if x <= 0.0 {
        0.0
    } else if x >= 1.0 {
        1.0
    } else if x < 0.04045 {
        x / 12.92
    } else {
        ((x + 0.055) / 1.055).powf(2.4)
    }
}

fn color_entries() -> &'static [ColorEntry] {
    static ENTRIES: OnceLock<Vec<ColorEntry>> = OnceLock::new();
    ENTRIES.get_or_init(parse_cpp_color_entries)
}

fn parse_cpp_color_entries() -> Vec<ColorEntry> {
    let source = include_str!("../../../../libzv/ColorConversion.cpp");
    let table = source
        .split_once("static ColorEntry colorEntries[] = {")
        .and_then(|(_, rest)| rest.split_once("};"))
        .map(|(table, _)| table)
        .expect("C++ color table should be present");
    let mut entries = Vec::new();
    for line in table.lines() {
        let line = line.trim();
        if !line.starts_with("{\"") {
            continue;
        }
        let mut quoted = line.split('"');
        let _ = quoted.next();
        let Some(class_name) = quoted.next() else { continue };
        let _ = quoted.next();
        let Some(color_name) = quoted.next() else { continue };
        let Some(after_name) = quoted.next() else { continue };
        let nums: Vec<u8> = after_name
            .trim_start_matches(',')
            .trim_end_matches(',')
            .trim_end_matches("},")
            .trim_end_matches('}')
            .split(',')
            .filter_map(|part| part.trim().parse().ok())
            .collect();
        if nums.len() == 3 {
            entries.push(ColorEntry {
                class_name: class_name.to_owned(),
                color_name: color_name.to_owned(),
                r: nums[0],
                g: nums[1],
                b: nums[2],
            });
        }
    }
    entries
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
    fn converts_srgba_to_hsv_display_values() {
        assert_eq!(
            PixelSRGBA::from_array([255, 0, 0, 255]).to_hsv().display_hsv(),
            (0, 100, 100)
        );
        assert_eq!(
            PixelSRGBA::from_array([0, 255, 0, 255]).to_hsv().display_hsv(),
            (120, 100, 100)
        );
        assert_eq!(
            PixelSRGBA::from_array([0, 0, 255, 255]).to_hsv().display_hsv(),
            (240, 100, 100)
        );
        assert_eq!(
            PixelSRGBA::from_array([128, 128, 128, 255]).to_hsv().display_hsv(),
            (0, 0, 50)
        );
    }

    #[test]
    fn converts_srgba_to_lab_reference_values() {
        let white = convert_srgba_to_lab(PixelSRGBA::from_array([255, 255, 255, 255]));
        assert!((white.l - 100.0).abs() < 0.01);
        assert!(white.a.abs() < 0.01);
        assert!(white.b.abs() < 0.01);

        let red = convert_srgba_to_xyz(PixelSRGBA::from_array([255, 0, 0, 255]));
        assert!((red.x - 41.24564).abs() < 0.001);
        assert!((red.y - 21.26729).abs() < 0.001);
        assert!((red.z - 1.93339).abs() < 0.001);
    }

    #[test]
    fn ciede2000_matches_reference_pair() {
        let d = color_distance_cie2000(
            PixelLab {
                l: 50.0,
                a: 2.6772,
                b: -79.7751,
            },
            PixelLab {
                l: 50.0,
                a: 0.0,
                b: -82.7485,
            },
        );
        assert!((d - 2.0425).abs() < 0.0001);
    }

    #[test]
    fn closest_color_entries_use_cpp_color_table() {
        assert!(color_entries().len() > 100);
        let closest = closest_color_entries(PixelSRGBA::from_array([255, 0, 0, 255]));
        assert_eq!(closest[0].entry.class_name, "Red");
        assert_eq!(closest[0].entry.color_name, "Red");
        assert!(closest[0].distance < 0.0001);
    }

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
