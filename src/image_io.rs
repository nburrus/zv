use std::path::Path;

use anyhow::Context;

use crate::color_image::ImageSRGBA;

pub fn load_rgba_image(path: &Path) -> anyhow::Result<ImageSRGBA> {
    if has_jpeg_extension(path) {
        match load_jpeg_rgba_image(path) {
            Ok(image) => return Ok(image),
            Err(jpeg_err) => {
                return load_rgba_image_generic(path).map_err(|fallback_err| {
                    anyhow::anyhow!(
                        "failed to read image '{}': TurboJPEG failed ({jpeg_err:#}); fallback image loader failed ({fallback_err:#})",
                        path.display()
                    )
                });
            }
        }
    }

    load_rgba_image_generic(path)
}

pub fn load_rgba_image_from_memory(encoded: &[u8], format_hint: Option<&str>) -> anyhow::Result<ImageSRGBA> {
    if format_hint.is_some_and(|hint| matches!(hint.to_ascii_lowercase().as_str(), "jpg" | "jpeg")) {
        match load_jpeg_rgba_image_from_memory(encoded) {
            Ok(image) => return Ok(image),
            Err(jpeg_err) => {
                return load_rgba_image_generic_from_memory(encoded).map_err(|fallback_err| {
                    anyhow::anyhow!(
                        "failed to decode remote image: TurboJPEG failed ({jpeg_err:#}); fallback image loader failed ({fallback_err:#})"
                    )
                });
            }
        }
    }

    load_rgba_image_generic_from_memory(encoded)
}

fn load_jpeg_rgba_image(path: &Path) -> anyhow::Result<ImageSRGBA> {
    let jpeg_data = std::fs::read(path).with_context(|| format!("failed to read JPEG data '{}'", path.display()))?;
    load_jpeg_rgba_image_from_memory(&jpeg_data)
        .with_context(|| format!("failed to decode JPEG '{}' with TurboJPEG", path.display()))
}

fn load_jpeg_rgba_image_from_memory(encoded: &[u8]) -> anyhow::Result<ImageSRGBA> {
    let rgba = turbojpeg::decompress(encoded, turbojpeg::PixelFormat::RGBA)
        .context("failed to decode JPEG bytes with TurboJPEG")?;

    Ok(ImageSRGBA::from_tightly_packed_bytes(
        rgba.width as u32,
        rgba.height as u32,
        &rgba.pixels,
    ))
}

fn load_rgba_image_generic(path: &Path) -> anyhow::Result<ImageSRGBA> {
    let image_data = std::fs::read(path).with_context(|| format!("failed to read image data '{}'", path.display()))?;
    load_rgba_image_generic_from_memory(&image_data)
        .with_context(|| format!("failed to decode image data '{}'", path.display()))
}

fn load_rgba_image_generic_from_memory(encoded: &[u8]) -> anyhow::Result<ImageSRGBA> {
    let dynamic = match image::load_from_memory(encoded) {
        Ok(dynamic) => dynamic,
        Err(generic_err) => {
            #[cfg(target_os = "macos")]
            if looks_like_heif(encoded) {
                return macos::load_heif_rgba_image(encoded).map_err(|native_err| {
                    anyhow::anyhow!(
                        "HEIF image was not supported by the generic loader ({generic_err}); macOS ImageIO failed ({native_err:#})"
                    )
                });
            }

            return Err(generic_err).context("image format was not recognized or the encoded data is invalid");
        }
    };
    let rgba = dynamic.into_rgba8();
    let (width, height) = rgba.dimensions();
    Ok(ImageSRGBA::from_tightly_packed_bytes(width, height, rgba.as_raw()))
}

#[cfg(any(target_os = "macos", test))]
fn looks_like_heif(encoded: &[u8]) -> bool {
    const HEVC_BRANDS: [[u8; 4]; 8] = [
        *b"heic", *b"heix", *b"hevc", *b"hevx", *b"heim", *b"heis", *b"hevm", *b"hevs",
    ];

    let Some(box_size_bytes) = encoded.get(..4) else {
        return false;
    };
    if encoded.get(4..8) != Some(b"ftyp") {
        return false;
    }

    let declared_size = u32::from_be_bytes(box_size_bytes.try_into().expect("four-byte slice")) as usize;
    let (header_size, box_size) = if declared_size == 1 {
        let Some(extended_size) = encoded.get(8..16) else {
            return false;
        };
        let Ok(extended_size) =
            usize::try_from(u64::from_be_bytes(extended_size.try_into().expect("eight-byte slice")))
        else {
            return false;
        };
        (16, extended_size)
    } else {
        (
            8,
            if declared_size == 0 {
                encoded.len()
            } else {
                declared_size
            },
        )
    };

    if box_size < header_size + 8 || box_size > encoded.len() {
        return false;
    }

    let brands = &encoded[header_size..box_size];
    brands
        .get(..4)
        .into_iter()
        .chain(
            brands
                .get(8..)
                .into_iter()
                .flat_map(|compatible| compatible.chunks_exact(4)),
        )
        .any(|brand| HEVC_BRANDS.iter().any(|candidate| brand == candidate))
}

