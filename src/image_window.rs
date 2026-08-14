use std::sync::{Arc, Mutex};
use std::time::Duration;

use eframe::egui;
use eframe::egui_wgpu;

use crate::annotation_tool::AnnotationTool;
use crate::annotations::WidgetToTextureTransform;
use crate::color_image::PixelSRGBA;
use crate::image_list::SelectedImageView;
use crate::image_view::{CellView, ImageView};
use crate::layout::LayoutConfig;
use crate::minimap::Minimap;
use crate::modified_image::ModifiedImage;
use crate::render::WgpuImageCallback;

#[derive(Clone)]
pub struct CursorPixelInfo {
    pub image_name: String,
    pub x: u32,
    pub y: u32,
    pub image_width: u32,
    pub image_height: u32,
    pub uv: egui::Vec2,
    pub rgba: [u8; 4],
    pub image_data: Arc<Mutex<ModifiedImage>>,
}

impl CursorPixelInfo {
    // Whether both infos describe the same pixel, as far as the controls
    // window display is concerned.
    fn same_pixel(&self, other: &Self) -> bool {
        self.image_name == other.image_name && self.x == other.x && self.y == other.y && self.rgba == other.rgba
    }
}

impl std::fmt::Debug for CursorPixelInfo {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("CursorPixelInfo")
            .field("image_name", &self.image_name)
            .field("x", &self.x)
            .field("y", &self.y)
            .field("image_width", &self.image_width)
            .field("image_height", &self.image_height)
            .field("uv", &self.uv)
            .field("rgba", &self.rgba)
            .finish_non_exhaustive()
    }
}

#[derive(Default)]
pub struct ImageWindow {
    pub view: ImageView,
    minimap: Minimap,
}

pub struct ImageWindowOutput {
    pub image_rect: Option<egui::Rect>,
    pub secondary_clicked: bool,
    // The cursor pixel info and annotation selection are shared with the
    // controls window, which lives in a separate viewport and stays idle
    // unless explicitly repainted when they change.
    pub shared_state_changed: bool,
}

