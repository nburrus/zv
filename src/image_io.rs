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
    let dynamic =
        image::load_from_memory(encoded).context("image format was not recognized or the encoded data is invalid")?;
    let rgba = dynamic.into_rgba8();
    let (width, height) = rgba.dimensions();
    Ok(ImageSRGBA::from_tightly_packed_bytes(width, height, rgba.as_raw()))
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
}
