//! The visible region of the image: which part of it is on screen, how that
//! region is shaped, and how it moves.
//!
//! This is deliberately independent of painting. `ImageView` holds the state
//! and the commands acting on it, and resolves to a `CellView` per grid cell,
//! which is the only thing the window needs in order to draw.

use eframe::egui;

use crate::viewer::ArrowKey;

/// Which axes hide part of the image and are therefore scrollable. This is a
/// property of the region, not of where it currently sits: an axis stays
/// scrollable at either end of its travel, so arrow keys keep belonging to
/// the view instead of falling through to the image list at the edge.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ScrollableAxes {
    pub horizontal: bool,
    pub vertical: bool,
}

impl ScrollableAxes {
    pub fn any(self) -> bool {
        self.horizontal || self.vertical
    }
}

/// Region state shared by every cell.
#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct Region {
    // Integer zoom level: 1 = the whole visible region, 2 = 2x zoom, etc.
    zoom_factor: u32,
    // Normalized UV coordinate of the zoom center (0..1 each).
    uv_center: egui::Vec2,
    /// Aspect ratio of the visible region, relative to the image's own: 1 is
    /// the whole image, above 1 keeps the full width and crops the height,
    /// below 1 keeps the full height and crops the width.
    ///
    /// `a` makes the window match the region by reshaping the window; `s`
    /// makes the region match the window by writing this ratio. Both are
    /// one-shot commands, so both go stale when the window is resized after.
    region_aspect: f32,
}

impl Default for Region {
    fn default() -> Self {
        Self {
            zoom_factor: 1,
            uv_center: egui::vec2(0.5, 0.5),
            region_aspect: 1.0,
        }
    }
}

impl Region {
    /// Half-extent of the visible region in UV, before zoom.
    ///
    /// `region_aspect` (`k` below) is the region's aspect ratio *divided by
    /// the image's own*, so it says how much wider-than-the-image the region
    /// is, independently of the image's actual proportions. The region is
    /// then the largest rectangle of that shape that still fits in the image,
    /// which means one axis is always full and the other is cropped by `k`:
    ///
    /// - `k == 1`: the region is shaped exactly like the image, so nothing is
    ///   cropped and the half-extent is `(0.5, 0.5)`, the whole image. This is
    ///   the default and what every command other than `s` leaves behind.
    /// - `k > 1`: the region is proportionally wider than the image (a wide
    ///   window showing a tall image). Width is already the limit, so it stays
    ///   full and the height is cropped to `1/k` of the image. A 1000x20000
    ///   image in an 800x600 window gives `k = 1.333/0.05 = 26.7`, so the
    ///   region is the full width by 1/26.7 of the height, and the remaining
    ///   height is scrolled.
    /// - `k < 1`: the mirror case. The region is proportionally taller than
    ///   the image (a tall window showing a wide image), so the height stays
    ///   full and the width is cropped to `k`.
    ///
    /// `min` applies the crop to whichever axis is the cropped one and leaves
    /// the other at 1, so the two cases need no branch. Zooming then divides
    /// both axes equally, preserving whatever shape this produced.
    fn base_half(&self) -> egui::Vec2 {
        // A non-finite or non-positive ratio has no meaningful region; fall
        // back to the whole image rather than producing a NaN region that
        // would poison the clamping below.
        let aspect = if self.region_aspect.is_finite() && self.region_aspect > 0.0 {
            self.region_aspect
        } else {
            1.0
        };
        egui::vec2(aspect.min(1.0), aspect.recip().min(1.0)) * 0.5
    }

    // Compute clamped uv_min/uv_max for the visible sub-region.
    fn uv_region(&self) -> (egui::Vec2, egui::Vec2) {
        let half = self.base_half() / self.zoom_factor as f32;
        let uv0 = self.uv_center - half;
        let uv1 = self.uv_center + half;

        // Clamp so the ROI stays within the image, shifting both edges together.
        // The two terms are mutually exclusive (only one edge can be out of bounds).
        let dx = f32::max(0.0, -uv0.x) + f32::min(0.0, 1.0 - uv1.x);
        let dy = f32::max(0.0, -uv0.y) + f32::min(0.0, 1.0 - uv1.y);
        (uv0 + egui::vec2(dx, dy), uv1 + egui::vec2(dx, dy))
    }
}