impl ImageWindow {
    pub fn show(
        &mut self,
        ctx: &egui::Context,
        layout: LayoutConfig,
        images: Vec<Option<SelectedImageView>>,
        cursor_info: Arc<Mutex<Option<CursorPixelInfo>>>,
        annotation_tool: Arc<Mutex<AnnotationTool>>,
    ) -> ImageWindowOutput {
        let mut output = ImageWindowOutput {
            image_rect: None,
            secondary_clicked: false,
            shared_state_changed: false,
        };

        egui::CentralPanel::default()
            .frame(egui::Frame::NONE.fill(egui::Color32::BLACK))
            .show(ctx, |ui| {
                let available_rect = ui.available_rect_before_wrap();
                output.image_rect = Some(available_rect);

                if images.is_empty() {
                    ui.label("Loading...");
                    return;
                }

                let cell_rects = layout_cell_rects(available_rect, layout, 1.0);
                let ctrl = ui.input(|i| i.modifiers.ctrl || i.modifiers.mac_cmd);
                let mut hovered: Option<HoveredImage> = None;
                let visible_images = images
                    .iter()
                    .filter_map(|image| image.as_ref()?.data.clone())
                    .collect::<Vec<_>>();
                let first_valid_index = images
                    .iter()
                    .position(|image| image.as_ref().and_then(|image| image.data.as_ref()).is_some());

                let image_sizes = images.iter().map(image_size_of).collect::<Vec<_>>();
                let cell_views = self.view.resolve_cells(&cell_rects, &image_sizes, first_valid_index);
                // View state is shared by every cell, so the fade is driven by
                // the first cell showing an image and applies to all of them.
                let reference_view = first_valid_index.and_then(|index| cell_views.get(index).copied().flatten());

                for (index, cell_rect) in cell_rects.iter().copied().enumerate() {
                    let response = ui.allocate_rect(cell_rect, egui::Sense::click_and_drag());
                    if response.secondary_clicked() {
                        output.secondary_clicked = true;
                    }

                    let Some(Some(image)) = images.get(index) else {
                        continue;
                    };

                    if let Some(error) = image.error.as_ref() {
                        paint_error(ui, cell_rect, &image.name, error);
                        continue;
                    }

                    let Some(image_data) = image.data.as_ref() else {
                        paint_loading(ui, cell_rect);
                        continue;
                    };

                    let Some(view) = cell_views.get(index).copied().flatten() else {
                        continue;
                    };

                    let callback = egui_wgpu::Callback::new_paint_callback(
                        view.paint_rect,
                        WgpuImageCallback::new(
                            image_data.clone(),
                            [view.uv_min.x, view.uv_min.y],
                            [view.uv_max.x, view.uv_max.y],
                        ),
                    );
                    ui.painter().add(callback);

                    if let Ok(mut tool) = annotation_tool.lock() {
                        let image_size = image_sizes
                            .get(index)
                            .copied()
                            .flatten()
                            .map(|size| [size.x as u32, size.y as u32])
                            .unwrap_or([1, 1]);
                        let annotation_output = tool.render_for_image(
                            ui,
                            &response,
                            image_data,
                            WidgetToTextureTransform {
                                widget_rect: view.paint_rect,
                                uv_min: view.uv_min,
                                uv_max: view.uv_max,
                                image_size,
                            },
                            first_valid_index == Some(index),
                            &visible_images,
                        );
                        output.shared_state_changed |= annotation_output.selection_changed;
                    }

                    // Unlike annotation editing, scrolling is a view gesture:
                    // every cell shares one view, so it is driven by whichever
                    // cell the pointer is over. The guards are inside.
                    self.handle_pan_input(ui, &response, &annotation_tool, view);

                    let Some(pointer_pos) = response.hover_pos() else {
                        continue;
                    };

                    let Some(sample) =
                        sample_image_at_pointer(image_data, view.paint_rect, pointer_pos, view.uv_min, view.uv_max)
                    else {
                        continue;
                    };

                    if ctrl && response.clicked() {
                        self.view.zoom_in_at(
                            sample.uv,
                            egui::vec2(sample.image_width as f32, sample.image_height as f32),
                        );
                    }

                    if ctrl && response.secondary_clicked() {
                        self.view.zoom_out();
                        output.secondary_clicked = false;
                    }

                    hovered = Some(HoveredImage {
                        slot_index: index,
                        image_name: image.name.clone(),
                        rect: cell_rect,
                        pointer_pos,
                        sample,
                        image_data: image_data.clone(),
                    });
                }

                if let Some(hovered) = hovered {
                    if let Ok(mut info) = cursor_info.lock() {
                        let new_info = CursorPixelInfo {
                            image_name: hovered.image_name.clone(),
                            x: hovered.sample.x,
                            y: hovered.sample.y,
                            image_width: hovered.sample.image_width,
                            image_height: hovered.sample.image_height,
                            uv: hovered.sample.uv,
                            rgba: hovered.sample.rgba,
                            image_data: hovered.image_data.clone(),
                        };
                        output.shared_state_changed |= !info.as_ref().is_some_and(|old| old.same_pixel(&new_info));
                        *info = Some(new_info);
                    }
                    paint_synced_cursor(ui, &images, &cell_views, hovered.slot_index, hovered.sample.uv);
                    paint_synced_status_bars(ui, &images, &cell_rects, &hovered);
                } else if let Ok(mut info) = cursor_info.lock() {
                    output.shared_state_changed |= info.take().is_some();
                }

                // Painted after every cell so it sits on top, and on each of
                // them so a comparison layout shows where all the views are.
                if let Some(reference_view) = reference_view {
                    let now = ui.input(|input| input.time);
                    if let Some(opacity) = self.minimap.opacity_for(reference_view, now) {
                        for (index, view) in cell_views.iter().enumerate() {
                            let (Some(view), Some(Some(image_size)), Some(Some(image))) =
                                (view, image_sizes.get(index), images.get(index))
                            else {
                                continue;
                            };
                            let Some(image_data) = image.data.as_ref() else {
                                continue;
                            };
                            Minimap::paint(ui, *view, image_data, *image_size, opacity);
                        }
                        // Nothing else animates, so the fade needs its own
                        // repaints to run to completion.
                        ui.ctx().request_repaint();
                    }
                }
            });

        output
    }

