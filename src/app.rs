use std::net::TcpListener;
use std::path::PathBuf;
use std::sync::mpsc::{Receiver, TryRecvError};
use std::time::Instant;

use eframe::egui;

use crate::annotations::{AnnotationRenderer, shared_font_definitions};
use crate::debug::{DebugConfig, RuntimeDebug};
use crate::networking::ServerSessionEvent;
use crate::platform_window::NativeWorkAreaCache;
use crate::render::WgpuImageRenderer;
use crate::viewer::Viewer;

pub struct ZvApp {
    viewer: Viewer,
    runtime_debug: Option<RuntimeDebug>,
    launched_at: Instant,
    logged_first_frame: bool,
    native_work_area: NativeWorkAreaCache,
    server_session_events: Option<Receiver<ServerSessionEvent>>,
}

impl ZvApp {
    pub fn new(
        cc: &eframe::CreationContext<'_>,
        image_paths: Vec<PathBuf>,
        launched_at: Instant,
        debug_config: DebugConfig,
        server_session_listener: Option<TcpListener>,
    ) -> Self {
        tracing::info!("creating zv app");
        cc.egui_ctx.set_fonts(shared_font_definitions());

        if let Some(render_state) = &cc.wgpu_render_state {
            let mut renderer = render_state.renderer.write();
            renderer
                .callback_resources
                .insert(WgpuImageRenderer::new(&render_state.device, render_state.target_format));
            renderer
                .callback_resources
                .insert(AnnotationRenderer::new(&render_state.device));
        } else {
            tracing::warn!("eframe did not provide a wgpu render state; image callbacks will not render");
        }

        let runtime_debug = debug_config.into_runtime();
        if runtime_debug.is_some() {
            tracing::info!("runtime debug script enabled");
        }
        let server_session_events = server_session_listener.map(|listener| {
            let ctx = cc.egui_ctx.clone();
            crate::networking::spawn_server_session(listener, move || {
                ctx.request_repaint_of(egui::ViewportId::ROOT);
            })
        });

        Self {
            viewer: Viewer::new(image_paths),
            runtime_debug,
            launched_at,
            logged_first_frame: false,
            native_work_area: NativeWorkAreaCache::default(),
            server_session_events,
        }
    }

    fn poll_server_session(&mut self, ctx: &egui::Context) {
        if self.server_session_events.is_none() {
            return;
        }
        loop {
            let event = self.server_session_events.as_ref().expect("checked above").try_recv();
            match event {
                Ok(ServerSessionEvent::Connected { capabilities }) => {
                    tracing::info!(capabilities, "remote client connected");
                }
                Ok(ServerSessionEvent::ImageOffered { offer, remote }) => {
                    tracing::info!(remote_id = offer.id, name = %offer.name, "remote image offered");
                    self.viewer.add_remote_image(offer, remote);
                    ctx.request_repaint_of(egui::ViewportId::ROOT);
                }
                Ok(ServerSessionEvent::Disconnected { reason }) => {
                    tracing::info!(%reason, "remote client disconnected");
                    self.server_session_events = None;
                    return;
                }
                Err(TryRecvError::Empty) => {
                    return;
                }
                Err(TryRecvError::Disconnected) => {
                    self.server_session_events = None;
                    return;
                }
            }
        }
    }
}

impl eframe::App for ZvApp {
    fn raw_input_hook(&mut self, _ctx: &egui::Context, raw_input: &mut egui::RawInput) {
        if let Some(runtime_debug) = &mut self.runtime_debug {
            tracing::trace!(
                viewport = ?raw_input.viewport_id,
                events = raw_input.events.len(),
                "runtime debug raw input hook"
            );
            runtime_debug.raw_input_hook(raw_input);
        }
    }

    fn update(&mut self, ctx: &egui::Context, frame: &mut eframe::Frame) {
        self.poll_server_session(ctx);
        let work_area = self.native_work_area.update(frame, ctx);
        let state = self.viewer.update(ctx, frame.wgpu_render_state(), work_area);
        if let Some(runtime_debug) = &mut self.runtime_debug {
            runtime_debug.update(ctx, &state, &mut self.viewer);
        }
        if !self.logged_first_frame {
            self.logged_first_frame = true;
            tracing::info!(
                elapsed_ms = self.launched_at.elapsed().as_millis(),
                "first frame rendered"
            );
        }
    }
}
