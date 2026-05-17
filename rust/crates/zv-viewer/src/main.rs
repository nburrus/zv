mod actions;
mod app;
mod controls_window;
mod debug;
mod geometry;
mod image;
mod image_window;
mod render;
mod shortcuts;
mod viewer;

use std::path::PathBuf;
use std::time::Instant;

use clap::Parser;

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
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "info,zv_viewer=debug".into()),
        )
        .init();

    let cli = Cli::parse();
    let options = eframe::NativeOptions {
        renderer: eframe::Renderer::Wgpu,
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
