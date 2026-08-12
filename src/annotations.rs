use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{OnceLock, mpsc};

use eframe::egui;
use eframe::egui_wgpu::{self, wgpu};

use crate::color_image::ImageSRGBA;
use crate::image_item_data::ImageItemData;

/// Font definitions shared by the UI context and [`text_context`], so
/// annotation text lays out with the same glyphs everywhere.
pub fn shared_font_definitions() -> egui::FontDefinitions {
    let mut fonts = egui::FontDefinitions::default();
    egui_phosphor::add_to_fonts(&mut fonts, egui_phosphor::Variant::Regular);
    fonts
}

/// Context used for all image-space text work: bounds measurement and the
/// composite rasterization in [`AnnotationRenderer`].
///
/// Rendering text at all forces a context into the composite path: egui's
/// glyph atlas lives inside a `Context` and is only populated during its
/// begin/end passes. Solid shapes (lines, boxes) tessellate context-free,
/// which is why the pre-text compositor didn't need one.
///
/// It has to be a *dedicated* context rather than the UI one, for three
/// reasons:
/// - Scale: glyph rasterization and metrics are quantized per
///   pixels_per_point. The composite maps one point to one image pixel
///   (scale 1.0); the UI context runs at the display scale. This also keeps
///   saved images identical across displays.
/// - Pass lifecycle: the composite must run its own begin/end pass to
///   rasterize glyphs and collect font-texture deltas, and the UI context is
///   mid-frame (owned by eframe) when compositing happens.
/// - Texture deltas are single-consumer: eframe's renderer drains the UI
///   context's deltas for its own GPU atlas; [`AnnotationRenderer`] drains
///   this context's deltas for its offscreen renderer. Sharing one stream
///   would starve one of the two.
///
/// The flip side is that all measurement must go through this context too
/// (see [`measure_text`]): bounds measured with one atlas and text rendered
/// with another would disagree by fractions of a glyph, and the composite
/// clips text hard to its bounds.
fn text_context() -> &'static egui::Context {
    static CONTEXT: OnceLock<egui::Context> = OnceLock::new();
    CONTEXT.get_or_init(|| {
        let ctx = egui::Context::default();
        ctx.set_fonts(shared_font_definitions());
        // Fonts only exist after a first pass; run an empty one so text can
        // be measured before the first composite. The pass output is
        // discarded: AnnotationRenderer re-seeds the full font atlas anyway.
        let _ = ctx.run(egui::RawInput::default(), |_| {});
        ctx
    })
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Hash)]
pub struct AnnotationId(u64);

impl AnnotationId {
    pub fn next() -> Self {
        static NEXT_ID: AtomicU64 = AtomicU64::new(1);
        Self(NEXT_ID.fetch_add(1, Ordering::Relaxed))
    }

    pub fn is_valid(self) -> bool {
        self.0 != 0
    }
}

/// Directed segment in normalized texture coordinates. The direction
/// matters: endpoint styles (e.g. arrowheads) are attached per end.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LineSegment {
    pub p1: egui::Vec2,
    pub p2: egui::Vec2,
}

/// Axis-aligned bounding box in normalized texture coordinates. Rectangles,
/// ellipses, and text annotations are positioned by one.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BoundingBox {
    pub min: egui::Vec2,
    pub max: egui::Vec2,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct StrokeStyle {
    pub color: egui::Color32,
    pub width: f32,
}

/// Full style of a line annotation: stroke plus per-endpoint decoration.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct LineStyle {
    pub stroke: StrokeStyle,
    pub start_style: LineEndpointStyle,
    pub end_style: LineEndpointStyle,
}

/// Full style of a text annotation; its geometry is a [`BoundingBox`].
#[derive(Clone, Debug, PartialEq)]
pub struct TextStyle {
    pub text: String,
    pub color: egui::Color32,
    /// Font size in image-space pixels.
    pub font_size: f32,
}

impl Default for TextStyle {
    fn default() -> Self {
        Self {
            text: "Text".to_owned(),
            color: egui::Color32::YELLOW,
            font_size: 24.0,
        }
    }
}

impl Default for StrokeStyle {
    fn default() -> Self {
        Self {
            color: egui::Color32::YELLOW,
            width: 2.0,
        }
    }
}

impl Default for LineSegment {
    fn default() -> Self {
        Self {
            p1: egui::vec2(0.1, 0.1),
            p2: egui::vec2(0.5, 0.5),
        }
    }
}

impl Default for BoundingBox {
    fn default() -> Self {
        Self {
            min: egui::vec2(0.1, 0.1),
            max: egui::vec2(0.5, 0.5),
        }
    }
}

impl BoundingBox {
    pub fn handle_pos(&self, handle: AnnotationHandle) -> Option<egui::Vec2> {
        match handle {
            AnnotationHandle::TopLeft => Some(self.min),
            AnnotationHandle::TopRight => Some(egui::vec2(self.max.x, self.min.y)),
            AnnotationHandle::BottomLeft => Some(egui::vec2(self.min.x, self.max.y)),
            AnnotationHandle::BottomRight => Some(self.max),
            AnnotationHandle::LineStart | AnnotationHandle::LineEnd => None,
        }
    }

    pub fn widget_rect(&self, transform: &WidgetToTextureTransform) -> egui::Rect {
        egui::Rect::from_two_pos(
            transform.texture_to_widget(self.min),
            transform.texture_to_widget(self.max),
        )
    }

