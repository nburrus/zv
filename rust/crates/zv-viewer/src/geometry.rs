#![allow(dead_code)]

use eframe::egui;

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Point {
    pub x: f32,
    pub y: f32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rect {
    pub origin: Point,
    pub size: Point,
}

const CONTROLS_WIDTH_WITH_PADDING: f32 = 320.0 + 12.0;
const CONTROLS_GAP: f32 = 8.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WindowGeometry {
    pub origin: egui::Pos2,
    pub size: egui::Vec2,
}

pub fn initial_image_window_geometry(
    image_size: egui::Vec2,
    monitor_size: egui::Vec2,
    viewer_index: usize,
) -> WindowGeometry {
    let mut geometry = WindowGeometry {
        origin: egui::pos2(
            monitor_size.x * (0.10 + 0.15 * viewer_index as f32),
            monitor_size.y * 0.10,
        ),
        size: image_size,
    };

    fit_widget_rect_in_screen(&mut geometry, image_size, monitor_size, true);
    geometry
}

pub fn controls_position_for_image_window(
    viewer_outer_rect: egui::Rect,
    monitor_size: egui::Vec2,
) -> Option<egui::Pos2> {
    if viewer_outer_rect.min.x > CONTROLS_WIDTH_WITH_PADDING {
        Some(egui::pos2(
            viewer_outer_rect.min.x - CONTROLS_WIDTH_WITH_PADDING,
            viewer_outer_rect.min.y,
        ))
    } else if monitor_size.x - viewer_outer_rect.min.x - viewer_outer_rect.width()
        > CONTROLS_WIDTH_WITH_PADDING
    {
        Some(egui::pos2(
            viewer_outer_rect.min.x + viewer_outer_rect.width() + CONTROLS_GAP,
            viewer_outer_rect.min.y,
        ))
    } else {
        None
    }
}

fn fit_widget_rect_in_screen(
    geometry: &mut WindowGeometry,
    normal_size: egui::Vec2,
    monitor_size: egui::Vec2,
    keep_aspect_ratio: bool,
) {
    let mut image_larger_than_screen = false;

    if geometry.size.x > monitor_size.x {
        geometry.size.x = monitor_size.x;
        if keep_aspect_ratio {
            let sx = monitor_size.x / normal_size.x;
            geometry.size.y = (normal_size.y * sx + 0.5).floor();
        }
        image_larger_than_screen = true;
    }

    if geometry.size.y > monitor_size.y {
        geometry.size.y = monitor_size.y;
        if keep_aspect_ratio {
            let sy = monitor_size.y / normal_size.y;
            geometry.size.x = (normal_size.x * sy + 0.5).floor();
        }
        image_larger_than_screen = true;
    }

    if image_larger_than_screen {
        geometry.origin.x = (monitor_size.x - geometry.size.x) * 0.5;
        geometry.origin.y = (monitor_size.y - geometry.size.y) * 0.5;
    }
}