    fn handle_pan_input(
        &mut self,
        ui: &egui::Ui,
        response: &egui::Response,
        annotation_tool: &Arc<Mutex<AnnotationTool>>,
        view: CellView,
    ) {
        let scroll_delta = ui.input(|input| input.smooth_scroll_delta);
        if scroll_delta != egui::Vec2::ZERO && response.hovered() {
            self.view.scroll_by(scroll_delta, view);
        }

        // The middle button is always a pan. The primary button is only a pan
        // when the annotation tool did not take the drag for itself, which it
        // already decided while handling this frame's input.
        let annotation_busy = annotation_tool
            .lock()
            .is_ok_and(|tool| tool.is_creating() || tool.is_editing());
        let dragged_to_pan = response.dragged_by(egui::PointerButton::Middle)
            || (!annotation_busy && response.dragged_by(egui::PointerButton::Primary));
        if dragged_to_pan {
            self.view.scroll_by(response.drag_delta(), view);
        }
    }
}

fn image_size_of(image: &Option<SelectedImageView>) -> Option<egui::Vec2> {
    let data = image.as_ref()?.data.as_ref()?.lock().ok()?;
    let data = data.final_data();
    if data.width() == 0 || data.height() == 0 {
        return None;
    }
    Some(egui::vec2(data.width() as f32, data.height() as f32))
}

#[derive(Clone)]
struct ImageSample {
    x: u32,
    y: u32,
    image_width: u32,
    image_height: u32,
    uv: egui::Vec2,
    rgba: [u8; 4],
}

struct HoveredImage {
    slot_index: usize,
    image_name: String,
    rect: egui::Rect,
    pointer_pos: egui::Pos2,
    sample: ImageSample,
    image_data: Arc<Mutex<ModifiedImage>>,
}

struct StatusBarInfo<'a> {
    image_name: &'a str,
    x: u32,
    y: u32,
    rgba: [u8; 4],
    pointer_pos: egui::Pos2,
}

fn layout_cell_rects(rect: egui::Rect, layout: LayoutConfig, padding: f32) -> Vec<egui::Rect> {
    let rows = layout.rows.max(1);
    let cols = layout.cols.max(1);
    let content_width = (rect.width() - (cols.saturating_sub(1) as f32 * padding)).max(1.0);
    let content_height = (rect.height() - (rows.saturating_sub(1) as f32 * padding)).max(1.0);
    let cell_width = content_width / cols as f32;
    let cell_height = content_height / rows as f32;

    let mut rects = Vec::with_capacity(rows * cols);
    for row in 0..rows {
        for col in 0..cols {
            let min = egui::pos2(
                rect.left() + col as f32 * (cell_width + padding),
                rect.top() + row as f32 * (cell_height + padding),
            );
            rects.push(egui::Rect::from_min_size(min, egui::vec2(cell_width, cell_height)));
        }
    }
    rects
}

