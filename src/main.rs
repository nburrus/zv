mod annotation_tool;
mod annotations;
mod app;
mod clipboard;
mod color_editor;
mod color_editor_ui;
mod color_image;
mod controls_window;
mod debug;
mod image_io;
mod image_item_data;
mod image_list;
mod image_view;
mod image_window;
mod image_window_geometry;
mod layout;
mod minimap;
mod modified_image;
mod networking;
mod platform_window;
mod protocol;
mod render;
mod shortcuts;
mod viewer;
mod viewport_geometry;

use std::path::PathBuf;
use std::time::Instant;

use clap::Parser;
use eframe::egui;

#[derive(Debug, Parser)]
#[command(name = "zv", version, about = "Lightweight image viewer for computer vision")]
struct Cli {
    #[arg(value_name = "IMAGE")]
    images: Vec<PathBuf>,

    /// Listen for remote Rust ZV clients and open one viewer process per client.
    #[arg(long, conflicts_with_all = ["client", "server_session"])]
    server: bool,

    /// Send image paths to a Rust ZV server.
    #[arg(long, conflicts_with_all = ["server", "server_session"])]
    client: bool,

    #[arg(long, default_value = "127.0.0.1")]
    host: String,

    #[arg(long, default_value_t = 4207)]
    port: u16,

    /// Internal one-client viewer mode used by the server supervisor.
    #[arg(long, hide = true, conflicts_with_all = ["server", "client"])]
    server_session: bool,

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
        .with_writer(std::io::stderr)
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| "info,zv=debug".into()),
        )
        .init();

    let cli = Cli::parse();
    if cli.server {
        if !cli.images.is_empty() {
            anyhow::bail!("--server does not accept image paths");
        }
        return networking::run_supervisor(&cli.host, cli.port);
    }
    if cli.client {
        return networking::run_client(&cli.host, cli.port, cli.images);
    }
    if cli.server_session {
        if !cli.images.is_empty() {
            anyhow::bail!("--server-session does not accept image paths");
        }
        let listener = networking::bind_server_session(&cli.host, cli.port)?;
        networking::announce_server_session(&listener)?;
        return run_viewer(cli, launched_at, Some(listener));
    }
    run_viewer(cli, launched_at, None)
}

fn run_viewer(
    cli: Cli,
    launched_at: Instant,
    server_session_listener: Option<std::net::TcpListener>,
) -> anyhow::Result<()> {
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
                server_session_listener,
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