/// Where an image is painted inside its cell, and which part of it is visible.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CellView {
    /// Screen rectangle the image is painted into: always the whole cell, as
    /// before. Only the visible region ever changes.
    pub paint_rect: egui::Rect,
    pub uv_min: egui::Vec2,
    pub uv_max: egui::Vec2,
}

impl CellView {
    pub(crate) fn compute(cell_rect: egui::Rect, region: &Region) -> Self {
        let (uv_min, uv_max) = region.uv_region();
        Self {
            paint_rect: cell_rect,
            uv_min,
            uv_max,
        }
    }

    /// Builds a view directly from a region, for tests in other modules that
    /// need a specific crop without going through a whole `ImageView`.
    #[cfg(test)]
    pub fn for_test(paint_rect: egui::Rect, uv_min: egui::Vec2, uv_max: egui::Vec2) -> Self {
        Self {
            paint_rect,
            uv_min,
            uv_max,
        }
    }

    /// A UV threshold would mean different things at different zooms, so the
    /// question is asked in the unit that matters to someone scrolling: how
    /// many screen points of image the axis hides in total.
    pub fn scrollable_axes(&self) -> ScrollableAxes {
        let extent = self.uv_max - self.uv_min;
        let size = self.paint_rect.size();
        let cropped = |extent: f32, side: f32| extent > 0.0 && (1.0 - extent) / extent * side >= MIN_SCROLL_POINTS;
        ScrollableAxes {
            horizontal: cropped(extent.x, size.x),
            vertical: cropped(extent.y, size.y),
        }
    }

    fn center(&self) -> egui::Vec2 {
        self.uv_min + self.half_extent()
    }

    fn half_extent(&self) -> egui::Vec2 {
        (self.uv_max - self.uv_min) * 0.5
    }

    /// UV offset that moves the visible region by `delta` screen points.
    fn uv_per_screen_delta(&self, delta: egui::Vec2) -> egui::Vec2 {
        let size = self.paint_rect.size();
        if size.x <= 0.0 || size.y <= 0.0 {
            return egui::Vec2::ZERO;
        }
        (self.uv_max - self.uv_min) * delta / size
    }
}

/// Below one screen point of hidden image there is nothing worth scrolling
/// to, and a window whose shape only nearly matches the image would otherwise
/// count as scrollable and capture the arrow keys.
const MIN_SCROLL_POINTS: f32 = 1.0;

/// Region aspect ratio that makes the visible region match `cell_size` on
/// screen pixel for pixel, so the image is shown undistorted.
///
/// The region is stretched over the whole cell, so the image is undistorted
/// exactly when the region has the cell's aspect ratio. `region_aspect` is
/// measured relative to the image, so dividing the two aspects gives the
/// ratio directly: a cell shaped like the image yields 1 (nothing cropped),
/// and the further the cell's shape is from the image's, the further from 1
/// the ratio gets, cropping that much more of one axis.
fn region_aspect_matching_cell(cell_size: egui::Vec2, image_size: egui::Vec2) -> Option<f32> {
    if cell_size.x <= 0.0 || cell_size.y <= 0.0 || image_size.x <= 0.0 || image_size.y <= 0.0 {
        return None;
    }
    let cell_aspect = cell_size.x / cell_size.y;
    let image_aspect = image_size.x / image_size.y;
    let aspect = cell_aspect / image_aspect;
    aspect.is_finite().then_some(aspect)
}

/// Screen points an arrow key scrolls the image by.
const KEYBOARD_SCROLL_STEP: f32 = 64.0;

#[derive(Default)]
pub struct ImageView {
    region: Region,
    /// Set by `s`. The ratio it wants needs the laid-out cell size, so it is
    /// resolved at the top of the next frame, before anything is painted.
    pending_aspect_match: bool,
    /// Scroll requested by a keyboard shortcut, in screen points, applied on
    /// the next frame where the view geometry is known.
    pending_pan: egui::Vec2,
    /// Scrollable axes of the last frame's reference cell. Keyboard shortcuts
    /// are collected before the image is laid out, so this is what an arrow
    /// key can consult; it is only ever one frame old.
    scrollable: ScrollableAxes,
}

