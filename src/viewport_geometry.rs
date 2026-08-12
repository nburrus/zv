use eframe::egui;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ViewportGeometry {
    pub monitor_size: egui::Vec2,
    /// Usable desktop-space rectangle of the current monitor, when the
    /// platform can provide it. Includes the monitor origin.
    pub work_area: Option<egui::Rect>,
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
    /// Top-left of the usable outer-window area in desktop coordinates.
    pub(crate) origin: egui::Pos2,
    /// Largest inner-window size after subtracting decorations.
    pub(crate) size: egui::Vec2,
    pub(crate) outer_extra_size: egui::Vec2,
    has_known_origin: bool,
}

impl InnerArea {
    pub(crate) fn from_viewport(viewport: ViewportGeometry) -> Self {
        let outer_extra_size = match (viewport.outer_rect, viewport.inner_rect) {
            (Some(outer), Some(inner)) => (outer.size() - inner.size()).max(egui::Vec2::ZERO),
            _ => egui::Vec2::ZERO,
        };

        // Portable fallback when no native work-area rectangle is available:
        // egui tells us the current monitor's size, but not its desktop origin.
        // We may safely use a zero origin only when the window center is inside
        // that zero-based rectangle, identifying the primary monitor. On other
        // monitors we still use the reported size for resize limits, but mark
        // the origin unknown so centering/clamping preserves the current window
        // position instead of accidentally moving it to the primary display.
        let fallback_area = egui::Rect::from_min_size(egui::Pos2::ZERO, viewport.monitor_size);
        let fallback_is_primary_monitor = viewport
            .outer_rect
            .is_some_and(|outer| fallback_area.contains(outer.center()));
        let (outer_area, has_known_origin) = viewport
            .work_area
            .map_or((fallback_area, fallback_is_primary_monitor), |area| (area, true));
        let size = (outer_area.size() - outer_extra_size).max(egui::vec2(1.0, 1.0));
        Self {
            origin: outer_area.min,
            size,
            outer_extra_size,
            has_known_origin,
        }
    }

    pub(crate) fn outer_position_for_centered_inner_size(self, inner_size: egui::Vec2) -> Option<egui::Pos2> {
        if !self.has_known_origin {
            return None;
        }
        let outer_size = inner_size + self.outer_extra_size;
        Some(self.origin + (self.size + self.outer_extra_size - outer_size) * 0.5)
    }

    pub(crate) fn outer_position_for_initial_window(self, viewer_index: usize) -> egui::Pos2 {
        if self.has_known_origin {
            self.origin
                + egui::vec2(
                    (self.size.x + self.outer_extra_size.x) * (0.10 + 0.15 * viewer_index as f32),
                    (self.size.y + self.outer_extra_size.y) * 0.10,
                )
        } else {
            egui::pos2(
                (self.size.x + self.outer_extra_size.x) * (0.10 + 0.15 * viewer_index as f32),
                (self.size.y + self.outer_extra_size.y) * 0.10,
            )
        }
    }

    pub(crate) fn clamp_outer_position(self, position: egui::Pos2, inner_size: egui::Vec2) -> egui::Pos2 {
        if !self.has_known_origin {
            return position;
        }
        let outer_size = inner_size + self.outer_extra_size;
        let max = self.origin + self.size + self.outer_extra_size - outer_size;
        egui::pos2(
            position.x.clamp(self.origin.x, max.x.max(self.origin.x)),
            position.y.clamp(self.origin.y, max.y.max(self.origin.y)),
        )
    }
}
