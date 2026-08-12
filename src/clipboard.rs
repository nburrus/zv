use anyhow::Context;

use crate::color_image::ImageSRGBA;

pub fn copy_image(ctx: &egui::Context, image: &ImageSRGBA) {
    ctx.copy_image(image.to_egui_color_image());
}

pub fn read_image() -> anyhow::Result<ImageSRGBA> {
    let mut clipboard = arboard::Clipboard::new().context("failed to access the system clipboard")?;
    let image = clipboard
        .get_image()
        .context("clipboard does not contain a readable image")?;
    ImageSRGBA::try_from_tightly_packed_bytes(image.width, image.height, &image.bytes)
}
