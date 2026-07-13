use std::collections::HashMap;

use crate::color_image::{ImageSRGBA, PixelSRGBA};

#[derive(Clone, Debug, PartialEq)]
pub struct ChannelStats {
    pub min: u8,
    pub max: u8,
    pub mean: f64,
    pub histogram: [u64; 256],
}

impl Default for ChannelStats {
    fn default() -> Self {
        Self {
            min: 255,
            max: 0,
            mean: 0.0,
            histogram: [0; 256],
        }
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct ImageColorStats {
    pub r: ChannelStats,
    pub g: ChannelStats,
    pub b: ChannelStats,
    pub a: ChannelStats,
    pub luma: ChannelStats,
    pub rgb_channels_equal: bool,
    pub alpha_all_opaque: bool,
    pub has_any_transparency: bool,
    pub single_color: bool,
    pub pixel_count: u64,
}

pub fn luma_srgb(pixel: PixelSRGBA) -> u8 {
    (0.2126 * f64::from(pixel.r) + 0.7152 * f64::from(pixel.g) + 0.0722 * f64::from(pixel.b)).round() as u8
}

pub fn compute_image_color_stats(image: &ImageSRGBA) -> ImageColorStats {
    let mut stats = ImageColorStats::default();
    stats.pixel_count = u64::from(image.width()) * u64::from(image.height());
    if stats.pixel_count == 0 {
        return stats;
    }

    let mut sums = [0_u64; 5];
    for y in 0..image.height() {
        for pixel in image.row(y).expect("row is in bounds") {
            let values = [pixel.r, pixel.g, pixel.b, pixel.a, luma_srgb(*pixel)];
            let channels = [&mut stats.r, &mut stats.g, &mut stats.b, &mut stats.a, &mut stats.luma];
            for ((channel, value), sum) in channels.into_iter().zip(values).zip(&mut sums) {
                channel.histogram[value as usize] += 1;
                channel.min = channel.min.min(value);
                channel.max = channel.max.max(value);
                *sum += u64::from(value);
            }
        }
    }
    let count = stats.pixel_count as f64;
    stats.r.mean = sums[0] as f64 / count;
    stats.g.mean = sums[1] as f64 / count;
    stats.b.mean = sums[2] as f64 / count;
    stats.a.mean = sums[3] as f64 / count;
    stats.luma.mean = sums[4] as f64 / count;
    stats.alpha_all_opaque = stats.a.min == 255;
    stats.has_any_transparency = stats.a.min < 255;
    stats.single_color = stats.r.min == stats.r.max && stats.g.min == stats.g.max && stats.b.min == stats.b.max;
    stats.rgb_channels_equal = is_grayscale_like(image);
    stats
}

fn is_grayscale_like(image: &ImageSRGBA) -> bool {
    let count = u64::from(image.width()) * u64::from(image.height());
    if count == 0 {
        return false;
    }
    let stride = (count / 10_000).max(1);
    (0..count).step_by(stride as usize).all(|i| {
        let p = image
            .pixel(
                (i % u64::from(image.width())) as u32,
                (i / u64::from(image.width())) as u32,
            )
            .unwrap();
        i16::from(p.r).abs_diff(i16::from(p.g)) <= 2 && i16::from(p.r).abs_diff(i16::from(p.b)) <= 2
    })
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct StatsCacheKey {
    pub image_identity: usize,
    pub display_revision: u64,
}

#[derive(Default)]
pub struct StatsCache {
    entries: HashMap<StatsCacheKey, ImageColorStats>,
}

impl StatsCache {
    pub fn get_or_compute(&mut self, key: StatsCacheKey, image: &ImageSRGBA) -> &ImageColorStats {
        self.entries
            .entry(key)
            .or_insert_with(|| compute_image_color_stats(image))
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LevelsParams {
    pub input_black: i32,
    pub input_white: i32,
    pub gamma: f32,
    pub output_black: i32,
    pub output_white: i32,
}

impl Default for LevelsParams {
    fn default() -> Self {
        Self {
            input_black: 0,
            input_white: 255,
            gamma: 1.0,
            output_black: 0,
            output_white: 255,
        }
    }
}

impl LevelsParams {
    pub fn is_identity(self) -> bool {
        self == Self::default()
    }
    pub fn sanitized(mut self) -> Self {
        self.input_black = self.input_black.clamp(0, 254);
        self.input_white = self.input_white.clamp(1, 255);
        if self.input_black >= self.input_white {
            self.input_black = self.input_white - 1;
        }
        self.gamma = self.gamma.clamp(0.1, 10.0);
        self.output_black = self.output_black.clamp(0, 255);
        self.output_white = self.output_white.clamp(0, 255);
        if self.output_black > self.output_white {
            std::mem::swap(&mut self.output_black, &mut self.output_white);
        }
        self
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct LevelsAdjustment {
    pub luma: LevelsParams,
    pub red: LevelsParams,
    pub green: LevelsParams,
    pub blue: LevelsParams,
}

impl LevelsAdjustment {
    pub fn is_identity(self) -> bool {
        self.luma.is_identity() && self.red.is_identity() && self.green.is_identity() && self.blue.is_identity()
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct CompiledLevelsLut {
    pub r: [u8; 256],
    pub g: [u8; 256],
    pub b: [u8; 256],
}

fn compile_channel_lut(params: LevelsParams) -> [u8; 256] {
    let p = params.sanitized();
    std::array::from_fn(|i| {
        let normalized = ((i as f64 - f64::from(p.input_black)) / f64::from((p.input_white - p.input_black).max(1)))
            .clamp(0.0, 1.0)
            .powf(f64::from(p.gamma));
        (f64::from(p.output_black) + normalized * f64::from(p.output_white - p.output_black))
            .round()
            .clamp(0.0, 255.0) as u8
    })
}

pub fn compile_levels_lut(params: LevelsAdjustment) -> CompiledLevelsLut {
    let luma = compile_channel_lut(params.luma);
    CompiledLevelsLut {
        r: if params.red.is_identity() {
            luma
        } else {
            compile_channel_lut(params.red)
        },
        g: if params.green.is_identity() {
            luma
        } else {
            compile_channel_lut(params.green)
        },
        b: if params.blue.is_identity() {
            luma
        } else {
            compile_channel_lut(params.blue)
        },
    }
}

pub fn apply_levels(image: &ImageSRGBA, params: LevelsAdjustment) -> ImageSRGBA {
    let lut = compile_levels_lut(params);
    map_pixels(image, |p| PixelSRGBA {
        r: lut.r[p.r as usize],
        g: lut.g[p.g as usize],
        b: lut.b[p.b as usize],
        a: p.a,
    })
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct HueShiftParams {
    pub degrees: f32,
}

pub fn apply_hue_shift(image: &ImageSRGBA, params: HueShiftParams) -> ImageSRGBA {
    let shift = params.degrees / 360.0;
    map_pixels(image, |p| {
        let (r, g, b) = (p.r as f32 / 255.0, p.g as f32 / 255.0, p.b as f32 / 255.0);
        let max = r.max(g).max(b);
        let min = r.min(g).min(b);
        let delta = max - min;
        let mut h = if delta <= 1e-6 {
            0.0
        } else if max == r {
            ((g - b) / delta).rem_euclid(6.0) / 6.0
        } else if max == g {
            ((b - r) / delta + 2.0) / 6.0
        } else {
            ((r - g) / delta + 4.0) / 6.0
        };
        h = (h + shift).rem_euclid(1.0);
        let s = if max > 1e-6 { delta / max } else { 0.0 };
        let hh = h * 6.0;
        let sector = hh.floor() as i32;
        let f = hh - sector as f32;
        let q = max * (1.0 - s * f);
        let t = max * (1.0 - s * (1.0 - f));
        let low = max * (1.0 - s);
        let (ro, go, bo) = match sector.rem_euclid(6) {
            0 => (max, t, low),
            1 => (q, max, low),
            2 => (low, max, t),
            3 => (low, q, max),
            4 => (t, low, max),
            _ => (max, low, q),
        };
        PixelSRGBA {
            r: (ro * 255.0).round() as u8,
            g: (go * 255.0).round() as u8,
            b: (bo * 255.0).round() as u8,
            a: p.a,
        }
    })
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum InvertTarget {
    Rgb,
    Red,
    Green,
    Blue,
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum GrayscaleMode {
    LumaSrgb,
    Red,
    Green,
    Blue,
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LabelColorizeParams {
    pub seed: u32,
    pub background_value: u8,
}
impl Default for LabelColorizeParams {
    fn default() -> Self {
        Self {
            seed: 1,
            background_value: 0,
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum OneShotOperation {
    SwapRedBlue,
    SwapRedGreen,
    SwapGreenBlue,
    Invert(InvertTarget),
    Grayscale(GrayscaleMode),
    HistogramEqualization,
    LabelColorize(LabelColorizeParams),
}

pub fn apply_one_shot(image: &ImageSRGBA, operation: OneShotOperation) -> ImageSRGBA {
    if matches!(operation, OneShotOperation::LabelColorize(_)) && !is_grayscale_like(image) {
        return image.clone();
    }
    let hist_lut =
        matches!(operation, OneShotOperation::HistogramEqualization).then(|| histogram_equalization_lut(image));
    map_pixels(image, |p| match operation {
        OneShotOperation::SwapRedBlue => PixelSRGBA {
            r: p.b,
            g: p.g,
            b: p.r,
            a: p.a,
        },
        OneShotOperation::SwapRedGreen => PixelSRGBA {
            r: p.g,
            g: p.r,
            b: p.b,
            a: p.a,
        },
        OneShotOperation::SwapGreenBlue => PixelSRGBA {
            r: p.r,
            g: p.b,
            b: p.g,
            a: p.a,
        },
        OneShotOperation::Invert(target) => PixelSRGBA {
            r: if matches!(target, InvertTarget::Rgb | InvertTarget::Red) {
                255 - p.r
            } else {
                p.r
            },
            g: if matches!(target, InvertTarget::Rgb | InvertTarget::Green) {
                255 - p.g
            } else {
                p.g
            },
            b: if matches!(target, InvertTarget::Rgb | InvertTarget::Blue) {
                255 - p.b
            } else {
                p.b
            },
            a: p.a,
        },
        OneShotOperation::Grayscale(mode) => {
            let v = match mode {
                GrayscaleMode::LumaSrgb => luma_srgb(p),
                GrayscaleMode::Red => p.r,
                GrayscaleMode::Green => p.g,
                GrayscaleMode::Blue => p.b,
            };
            PixelSRGBA {
                r: v,
                g: v,
                b: v,
                a: p.a,
            }
        }
        OneShotOperation::HistogramEqualization => {
            let v = hist_lut.unwrap()[luma_srgb(p) as usize];
            PixelSRGBA {
                r: v,
                g: v,
                b: v,
                a: p.a,
            }
        }
        OneShotOperation::LabelColorize(params) => {
            if p.r == params.background_value {
                p
            } else {
                let [r, g, b] = label_color(params.seed, p.r);
                PixelSRGBA { r, g, b, a: p.a }
            }
        }
    })
}

fn histogram_equalization_lut(image: &ImageSRGBA) -> [u8; 256] {
    let mut histogram = [0_u64; 256];
    for y in 0..image.height() {
        for p in image.row(y).unwrap() {
            histogram[luma_srgb(*p) as usize] += 1;
        }
    }
    let count = u64::from(image.width()) * u64::from(image.height());
    let cdf_min = histogram.iter().copied().find(|&n| n != 0).unwrap_or(0);
    if count == 0 || cdf_min >= count {
        return std::array::from_fn(|i| i as u8);
    }
    let mut cdf = 0;
    std::array::from_fn(|i| {
        cdf += histogram[i];
        (255.0 * ((cdf.saturating_sub(cdf_min)) as f64 / (count - cdf_min) as f64)).round() as u8
    })
}

// A small stable mixer makes label colors deterministic without coupling the file format to a RNG crate.
fn label_color(seed: u32, label: u8) -> [u8; 3] {
    let mut x = u64::from(seed) ^ u64::from(label).wrapping_mul(0x9e37_79b9);
    x ^= x >> 16;
    x = x.wrapping_mul(0x21f0_aaad);
    x ^= x >> 15;
    let hue = (x % 360) as f32 / 60.0;
    let s = (58 + (x / 360 % 35) as u8) as f32 / 100.0;
    let v = (62 + (x / 12600 % 35) as u8) as f32 / 100.0;
    let i = hue.floor() as i32;
    let f = hue - hue.floor();
    let p = v * (1.0 - s);
    let q = v * (1.0 - s * f);
    let t = v * (1.0 - s * (1.0 - f));
    let (r, g, b) = match i % 6 {
        0 => (v, t, p),
        1 => (q, v, p),
        2 => (p, v, t),
        3 => (p, q, v),
        4 => (t, p, v),
        _ => (v, p, q),
    };
    [
        (r * 255.0).round() as u8,
        (g * 255.0).round() as u8,
        (b * 255.0).round() as u8,
    ]
}

fn map_pixels(image: &ImageSRGBA, f: impl Fn(PixelSRGBA) -> PixelSRGBA) -> ImageSRGBA {
    let mut out = ImageSRGBA::new(image.width(), image.height());
    for y in 0..image.height() {
        for (dst, src) in out.row_mut(y).unwrap().iter_mut().zip(image.row(y).unwrap()) {
            *dst = f(*src);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    fn image(pixels: &[[u8; 4]]) -> ImageSRGBA {
        ImageSRGBA::from_tightly_packed_bytes(pixels.len() as u32, 1, bytemuck::cast_slice(pixels))
    }
    fn pixels(im: &ImageSRGBA) -> Vec<PixelSRGBA> {
        im.row(0).unwrap().to_vec()
    }

    #[test]
    fn stats_histograms_luma_flags_and_empty() {
        let empty = compute_image_color_stats(&ImageSRGBA::new(0, 0));
        assert_eq!(empty.pixel_count, 0);
        let s = compute_image_color_stats(&image(&[[0, 0, 0, 255], [100, 200, 50, 0]]));
        assert_eq!((s.r.min, s.r.max, s.r.mean), (0, 100, 50.0));
        assert_eq!(s.r.histogram[0], 1);
        assert_eq!(s.r.histogram[100], 1);
        assert_eq!(s.luma.histogram[168], 1);
        assert!(!s.rgb_channels_equal);
        assert!(s.has_any_transparency);
        assert!(!s.alpha_all_opaque);
    }
    #[test]
    fn grayscale_tolerance_and_single_color() {
        let s = compute_image_color_stats(&image(&[[42, 43, 44, 255], [42, 43, 44, 7]]));
        assert!(s.rgb_channels_equal);
        assert!(s.single_color);
    }
    #[test]
    fn stats_cache_keys_identity_and_revision() {
        let im = image(&[[1, 1, 1, 255]]);
        let mut c = StatsCache::default();
        let a = c.get_or_compute(
            StatsCacheKey {
                image_identity: 3,
                display_revision: 0,
            },
            &im,
        ) as *const _;
        let b = c.get_or_compute(
            StatsCacheKey {
                image_identity: 3,
                display_revision: 0,
            },
            &image(&[[9, 9, 9, 255]]),
        ) as *const _;
        assert_eq!(a, b);
        assert_eq!(
            c.get_or_compute(
                StatsCacheKey {
                    image_identity: 3,
                    display_revision: 1
                },
                &im
            )
            .r
            .mean,
            1.0
        );
    }
    #[test]
    fn levels_identity_replacement_gamma_clamping_alpha() {
        let mut p = LevelsAdjustment::default();
        assert!(p.is_identity());
        let id = compile_levels_lut(p);
        assert_eq!((id.r[0], id.r[127], id.r[255]), (0, 127, 255));
        p.luma.output_black = 10;
        p.luma.output_white = 200;
        p.red.input_black = 128;
        let lut = compile_levels_lut(p);
        assert_eq!((lut.g[0], lut.g[255]), (10, 200));
        assert_eq!((lut.r[127], lut.r[255]), (0, 255));
        p.blue.gamma = 2.0;
        assert_eq!(compile_levels_lut(p).b[128], 64);
        p.green.input_black = 999;
        p.green.input_white = -3;
        p.green.gamma = 99.0;
        let out = apply_levels(&image(&[[128, 64, 32, 17]]), p);
        assert_eq!(pixels(&out)[0].a, 17);
    }
    #[test]
    fn hue_wrap_primary_colors_gray_and_alpha() {
        let red = image(&[[255, 0, 0, 42]]);
        assert_eq!(
            pixels(&apply_hue_shift(&red, HueShiftParams { degrees: 120.0 }))[0],
            PixelSRGBA {
                r: 0,
                g: 255,
                b: 0,
                a: 42
            }
        );
        assert_eq!(
            pixels(&apply_hue_shift(&red, HueShiftParams { degrees: 180.0 }))[0],
            PixelSRGBA {
                r: 0,
                g: 255,
                b: 255,
                a: 42
            }
        );
        let gray = image(&[[128, 128, 128, 9]]);
        assert_eq!(
            pixels(&apply_hue_shift(&gray, HueShiftParams { degrees: 497.0 })),
            pixels(&gray)
        );
        assert_eq!(
            pixels(&apply_hue_shift(&red, HueShiftParams { degrees: 360.0 })),
            pixels(&red)
        );
    }
    #[test]
    fn swaps_invert_and_grayscale() {
        let im = image(&[[10, 20, 30, 77]]);
        assert_eq!(
            pixels(&apply_one_shot(&im, OneShotOperation::SwapRedBlue))[0].as_array(),
            [30, 20, 10, 77]
        );
        assert_eq!(
            pixels(&apply_one_shot(&im, OneShotOperation::SwapRedGreen))[0].as_array(),
            [20, 10, 30, 77]
        );
        assert_eq!(
            pixels(&apply_one_shot(&im, OneShotOperation::SwapGreenBlue))[0].as_array(),
            [10, 30, 20, 77]
        );
        assert_eq!(
            pixels(&apply_one_shot(&im, OneShotOperation::Invert(InvertTarget::Rgb)))[0].as_array(),
            [245, 235, 225, 77]
        );
        assert_eq!(
            pixels(&apply_one_shot(&im, OneShotOperation::Invert(InvertTarget::Red)))[0].as_array(),
            [245, 20, 30, 77]
        );
        assert_eq!(
            pixels(&apply_one_shot(&im, OneShotOperation::Invert(InvertTarget::Green)))[0].as_array(),
            [10, 235, 30, 77]
        );
        assert_eq!(
            pixels(&apply_one_shot(&im, OneShotOperation::Invert(InvertTarget::Blue)))[0].as_array(),
            [10, 20, 225, 77]
        );
        assert_eq!(
            pixels(&apply_one_shot(
                &image(&[[255, 0, 0, 7]]),
                OneShotOperation::Grayscale(GrayscaleMode::LumaSrgb)
            ))[0]
                .as_array(),
            [54, 54, 54, 7]
        );
        for (mode, expected) in [
            (GrayscaleMode::Red, 10),
            (GrayscaleMode::Green, 20),
            (GrayscaleMode::Blue, 30),
        ] {
            assert_eq!(
                pixels(&apply_one_shot(&im, OneShotOperation::Grayscale(mode)))[0].as_array(),
                [expected, expected, expected, 77]
            );
        }
    }
    #[test]
    fn histogram_equalization_and_flat() {
        let im = image(&[
            [0, 0, 0, 11],
            [64, 64, 64, 22],
            [128, 128, 128, 33],
            [255, 255, 255, 44],
        ]);
        assert_eq!(
            pixels(&apply_one_shot(&im, OneShotOperation::HistogramEqualization))
                .iter()
                .map(|p| p.as_array())
                .collect::<Vec<_>>(),
            vec![
                [0, 0, 0, 11],
                [85, 85, 85, 22],
                [170, 170, 170, 33],
                [255, 255, 255, 44]
            ]
        );
        let flat = image(&[[42, 42, 42, 8], [42, 42, 42, 9]]);
        assert_eq!(
            pixels(&apply_one_shot(&flat, OneShotOperation::HistogramEqualization)),
            pixels(&flat)
        );
    }
    #[test]
    fn label_colorize_is_deterministic_preserves_background_alpha_and_skips_color() {
        let gray = image(&[[0, 0, 0, 7], [5, 5, 5, 8], [5, 5, 5, 9]]);
        let op = OneShotOperation::LabelColorize(LabelColorizeParams {
            seed: 1234,
            background_value: 0,
        });
        let a = apply_one_shot(&gray, op);
        let b = apply_one_shot(&gray, op);
        assert_eq!(pixels(&a), pixels(&b));
        assert_eq!(pixels(&a)[0].as_array(), [0, 0, 0, 7]);
        assert_eq!(&pixels(&a)[1].as_array()[..3], &pixels(&a)[2].as_array()[..3]);
        assert_eq!((pixels(&a)[1].a, pixels(&a)[2].a), (8, 9));
        let color = image(&[[10, 20, 30, 77]]);
        assert_eq!(pixels(&apply_one_shot(&color, op)), pixels(&color));
    }
}
