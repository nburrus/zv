use eframe::egui;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ViewportGeometry {
    pub monitor_size: egui::Vec2,
    pub outer_rect: Option<egui::Rect>, // window size, including decorations
    pub inner_rect: Option<egui::Rect>, // window size, only the content rendered by us
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ViewportResizeCommand {
    pub outer_position: Option<egui::Pos2>,
    pub inner_size: Option<egui::Vec2>,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct InnerArea {
    pub(crate) origin: egui::Pos2,
    pub(crate) size: egui::Vec2,
    pub(crate) outer_extra_size: egui::Vec2,
}

impl InnerArea {
    pub(crate) fn from_viewport(viewport: ViewportGeometry) -> Self {
        // Approximate the maximum drawable image area from the monitor size and
        // observed window decoration size. Menu bars/docks are not represented
        // here; clamped maxspect feedback corrects platforms that enforce a
        // smaller usable work area.
        let outer_extra_size = match (viewport.outer_rect, viewport.inner_rect) {
            (Some(outer), Some(inner)) => (outer.size() - inner.size()).max(egui::Vec2::ZERO),
            _ => egui::Vec2::ZERO,
        };
        let inner_offset = match (viewport.outer_rect, viewport.inner_rect) {
            (Some(outer), Some(inner)) => inner.min - outer.min,
            _ => egui::Vec2::ZERO,
        };
        let size = (viewport.monitor_size - outer_extra_size).max(egui::vec2(1.0, 1.0));
        let origin = egui::pos2(inner_offset.x, inner_offset.y);
        Self {
            origin,
            size,
            outer_extra_size,
        }
    }

    pub(crate) fn outer_position_for_centered_inner_size(self, inner_size: egui::Vec2) -> egui::Pos2 {
        // self.origin is the inner-to-outer offset (e.g. title bar height).
        // Deliberately unused here: we're positioning the outer window, so
        // centering uses outer dimensions against the full monitor extent
        // (self.size + outer_extra_size).
        let outer_size = inner_size + self.outer_extra_size;
        egui::pos2(
            (self.size.x + self.outer_extra_size.x - outer_size.x) * 0.5,
            (self.size.y + self.outer_extra_size.y - outer_size.y) * 0.5,
        )
    }
}