impl ImageView {
    /// Reshapes the visible region to the window's aspect ratio, so the image
    /// is shown undistorted and whatever no longer fits is scrolled. This is
    /// the dual of `a`, which reshapes the window to the image instead.
    pub fn match_aspect_to_window(&mut self) {
        self.pending_aspect_match = true;
    }

    /// Puts the whole image back in the region. Paired with the window resize
    /// actions that make the window image-shaped, where a leftover crop would
    /// fight the new window.
    pub fn reset_aspect(&mut self) {
        self.region.region_aspect = 1.0;
    }

    /// Scrolls one step in the direction of `arrow` if that axis is
    /// scrollable at all. Returns whether the arrow belonged to the view, so
    /// the caller only navigates the image list for an axis that shows the
    /// whole image. Once an axis scrolls, it keeps the key even at the end of
    /// its travel, where the step simply clamps to nothing.
    pub fn scroll_by_arrow_key(&mut self, arrow: ArrowKey) -> bool {
        let step = KEYBOARD_SCROLL_STEP;
        // Screen-space deltas: the content follows the gesture, so scrolling
        // the view down moves the image up.
        let (scrollable, delta) = match arrow {
            ArrowKey::Down => (self.scrollable.vertical, egui::vec2(0.0, -step)),
            ArrowKey::Up => (self.scrollable.vertical, egui::vec2(0.0, step)),
            ArrowKey::Right => (self.scrollable.horizontal, egui::vec2(-step, 0.0)),
            ArrowKey::Left => (self.scrollable.horizontal, egui::vec2(step, 0.0)),
        };
        if scrollable {
            self.pending_pan += delta;
        }
        scrollable
    }

    /// Zooms in one step around `uv`, unless the region is already down to a
    /// handful of pixels.
    pub fn zoom_in_at(&mut self, uv: egui::Vec2, image_size: egui::Vec2) {
        let min_visible = 16.0 / self.region.zoom_factor as f32;
        if image_size.x > min_visible && image_size.y > min_visible {
            self.region.zoom_factor *= 2;
            self.region.uv_center = uv;
        }
    }

    pub fn zoom_out(&mut self) {
        if self.region.zoom_factor >= 2 {
            self.region.zoom_factor /= 2;
        }
    }

    /// Resolves everything queued against the previous frame's geometry, then
    /// builds the view every cell paints with. Doing it in one pass up front
    /// means a keyboard scroll lands on the frame that follows the key press,
    /// and a zoom click during painting only takes effect on the next frame,
    /// for every cell at once.
    pub fn resolve_cells(
        &mut self,
        cell_rects: &[egui::Rect],
        image_sizes: &[Option<egui::Vec2>],
        reference_index: Option<usize>,
    ) -> Vec<Option<CellView>> {
        let reference_cell = reference_index.and_then(|index| {
            let cell_rect = *cell_rects.get(index)?;
            let image_size = (*image_sizes.get(index)?)?;
            Some((cell_rect, image_size))
        });

        if let Some((cell_rect, image_size)) = reference_cell {
            // `s` reshapes the region, so it has to land before anything that
            // measures the region's scroll room.
            if std::mem::take(&mut self.pending_aspect_match)
                && let Some(aspect) = region_aspect_matching_cell(cell_rect.size(), image_size)
            {
                self.region.region_aspect = aspect;
            }

            let view = CellView::compute(cell_rect, &self.region);
            // Keep the stored center equal to what is actually on screen: a
            // window resize or an `s` can shrink the scroll room around it.
            self.recenter_on(view, view.center());
            let pending_pan = std::mem::replace(&mut self.pending_pan, egui::Vec2::ZERO);
            self.scroll_by(pending_pan, view);
            // Scrolling only moves the region, and the scrollable axes depend
            // on its size, so the view computed above is still current.
            self.scrollable = view.scrollable_axes();
        } else {
            self.pending_pan = egui::Vec2::ZERO;
            self.scrollable = ScrollableAxes::default();
        }

        cell_rects
            .iter()
            .zip(image_sizes)
            .map(|(cell_rect, image_size)| image_size.map(|_| CellView::compute(*cell_rect, &self.region)))
            .collect()
    }

