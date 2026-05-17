use std::path::Path;

use anyhow::Context;

use crate::image::RgbaImage;

pub fn load_rgba_image(path: &Path) -> anyhow::Result<RgbaImage> {
    let dynamic =
        image::open(path).with_context(|| format!("failed to read image '{}'", path.display()))?;
    let rgba = dynamic.into_rgba8();
    let (width, height) = rgba.dimensions();
    Ok(RgbaImage::from_tightly_packed_rgba(
        width,
        height,
        rgba.as_raw(),
    ))
}
