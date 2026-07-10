use std::path::PathBuf;
use std::time::Instant;

use eframe::egui;

use crate::annotations::AnnotationRenderer;
use crate::debug::{DebugConfig, RuntimeDebug};
use crate::render::WgpuImageRenderer;
use crate::viewer::Viewer;

pub struct ZvApp {
    viewer: Viewer,
    runtime_debug: Option<RuntimeDebug>,
    launched_at: Instant,
    logged_first_frame: bool,
}

impl ZvApp {
    pub fn new(
        cc: &eframe::CreationContext<'_>,
        image_paths: Vec<PathBuf>,
        launched_at: Instant,
        debug_config: DebugConfig,
    ) -> Self {
        tracing::info!("creating zv-viewer app");
        let mut fonts = egui::FontDefinitions::default();
        egui_phosphor::add_to_fonts(&mut fonts, egui_phosphor::Variant::Regular);
        cc.egui_ctx.set_fonts(fonts);

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

        Self {
            viewer: Viewer::new(image_paths),
            runtime_debug,
            launched_at,
            logged_first_frame: false,
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
        let state = self.viewer.update(ctx, frame.wgpu_render_state());
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
