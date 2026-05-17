use std::path::PathBuf;

use eframe::egui;

use crate::render::WgpuImageRenderer;
use crate::viewer::Viewer;

pub struct ZvApp {
    viewer: Viewer,
}

impl ZvApp {
    pub fn new(cc: &eframe::CreationContext<'_>, image_paths: Vec<PathBuf>) -> Self {
        if let Some(render_state) = &cc.wgpu_render_state {
            let mut renderer = render_state.renderer.write();
            renderer.callback_resources.insert(WgpuImageRenderer::new(
                &render_state.device,
                render_state.target_format,
            ));
        } else {
            tracing::warn!(
                "eframe did not provide a wgpu render state; image callbacks will not render"
            );
        }

        Self {
            viewer: Viewer::new(image_paths),
        }
    }
}

impl eframe::App for ZvApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.viewer.update(ctx);
    }
}
