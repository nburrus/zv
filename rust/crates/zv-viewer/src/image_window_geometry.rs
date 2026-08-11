use eframe::egui;

use crate::viewport_geometry::{InnerArea, ViewportGeometry, ViewportResizeCommand};

#[derive(Clone, Copy, Debug, PartialEq)]
struct WindowGeometry {
    origin: egui::Pos2,
    size: egui::Vec2,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum WindowResizeAction {
    Normal,
    RestoreAspectRatio,
    Maxspect,
    Double,
    Half,
    Increase10Percent,
    Decrease10Percent,
    Custom { width: u32, height: u32, lock_ratio: bool },
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum WindowGeometryMode {
    UserDefined,
    Normal,
    AspectRatio,
    ScaleSpect,
    Maxspect,
}

#[derive(Debug)]
pub struct ImageWindowGeometryState {
    initial_geometry_applied: bool,
    normal_size: Option<egui::Vec2>,
    aspect_ratio_source_size: Option<egui::Vec2>,
    last_observed_inner_size: Option<egui::Vec2>,
    last_requested_inner_size: Option<egui::Vec2>,
    last_requested_action: Option<WindowResizeAction>,
    // eframe/winit expose full monitor size, but not a portable work area.
    // Even GLFW cannot always report work area on Wayland, so keep this
    // fallback even if some platforms later get native work-area queries.
    // When the OS clamps a maxspect request, infer per-axis platform limits
    // from the granted size and use them until the display is invalidated.
    platform_max_inner_size: Option<egui::Vec2>,
    last_work_area: Option<egui::Rect>,
    last_monitor_size: Option<egui::Vec2>,
    last_geometry_mode: WindowGeometryMode,
}

impl Default for ImageWindowGeometryState {
    fn default() -> Self {
        Self {
            initial_geometry_applied: false,
            normal_size: None,
            aspect_ratio_source_size: None,
            last_observed_inner_size: None,
            last_requested_inner_size: None,
            last_requested_action: None,
            platform_max_inner_size: None,
            last_work_area: None,
            last_monitor_size: None,
            last_geometry_mode: WindowGeometryMode::Normal,
        }
    }
}

impl ImageWindowGeometryState {
    pub fn prepare_initial_geometry(
        &mut self,
        image_size: egui::Vec2,
        viewport: ViewportGeometry,
        viewer_index: usize,
    ) -> Option<ViewportResizeCommand> {
        self.normal_size = Some(image_size);
        if self.initial_geometry_applied {
            return None;
        }

        let (geometry, clamped_to_screen) =
            initial_image_window_geometry_for_viewport(image_size, viewport, viewer_index);
        self.initial_geometry_applied = true;
        if clamped_to_screen {
            self.last_geometry_mode = WindowGeometryMode::Maxspect;
            self.record_programmatic_resize(geometry.size, Some(WindowResizeAction::Maxspect));
        } else {
            self.last_geometry_mode = WindowGeometryMode::Normal;
            self.record_programmatic_resize(geometry.size, None);
        }
        Some(ViewportResizeCommand {
            outer_position: Some(geometry.origin),
            inner_size: Some(geometry.size),
        })
    }

    /// Called when the displayed image changes. Updates `normal_size` to the
    /// new image and, if the user hasn't manually resized the window (i.e.
    /// last mode is Normal/AspectRatio/Maxspect), re-applies the same mode so
    /// the window snaps to the new image's natural size. ScaleSpect (< >) and
    /// UserDefined leave the window alone — the user took control.
    pub fn on_image_changed(
        &mut self,
        image_size: egui::Vec2,
        viewport: ViewportGeometry,
    ) -> Option<ViewportResizeCommand> {
        self.normal_size = Some(image_size);
        let action = match self.last_geometry_mode {
            WindowGeometryMode::Normal => WindowResizeAction::Normal,
            WindowGeometryMode::AspectRatio => WindowResizeAction::RestoreAspectRatio,
            WindowGeometryMode::Maxspect => WindowResizeAction::Maxspect,
            WindowGeometryMode::ScaleSpect | WindowGeometryMode::UserDefined => return None,
        };
        self.apply_resize_action(viewport, action)
    }

    pub fn apply_resize_action(
        &mut self,
        viewport: ViewportGeometry,
        action: WindowResizeAction,
    ) -> Option<ViewportResizeCommand> {
        let normal_size = self.normal_size?;
        let current_size = viewport.inner_rect.map(|rect| rect.size()).unwrap_or(normal_size);
        self.observe_display(viewport);
        self.observe_current_inner_size(current_size, InnerArea::from_viewport(viewport));
        let inner_area = InnerArea::from_viewport(viewport);

        let mut target_origin = viewport.outer_rect.map(|rect| rect.min);

        let target_size = match action {
            WindowResizeAction::Normal => {
                let max_inner_size = self.max_inner_size(inner_area);
                let target_size = aspect_fit_size(normal_size, normal_size, max_inner_size, true);
                if target_size.x < normal_size.x || target_size.y < normal_size.y {
                    target_origin = inner_area
                        .outer_position_for_centered_inner_size(target_size)
                        .or(target_origin);
                }
                self.last_geometry_mode = WindowGeometryMode::Normal;
                self.platform_max_inner_size = None;
                target_size
            }
            WindowResizeAction::RestoreAspectRatio => {
                // No platform_max_inner_size clamp: aspect_ratio_adjusted_size
                // only shrinks the smaller dimension of current_size, never
                // grows either dimension, so it cannot exceed an already-valid
                // window size.
                let target_size = self.aspect_ratio_adjusted_size(current_size, normal_size);
                self.last_geometry_mode = WindowGeometryMode::AspectRatio;
                target_size
            }
            WindowResizeAction::Double => {
                let target_size = current_size * 2.0;
                let max_inner_size = self.max_inner_size(inner_area);
                if target_size.x > max_inner_size.x || target_size.y > max_inner_size.y {
                    return None;
                }
                target_origin = move_window_if_needed(target_origin, target_size, inner_area);
                self.mark_scale_spect_if_not_user_defined();
                target_size
            }
            WindowResizeAction::Half => {
                if current_size.x <= 96.0 || current_size.y <= 96.0 {
                    return None;
                }
                let target_size = current_size * 0.5;
                self.mark_scale_spect_if_not_user_defined();
                target_size
            }
            WindowResizeAction::Increase10Percent => {
                let target_size = current_size * 1.1;
                let max_inner_size = self.max_inner_size(inner_area);
                if target_size.x > max_inner_size.x || target_size.y > max_inner_size.y {
                    return None;
                }
                target_origin = move_window_if_needed(target_origin, target_size, inner_area);
                self.mark_scale_spect_if_not_user_defined();
                target_size
            }
            WindowResizeAction::Decrease10Percent => {
                if current_size.x <= 64.0 || current_size.y <= 64.0 {
                    return None;
                }
                let target_size = current_size * 0.9;
                self.mark_scale_spect_if_not_user_defined();
                target_size
            }
            WindowResizeAction::Maxspect => {
                let max_inner_size = self.max_inner_size(inner_area);
                let target_size = aspect_fit_size(max_inner_size * 2.0, normal_size, max_inner_size, true);
                target_origin = inner_area
                    .outer_position_for_centered_inner_size(target_size)
                    .or(target_origin);
                self.last_geometry_mode = WindowGeometryMode::Maxspect;
                target_size
            }
            WindowResizeAction::Custom {
                width,
                height,
                lock_ratio,
            } => {
                let requested = egui::vec2(width.max(1) as f32, height.max(1) as f32);
                let target_size = if lock_ratio {
                    aspect_fit_size(requested, normal_size, requested, false)
                } else {
                    requested
                };
                target_origin = move_window_if_needed(target_origin, target_size, inner_area);
                self.last_geometry_mode = WindowGeometryMode::UserDefined;
                target_size
            }
        };

        self.record_programmatic_resize(target_size, Some(action));
        Some(ViewportResizeCommand {
            outer_position: target_origin,
            inner_size: Some(target_size),
        })
    }

    pub fn observe_viewport(&mut self, viewport: ViewportGeometry) -> Option<ViewportResizeCommand> {
        let current_size = viewport.inner_rect.map(|rect| rect.size())?;
        self.observe_display(viewport);
        self.observe_current_inner_size(current_size, InnerArea::from_viewport(viewport))
    }

    fn observe_display(&mut self, viewport: ViewportGeometry) {
        let display_changed = match (self.last_work_area, viewport.work_area) {
            (Some(previous), Some(current)) => previous != current,
            // Without native monitor identity/origin, a size change is the only
            // portable signal that the window moved to a different display.
            // Equal-sized displays remain indistinguishable in this fallback.
            (None, None) => self
                .last_monitor_size
                .is_some_and(|previous| !sizes_nearly_equal(previous, viewport.monitor_size)),
            (None, Some(_)) => self
                .last_monitor_size
                .is_some_and(|previous| !sizes_nearly_equal(previous, viewport.monitor_size)),
            // A native work-area query can be temporarily unavailable while a
            // window is moving or off-screen. Absence is not evidence that the
            // display changed, so retain the last known area and in-flight state.
            (Some(_), None) => false,
        };
        if display_changed {
            self.platform_max_inner_size = None;
            self.last_requested_inner_size = None;
            self.last_requested_action = None;
        }
        if viewport.work_area.is_some() {
            self.last_work_area = viewport.work_area;
        }
        self.last_monitor_size = Some(viewport.monitor_size);
    }

    fn record_programmatic_resize(&mut self, inner_size: egui::Vec2, action: Option<WindowResizeAction>) {
        self.last_requested_inner_size = Some(inner_size);
        self.last_requested_action = action;
    }

    fn observe_current_inner_size(
        &mut self,
        current_size: egui::Vec2,
        inner_area: InnerArea,
    ) -> Option<ViewportResizeCommand> {
        let previous_observed_size = self.last_observed_inner_size.replace(current_size);
        let Some(requested_size) = self.last_requested_inner_size else {
            if previous_observed_size.is_some_and(|previous| !sizes_nearly_equal(previous, current_size)) {
                self.mark_user_defined_resize();
            }
            return None;
        };

        if sizes_nearly_equal(current_size, requested_size) {
            self.last_requested_inner_size = None;
            self.last_requested_action = None;
        } else {
            // This is the one deliberately heuristic path. C++ computes maxspect
            // from GLFW's monitor work area where available, but work-area data is
            // not portable, notably on Wayland. If a programmatic aspect-preserving
            // resize comes back smaller, treat it as OS clamp feedback rather than
            // a user resize.
            if matches!(
                self.last_requested_action,
                Some(WindowResizeAction::Normal | WindowResizeAction::Maxspect)
            ) && current_size.x <= requested_size.x + 1.0
                && current_size.y <= requested_size.y + 1.0
            {
                // A granted aspect-fitted window size is not itself a work-area
                // size. Preserve the full limit on axes the OS did not clamp;
                // otherwise changing to a wider/taller image layout would reuse
                // an artificially small maximum from the previous aspect ratio.
                self.platform_max_inner_size = Some(egui::vec2(
                    if current_size.x + 1.0 < requested_size.x {
                        current_size.x
                    } else {
                        inner_area.size.x
                    },
                    if current_size.y + 1.0 < requested_size.y {
                        current_size.y
                    } else {
                        inner_area.size.y
                    },
                ));
                return self.correct_clamped_aspect_resize(current_size, inner_area);
            }
            self.mark_user_defined_resize();
        }
        None
    }

    fn mark_user_defined_resize(&mut self) {
        self.platform_max_inner_size = None;
        self.aspect_ratio_source_size = None;
        self.last_geometry_mode = WindowGeometryMode::UserDefined;
        self.last_requested_inner_size = None;
        self.last_requested_action = None;
    }

    fn correct_clamped_aspect_resize(
        &mut self,
        granted_size: egui::Vec2,
        inner_area: InnerArea,
    ) -> Option<ViewportResizeCommand> {
        let normal_size = self.normal_size?;
        let target_size = aspect_fit_size(granted_size * 2.0, normal_size, granted_size, true);
        if sizes_nearly_equal(target_size, granted_size) {
            self.last_requested_inner_size = None;
            self.last_requested_action = None;
        } else {
            self.record_programmatic_resize(target_size, self.last_requested_action);
        }
        self.last_geometry_mode = match self.last_requested_action {
            Some(WindowResizeAction::Normal) => WindowGeometryMode::Normal,
            Some(WindowResizeAction::Maxspect) => WindowGeometryMode::Maxspect,
            Some(WindowResizeAction::Custom { .. }) => WindowGeometryMode::UserDefined,
            _ => self.last_geometry_mode,
        };
        self.aspect_ratio_source_size = None;
        Some(ViewportResizeCommand {
            outer_position: inner_area.outer_position_for_centered_inner_size(target_size),
            inner_size: Some(target_size),
        })
    }

    fn aspect_ratio_adjusted_size(&mut self, current_size: egui::Vec2, normal_size: egui::Vec2) -> egui::Vec2 {
        let source_size = if self.last_geometry_mode == WindowGeometryMode::AspectRatio {
            self.aspect_ratio_source_size.unwrap_or(current_size)
        } else {
            self.aspect_ratio_source_size = Some(current_size);
            current_size
        };

        let ratio_x = source_size.x / normal_size.x;
        let ratio_y = source_size.y / normal_size.y;
        if ratio_x <= ratio_y {
            egui::vec2(source_size.x, (ratio_x * normal_size.y + 0.5).floor())
        } else {
            egui::vec2((ratio_y * normal_size.x + 0.5).floor(), source_size.y)
        }
    }

    fn mark_scale_spect_if_not_user_defined(&mut self) {
        if self.last_geometry_mode != WindowGeometryMode::UserDefined {
            self.last_geometry_mode = WindowGeometryMode::ScaleSpect;
        }
    }

    fn max_inner_size(&self, inner_area: InnerArea) -> egui::Vec2 {
        // Native work areas normally make `inner_area.size` exact. The cached
        // limit is the portable fallback learned when the window manager grants
        // less than a Normal/Maxspect request (for example because of a taskbar
        // or desktop panel). Clamp each axis independently so an aspect-fitted
        // size from one image layout does not constrain a different layout.
        if let Some(platform_max) = self.platform_max_inner_size {
            egui::vec2(
                inner_area.size.x.min(platform_max.x),
                inner_area.size.y.min(platform_max.y),
            )
        } else {
            inner_area.size
        }
    }
}

fn initial_image_window_geometry_for_viewport(
    image_size: egui::Vec2,
    viewport: ViewportGeometry,
    viewer_index: usize,
) -> (WindowGeometry, bool) {
    let inner_area = InnerArea::from_viewport(viewport);
    let max_inner_size = inner_area.size;
    let clamped_to_screen = image_size.x > max_inner_size.x || image_size.y > max_inner_size.y;
    let mut geometry = WindowGeometry {
        origin: inner_area.outer_position_for_initial_window(viewer_index),
        size: aspect_fit_size(image_size, image_size, max_inner_size, true),
    };

    if clamped_to_screen && let Some(origin) = inner_area.outer_position_for_centered_inner_size(geometry.size) {
        geometry.origin = origin;
    }

    (geometry, clamped_to_screen)
}

fn sizes_nearly_equal(a: egui::Vec2, b: egui::Vec2) -> bool {
    (a.x - b.x).abs() <= 1.0 && (a.y - b.y).abs() <= 1.0
}

fn aspect_fit_size(
    mut size: egui::Vec2,
    normal_size: egui::Vec2,
    monitor_size: egui::Vec2,
    keep_aspect_ratio: bool,
) -> egui::Vec2 {
    if size.x > monitor_size.x {
        size.x = monitor_size.x;
        if keep_aspect_ratio {
            let sx = monitor_size.x / normal_size.x;
            size.y = (normal_size.y * sx + 0.5).floor();
        }
    }

    if size.y > monitor_size.y {
        size.y = monitor_size.y;
        if keep_aspect_ratio {
            let sy = monitor_size.y / normal_size.y;
            size.x = (normal_size.x * sy + 0.5).floor();
        }
    }
    size
}

fn move_window_if_needed(
    origin: Option<egui::Pos2>,
    target_size: egui::Vec2,
    inner_area: InnerArea,
) -> Option<egui::Pos2> {
    origin.map(|origin| inner_area.clamp_outer_position(origin, target_size))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn initial_geometry_preserves_aspect_for_large_image_with_window_decorations() {
        let mut state = ImageWindowGeometryState::default();
        let command = state
            .prepare_initial_geometry(
                egui::vec2(3264.0, 2448.0),
                decorated_viewport(
                    egui::vec2(1000.0, 800.0),
                    egui::pos2(100.0, 100.0),
                    egui::vec2(640.0, 480.0),
                    28.0,
                ),
                0,
            )
            .expect("initial resize command should apply");

        assert_size_near(command.inner_size.unwrap(), egui::vec2(1000.0, 750.0));
    }

    #[test]
    fn initial_large_image_uses_maxspect_clamp_feedback() {
        let mut state = ImageWindowGeometryState::default();
        let command = state
            .prepare_initial_geometry(
                egui::vec2(1024.0, 1024.0),
                decorated_viewport(
                    egui::vec2(1000.0, 1000.0),
                    egui::pos2(100.0, 100.0),
                    egui::vec2(640.0, 480.0),
                    0.0,
                ),
                0,
            )
            .expect("initial resize command should apply");
        assert_size_near(command.inner_size.unwrap(), egui::vec2(1000.0, 1000.0));

        let correction = state
            .observe_viewport(decorated_viewport(
                egui::vec2(1000.0, 1000.0),
                egui::pos2(0.0, 0.0),
                egui::vec2(1000.0, 930.0),
                0.0,
            ))
            .expect("initial clamped maxspect should be corrected");
        assert_size_near(correction.inner_size.unwrap(), egui::vec2(930.0, 930.0));
    }

    #[test]
    fn aspect_and_maxspect_commands_are_stable_with_window_decorations() {
        let monitor_size = egui::vec2(1000.0, 800.0);
        let image_size = egui::vec2(400.0, 300.0);
        let decoration_top = 28.0;
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(image_size);

        let mut inner_size = egui::vec2(613.0, 511.0);
        let mut outer_position = egui::pos2(100.0, 100.0);

        let apply = |state: &mut ImageWindowGeometryState,
                     outer_position: &mut egui::Pos2,
                     inner_size: &mut egui::Vec2,
                     action| {
            let command = state
                .apply_resize_action(
                    decorated_viewport(monitor_size, *outer_position, *inner_size, decoration_top),
                    action,
                )
                .expect("resize command should apply");
            if let Some(next_position) = command.outer_position {
                *outer_position = next_position;
            }
            if let Some(next_size) = command.inner_size {
                *inner_size = next_size;
            }
        };

        apply(
            &mut state,
            &mut outer_position,
            &mut inner_size,
            WindowResizeAction::RestoreAspectRatio,
        );
        let aspect_size = inner_size;
        apply(
            &mut state,
            &mut outer_position,
            &mut inner_size,
            WindowResizeAction::Maxspect,
        );
        let maxspect_size = inner_size;
        apply(
            &mut state,
            &mut outer_position,
            &mut inner_size,
            WindowResizeAction::RestoreAspectRatio,
        );
        assert_size_near(inner_size, maxspect_size);
        apply(
            &mut state,
            &mut outer_position,
            &mut inner_size,
            WindowResizeAction::Maxspect,
        );
        assert_size_near(inner_size, maxspect_size);

        assert!(maxspect_size.x >= aspect_size.x);
        assert!(maxspect_size.y >= aspect_size.y);
        assert_size_near(maxspect_size, egui::vec2(1000.0, 750.0));
    }

    #[test]
    fn normal_preserves_aspect_for_image_larger_than_monitor() {
        let monitor_size = egui::vec2(1000.0, 800.0);
        let image_size = egui::vec2(3264.0, 2448.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(image_size);

        let normal = state
            .apply_resize_action(
                decorated_viewport(monitor_size, egui::pos2(100.0, 100.0), egui::vec2(600.0, 500.0), 28.0),
                WindowResizeAction::Normal,
            )
            .expect("normal should apply");

        assert_size_near(normal.inner_size.unwrap(), egui::vec2(1000.0, 750.0));
    }

    #[test]
    fn aspect_mode_image_switching_reuses_original_aspect_source_size() {
        let monitor_size = egui::vec2(2000.0, 2000.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(egui::vec2(400.0, 300.0));

        let viewport = decorated_viewport(monitor_size, egui::pos2(100.0, 100.0), egui::vec2(600.0, 500.0), 0.0);
        let aspect = state
            .apply_resize_action(viewport, WindowResizeAction::RestoreAspectRatio)
            .expect("aspect should apply");
        assert_size_near(aspect.inner_size.unwrap(), egui::vec2(600.0, 450.0));

        let mut current_size = aspect.inner_size.unwrap();
        for image_size in [
            egui::vec2(300.0, 300.0),
            egui::vec2(400.0, 300.0),
            egui::vec2(300.0, 300.0),
            egui::vec2(400.0, 300.0),
        ] {
            let command = state
                .on_image_changed(
                    image_size,
                    decorated_viewport(monitor_size, egui::pos2(100.0, 100.0), current_size, 0.0),
                )
                .expect("aspect image change should resize");
            current_size = command.inner_size.unwrap();
        }

        assert_size_near(current_size, egui::vec2(600.0, 450.0));
    }

    #[test]
    fn clamped_normal_is_corrected_when_viewport_feedback_arrives() {
        let monitor_size = egui::vec2(1000.0, 1000.0);
        let image_size = egui::vec2(1024.0, 1024.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(image_size);

        let normal = state
            .apply_resize_action(
                decorated_viewport(monitor_size, egui::pos2(100.0, 100.0), egui::vec2(600.0, 500.0), 0.0),
                WindowResizeAction::Normal,
            )
            .expect("normal should apply");
        assert_size_near(normal.inner_size.unwrap(), egui::vec2(1000.0, 1000.0));

        let correction = state
            .observe_viewport(decorated_viewport(
                monitor_size,
                egui::pos2(0.0, 0.0),
                egui::vec2(1000.0, 930.0),
                0.0,
            ))
            .expect("clamped normal should be corrected");
        assert_size_near(correction.inner_size.unwrap(), egui::vec2(930.0, 930.0));
    }

    #[test]
    fn maxspect_uses_platform_granted_size_after_os_clamps_request() {
        let monitor_size = egui::vec2(1000.0, 1000.0);
        let image_size = egui::vec2(1024.0, 1024.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(image_size);

        let mut inner_size = egui::vec2(600.0, 500.0);
        let mut outer_position = egui::pos2(100.0, 100.0);

        let maxspect = state
            .apply_resize_action(
                decorated_viewport(monitor_size, outer_position, inner_size, 0.0),
                WindowResizeAction::Maxspect,
            )
            .expect("maxspect should apply");
        assert_size_near(maxspect.inner_size.unwrap(), egui::vec2(1000.0, 1000.0));

        outer_position = maxspect.outer_position.unwrap_or(outer_position);
        inner_size = egui::vec2(1000.0, 930.0);

        let aspect = state
            .apply_resize_action(
                decorated_viewport(monitor_size, outer_position, inner_size, 0.0),
                WindowResizeAction::RestoreAspectRatio,
            )
            .expect("aspect should apply");
        assert_size_near(aspect.inner_size.unwrap(), egui::vec2(930.0, 930.0));

        inner_size = aspect.inner_size.unwrap();
        let maxspect_again = state
            .apply_resize_action(
                decorated_viewport(monitor_size, outer_position, inner_size, 0.0),
                WindowResizeAction::Maxspect,
            )
            .expect("maxspect should apply with cached platform max");
        assert_size_near(maxspect_again.inner_size.unwrap(), egui::vec2(930.0, 930.0));
    }

    #[test]
    fn clamped_maxspect_is_corrected_when_viewport_feedback_arrives() {
        let monitor_size = egui::vec2(1000.0, 1000.0);
        let image_size = egui::vec2(1024.0, 1024.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(image_size);

        let maxspect = state
            .apply_resize_action(
                decorated_viewport(monitor_size, egui::pos2(100.0, 100.0), egui::vec2(600.0, 500.0), 0.0),
                WindowResizeAction::Maxspect,
            )
            .expect("maxspect should apply");
        assert_size_near(maxspect.inner_size.unwrap(), egui::vec2(1000.0, 1000.0));

        let correction = state
            .observe_viewport(decorated_viewport(
                monitor_size,
                egui::pos2(0.0, 0.0),
                egui::vec2(1000.0, 930.0),
                0.0,
            ))
            .expect("clamped maxspect should be corrected");
        assert_size_near(correction.inner_size.unwrap(), egui::vec2(930.0, 930.0));
    }

    #[test]
    fn aspect_specific_clamp_does_not_cap_a_wider_layout() {
        let monitor_size = egui::vec2(1600.0, 1000.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(egui::vec2(800.0, 600.0));

        let requested = state
            .apply_resize_action(
                decorated_viewport(monitor_size, egui::pos2(100.0, 100.0), egui::vec2(800.0, 600.0), 0.0),
                WindowResizeAction::Maxspect,
            )
            .expect("maxspect should apply");
        assert_size_near(requested.inner_size.unwrap(), egui::vec2(1333.0, 1000.0));

        let correction = state
            .observe_viewport(decorated_viewport(
                monitor_size,
                egui::pos2(100.0, 0.0),
                egui::vec2(1333.0, 900.0),
                0.0,
            ))
            .expect("height clamp should be corrected");
        assert_size_near(correction.inner_size.unwrap(), egui::vec2(1200.0, 900.0));
        assert!(
            state
                .observe_viewport(decorated_viewport(
                    monitor_size,
                    egui::pos2(100.0, 0.0),
                    egui::vec2(1200.0, 900.0),
                    0.0,
                ))
                .is_none()
        );

        let wider_layout = state
            .on_image_changed(
                egui::vec2(1601.0, 600.0),
                decorated_viewport(monitor_size, egui::pos2(100.0, 0.0), egui::vec2(1200.0, 900.0), 0.0),
            )
            .expect("maxspect should be reapplied for the new layout");
        assert_size_near(wider_layout.inner_size.unwrap(), egui::vec2(1600.0, 600.0));
    }

    #[test]
    fn maxspect_uses_current_work_area_origin_and_decorations() {
        let work_area = egui::Rect::from_min_size(egui::pos2(-1440.0, 25.0), egui::vec2(1440.0, 875.0));
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(egui::vec2(1600.0, 900.0));

        let command = state
            .apply_resize_action(
                viewport_with_work_area(work_area, egui::pos2(-1200.0, 100.0), egui::vec2(800.0, 450.0), 25.0),
                WindowResizeAction::Maxspect,
            )
            .expect("maxspect should apply");

        assert_size_near(command.inner_size.unwrap(), egui::vec2(1440.0, 810.0));
        assert_eq!(command.outer_position, Some(egui::pos2(-1440.0, 45.0)));
    }

    #[test]
    fn changing_work_areas_invalidates_a_stale_platform_clamp() {
        let first_area = egui::Rect::from_min_size(egui::pos2(0.0, 25.0), egui::vec2(1200.0, 775.0));
        let second_area = egui::Rect::from_min_size(egui::pos2(1200.0, 0.0), egui::vec2(1800.0, 1100.0));
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(egui::vec2(1600.0, 900.0));
        state.platform_max_inner_size = Some(egui::vec2(900.0, 700.0));
        state.last_work_area = Some(first_area);
        state.last_monitor_size = Some(first_area.size());

        let command = state
            .apply_resize_action(
                viewport_with_work_area(second_area, egui::pos2(1300.0, 100.0), egui::vec2(800.0, 450.0), 0.0),
                WindowResizeAction::Maxspect,
            )
            .expect("maxspect should apply on the new display");

        assert_size_near(command.inner_size.unwrap(), egui::vec2(1800.0, 1013.0));
        assert_eq!(command.outer_position, Some(egui::pos2(1200.0, 43.5)));
        assert_eq!(state.platform_max_inner_size, None);
    }

    #[test]
    fn completed_resize_feedback_is_not_kept_as_a_stale_request() {
        let monitor_size = egui::vec2(1600.0, 1000.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(egui::vec2(800.0, 600.0));
        let command = state
            .apply_resize_action(
                decorated_viewport(monitor_size, egui::pos2(100.0, 100.0), egui::vec2(500.0, 400.0), 0.0),
                WindowResizeAction::Increase10Percent,
            )
            .expect("increase should apply");

        assert!(
            state
                .observe_viewport(decorated_viewport(
                    monitor_size,
                    egui::pos2(100.0, 100.0),
                    command.inner_size.unwrap(),
                    0.0,
                ))
                .is_none()
        );
        assert_eq!(state.last_requested_inner_size, None);
        assert_eq!(state.last_requested_action, None);
    }

    #[test]
    fn manual_resize_after_completed_maxspect_preserves_user_size_on_image_change() {
        let monitor_size = egui::vec2(1000.0, 800.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(egui::vec2(800.0, 600.0));
        let maxspect = state
            .apply_resize_action(
                decorated_viewport(monitor_size, egui::pos2(100.0, 100.0), egui::vec2(600.0, 450.0), 0.0),
                WindowResizeAction::Maxspect,
            )
            .expect("maxspect should apply");
        let maxspect_size = maxspect.inner_size.unwrap();
        assert!(
            state
                .observe_viewport(decorated_viewport(
                    monitor_size,
                    egui::pos2(0.0, 25.0),
                    maxspect_size,
                    0.0,
                ))
                .is_none()
        );

        assert!(
            state
                .observe_viewport(decorated_viewport(
                    monitor_size,
                    egui::pos2(100.0, 100.0),
                    egui::vec2(700.0, 500.0),
                    0.0,
                ))
                .is_none()
        );
        assert!(
            state
                .on_image_changed(
                    egui::vec2(640.0, 480.0),
                    decorated_viewport(monitor_size, egui::pos2(100.0, 100.0), egui::vec2(700.0, 500.0), 0.0,),
                )
                .is_none()
        );
    }

    #[test]
    fn fallback_maxspect_centers_on_primary_monitor() {
        let monitor_size = egui::vec2(1000.0, 800.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(egui::vec2(800.0, 600.0));

        let command = state
            .apply_resize_action(
                decorated_viewport(monitor_size, egui::pos2(100.0, 100.0), egui::vec2(600.0, 450.0), 20.0),
                WindowResizeAction::Maxspect,
            )
            .expect("maxspect should apply");

        assert_size_near(command.inner_size.unwrap(), egui::vec2(1000.0, 750.0));
        assert_eq!(command.outer_position, Some(egui::pos2(0.0, 15.0)));
    }

    #[test]
    fn fallback_growth_stays_on_primary_monitor() {
        let monitor_size = egui::vec2(1000.0, 800.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(egui::vec2(800.0, 600.0));

        let command = state
            .apply_resize_action(
                decorated_viewport(monitor_size, egui::pos2(190.0, 170.0), egui::vec2(800.0, 600.0), 20.0),
                WindowResizeAction::Increase10Percent,
            )
            .expect("increase should fit");

        assert_size_near(command.inner_size.unwrap(), egui::vec2(880.0, 660.0));
        assert_eq!(command.outer_position, Some(egui::pos2(120.0, 120.0)));
    }

    #[test]
    fn fallback_does_not_move_a_window_assumed_to_be_on_another_monitor() {
        let monitor_size = egui::vec2(1000.0, 800.0);
        let outer_position = egui::pos2(1200.0, 100.0);
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(egui::vec2(800.0, 600.0));

        let command = state
            .apply_resize_action(
                decorated_viewport(monitor_size, outer_position, egui::vec2(600.0, 450.0), 20.0),
                WindowResizeAction::Maxspect,
            )
            .expect("maxspect should apply");

        assert_eq!(command.outer_position, Some(outer_position));
    }

    #[test]
    fn missing_work_area_does_not_discard_in_flight_resize() {
        let work_area = egui::Rect::from_min_size(egui::pos2(-1440.0, 25.0), egui::vec2(1440.0, 875.0));
        let mut state = ImageWindowGeometryState::default();
        state.normal_size = Some(egui::vec2(1600.0, 900.0));
        state
            .apply_resize_action(
                viewport_with_work_area(work_area, egui::pos2(-1200.0, 100.0), egui::vec2(800.0, 450.0), 25.0),
                WindowResizeAction::Maxspect,
            )
            .expect("maxspect should apply");
        let requested_size = state.last_requested_inner_size;

        state.observe_display(decorated_viewport(
            work_area.size(),
            egui::pos2(-1200.0, 100.0),
            egui::vec2(800.0, 450.0),
            25.0,
        ));

        assert_eq!(state.last_work_area, Some(work_area));
        assert_eq!(state.last_requested_inner_size, requested_size);
        assert_eq!(state.last_requested_action, Some(WindowResizeAction::Maxspect));
    }

    fn decorated_viewport(
        monitor_size: egui::Vec2,
        outer_position: egui::Pos2,
        inner_size: egui::Vec2,
        decoration_top: f32,
    ) -> ViewportGeometry {
        let outer_rect =
            egui::Rect::from_min_size(outer_position, egui::vec2(inner_size.x, inner_size.y + decoration_top));
        let inner_rect = egui::Rect::from_min_size(outer_position + egui::vec2(0.0, decoration_top), inner_size);
        ViewportGeometry {
            monitor_size,
            work_area: None,
            outer_rect: Some(outer_rect),
            inner_rect: Some(inner_rect),
        }
    }

    fn viewport_with_work_area(
        work_area: egui::Rect,
        outer_position: egui::Pos2,
        inner_size: egui::Vec2,
        decoration_top: f32,
    ) -> ViewportGeometry {
        let mut viewport = decorated_viewport(work_area.size(), outer_position, inner_size, decoration_top);
        viewport.work_area = Some(work_area);
        viewport
    }

    fn assert_size_near(a: egui::Vec2, b: egui::Vec2) {
        assert!(sizes_nearly_equal(a, b), "expected {a:?} to be nearly equal to {b:?}");
    }
}
