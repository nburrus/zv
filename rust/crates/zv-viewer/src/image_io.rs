use std::path::Path;

use anyhow::Context;

use crate::color_image::ImageSRGBA;

pub fn load_rgba_image(path: &Path) -> anyhow::Result<ImageSRGBA> {
    let dynamic =
        image::open(path).with_context(|| format!("failed to read image '{}'", path.display()))?;
    let rgba = dynamic.into_rgba8();
    let (width, height) = rgba.dimensions();
    Ok(ImageSRGBA::from_tightly_packed_bytes(
        width,
        height,
        rgba.as_raw(),
    ))
}