#[cfg(target_os = "macos")]
mod macos {
    use std::ffi::c_void;

    use anyhow::Context;
    use core_graphics::base::kCGImageAlphaPremultipliedLast;
    use core_graphics::color_space::{CGColorSpace, kCGColorSpaceSRGB};
    use core_graphics::context::CGContext;
    use core_graphics::data_provider::CGDataProvider;
    use core_graphics::geometry::{CGPoint, CGRect, CGSize};
    use core_graphics::image::CGImage;
    use foreign_types::ForeignType;

    use crate::color_image::ImageSRGBA;

    type CGImageSourceRef = *const c_void;

    struct ImageSource(CGImageSourceRef);

    impl Drop for ImageSource {
        fn drop(&mut self) {
            unsafe { CFRelease(self.0) };
        }
    }

    pub(super) fn load_heif_rgba_image(encoded: &[u8]) -> anyhow::Result<ImageSRGBA> {
        let provider = unsafe { CGDataProvider::from_slice(encoded) };
        let source = ImageSource(unsafe { CGImageSourceCreateWithDataProvider(provider.as_ptr(), std::ptr::null()) });
        anyhow::ensure!(!source.0.is_null(), "ImageIO could not create an image source");

        let index = unsafe { CGImageSourceGetPrimaryImageIndex(source.0) };
        let image_ptr = unsafe { CGImageSourceCreateImageAtIndex(source.0, index, std::ptr::null()) };
        anyhow::ensure!(!image_ptr.is_null(), "ImageIO could not decode the primary image");
        let image = unsafe { CGImage::from_ptr(image_ptr) };

        let width = image.width();
        let height = image.height();
        anyhow::ensure!(width > 0 && height > 0, "ImageIO returned empty image dimensions");
        let bytes_per_row = width.checked_mul(4).context("HEIF row size overflow")?;
        let byte_count = bytes_per_row.checked_mul(height).context("HEIF image size overflow")?;
        let mut rgba = vec![0_u8; byte_count];

        let color_space = CGColorSpace::create_with_name(unsafe { kCGColorSpaceSRGB })
            .context("could not create the sRGB color space")?;
        let context = CGContext::create_bitmap_context(
            Some(rgba.as_mut_ptr().cast()),
            width,
            height,
            8,
            bytes_per_row,
            &color_space,
            kCGImageAlphaPremultipliedLast,
        );
        context.draw_image(
            CGRect::new(&CGPoint::new(0.0, 0.0), &CGSize::new(width as f64, height as f64)),
            &image,
        );
        context.flush();
        drop(context);

        unpremultiply_rgba(&mut rgba);
        ImageSRGBA::try_from_tightly_packed_bytes(width, height, &rgba)
    }

    fn unpremultiply_rgba(rgba: &mut [u8]) {
        for pixel in rgba.chunks_exact_mut(4) {
            let alpha = u32::from(pixel[3]);
            if alpha != 0 && alpha != 255 {
                for channel in &mut pixel[..3] {
                    *channel = ((u32::from(*channel) * 255 + alpha / 2) / alpha).min(255) as u8;
                }
            }
        }
    }

    #[link(name = "ImageIO", kind = "framework")]
    unsafe extern "C" {
        fn CGImageSourceCreateWithDataProvider(
            provider: core_graphics::sys::CGDataProviderRef,
            options: *const c_void,
        ) -> CGImageSourceRef;
        fn CGImageSourceGetPrimaryImageIndex(source: CGImageSourceRef) -> usize;
        fn CGImageSourceCreateImageAtIndex(
            source: CGImageSourceRef,
            index: usize,
            options: *const c_void,
        ) -> core_graphics::sys::CGImageRef;
    }

    #[link(name = "CoreFoundation", kind = "framework")]
    unsafe extern "C" {
        fn CFRelease(value: *const c_void);
    }
}

fn has_jpeg_extension(path: &Path) -> bool {
    path.extension()
        .and_then(|extension| extension.to_str())
        .is_some_and(|extension| matches!(extension.to_ascii_lowercase().as_str(), "jpg" | "jpeg"))
}