    pub fn pixel_rect(&self, width: u32, height: u32) -> egui::Rect {
        egui::Rect::from_min_max(
            egui::pos2(self.min.x * width as f32, self.min.y * height as f32),
            egui::pos2(self.max.x * width as f32, self.max.y * height as f32),
        )
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum LineEndpointStyle {
    #[default]
    None,
    Arrow,
}

impl LineEndpointStyle {
    pub fn label(self) -> &'static str {
        match self {
            Self::None => "None",
            Self::Arrow => "Arrow",
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum AnnotationElement {
    Line {
        id: AnnotationId,
        segment: LineSegment,
        style: LineStyle,
    },
    Rectangle {
        id: AnnotationId,
        bounds: BoundingBox,
        stroke: StrokeStyle,
    },
    Ellipse {
        id: AnnotationId,
        bounds: BoundingBox,
        stroke: StrokeStyle,
    },
    Text {
        id: AnnotationId,
        bounds: BoundingBox,
        style: TextStyle,
    },
}

/// How the shape under construction or edit reacts to Shift being held.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ShiftConstraint {
    /// Snap the dragged endpoint to 45° increments around the anchor.
    SnapTo45Degrees,
    /// Force equal extents on both axes around the anchor (square, circle).
    Square,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AnnotationKind {
    Line,
    Rectangle,
    Ellipse,
    Text,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AnnotationHandle {
    LineStart,
    LineEnd,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
}

impl AnnotationHandle {
    pub fn opposite(self) -> Self {
        match self {
            Self::LineStart => Self::LineEnd,
            Self::LineEnd => Self::LineStart,
            Self::TopLeft => Self::BottomRight,
            Self::TopRight => Self::BottomLeft,
            Self::BottomLeft => Self::TopRight,
            Self::BottomRight => Self::TopLeft,
        }
    }
}

impl AnnotationElement {
    pub fn id(&self) -> AnnotationId {
        match self {
            Self::Line { id, .. } | Self::Rectangle { id, .. } | Self::Ellipse { id, .. } | Self::Text { id, .. } => {
                *id
            }
        }
    }

    pub fn line_style(&self) -> Option<&LineStyle> {
        match self {
            Self::Line { style, .. } => Some(style),
            Self::Rectangle { .. } | Self::Ellipse { .. } | Self::Text { .. } => None,
        }
    }

    pub fn line_style_mut(&mut self) -> Option<&mut LineStyle> {
        match self {
            Self::Line { style, .. } => Some(style),
            Self::Rectangle { .. } | Self::Ellipse { .. } | Self::Text { .. } => None,
        }
    }

    pub fn kind(&self) -> AnnotationKind {
        match self {
            Self::Line { .. } => AnnotationKind::Line,
            Self::Rectangle { .. } => AnnotationKind::Rectangle,
            Self::Ellipse { .. } => AnnotationKind::Ellipse,
            Self::Text { .. } => AnnotationKind::Text,
        }
    }

    pub fn stroke(&self) -> Option<&StrokeStyle> {
        match self {
            Self::Line { style, .. } => Some(&style.stroke),
            Self::Rectangle { stroke, .. } | Self::Ellipse { stroke, .. } => Some(stroke),
            Self::Text { .. } => None,
        }
    }

    pub fn stroke_mut(&mut self) -> Option<&mut StrokeStyle> {
        match self {
            Self::Line { style, .. } => Some(&mut style.stroke),
            Self::Rectangle { stroke, .. } | Self::Ellipse { stroke, .. } => Some(stroke),
            Self::Text { .. } => None,
        }
    }

    pub fn shift_constraint(&self) -> Option<ShiftConstraint> {
        match self {
            Self::Line { .. } => Some(ShiftConstraint::SnapTo45Degrees),
            Self::Rectangle { .. } | Self::Ellipse { .. } => Some(ShiftConstraint::Square),
            Self::Text { .. } => None,
        }
    }

    pub fn handles(&self) -> &'static [AnnotationHandle] {
        match self {
            Self::Line { .. } => &[AnnotationHandle::LineStart, AnnotationHandle::LineEnd],
            _ => &[
                AnnotationHandle::TopLeft,
                AnnotationHandle::TopRight,
                AnnotationHandle::BottomLeft,
                AnnotationHandle::BottomRight,
            ],
        }
    }

    pub fn handle_texture_pos(&self, handle: AnnotationHandle) -> Option<egui::Vec2> {
        match self {
            Self::Line { segment, .. } => match handle {
                AnnotationHandle::LineStart => Some(segment.p1),
                AnnotationHandle::LineEnd => Some(segment.p2),
                _ => None,
            },
            Self::Rectangle { bounds, .. } | Self::Ellipse { bounds, .. } | Self::Text { bounds, .. } => {
                bounds.handle_pos(handle)
            }
        }
    }

    pub fn move_by(&mut self, delta: egui::Vec2) {
        match self {
            Self::Line { segment, .. } => {
                segment.p1 += delta;
                segment.p2 += delta;
            }
            Self::Rectangle { bounds, .. } | Self::Ellipse { bounds, .. } | Self::Text { bounds, .. } => {
                bounds.min += delta;
                bounds.max += delta;
            }
        }
    }

    pub fn move_handle_to(&mut self, handle: AnnotationHandle, texture_pos: egui::Vec2) {
        let opposite = self.handle_texture_pos(handle.opposite());
        if let Some(opposite) = opposite {
            self.move_handle_with_anchor(handle, texture_pos, opposite);
        }
    }

    /// Moves `handle` to `texture_pos`. `opposite` pins the corner across
    /// from a box handle so it stays fixed even when the drag crosses it;
    /// lines only move the dragged endpoint and ignore it.
    pub fn move_handle_with_anchor(&mut self, handle: AnnotationHandle, texture_pos: egui::Vec2, opposite: egui::Vec2) {
        match self {
            Self::Line { segment, .. } => match handle {
                AnnotationHandle::LineStart => segment.p1 = texture_pos,
                AnnotationHandle::LineEnd => segment.p2 = texture_pos,
                _ => {}
            },
            Self::Rectangle { bounds, .. } | Self::Ellipse { bounds, .. } | Self::Text { bounds, .. } => {
                bounds.min = texture_pos.min(opposite);
                bounds.max = texture_pos.max(opposite);
            }
        }
    }
}

pub fn fit_text_font_size(extent: egui::Vec2, current_size: f32, box_size: egui::Vec2) -> f32 {
    const MIN_SIZE: f32 = 1.0;
    const MAX_SIZE: f32 = 512.0;
    if current_size <= 0.0 || extent.x <= 0.0 || extent.y <= 0.0 || box_size.x <= 0.0 || box_size.y <= 0.0 {
        return current_size.clamp(MIN_SIZE, MAX_SIZE);
    }
    let fitted = current_size * (box_size.x / extent.x).min(box_size.y / extent.y);
    // Quantize to 0.5: every distinct font size rasterizes a fresh glyph set
    // into the font atlas, and a resize drag would otherwise fill it with
    // hundreds of one-off sizes.
    ((fitted * 2.0).round() * 0.5).clamp(MIN_SIZE, MAX_SIZE)
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AnnotationHitPart {
    Body,
    Handle(AnnotationHandle),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AnnotationHitResult {
    pub id: AnnotationId,
    pub part: AnnotationHitPart,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct AnnotationDocument {
    elements: Vec<AnnotationElement>,
}

impl AnnotationDocument {
    pub fn add_element(&mut self, element: AnnotationElement) {
        self.elements.push(element);
    }

    pub fn remove_by_id(&mut self, id: AnnotationId) -> Option<AnnotationElement> {
        let index = self.elements.iter().position(|element| element.id() == id)?;
        Some(self.elements.remove(index))
    }

    pub fn find_by_id(&self, id: AnnotationId) -> Option<&AnnotationElement> {
        self.elements.iter().find(|element| element.id() == id)
    }

    pub fn find_by_id_mut(&mut self, id: AnnotationId) -> Option<&mut AnnotationElement> {
        self.elements.iter_mut().find(|element| element.id() == id)
    }

    pub fn replace_by_id(&mut self, replacement: AnnotationElement) -> bool {
        let Some(element) = self
            .elements
            .iter_mut()
            .find(|element| element.id() == replacement.id())
        else {
            return false;
        };
        *element = replacement;
        true
    }

    pub fn elements(&self) -> &[AnnotationElement] {
        &self.elements
    }

    pub fn elements_mut(&mut self) -> &mut [AnnotationElement] {
        &mut self.elements
    }

    pub fn is_empty(&self) -> bool {
        self.elements.is_empty()
    }

    pub fn clear(&mut self) {
        self.elements.clear();
    }

    pub fn hit_test(
        &self,
        widget_pos: egui::Pos2,
        transform: &WidgetToTextureTransform,
        selected_id: AnnotationId,
        handle_radius_px: f32,
        body_tolerance_px: f32,
    ) -> Option<AnnotationHitResult> {
        if selected_id.is_valid() {
            if let Some(element) = self.find_by_id(selected_id) {
                if let Some(handle) = hit_handle(element, widget_pos, transform, handle_radius_px) {
                    return Some(AnnotationHitResult {
                        id: selected_id,
                        part: AnnotationHitPart::Handle(handle),
                    });
                }
            }
        }

        for element in self.elements.iter().rev() {
            if let Some(handle) = hit_handle(element, widget_pos, transform, handle_radius_px) {
                return Some(AnnotationHitResult {
                    id: element.id(),
                    part: AnnotationHitPart::Handle(handle),
                });
            }

            let body_hit = match element {
                AnnotationElement::Line { segment, style, .. } => {
                    let p1 = transform.texture_to_widget(segment.p1);
                    let p2 = transform.texture_to_widget(segment.p2);
                    distance_to_segment(widget_pos, p1, p2)
                        <= body_tolerance_px + transform.stroke_hit_tolerance_widget(style.stroke.width)
                }
                AnnotationElement::Rectangle { bounds, stroke, .. } => {
                    let rect = bounds.widget_rect(transform);
                    let tolerance = transform.stroke_hit_tolerance_widget(stroke.width) + body_tolerance_px;
                    rect.expand(tolerance).contains(widget_pos) && !rect.shrink(tolerance).contains(widget_pos)
                }
                AnnotationElement::Ellipse { bounds, stroke, .. } => ellipse_border_hit(
                    widget_pos,
                    bounds.widget_rect(transform),
                    transform.stroke_hit_tolerance_widget(stroke.width) + body_tolerance_px,
                ),
                AnnotationElement::Text { bounds, .. } => bounds.widget_rect(transform).contains(widget_pos),
            };
            if body_hit {
                return Some(AnnotationHitResult {
                    id: element.id(),
                    part: AnnotationHitPart::Body,
                });
            }
        }

        None
    }
}

#[derive(Clone, Copy, Debug)]
pub struct WidgetToTextureTransform {
    pub widget_rect: egui::Rect,
    pub uv_min: egui::Vec2,
    pub uv_max: egui::Vec2,
    pub image_size: [u32; 2],
}

impl WidgetToTextureTransform {
    pub fn widget_to_texture(&self, pos: egui::Pos2) -> egui::Vec2 {
        let widget_pos = (pos + egui::vec2(0.5, 0.5)) - self.widget_rect.min;
        let uv_window = widget_pos / self.widget_rect.size();
        self.uv_min + uv_window * (self.uv_max - self.uv_min)
    }

    pub fn texture_to_widget(&self, uv: egui::Vec2) -> egui::Pos2 {
        let uv_window = (uv - self.uv_min) / (self.uv_max - self.uv_min);
        self.widget_rect.min + uv_window * self.widget_rect.size()
    }

    pub fn image_pixel_to_widget_scale(&self) -> egui::Vec2 {
        let visible_texels = (self.uv_max - self.uv_min)
            * egui::vec2(self.image_size[0].max(1) as f32, self.image_size[1].max(1) as f32);
        self.widget_rect.size() / visible_texels
    }

    pub fn stroke_width_widget(&self, stroke_width: f32) -> f32 {
        (stroke_width * self.image_pixel_to_widget_scale().x).max(1.0)
    }

    pub fn stroke_hit_tolerance_widget(&self, stroke_width: f32) -> f32 {
        let scale = self.image_pixel_to_widget_scale();
        stroke_width * 0.5 * scale.x.max(scale.y)
    }
}

fn annotation_shapes_for_image(
    document: &AnnotationDocument,
    width: u32,
    height: u32,
) -> Vec<egui::epaint::ClippedShape> {
    let mut shapes = Vec::new();
    let full_clip = egui::Rect::from_min_size(egui::Pos2::ZERO, egui::vec2(width as f32, height as f32));
    for element in document.elements() {
        let (clip_rect, element_shapes) = match element {
            AnnotationElement::Line { segment, style, .. } => {
                let p1 = egui::pos2(segment.p1.x * width as f32, segment.p1.y * height as f32);
                let p2 = egui::pos2(segment.p2.x * width as f32, segment.p2.y * height as f32);
                (full_clip, line_shapes(style, p1, p2, style.stroke.width.max(1.0)))
            }
            AnnotationElement::Rectangle { bounds, stroke, .. } => (
                full_clip,
                vec![egui::Shape::rect_stroke(
                    bounds.pixel_rect(width, height),
                    0.0,
                    egui::Stroke::new(stroke.width.max(1.0), stroke.color),
                    egui::StrokeKind::Middle,
                )],
            ),
            AnnotationElement::Ellipse { bounds, stroke, .. } => {
                let rect = bounds.pixel_rect(width, height);
                (
                    full_clip,
                    vec![egui::Shape::ellipse_stroke(
                        rect.center(),
                        rect.size() * 0.5,
                        egui::Stroke::new(stroke.width.max(1.0), stroke.color),
                    )],
                )
            }
            AnnotationElement::Text { bounds, style, .. } => {
                let rect = bounds.pixel_rect(width, height);
                let shape = text_context().fonts_mut(|fonts| {
                    egui::Shape::text(
                        fonts,
                        rect.min + egui::vec2(text_box_padding(style.font_size), 0.0),
                        egui::Align2::LEFT_TOP,
                        &style.text,
                        egui::FontId::proportional(style.font_size.max(1.0)),
                        style.color,
                    )
                });
                (rect.intersect(full_clip), vec![shape])
            }
        };
        shapes.extend(
            element_shapes
                .into_iter()
                .map(|shape| egui::epaint::ClippedShape { clip_rect, shape }),
        );
    }
    shapes
}

/// Rendered size of `text` in image-space pixels. Must lay out through
/// [`text_context`] — the same fonts the composite renders with — so the
/// boxes derived from it enclose the rasterized text exactly.
fn measure_text(text: &str, font_size: f32) -> egui::Vec2 {
    text_context().fonts_mut(|fonts| {
        fonts
            .layout_no_wrap(
                // Keep a minimal non-empty extent so an emptied annotation
                // still has a visible, grabbable box.
                if text.is_empty() { " " } else { text }.to_owned(),
                egui::FontId::proportional(font_size.max(1.0)),
                egui::Color32::WHITE,
            )
            .size()
    })
}

/// Horizontal padding between a text annotation's box and its glyphs, in
/// image-space pixels. Proportional to the font size so the box keeps the
/// same look at any scale.
fn text_box_padding(font_size: f32) -> f32 {
    font_size * 0.25
}

/// Rendered text size plus the horizontal box padding on both sides.
fn padded_text_extent(style: &TextStyle) -> egui::Vec2 {
    measure_text(&style.text, style.font_size) + egui::vec2(2.0 * text_box_padding(style.font_size), 0.0)
}

pub fn fit_text_to_bounds(bounds: &BoundingBox, style: &mut TextStyle, image_size: [u32; 2]) {
    // Both the text extent and the padding scale linearly with the font
    // size, so fitting the padded extent yields the exact padded-box fit.
    let extent = padded_text_extent(style);
    let box_size = bounds.pixel_rect(image_size[0], image_size[1]).size();
    style.font_size = fit_text_font_size(extent, style.font_size, box_size);
}

/// Resizes the box to the rendered text while preserving its top-left corner
/// and the requested image-space font size. This is used for controls-window
/// edits; direct box manipulation uses [`fit_text_to_bounds`] instead.
pub fn resize_text_bounds_to_content(bounds: &mut BoundingBox, style: &TextStyle, image_size: [u32; 2]) {
    let extent = padded_text_extent(style);
    let image_size = egui::vec2(image_size[0].max(1) as f32, image_size[1].max(1) as f32);
    bounds.max = bounds.min + extent / image_size;
}

pub fn text_bounds_at(center: egui::Vec2, style: &TextStyle, image_size: [u32; 2]) -> BoundingBox {
    let extent = padded_text_extent(style);
    let image_size = egui::vec2(image_size[0].max(1) as f32, image_size[1].max(1) as f32);
    let half_extent = extent / image_size * 0.5;
    BoundingBox {
        min: center - half_extent,
        max: center + half_extent,
    }
}

/// Paints the widget-space preview of an element, e.g. while it is being
/// dragged into existence and does not belong to a document yet.
pub fn paint_element_overlay(
    painter: &egui::Painter,
    element: &AnnotationElement,
    transform: &WidgetToTextureTransform,
) {
    match element {
        AnnotationElement::Line { segment, style, .. } => {
            painter.extend(line_shapes(
                style,
                transform.texture_to_widget(segment.p1),
                transform.texture_to_widget(segment.p2),
                transform.stroke_width_widget(style.stroke.width),
            ));
        }
        AnnotationElement::Rectangle { bounds, stroke, .. } => {
            let stroke = egui::Stroke::new(transform.stroke_width_widget(stroke.width), stroke.color);
            painter.rect_stroke(bounds.widget_rect(transform), 0.0, stroke, egui::StrokeKind::Middle);
        }
        AnnotationElement::Ellipse { bounds, stroke, .. } => {
            let rect = bounds.widget_rect(transform);
            let stroke = egui::Stroke::new(transform.stroke_width_widget(stroke.width), stroke.color);
            painter.add(egui::Shape::ellipse_stroke(rect.center(), rect.size() * 0.5, stroke));
        }
        AnnotationElement::Text { bounds, style, .. } => {
            let rect = bounds.widget_rect(transform);
            let scale = transform.image_pixel_to_widget_scale().x;
            painter.with_clip_rect(rect).text(
                rect.min + egui::vec2(text_box_padding(style.font_size) * scale, 0.0),
                egui::Align2::LEFT_TOP,
                &style.text,
                egui::FontId::proportional((style.font_size * scale).max(1.0)),
                style.color,
            );
        }
    }
}

fn ellipse_border_hit(pos: egui::Pos2, rect: egui::Rect, tolerance: f32) -> bool {
    let radii = rect.size() * 0.5;
    if radii.x <= 0.0 || radii.y <= 0.0 {
        return false;
    }
    let normalized = (pos - rect.center()) / radii;
    let radial = normalized.length();
    // Normalizing the tolerance by the smaller radius slightly widens the hit
    // band along the long axis of eccentric ellipses. Good enough for pointer
    // picking and much cheaper than a true distance-to-ellipse computation.
    (radial - 1.0).abs() <= tolerance / radii.x.min(radii.y)
}

fn line_shapes(style: &LineStyle, p1: egui::Pos2, p2: egui::Pos2, thickness: f32) -> Vec<egui::Shape> {
    let delta = p2 - p1;
    let length = delta.length();
    if length <= 0.5 {
        return Vec::new();
    }

    let direction = delta / length;
    let head_length = 10.0_f32.max(thickness * 4.0);
    let mut shaft_start = p1;
    let mut shaft_end = p2;
    if style.start_style == LineEndpointStyle::Arrow {
        shaft_start += direction * head_length.min(length * 0.45);
    }
    if style.end_style == LineEndpointStyle::Arrow {
        shaft_end -= direction * head_length.min(length * 0.45);
    }

    let mut shapes = vec![egui::Shape::line_segment(
        [shaft_start, shaft_end],
        egui::Stroke::new(thickness, style.stroke.color),
    )];
    if style.start_style == LineEndpointStyle::Arrow {
        shapes.push(arrowhead_shape(p1, -direction, style.stroke.color, thickness));
    }
    if style.end_style == LineEndpointStyle::Arrow {
        shapes.push(arrowhead_shape(p2, direction, style.stroke.color, thickness));
    }
    shapes
}

fn arrowhead_shape(
    tip: egui::Pos2,
    direction_toward_tip: egui::Vec2,
    color: egui::Color32,
    thickness: f32,
) -> egui::Shape {
    let head_length = 10.0_f32.max(thickness * 4.0);
    let head_width = 7.0_f32.max(thickness * 2.5);
    let perpendicular = egui::vec2(-direction_toward_tip.y, direction_toward_tip.x);
    let base = tip - direction_toward_tip * head_length;
    egui::Shape::convex_polygon(
        vec![
            tip,
            base + perpendicular * (head_width * 0.5),
            base - perpendicular * (head_width * 0.5),
        ],
        color,
        egui::Stroke::NONE,
    )
}

pub fn paint_annotation_handles(
    painter: &egui::Painter,
    element: &AnnotationElement,
    transform: &WidgetToTextureTransform,
) {
    if let AnnotationElement::Text { bounds, .. } = element {
        painter.rect_stroke(
            bounds.widget_rect(transform),
            0.0,
            egui::Stroke::new(1.0, egui::Color32::from_white_alpha(180)),
            egui::StrokeKind::Inside,
        );
    }
    for &handle in element.handles() {
        let Some(handle_pos) = element.handle_texture_pos(handle) else {
            continue;
        };
        let center = transform.texture_to_widget(handle_pos);
        painter.circle_filled(center, 6.0, egui::Color32::from_white_alpha(220));
        painter.circle_stroke(
            center,
            6.0,
            egui::Stroke::new(1.5, egui::Color32::from_black_alpha(220)),
        );
    }
}

pub struct AnnotationRenderer {
    renderer: egui_wgpu::Renderer,
    font_texture_seeded: bool,
    composite_resources: Option<AnnotationCompositeResources>,
}

struct AnnotationCompositeResources {
    size: wgpu::Extent3d,
    texture: wgpu::Texture,
    view: wgpu::TextureView,
    readback_buffer: wgpu::Buffer,
    padded_bytes_per_row: usize,
}

impl AnnotationRenderer {
    pub fn new(device: &wgpu::Device) -> Self {
        Self {
            renderer: egui_wgpu::Renderer::new(
                device,
                wgpu::TextureFormat::Rgba8Unorm,
                egui_wgpu::RendererOptions::default(),
            ),
            font_texture_seeded: false,
            composite_resources: None,
        }
    }

    pub fn composite(
        &mut self,
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        input: &ImageItemData,
        document: &AnnotationDocument,
    ) -> Option<ImageItemData> {
        let base = input.cpu_data();
        if base.width() == 0 || base.height() == 0 {
            return None;
        }

        let width = base.width();
        let height = base.height();
        self.ensure_composite_resources(device, width, height);
        let (texture, view, readback_buffer, size, padded_bytes_per_row) = {
            let resources = self.composite_resources.as_ref()?;
            resources.upload_base_image(queue, base);
            (
                resources.texture.clone(),
                resources.view.clone(),
                resources.readback_buffer.clone(),
                resources.size,
                resources.padded_bytes_per_row,
            )
        };

        // Text shapes must be built inside a pass of the text context: laying
        // them out rasterizes any new glyphs into its font atlas, and end_pass
        // hands back the texture deltas that keep this renderer's GPU copy of
        // that atlas in sync.
        let ctx = text_context();
        ctx.begin_pass(egui::RawInput::default());
        let shapes = annotation_shapes_for_image(document, width, height);
        let full_output = ctx.end_pass();
        let paint_jobs = ctx.tessellate(shapes, 1.0);
        let screen_descriptor = egui_wgpu::ScreenDescriptor {
            size_in_pixels: [width, height],
            pixels_per_point: 1.0,
        };
        self.seed_font_texture(device, queue);
        for (id, delta) in &full_output.textures_delta.set {
            self.renderer.update_texture(device, queue, *id, delta);
        }

        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("zv annotation composite encoder"),
        });
        self.renderer
            .update_buffers(device, queue, &mut encoder, &paint_jobs, &screen_descriptor);
        {
            let mut render_pass = encoder
                .begin_render_pass(&wgpu::RenderPassDescriptor {
                    label: Some("zv annotation composite pass"),
                    color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                        view: &view,
                        depth_slice: None,
                        resolve_target: None,
                        ops: wgpu::Operations {
                            load: wgpu::LoadOp::Load,
                            store: wgpu::StoreOp::Store,
                        },
                    })],
                    depth_stencil_attachment: None,
                    timestamp_writes: None,
                    occlusion_query_set: None,
                })
                .forget_lifetime();
            self.renderer.render(&mut render_pass, &paint_jobs, &screen_descriptor);
        }

        encoder.copy_texture_to_buffer(
            wgpu::TexelCopyTextureInfo {
                texture: &texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            wgpu::TexelCopyBufferInfo {
                buffer: &readback_buffer,
                layout: wgpu::TexelCopyBufferLayout {
                    offset: 0,
                    bytes_per_row: Some(padded_bytes_per_row as u32),
                    rows_per_image: Some(height),
                },
            },
            size,
        );
        let submission = queue.submit(Some(encoder.finish()));

        let slice = readback_buffer.slice(..);
        let (sender, receiver) = mpsc::channel();
        slice.map_async(wgpu::MapMode::Read, move |result| {
            let _ = sender.send(result);
        });
        let _ = device.poll(wgpu::PollType::Wait {
            submission_index: Some(submission),
            timeout: None,
        });
        if receiver.recv().ok()?.is_err() {
            return None;
        }

        let mapped = slice.get_mapped_range();
        let mut tight = vec![0; width as usize * height as usize * 4];
        let tight_bytes_per_row = width as usize * 4;
        for row in 0..height as usize {
            let src = row * padded_bytes_per_row;
            let dst = row * tight_bytes_per_row;
            tight[dst..dst + tight_bytes_per_row].copy_from_slice(&mapped[src..src + tight_bytes_per_row]);
        }
        drop(mapped);
        readback_buffer.unmap();
        for id in &full_output.textures_delta.free {
            self.renderer.free_texture(id);
        }

        Some(ImageItemData::new(ImageSRGBA::from_tightly_packed_bytes(
            width, height, &tight,
        )))
    }

    /// Uploads the full font atlas (which also holds the white pixel used by
    /// solid shapes) the first time this renderer composites. Necessary
    /// because the atlas's initial full delta may already have been consumed
    /// by a [`text_context`] pass whose output nobody rendered; from then on
    /// the per-pass deltas are incremental.
    fn seed_font_texture(&mut self, device: &wgpu::Device, queue: &wgpu::Queue) {
        if self.font_texture_seeded {
            return;
        }
        let image = text_context().fonts(|fonts| fonts.image());
        let delta = egui::epaint::ImageDelta::full(image, egui::TextureOptions::LINEAR);
        self.renderer
            .update_texture(device, queue, egui::TextureId::default(), &delta);
        self.font_texture_seeded = true;
    }

    fn ensure_composite_resources(&mut self, device: &wgpu::Device, width: u32, height: u32) {
        let needs_reallocate = self
            .composite_resources
            .as_ref()
            .is_none_or(|resources| resources.size.width != width || resources.size.height != height);
        if !needs_reallocate {
            return;
        }

        let size = wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        };
        let texture = device.create_texture(&wgpu::TextureDescriptor {
            label: Some("zv annotation composite texture"),
            size,
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu::TextureFormat::Rgba8Unorm,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT
                | wgpu::TextureUsages::COPY_SRC
                | wgpu::TextureUsages::COPY_DST,
            view_formats: &[wgpu::TextureFormat::Rgba8Unorm],
        });
        let view = texture.create_view(&wgpu::TextureViewDescriptor::default());
        let padded_bytes_per_row = (width as usize * 4).next_multiple_of(wgpu::COPY_BYTES_PER_ROW_ALIGNMENT as usize);
        let readback_buffer_size = padded_bytes_per_row * height as usize;
        let readback_buffer = device.create_buffer(&wgpu::BufferDescriptor {
            label: Some("zv annotation composite readback"),
            size: readback_buffer_size as u64,
            usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
            mapped_at_creation: false,
        });
        self.composite_resources = Some(AnnotationCompositeResources {
            size,
            texture,
            view,
            readback_buffer,
            padded_bytes_per_row,
        });
    }
}

impl AnnotationCompositeResources {
    fn upload_base_image(&self, queue: &wgpu::Queue, base: &ImageSRGBA) {
        queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture: &self.texture,
                mip_level: 0,
                origin: wgpu::Origin3d::ZERO,
                aspect: wgpu::TextureAspect::All,
            },
            base.bytes(),
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(base.bytes_per_row() as u32),
                rows_per_image: Some(base.height()),
            },
            self.size,
        );
    }
}

