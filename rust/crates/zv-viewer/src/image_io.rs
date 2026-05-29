use std::path::Path;

use anyhow::Context;

use crate::color_image::ImageSRGBA;

pub fn load_rgba_image(path: &Path) -> anyhow::Result<ImageSRGBA> {
    let dynamic = image::open(path).with_context(|| format!("failed to read image '{}'", path.display()))?;
    let rgba = dynamic.into_rgba8();
    let (width, height) = rgba.dimensions();
    Ok(ImageSRGBA::from_tightly_packed_bytes(width, height, rgba.as_raw()))
}

pub fn write_rgba_image(path: &Path, image: &ImageSRGBA) -> anyhow::Result<()> {
    let tight_bytes_per_row = image.width() as usize * 4;
    let mut tight = vec![0; tight_bytes_per_row * image.height() as usize];
    for row in 0..image.height() as usize {
        let src = image
            .row_bytes(row as u32)
            .ok_or_else(|| anyhow::anyhow!("missing row {row} while writing image"))?;
        let dst_start = row * tight_bytes_per_row;
        tight[dst_start..dst_start + tight_bytes_per_row].copy_from_slice(&src[..tight_bytes_per_row]);
    }
    ::image::save_buffer(path, &tight, image.width(), image.height(), ::image::ColorType::Rgba8)
        .with_context(|| format!("failed to write image '{}'", path.display()))
}
