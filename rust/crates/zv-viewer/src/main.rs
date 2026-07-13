mod annotation_tool;
mod annotations;
mod app;
mod color_editor;
mod color_image;
mod controls_window;
mod debug;
mod image_io;
mod image_item_data;
mod image_list;
mod image_window;
mod image_window_geometry;
mod layout;
mod modified_image;
mod render;
mod shortcuts;
mod viewer;
mod viewport_geometry;

use std::path::PathBuf;
use std::time::Instant;

use clap::Parser;
use eframe::egui;

#[derive(Debug, Parser)]
#[command(name = "zv-viewer", about = "Rust ZV viewer prototype")]
struct Cli {
    #[arg(value_name = "IMAGE")]
    images: Vec<PathBuf>,

    #[arg(long, value_name = "JSON")]
    debug_script_json: Option<PathBuf>,

    #[arg(long, value_name = "DIR")]
    debug_artifact_dir: Option<PathBuf>,

    #[arg(long, value_name = "FRAMES")]
    debug_wait_frames: Option<u64>,
}

fn main() -> anyhow::Result<()> {
    let launched_at = Instant::now();
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| "info,zv_viewer=debug".into()),
        )
        .init();

    let cli = Cli::parse();
    let initial_viewport = initial_root_viewport(&cli.images);
    let options = eframe::NativeOptions {
        renderer: eframe::Renderer::Wgpu,
        viewport: initial_viewport,
        ..Default::default()
    };

    eframe::run_native(
        "ZV Rust Viewer",
        options,
        Box::new(move |cc| {
            Ok(Box::new(app::ZvApp::new(
                cc,
                cli.images.clone(),
                launched_at,
                debug::DebugConfig::new(
                    cli.debug_script_json.clone(),
                    cli.debug_artifact_dir.clone(),
                    cli.debug_wait_frames,
                ),
            )))
        }),
    )
    .map_err(|err| anyhow::anyhow!("failed to run native viewer: {err}"))
}

fn initial_root_viewport(images: &[PathBuf]) -> egui::ViewportBuilder {
    // Pre-size the root viewport before the first frame to avoid the
    // visible default-size flash followed by a resize in Viewer::update.
    let initial_size = images
        .first()
        .and_then(|path| ::image::image_dimensions(path).ok())
        .map(|(w, h)| egui::vec2(w as f32, h as f32))
        .unwrap_or_else(|| egui::vec2(256.0, 256.0));
    egui::ViewportBuilder::default().with_inner_size(initial_size)
}