fn hit_handle(
    element: &AnnotationElement,
    widget_pos: egui::Pos2,
    transform: &WidgetToTextureTransform,
    handle_radius_px: f32,
) -> Option<AnnotationHandle> {
    element.handles().iter().copied().find(|&handle| {
        element
            .handle_texture_pos(handle)
            .is_some_and(|pos| transform.texture_to_widget(pos).distance(widget_pos) <= handle_radius_px)
    })
}

fn distance_to_segment(p: egui::Pos2, a: egui::Pos2, b: egui::Pos2) -> f32 {
    let ab = b - a;
    let len_sq = ab.length_sq();
    if len_sq <= f32::EPSILON {
        return p.distance(a);
    }
    let ap = p - a;
    let t = (ap.dot(ab) / len_sq).clamp(0.0, 1.0);
    p.distance(a + ab * t)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn transform() -> WidgetToTextureTransform {
        WidgetToTextureTransform {
            widget_rect: egui::Rect::from_min_size(egui::Pos2::ZERO, egui::vec2(100.0, 100.0)),
            uv_min: egui::Vec2::ZERO,
            uv_max: egui::vec2(1.0, 1.0),
            image_size: [100, 100],
        }
    }

    #[test]
    fn line_handles_are_texture_endpoints() {
        let element = AnnotationElement::Line {
            id: AnnotationId(1),
            segment: LineSegment {
                p1: egui::vec2(0.25, 0.5),
                p2: egui::vec2(0.75, 0.5),
            },
            style: LineStyle::default(),
        };
        assert_eq!(
            element.handle_texture_pos(AnnotationHandle::LineStart),
            Some(egui::vec2(0.25, 0.5))
        );
        assert_eq!(
            element.handle_texture_pos(AnnotationHandle::LineEnd),
            Some(egui::vec2(0.75, 0.5))
        );
        assert_eq!(element.handle_texture_pos(AnnotationHandle::TopLeft), None);
    }

    #[test]
    fn line_hit_test_prefers_selected_handle() {
        let mut document = AnnotationDocument::default();
        let id = AnnotationId(3);
        document.add_element(AnnotationElement::Line {
            id,
            segment: LineSegment {
                p1: egui::vec2(0.2, 0.2),
                p2: egui::vec2(0.8, 0.2),
            },
            style: LineStyle::default(),
        });

        let hit = document
            .hit_test(egui::pos2(20.0, 20.0), &transform(), id, 6.0, 4.0)
            .expect("handle hit");
        assert_eq!(hit.part, AnnotationHitPart::Handle(AnnotationHandle::LineStart));
    }

    #[test]
    fn line_body_can_be_moved() {
        let mut element = AnnotationElement::Line {
            id: AnnotationId(7),
            segment: LineSegment {
                p1: egui::vec2(0.1, 0.2),
                p2: egui::vec2(0.3, 0.4),
            },
            style: LineStyle::default(),
        };
        element.move_by(egui::vec2(0.1, -0.1));
        let AnnotationElement::Line { segment, .. } = element else {
            panic!("expected line");
        };
        assert_eq!(segment.p1, egui::vec2(0.2, 0.1));
        assert_eq!(segment.p2, egui::vec2(0.4, 0.3));
    }

    #[test]
    fn line_hit_test_scales_stroke_width_to_widget_pixels() {
        let mut document = AnnotationDocument::default();
        let id = AnnotationId(9);
        document.add_element(AnnotationElement::Line {
            id,
            segment: LineSegment {
                p1: egui::vec2(0.1, 0.5),
                p2: egui::vec2(0.9, 0.5),
            },
            style: LineStyle {
                stroke: StrokeStyle {
                    width: 10.0,
                    ..Default::default()
                },
                ..Default::default()
            },
        });
        let transform = WidgetToTextureTransform {
            widget_rect: egui::Rect::from_min_size(egui::Pos2::ZERO, egui::vec2(200.0, 200.0)),
            uv_min: egui::Vec2::ZERO,
            uv_max: egui::vec2(1.0, 1.0),
            image_size: [100, 100],
        };

        let hit = document
            .hit_test(egui::pos2(100.0, 113.0), &transform, AnnotationId::default(), 6.0, 4.0)
            .expect("scaled stroke hit");
        assert_eq!(hit.id, id);
        assert_eq!(hit.part, AnnotationHitPart::Body);
    }

    #[test]
    fn rectangle_handles_resize_from_the_opposite_corner() {
        let mut element = AnnotationElement::Rectangle {
            id: AnnotationId(10),
            bounds: BoundingBox {
                min: egui::vec2(0.2, 0.3),
                max: egui::vec2(0.7, 0.8),
            },
            stroke: StrokeStyle::default(),
        };
        element.move_handle_to(AnnotationHandle::TopLeft, egui::vec2(0.1, 0.2));
        let AnnotationElement::Rectangle { bounds, .. } = element else {
            panic!("expected rectangle");
        };
        assert_eq!(bounds.min, egui::vec2(0.1, 0.2));
        assert_eq!(bounds.max, egui::vec2(0.7, 0.8));
    }

    #[test]
    fn rectangle_handle_can_cross_its_fixed_opposite_corner() {
        let mut element = AnnotationElement::Rectangle {
            id: AnnotationId(12),
            bounds: BoundingBox {
                min: egui::vec2(0.2, 0.3),
                max: egui::vec2(0.7, 0.8),
            },
            stroke: StrokeStyle::default(),
        };
        element.move_handle_with_anchor(
            AnnotationHandle::BottomRight,
            egui::vec2(0.1, 0.2),
            egui::vec2(0.2, 0.3),
        );
        let AnnotationElement::Rectangle { bounds, .. } = element else {
            panic!("expected rectangle");
        };
        assert_eq!(bounds.min, egui::vec2(0.1, 0.2));
        assert_eq!(bounds.max, egui::vec2(0.2, 0.3));
    }

    #[test]
    fn ellipse_hit_test_targets_the_border_not_the_center() {
        let mut document = AnnotationDocument::default();
        let id = AnnotationId(11);
        document.add_element(AnnotationElement::Ellipse {
            id,
            bounds: BoundingBox {
                min: egui::vec2(0.2, 0.2),
                max: egui::vec2(0.8, 0.8),
            },
            stroke: StrokeStyle::default(),
        });
        assert!(
            document
                .hit_test(egui::pos2(50.0, 20.0), &transform(), AnnotationId::default(), 6.0, 4.0)
                .is_some()
        );
        assert!(
            document
                .hit_test(egui::pos2(50.0, 50.0), &transform(), AnnotationId::default(), 6.0, 4.0)
                .is_none()
        );
    }

    #[test]
    fn text_uses_bounding_box_handles_and_body_hit_testing() {
        let id = AnnotationId::next();
        let mut document = AnnotationDocument::default();
        document.add_element(AnnotationElement::Text {
            id,
            bounds: BoundingBox {
                min: egui::vec2(0.2, 0.3),
                max: egui::vec2(0.7, 0.8),
            },
            style: TextStyle::default(),
        });

        let hit = document.hit_test(egui::pos2(50.0, 50.0), &transform(), AnnotationId::default(), 6.0, 4.0);
        assert_eq!(hit.map(|hit| hit.part), Some(AnnotationHitPart::Body));
        let handle = document.hit_test(egui::pos2(20.0, 30.0), &transform(), id, 6.0, 4.0);
        assert_eq!(
            handle.map(|hit| hit.part),
            Some(AnnotationHitPart::Handle(AnnotationHandle::TopLeft))
        );
    }

    #[test]
    fn text_move_and_resize_reuse_bounding_box_geometry() {
        let mut element = AnnotationElement::Text {
            id: AnnotationId::next(),
            bounds: BoundingBox {
                min: egui::vec2(0.2, 0.3),
                max: egui::vec2(0.7, 0.8),
            },
            style: TextStyle::default(),
        };
        element.move_by(egui::vec2(0.1, -0.1));
        element.move_handle_to(AnnotationHandle::TopLeft, egui::vec2(0.1, 0.1));
        let AnnotationElement::Text { bounds, .. } = element else {
            unreachable!()
        };
        assert_eq!(bounds.min, egui::vec2(0.1, 0.1));
        assert_eq!(bounds.max, egui::vec2(0.8, 0.7));
    }

    #[test]
    fn fit_text_font_size_uses_limiting_dimension_and_handles_degenerate_input() {
        assert_eq!(
            fit_text_font_size(egui::vec2(200.0, 50.0), 20.0, egui::vec2(100.0, 100.0)),
            10.0
        );
        assert_eq!(
            fit_text_font_size(egui::Vec2::ZERO, 24.0, egui::vec2(100.0, 100.0)),
            24.0
        );
        assert_eq!(
            fit_text_font_size(egui::vec2(10.0, 10.0), 20.0, egui::vec2(1000.0, 1000.0)),
            512.0
        );
    }
}