    /// Scrolls the view by `delta` screen points: content follows the gesture,
    /// so a positive delta moves the image right/down and the view left/up.
    pub fn scroll_by(&mut self, delta: egui::Vec2, view: CellView) {
        if delta == egui::Vec2::ZERO {
            return;
        }
        self.recenter_on(view, view.center() - view.uv_per_screen_delta(delta));
    }

    /// The single writer of the region's center. It always stores a center the
    /// view can actually show, so a gesture past an edge never builds up an
    /// offset that has to be scrolled back off before the view moves again,
    /// and a window resize that shrinks the scroll room cannot strand it.
    fn recenter_on(&mut self, view: CellView, center: egui::Vec2) {
        let half = view.half_extent();
        self.region.uv_center = egui::vec2(
            center.x.clamp(half.x, 1.0 - half.x),
            center.y.clamp(half.y, 1.0 - half.y),
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TALL: egui::Vec2 = egui::vec2(1000.0, 20000.0);
    const WIDE: egui::Vec2 = egui::vec2(20000.0, 1000.0);

    fn cell(width: f32, height: f32) -> egui::Rect {
        egui::Rect::from_min_size(egui::pos2(0.0, 0.0), egui::vec2(width, height))
    }

    /// A window showing `image_size` in a `cell_rect` cell, after `s`.
    fn matched_window(cell_rect: egui::Rect, image_size: egui::Vec2) -> ImageView {
        let region = Region {
            region_aspect: region_aspect_matching_cell(cell_rect.size(), image_size).expect("finite aspect"),
            ..Default::default()
        };
        ImageView {
            region,
            ..Default::default()
        }
    }

    /// The view that window shows.
    fn matched_view(cell_rect: egui::Rect, image_size: egui::Vec2) -> CellView {
        CellView::compute(cell_rect, &matched_window(cell_rect, image_size).region)
    }

    fn assert_near(a: f32, b: f32) {
        assert!((a - b).abs() < 1.0e-4, "expected {a} to be nearly equal to {b}");
    }

    #[test]
    fn matching_a_window_already_shaped_like_the_image_changes_nothing() {
        let cell_rect = cell(500.0, 10000.0);
        assert_near(
            region_aspect_matching_cell(cell_rect.size(), TALL).expect("finite aspect"),
            1.0,
        );
        assert_eq!(
            matched_view(cell_rect, TALL),
            CellView::compute(cell_rect, &Region::default())
        );
    }

    #[test]
    fn matching_crops_a_tall_image_vertically() {
        let view = matched_view(cell(800.0, 600.0), TALL);
        // The cell is 800x600, so the widest region that fits is the full
        // 1000px width by 750px of height: full width, a sliver of the height.
        assert_near(view.uv_max.x - view.uv_min.x, 1.0);
        assert_near(view.uv_max.y - view.uv_min.y, 750.0 / 20000.0);
        let scrollable = view.scrollable_axes();
        assert!(scrollable.vertical);
        assert!(!scrollable.horizontal);
    }

    #[test]
    fn matching_crops_a_wide_image_horizontally() {
        let view = matched_view(cell(600.0, 800.0), WIDE);
        assert_near(view.uv_max.y - view.uv_min.y, 1.0);
        assert_near(view.uv_max.x - view.uv_min.x, 750.0 / 20000.0);
        let scrollable = view.scrollable_axes();
        assert!(scrollable.horizontal);
        assert!(!scrollable.vertical);
    }

    #[test]
    fn a_matched_region_shows_the_image_undistorted() {
        for (cell_rect, image_size) in [
            (cell(800.0, 600.0), TALL),
            (cell(600.0, 800.0), WIDE),
            (cell(1000.0, 1000.0), TALL),
            (cell(133.0, 977.0), WIDE),
        ] {
            let view = matched_view(cell_rect, image_size);
            // Image pixels per screen point must match on both axes, or the
            // image is stretched; that is the whole point of matching.
            let pixels_per_point_x = (view.uv_max.x - view.uv_min.x) * image_size.x / cell_rect.width();
            let pixels_per_point_y = (view.uv_max.y - view.uv_min.y) * image_size.y / cell_rect.height();
            assert_near(pixels_per_point_x, pixels_per_point_y);
        }
    }

    #[test]
    fn zoom_subdivides_whichever_region_the_aspect_produced() {
        let cell_rect = cell(800.0, 600.0);
        let region = Region {
            zoom_factor: 4,
            ..matched_window(cell_rect, TALL).region
        };
        let view = CellView::compute(cell_rect, &region);
        assert_near(view.uv_max.x - view.uv_min.x, 0.25);
        assert_near(view.uv_max.y - view.uv_min.y, 750.0 / 20000.0 / 4.0);
    }

    #[test]
    fn scrolling_past_an_edge_does_not_accumulate_a_hidden_offset() {
        let cell_rect = cell(800.0, 600.0);
        let mut window = matched_window(cell_rect, TALL);
        let view = CellView::compute(cell_rect, &window.region);

        // Slam the view against the top edge, far past what the image allows.
        window.scroll_by(egui::vec2(0.0, 100_000.0), view);
        let at_top = CellView::compute(cell_rect, &window.region);
        assert_near(at_top.uv_min.y, 0.0);

        // One step back down must move the view immediately.
        window.scroll_by(egui::vec2(0.0, -60.0), at_top);
        let after = CellView::compute(cell_rect, &window.region);
        assert!(after.uv_min.y > 0.0, "view should have left the top edge");
    }

    #[test]
    fn an_arrow_key_belongs_to_the_view_for_as_long_as_its_axis_scrolls() {
        let cell_rect = cell(800.0, 600.0);
        let mut view = matched_window(cell_rect, TALL);
        view.scrollable = CellView::compute(cell_rect, &view.region).scrollable_axes();

        assert!(view.scroll_by_arrow_key(ArrowKey::Down));
        assert_eq!(view.pending_pan, egui::vec2(0.0, -KEYBOARD_SCROLL_STEP));

        // The horizontal axis shows the whole image, so those keys stay free
        // for the image list.
        assert!(!view.scroll_by_arrow_key(ArrowKey::Left));
        assert!(!view.scroll_by_arrow_key(ArrowKey::Right));

        // Pin the region to the very bottom: a scrollable axis must keep its
        // keys there too, rather than spilling over into the next image.
        view.scroll_by(egui::vec2(0.0, -1.0e6), CellView::compute(cell_rect, &view.region));
        let at_bottom = CellView::compute(cell_rect, &view.region);
        assert!(at_bottom.uv_max.y > 1.0 - 1.0e-4);

        assert!(view.scroll_by_arrow_key(ArrowKey::Down));
        view.scroll_by(view.pending_pan, at_bottom);
        assert_eq!(
            CellView::compute(cell_rect, &view.region),
            at_bottom,
            "the view should simply stay put"
        );
    }

    #[test]
    fn the_visible_region_is_a_fixed_point_of_recentering() {
        // `recenter_on` rewrites `uv_center` from the region every frame. If
        // that round trip drifted, the region would change on every idle
        // frame, so the minimap would never fade and its repaint request
        // would spin forever.
        let cell_rect = cell(800.0, 600.0);
        let mut window = matched_window(cell_rect, TALL);
        window.scroll_by(egui::vec2(0.0, -137.0), CellView::compute(cell_rect, &window.region));

        let settled = CellView::compute(cell_rect, &window.region);
        for _ in 0..200 {
            let view = CellView::compute(cell_rect, &window.region);
            assert_eq!(view, settled, "an idle frame must not move the region");
            window.recenter_on(view, view.center());
        }
    }

    #[test]
    fn a_degenerate_or_invalid_aspect_falls_back_to_the_whole_image() {
        // The default region is the whole image, and anything that cannot
        // produce a meaningful ratio must land back on it rather than on a
        // NaN region.
        assert_eq!(Region::default().base_half(), egui::vec2(0.5, 0.5));
        assert_eq!(region_aspect_matching_cell(egui::Vec2::ZERO, TALL), None);
        assert_eq!(
            region_aspect_matching_cell(egui::vec2(800.0, 600.0), egui::Vec2::ZERO),
            None
        );
        for aspect in [0.0, -1.0, f32::NAN, f32::INFINITY] {
            let region = Region {
                region_aspect: aspect,
                ..Default::default()
            };
            assert_eq!(region.base_half(), egui::vec2(0.5, 0.5), "aspect {aspect}");
        }
    }
}
