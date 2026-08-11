use std::time::{Duration, Instant};

use eframe::egui;

const WORK_AREA_REFRESH_INTERVAL: Duration = Duration::from_secs(1);

/// Caches the native monitor work area so ordinary render frames do not call
/// into the windowing system. Window/monitor geometry changes refresh it
/// immediately; a low-frequency poll catches menu bar, Dock, and taskbar
/// changes that do not move the window.
#[derive(Debug, Default)]
pub struct NativeWorkAreaCache {
    current: Option<egui::Rect>,
    last_outer_rect: Option<egui::Rect>,
    last_monitor_size: Option<egui::Vec2>,
    last_query_at: Option<Instant>,
}

impl NativeWorkAreaCache {
    pub fn update(&mut self, frame: &eframe::Frame, ctx: &egui::Context) -> Option<egui::Rect> {
        let (outer_rect, monitor_size) =
            ctx.input(|input| (input.viewport().outer_rect, input.viewport().monitor_size));
        let now = Instant::now();
        let geometry_changed = self.last_outer_rect != outer_rect || self.last_monitor_size != monitor_size;
        let periodic_refresh_due = self
            .last_query_at
            .is_none_or(|last_query| now.duration_since(last_query) >= WORK_AREA_REFRESH_INTERVAL);

        self.last_outer_rect = outer_rect;
        self.last_monitor_size = monitor_size;
        if geometry_changed || periodic_refresh_due {
            self.last_query_at = Some(now);
            // Keep the previous concrete area if a native query is transiently
            // unavailable while the window is moving or off-screen.
            if let Some(work_area) = native_current_monitor_work_area(frame, ctx.zoom_factor()) {
                self.current = Some(work_area);
            }
        }
        self.current
    }
}

/// Returns the native usable rectangle for the monitor containing the root
/// window, including its desktop-space origin.
///
/// `None` deliberately selects the portable fallback implemented by
/// `viewport_geometry::InnerArea` and `image_window_geometry`:
///
/// - egui's current `monitor_size` minus observed window decorations provides
///   an approximate maximum inner size;
/// - a zero-origin monitor is used for positioning only when the current
///   window geometry identifies it safely as the primary monitor;
/// - OS-clamped resize feedback supplies missing per-axis work-area limits.
///
/// The fallback cannot recover a secondary monitor's desktop origin or
/// distinguish equal-sized monitors because egui/eframe do not expose that
/// information. Native implementations should be added here when a platform
/// has a reliable work-area API.
#[cfg(target_os = "macos")]
fn native_current_monitor_work_area(frame: &eframe::Frame, zoom_factor: f32) -> Option<egui::Rect> {
    use core_graphics::display::CGDisplay;
    use objc2_app_kit::NSView;
    use raw_window_handle::{HasWindowHandle, RawWindowHandle};

    let RawWindowHandle::AppKit(handle) = frame.window_handle().ok()?.as_raw() else {
        return None;
    };
    // SAFETY: eframe guarantees that the AppKit raw handle points to a live
    // NSView for the lifetime of `frame`, and App callbacks run on macOS's
    // main thread.
    let view = unsafe { handle.ns_view.cast::<NSView>().as_ref() };
    let screen = view.window()?.screen()?;
    let visible = screen.visibleFrame();
    // AppKit screen coordinates are always relative to the fixed Core
    // Graphics main display, not AppKit's `mainScreen` (which can follow the
    // key window).
    let main_screen_height = CGDisplay::main().bounds().size.height;

    // AppKit uses a bottom-left origin with y pointing upwards. Winit and egui
    // use a top-left desktop origin with y pointing downwards. AppKit points
    // are winit logical pixels; divide by egui's extra UI zoom to get egui
    // points, matching ViewportInfo::outer_rect.
    let zoom = f64::from(zoom_factor.max(f32::EPSILON));
    let x = visible.origin.x / zoom;
    let y = (main_screen_height - visible.size.height - visible.origin.y) / zoom;
    let width = visible.size.width / zoom;
    let height = visible.size.height / zoom;
    Some(egui::Rect::from_min_size(
        egui::pos2(x as f32, y as f32),
        egui::vec2(width as f32, height as f32),
    ))
}

#[cfg(not(target_os = "macos"))]
fn native_current_monitor_work_area(_frame: &eframe::Frame, _zoom_factor: f32) -> Option<egui::Rect> {
    // Windows could provide this through MonitorFromWindow/GetMonitorInfoW.
    // X11 needs desktop/window-manager-specific work-area handling, while
    // Wayland generally does not expose global window or monitor positions.
    // Until those native paths exist, returning None activates the portable
    // geometry/clamp-feedback fallback described above.
    None
}
