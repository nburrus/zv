mod actions;
mod app;
mod controls_window;
mod geometry;
mod image;
mod image_window;
mod render;
mod viewer;

use std::path::PathBuf;

use clap::Parser;

#[derive(Debug, Parser)]
#[command(name = "zv-viewer", about = "Rust ZV viewer prototype")]
struct Cli {
    #[arg(value_name = "IMAGE")]
    images: Vec<PathBuf>,
}

fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "zv_viewer=info".into()),
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
        Box::new(move |cc| Ok(Box::new(app::ZvApp::new(cc, cli.images.clone())))),
    )
    .map_err(|err| anyhow::anyhow!("failed to run native viewer: {err}"))
}