fn sample_image_at_pointer(
    image_data: &Arc<Mutex<ModifiedImage>>,
    rect: egui::Rect,
    pointer_pos: egui::Pos2,
    uv_min: egui::Vec2,
    uv_max: egui::Vec2,
) -> Option<ImageSample> {
    let Ok(data) = image_data.lock() else {
        return None;
    };
    let data = data.final_data();
    if data.width() == 0 || data.height() == 0 {
        return None;
    }

    let widget_pos = (pointer_pos + egui::vec2(0.5, 0.5)) - rect.min;
    let uv_window = widget_pos / rect.size();
    let tex_uv = uv_min + uv_window * (uv_max - uv_min);
    if !(0.0..=1.0).contains(&tex_uv.x) || !(0.0..=1.0).contains(&tex_uv.y) {
        return None;
    }

    sample_image_at_uv(data, tex_uv)
}

fn sample_image_at_uv(data: &crate::image_item_data::ImageItemData, uv: egui::Vec2) -> Option<ImageSample> {
    if data.width() == 0 || data.height() == 0 {
        return None;
    }
    let x = ((uv.x * data.width() as f32).floor() as u32).min(data.width().saturating_sub(1));
    let y = ((uv.y * data.height() as f32).floor() as u32).min(data.height().saturating_sub(1));
    let rgba = data.pixel_rgba(x, y)?;
    Some(ImageSample {
        x,
        y,
        image_width: data.width(),
        image_height: data.height(),
        uv,
        rgba,
    })
}

fn paint_loading(ui: &egui::Ui, rect: egui::Rect) {
    const REPAINT_INTERVAL: Duration = Duration::from_millis(100);
    const SPINNER_RADIUS: f32 = 9.0;
    const SPINNER_SEGMENTS: usize = 18;

    ui.ctx().request_repaint_after(REPAINT_INTERVAL);

    let spinner_center = rect.center() - egui::vec2(0.0, 13.0);
    let start_angle = ui.input(|input| input.time as f32) * std::f32::consts::TAU;
    let sweep = std::f32::consts::TAU * 0.72;
    let points = (0..=SPINNER_SEGMENTS)
        .map(|index| {
            let angle = start_angle + sweep * index as f32 / SPINNER_SEGMENTS as f32;
            spinner_center + SPINNER_RADIUS * egui::vec2(angle.cos(), angle.sin())
        })
        .collect();
    ui.painter()
        .add(egui::Shape::line(points, egui::Stroke::new(2.5, egui::Color32::WHITE)));

    ui.painter().text(
        rect.center() + egui::vec2(0.0, 13.0),
        egui::Align2::CENTER_CENTER,
        "Loading image…",
        egui::TextStyle::Body.resolve(ui.style()),
        ui.visuals().weak_text_color(),
    );
}

fn paint_error(ui: &egui::Ui, rect: egui::Rect, image_name: &str, error: &str) {
    let text = format!("ERROR: could not load {image_name}\n{error}");
    ui.painter().text(
        rect.left_top() + egui::vec2(8.0, 8.0),
        egui::Align2::LEFT_TOP,
        text,
        egui::TextStyle::Body.resolve(ui.style()),
        egui::Color32::from_rgb(255, 96, 96),
    );
}

fn paint_synced_cursor(
    ui: &egui::Ui,
    images: &[Option<SelectedImageView>],
    views: &[Option<CellView>],
    hovered_index: usize,
    uv: egui::Vec2,
) {
    for (index, image) in images.iter().enumerate() {
        if index == hovered_index || image.as_ref().and_then(|image| image.data.as_ref()).is_none() {
            continue;
        }
        // Each cell resolves the shared texture coordinate through its own
        // visible region: differently sized images crop differently in Fill.
        let Some(view) = views.get(index).copied().flatten() else {
            continue;
        };
        let uv_window = (uv - view.uv_min) / (view.uv_max - view.uv_min);
        let center = view.paint_rect.min + uv_window * view.paint_rect.size();
        ui.painter().circle_stroke(
            center,
            5.0,
            egui::Stroke::new(1.0, egui::Color32::from_black_alpha(180)),
        );
        ui.painter().circle_stroke(
            center,
            4.0,
            egui::Stroke::new(2.0, egui::Color32::from_white_alpha(180)),
        );
        ui.painter().circle_stroke(
            center,
            3.0,
            egui::Stroke::new(1.0, egui::Color32::from_black_alpha(180)),
        );
    }
}