pub fn write_rgba_image(path: &Path, image: &ImageSRGBA) -> anyhow::Result<()> {
    let tight = image.to_tightly_packed_bytes();
    ::image::save_buffer(path, &tight, image.width(), image.height(), ::image::ColorType::Rgba8)
        .with_context(|| format!("failed to write image '{}'", path.display()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::color_image::PixelSRGBA;
    use std::path::PathBuf;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_image_path(name: &str, extension: &str) -> PathBuf {
        let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        let dir = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tmp/image-io-tests");
        std::fs::create_dir_all(&dir).unwrap();
        dir.join(format!("{name}-{}-{stamp}.{extension}", std::process::id()))
    }

    fn write_png(path: &Path) {
        let rgba = [
            255, 0, 0, 255, //
            0, 255, 0, 128, //
            0, 0, 255, 64, //
            255, 255, 255, 0,
        ];
        image::save_buffer_with_format(path, &rgba, 2, 2, image::ColorType::Rgba8, image::ImageFormat::Png).unwrap();
    }

    fn write_jpeg(path: &Path) {
        let rgb = [
            255, 0, 0, //
            0, 255, 0, //
            0, 0, 255, //
            255, 255, 255,
        ];
        image::save_buffer_with_format(path, &rgb, 2, 2, image::ColorType::Rgb8, image::ImageFormat::Jpeg).unwrap();
    }

    #[test]
    fn jpeg_files_decode_with_turbojpeg() {
        let path = temp_image_path("valid-jpeg", "jpg");
        write_jpeg(&path);

        let image = load_jpeg_rgba_image(&path).unwrap();

        assert_eq!(image.width(), 2);
        assert_eq!(image.height(), 2);
        for y in 0..image.height() {
            for x in 0..image.width() {
                assert_eq!(image.pixel(x, y).unwrap().a, 255);
            }
        }

        let _ = std::fs::remove_file(path);
    }

    #[test]
    fn png_data_with_jpg_extension_falls_back_to_generic_loader() {
        let path = temp_image_path("png-with-jpg-extension", "jpg");
        write_png(&path);

        let image = load_rgba_image(&path).unwrap();

        assert_eq!(image.width(), 2);
        assert_eq!(image.height(), 2);
        assert_eq!(image.pixel(0, 0).unwrap().as_array(), [255, 0, 0, 255]);
        assert_eq!(image.pixel(1, 0).unwrap().as_array(), [0, 255, 0, 128]);
        assert_eq!(image.pixel(0, 1).unwrap().as_array(), [0, 0, 255, 64]);
        assert_eq!(image.pixel(1, 1).unwrap().as_array(), [255, 255, 255, 0]);

        let _ = std::fs::remove_file(path);
    }

    #[test]
    fn png_files_use_generic_loader() {
        let path = temp_image_path("normal-png", "png");
        write_png(&path);

        let image = load_rgba_image(&path).unwrap();

        assert_eq!(image.width(), 2);
        assert_eq!(image.height(), 2);
        assert_eq!(
            image.pixel(0, 0).unwrap(),
            PixelSRGBA {
                r: 255,
                g: 0,
                b: 0,
                a: 255,
            }
        );

        let _ = std::fs::remove_file(path);
    }

    #[test]
    fn remote_encoded_bytes_decode_from_memory() {
        let path = temp_image_path("remote-png", "png");
        write_png(&path);
        let encoded = std::fs::read(&path).unwrap();

        let image = load_rgba_image_from_memory(&encoded, Some("png")).unwrap();

        assert_eq!((image.width(), image.height()), (2, 2));
        assert_eq!(image.pixel(1, 0).unwrap().as_array(), [0, 255, 0, 128]);
        let _ = std::fs::remove_file(path);
    }

    #[test]
    fn invalid_jpeg_extension_error_mentions_turbojpeg_and_fallback() {
        let path = temp_image_path("invalid-jpeg-extension", "jpeg");
        std::fs::write(&path, b"not an image").unwrap();

        let err = load_rgba_image(&path).unwrap_err();
        let message = format!("{err:#}");

        assert!(message.contains("TurboJPEG failed"), "{message}");
        assert!(message.contains("fallback image loader failed"), "{message}");

        let _ = std::fs::remove_file(path);
    }

    #[test]
    fn heif_detector_requires_an_hevc_brand_in_a_valid_ftyp_box() {
        fn ftyp(major_brand: &[u8; 4], compatible_brand: &[u8; 4]) -> Vec<u8> {
            let mut encoded = Vec::from(24_u32.to_be_bytes());
            encoded.extend_from_slice(b"ftyp");
            encoded.extend_from_slice(major_brand);
            encoded.extend_from_slice(&0_u32.to_be_bytes());
            encoded.extend_from_slice(compatible_brand);
            encoded.extend_from_slice(b"data");
            encoded
        }

        assert!(looks_like_heif(&ftyp(b"heic", b"mif1")));
        assert!(looks_like_heif(&ftyp(b"mif1", b"heix")));
        assert!(!looks_like_heif(&ftyp(b"avif", b"mif1")));

        let mut truncated = ftyp(b"heic", b"mif1");
        truncated[..4].copy_from_slice(&64_u32.to_be_bytes());
        assert!(!looks_like_heif(&truncated));
    }

    #[cfg(target_os = "macos")]
    #[test]
    fn heic_bytes_decode_with_imageio_in_display_orientation() {
        let encoded = include_bytes!("../tests-data/rgbgrid.heic");

        let image = load_rgba_image_from_memory(encoded, None).unwrap();

        assert_eq!((image.width(), image.height()), (216, 216));
        let top_left = image.pixel(0, 0).unwrap();
        let bottom_right = image.pixel(215, 215).unwrap();
        assert!(top_left.r < 30 && top_left.g < 30 && top_left.b < 30, "{top_left:?}");
        assert_eq!(top_left.a, 255);
        assert!(
            bottom_right.r > 220 && bottom_right.g > 220 && bottom_right.b > 220,
            "{bottom_right:?}"
        );
        assert_eq!(bottom_right.a, 255);
    }
}
