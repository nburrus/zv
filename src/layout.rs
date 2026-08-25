#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LayoutConfig {
    pub rows: usize,
    pub cols: usize,
}

/// Maximum number of images shown by the automatic mosaic.
pub const MAX_MOSAIC_IMAGES: usize = 64;

impl LayoutConfig {
    pub const fn image_count(self) -> usize {
        self.rows * self.cols
    }
}

impl Default for LayoutConfig {
    fn default() -> Self {
        Self { rows: 1, cols: 1 }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct LayoutMenuEntry {
    pub label: &'static str,
    pub shortcut: Option<&'static str>,
    pub config: LayoutConfig,
}

pub const LAYOUT_MENU_ENTRIES: &[LayoutMenuEntry] = &[
    LayoutMenuEntry {
        label: "Single image",
        shortcut: Some("1"),
        config: LayoutConfig { rows: 1, cols: 1 },
    },
    LayoutMenuEntry {
        label: "2 columns",
        shortcut: Some("2"),
        config: LayoutConfig { rows: 1, cols: 2 },
    },
    LayoutMenuEntry {
        label: "3 columns",
        shortcut: Some("3"),
        config: LayoutConfig { rows: 1, cols: 3 },
    },
    LayoutMenuEntry {
        label: "2 rows",
        shortcut: None,
        config: LayoutConfig { rows: 2, cols: 1 },
    },
    LayoutMenuEntry {
        label: "3 rows",
        shortcut: None,
        config: LayoutConfig { rows: 3, cols: 1 },
    },
    LayoutMenuEntry {
        label: "2x2",
        shortcut: Some("4"),
        config: LayoutConfig { rows: 2, cols: 2 },
    },
    LayoutMenuEntry {
        label: "2x3",
        shortcut: Some("5/6"),
        config: LayoutConfig { rows: 2, cols: 3 },
    },
    LayoutMenuEntry {
        label: "3x2",
        shortcut: None,
        config: LayoutConfig { rows: 3, cols: 2 },
    },
    LayoutMenuEntry {
        label: "2x4",
        shortcut: Some("7/8"),
        config: LayoutConfig { rows: 2, cols: 4 },
    },
    LayoutMenuEntry {
        label: "4x2",
        shortcut: None,
        config: LayoutConfig { rows: 4, cols: 2 },
    },
    LayoutMenuEntry {
        label: "3x3",
        shortcut: Some("9"),
        config: LayoutConfig { rows: 3, cols: 3 },
    },
    LayoutMenuEntry {
        label: "3x4",
        shortcut: None,
        config: LayoutConfig { rows: 3, cols: 4 },
    },
    LayoutMenuEntry {
        label: "4x3",
        shortcut: None,
        config: LayoutConfig { rows: 4, cols: 3 },
    },
];

pub fn best_layout_for_image_count(num_images: usize, max_images: usize, target_aspect_ratio: f32) -> LayoutConfig {
    if num_images == 0 || max_images == 0 {
        return LayoutConfig::default();
    }

    let capped = num_images.min(max_images);
    let mut best = LayoutConfig::default();
    let mut best_waste = usize::MAX;
    let mut best_aspect_error = f32::INFINITY;

    for rows in 1..=capped {
        for cols in 1..=capped {
            let layout_size = rows * cols;
            if layout_size < capped || layout_size > max_images {
                continue;
            }

            let waste = layout_size - capped;
            let aspect_ratio = cols as f32 / rows as f32;
            let aspect_error = (aspect_ratio / target_aspect_ratio).ln().abs();
            if waste < best_waste
                || (waste == best_waste && aspect_error < best_aspect_error)
                || (waste == best_waste && aspect_error == best_aspect_error && layout_size < best.image_count())
            {
                best = LayoutConfig { rows, cols };
                best_waste = waste;
                best_aspect_error = aspect_error;
            }
        }
    }

    best
}

pub fn shortcut_layout_for_image_count(num_images: usize) -> LayoutConfig {
    match num_images {
        1 => LayoutConfig { rows: 1, cols: 1 },
        2 => LayoutConfig { rows: 1, cols: 2 },
        3 => LayoutConfig { rows: 1, cols: 3 },
        4 => LayoutConfig { rows: 2, cols: 2 },
        5 | 6 => LayoutConfig { rows: 2, cols: 3 },
        7 | 8 => LayoutConfig { rows: 2, cols: 4 },
        9 => LayoutConfig { rows: 3, cols: 3 },
        _ => best_layout_for_image_count(num_images, 128, 4.0 / 3.0),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn best_layout_matches_cpp_for_six_images() {
        assert_eq!(
            best_layout_for_image_count(6, 128, 4.0 / 3.0),
            LayoutConfig { rows: 2, cols: 3 }
        );
    }

    #[test]
    fn automatic_mosaic_respects_its_image_limit() {
        let layout = best_layout_for_image_count(1_000, MAX_MOSAIC_IMAGES, 4.0 / 3.0);

        assert_eq!(layout, LayoutConfig { rows: 8, cols: 8 });
        assert_eq!(layout.image_count(), MAX_MOSAIC_IMAGES);
    }

    #[test]
    fn shortcut_layouts_match_cpp() {
        assert_eq!(shortcut_layout_for_image_count(1), LayoutConfig { rows: 1, cols: 1 });
        assert_eq!(shortcut_layout_for_image_count(2), LayoutConfig { rows: 1, cols: 2 });
        assert_eq!(shortcut_layout_for_image_count(3), LayoutConfig { rows: 1, cols: 3 });
        assert_eq!(shortcut_layout_for_image_count(4), LayoutConfig { rows: 2, cols: 2 });
        assert_eq!(shortcut_layout_for_image_count(5), LayoutConfig { rows: 2, cols: 3 });
        assert_eq!(shortcut_layout_for_image_count(6), LayoutConfig { rows: 2, cols: 3 });
        assert_eq!(shortcut_layout_for_image_count(7), LayoutConfig { rows: 2, cols: 4 });
        assert_eq!(shortcut_layout_for_image_count(8), LayoutConfig { rows: 2, cols: 4 });
        assert_eq!(shortcut_layout_for_image_count(9), LayoutConfig { rows: 3, cols: 3 });
    }
}