fn paint_synced_status_bars(
    ui: &egui::Ui,
    images: &[Option<SelectedImageView>],
    rects: &[egui::Rect],
    hovered: &HoveredImage,
) {
    for (index, image) in images.iter().enumerate() {
        let Some(image) = image else {
            continue;
        };
        let Some(image_data) = image.data.as_ref() else {
            continue;
        };
        let Some(rect) = rects.get(index).copied() else {
            continue;
        };
        let Ok(data) = image_data.lock() else {
            continue;
        };
        let Some(sample) = sample_image_at_uv(data.final_data(), hovered.sample.uv) else {
            continue;
        };
        let pointer_pos = if index == hovered.slot_index {
            hovered.pointer_pos
        } else {
            rect.min + (hovered.pointer_pos - hovered.rect.min)
        };
        paint_status_bar(
            ui,
            rect,
            &StatusBarInfo {
                image_name: &image.name,
                x: sample.x,
                y: sample.y,
                rgba: sample.rgba,
                pointer_pos,
            },
        );
    }
}

fn paint_status_bar(ui: &egui::Ui, image_rect: egui::Rect, status: &StatusBarInfo<'_>) {
    let painter = ui.painter();
    let font_id = egui::TextStyle::Monospace.resolve(ui.style());
    let font_size = font_id.size;

    let hsv = PixelSRGBA::from_array(status.rgba).to_hsv().display_hsv();
    let line1 = status.image_name.to_owned();
    let line2 = format!(
        "{:4}, {:4} (sRGBA {:3} {:3} {:3} {:3}) (HSV {:3} {:3} {:3})",
        status.x, status.y, status.rgba[0], status.rgba[1], status.rgba[2], status.rgba[3], hsv.0, hsv.1, hsv.2,
    );

    let line1_galley = painter.layout_no_wrap(line1, font_id.clone(), egui::Color32::WHITE);
    let line2_galley = painter.layout_no_wrap(line2, font_id, egui::Color32::WHITE);
    let line1_height = line1_galley.size().y;
    let line2_height = line2_galley.size().y;
    let line_gap = 0.0;
    let text_height = line1_height + line_gap + line2_height;
    let top_bar_height = (font_size * 2.2).max(text_height + font_size * 0.35);
    let bottom_bar_height = (font_size * 2.55).max(text_height + font_size * 0.55);

    let mouse_y_in_widget = status.pointer_pos.y - image_rect.top();
    let show_on_bottom = (image_rect.height() - mouse_y_in_widget) > top_bar_height;
    let bar_height = if show_on_bottom {
        bottom_bar_height
    } else {
        top_bar_height
    };

    let bar_rect = if show_on_bottom {
        egui::Rect::from_min_max(
            egui::pos2(image_rect.left(), image_rect.bottom() - bar_height),
            image_rect.right_bottom(),
        )
    } else {
        egui::Rect::from_min_max(
            image_rect.left_top(),
            egui::pos2(image_rect.right(), image_rect.top() + bar_height),
        )
    };

    let text_x = image_rect.left() + font_size * 0.5;
    let text_y = bar_rect.top() + (bar_rect.height() - text_height) * 0.5;
    let clip_rect = bar_rect;

    painter.rect_filled(bar_rect, 0.0, egui::Color32::from_rgba_premultiplied(0, 0, 0, 127));
    let clipped = painter.with_clip_rect(clip_rect);
    clipped.galley(egui::pos2(text_x, text_y), line1_galley, egui::Color32::WHITE);
    clipped.galley(
        egui::pos2(text_x, text_y + line1_height + line_gap),
        line2_galley,
        egui::Color32::WHITE,
    );
}
