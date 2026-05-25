use std::collections::{BTreeMap, HashMap, VecDeque};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{self, Receiver, TryRecvError};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use crate::color_image::{ImageSRGBA, PixelSRGBA};
use crate::image_io::load_rgba_image;
use crate::image_item_data::ImageItemData;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub struct ImageId(u64);

pub struct ImageListRow<'a> {
    pub index: usize,
    pub selected: bool,
    pub name: &'a str,
    pub source_path: Option<&'a Path>,
    pub size: Option<(u32, u32)>,
}

#[derive(Clone)]
pub struct SelectedImageView {
    pub id: ImageId,
    pub name: String,
    pub data: Option<Arc<Mutex<ImageItemData>>>,
    pub error: Option<String>,
}

pub struct ImageLoadTiming {
    pub path: PathBuf,
    pub elapsed: Duration,
    pub succeeded: bool,
}

pub struct ImageList {
    items: Vec<ImageItem>,
    selected_id: ImageId,
    filter_text: String,
    cache: ImageItemCache,
    pending_preloads: HashMap<ImageId, Receiver<PreloadResult>>,
}

struct ImageItem {
    id: ImageId,
    source_image_path: Option<PathBuf>,
    pretty_name: String,
    metadata: Option<(u32, u32)>,
    error: Option<String>,
}

struct LoadDataResult {
    data: Option<Arc<Mutex<ImageItemData>>>,
    timing: Option<ImageLoadTiming>,
}

struct PreloadResult {
    id: ImageId,
    path: PathBuf,
    elapsed: Duration,
    result: Result<ImageSRGBA, String>,
}

struct ImageItemCache {
    max_size: usize,
    entries: HashMap<ImageId, Arc<Mutex<ImageItemData>>>,
    lru: VecDeque<ImageId>,
}

impl ImageItemCache {
    fn new(max_size: usize) -> Self {
        Self {
            max_size,
            entries: HashMap::new(),
            lru: VecDeque::new(),
        }
    }

    fn get_cached(&self, id: ImageId) -> Option<Arc<Mutex<ImageItemData>>> {
        self.entries.get(&id).cloned()
    }

    fn get(&mut self, id: ImageId) -> Option<Arc<Mutex<ImageItemData>>> {
        let data = self.entries.get(&id).cloned()?;
        self.touch(id);
        Some(data)
    }

    fn put(&mut self, id: ImageId, data: Arc<Mutex<ImageItemData>>) {
        self.entries.insert(id, data);
        self.touch(id);
        while self.entries.len() > self.max_size {
            let Some(oldest) = self.lru.pop_front() else {
                break;
            };
            if oldest != id {
                self.entries.remove(&oldest);
            }
        }
    }

    fn touch(&mut self, id: ImageId) {
        self.lru.retain(|&existing| existing != id);
        self.lru.push_back(id);
    }

    #[cfg(test)]
    fn contains(&self, id: ImageId) -> bool {
        self.entries.contains_key(&id)
    }
}

impl ImageList {
    pub fn new(paths: Vec<PathBuf>) -> Self {
        let mut items = if paths.is_empty() {
            vec![ImageItem::default_image(ImageId(1))]
        } else {
            paths
                .into_iter()
                .enumerate()
                .map(|(index, path)| ImageItem::from_path(ImageId(index as u64 + 1), path))
                .collect::<Vec<_>>()
        };

        refresh_pretty_names(&mut items);
        let selected_id = items.first().map(|item| item.id).unwrap_or(ImageId(0));

        Self {
            items,
            selected_id,
            filter_text: String::new(),
            cache: ImageItemCache::new(8),
            pending_preloads: HashMap::new(),
        }
    }

    pub fn filter_text(&self) -> &str {
        &self.filter_text
    }

    pub fn visible_rows(&self) -> impl Iterator<Item = ImageListRow<'_>> {
        self.items.iter().enumerate().filter_map(|(index, item)| {
            self.item_enabled(index).then_some(ImageListRow {
                index,
                selected: item.id == self.selected_id,
                name: &item.pretty_name,
                source_path: item.source_image_path.as_deref(),
                size: item.metadata,
            })
        })
    }

    pub fn set_filter(&mut self, filter_text: String) {
        if self.filter_text == filter_text {
            return;
        }
        self.filter_text = filter_text;
        if !self.selected_index().is_some_and(|index| self.item_enabled(index)) {
            if let Some(index) = self.enabled_indices().first().copied() {
                self.selected_id = self.items[index].id;
            }
        }
    }

    pub fn select_index(&mut self, index: usize) {
        if index < self.items.len() && self.item_enabled(index) {
            self.selected_id = self.items[index].id;
        }
    }

    pub fn select_relative(&mut self, offset: isize) {
        let enabled = self.enabled_indices();
        if enabled.is_empty() {
            return;
        }

        let current_enabled_index = self
            .selected_index()
            .and_then(|selected| enabled.iter().position(|&index| index == selected))
            .unwrap_or(0);
        let count = enabled.len() as isize;
        let next = (current_enabled_index as isize + offset).rem_euclid(count) as usize;
        self.selected_id = self.items[enabled[next]].id;
    }

    pub fn move_item(&mut self, from: usize, to: usize) {
        if from >= self.items.len() {
            return;
        }

        let mut target = to.min(self.items.len());
        if from == target || from + 1 == target {
            return;
        }

        let selected_id = self.selected_id;
        let item = self.items.remove(from);
        if from < target {
            target -= 1;
        }
        self.items.insert(target, item);
        self.selected_id = selected_id;
    }

    pub fn poll_preloads(&mut self) {
        let pending_ids = self.pending_preloads.keys().copied().collect::<Vec<_>>();
        for id in pending_ids {
            let Some(receiver) = self.pending_preloads.get(&id) else {
                continue;
            };
            match receiver.try_recv() {
                Ok(result) => {
                    self.pending_preloads.remove(&id);
                    self.apply_preload_result(result);
                }
                Err(TryRecvError::Empty) => {}
                Err(TryRecvError::Disconnected) => {
                    self.pending_preloads.remove(&id);
                }
            }
        }
    }

    pub fn ensure_selected_loaded(&mut self) -> Option<ImageLoadTiming> {
        self.poll_preloads();
        let index = self.selected_index()?;
        if self.pending_preloads.contains_key(&self.items[index].id) {
            return None;
        }
        self.get_data_for_index(index).and_then(|result| result.timing)
    }

    pub fn preload_next_from_selection(&mut self, on_done: impl FnOnce() + Send + 'static) -> bool {
        self.poll_preloads();
        let enabled = self.enabled_indices();
        if enabled.len() < 2 {
            return false;
        }

        let current_enabled_index = self
            .selected_index()
            .and_then(|selected| enabled.iter().position(|&index| index == selected))
            .unwrap_or(0);
        let next_enabled_index = (current_enabled_index + 1) % enabled.len();
        let next_index = enabled[next_enabled_index];
        self.start_preload_for_index(next_index, on_done)
    }

    pub fn selected_view(&self) -> Option<SelectedImageView> {
        let item = self.selected_item()?;
        Some(SelectedImageView {
            id: item.id,
            name: item.pretty_name.clone(),
            data: self.cache.get_cached(item.id),
            error: item.error.clone(),
        })
    }


    fn selected_item(&self) -> Option<&ImageItem> {
        self.items.iter().find(|item| item.id == self.selected_id)
    }

    fn selected_index(&self) -> Option<usize> {
        self.items.iter().position(|item| item.id == self.selected_id)
    }

    fn enabled_indices(&self) -> Vec<usize> {
        self.items
            .iter()
            .enumerate()
            .filter_map(|(index, _)| self.item_enabled(index).then_some(index))
            .collect()
    }

    fn item_enabled(&self, index: usize) -> bool {
        let Some(item) = self.items.get(index) else {
            return false;
        };
        let filter = self.filter_text.trim();
        filter.is_empty() || item.pretty_name.to_lowercase().contains(&filter.to_lowercase())
    }

    fn get_data_for_index(&mut self, index: usize) -> Option<LoadDataResult> {
        let item = self.items.get_mut(index)?;
        if let Some(data) = self.cache.get(item.id) {
            return Some(LoadDataResult {
                data: Some(data),
                timing: None,
            });
        }
        if item.error.is_some() {
            return None;
        }

        let loaded = item.load_data()?;
        if let Some(data) = loaded.data.as_ref() {
            self.cache.put(item.id, data.clone());
        }
        Some(loaded)
    }

    fn start_preload_for_index(&mut self, index: usize, on_done: impl FnOnce() + Send + 'static) -> bool {
        let Some(item) = self.items.get(index) else {
            return false;
        };
        if self.cache.get_cached(item.id).is_some()
            || self.pending_preloads.contains_key(&item.id)
            || item.error.is_some()
        {
            return false;
        }
        let Some(path) = item.source_image_path.clone() else {
            let data = Arc::new(Mutex::new(ImageItemData::new(default_image())));
            self.cache.put(item.id, data);
            return true;
        };

        let id = item.id;
        let (sender, receiver) = mpsc::channel();
        std::thread::spawn(move || {
            let start = Instant::now();
            let result = load_rgba_image(&path).map_err(|err| format!("{err:#}"));
            let _ = sender.send(PreloadResult {
                id,
                path,
                elapsed: start.elapsed(),
                result,
            });
            on_done();
        });
        self.pending_preloads.insert(id, receiver);
        true
    }

    fn apply_preload_result(&mut self, result: PreloadResult) {
        let Some(index) = self.items.iter().position(|item| item.id == result.id) else {
            return;
        };
        match result.result {
            Ok(image) => {
                self.items[index].metadata = Some((image.width(), image.height()));
                self.cache
                    .put(result.id, Arc::new(Mutex::new(ImageItemData::new(image))));
                tracing::debug!(
                    elapsed_ms = result.elapsed.as_millis(),
                    image = %result.path.display(),
                    "preloaded image"
                );
            }
            Err(error) => {
                self.items[index].error = Some(error);
                tracing::debug!(
                    elapsed_ms = result.elapsed.as_millis(),
                    image = %result.path.display(),
                    "image preload failed"
                );
            }
        }
    }
}

impl ImageItem {
    fn from_path(id: ImageId, path: PathBuf) -> Self {
        let metadata = ::image::image_dimensions(&path).ok();
        Self {
            id,
            pretty_name: display_name(&path),
            source_image_path: Some(path),
            metadata,
            error: None,
        }
    }

    fn default_image(id: ImageId) -> Self {
        Self {
            id,
            source_image_path: None,
            pretty_name: "<<default>>".to_owned(),
            metadata: Some((256, 256)),
            error: None,
        }
    }

    fn load_data(&mut self) -> Option<LoadDataResult> {
        let Some(path) = self.source_image_path.as_ref() else {
            return Some(LoadDataResult {
                data: Some(Arc::new(Mutex::new(ImageItemData::new(default_image())))),
                timing: None,
            });
        };

        let start = Instant::now();
        match load_rgba_image(path) {
            Ok(image) => {
                self.metadata = Some((image.width(), image.height()));
                return Some(LoadDataResult {
                    data: Some(Arc::new(Mutex::new(ImageItemData::new(image)))),
                    timing: Some(ImageLoadTiming {
                        path: path.clone(),
                        elapsed: start.elapsed(),
                        succeeded: true,
                    }),
                });
            }
            Err(err) => self.error = Some(format!("{err:#}")),
        }
        Some(LoadDataResult {
            data: None,
            timing: Some(ImageLoadTiming {
                path: path.clone(),
                elapsed: start.elapsed(),
                succeeded: false,
            }),
        })
    }
}

fn default_image() -> ImageSRGBA {
    let mut image = ImageSRGBA::new(256, 256);
    let width = image.width();
    let height = image.height();
    for row in 0..height {
        if let Some(row_pixels) = image.row_mut(row) {
            for col in 0..width {
                row_pixels[col as usize] = PixelSRGBA {
                    r: row as u8,
                    g: col as u8,
                    b: (row + col) as u8,
                    a: 255,
                };
            }
        }
    }
    image
}

fn display_name(path: &Path) -> String {
    path.file_name()
        .and_then(|name| name.to_str())
        .map(ToOwned::to_owned)
        .unwrap_or_else(|| path.display().to_string())
}

fn refresh_pretty_names(items: &mut [ImageItem]) {
    for item in items.iter_mut() {
        if let Some(path) = &item.source_image_path {
            item.pretty_name = display_name(path);
        }
    }

    let mut grouped_names: BTreeMap<String, Vec<usize>> = BTreeMap::new();
    for (index, item) in items.iter().enumerate() {
        let Some(path) = &item.source_image_path else {
            continue;
        };
        grouped_names.entry(display_name(path)).or_default().push(index);
    }

    for path_indices in grouped_names.values() {
        if path_indices.len() < 2 {
            continue;
        }

        let path_names = path_indices
            .iter()
            .filter_map(|&index| items[index].source_image_path.as_ref())
            .map(|path| path.display().to_string())
            .collect::<Vec<_>>();
        let unique_names = unique_pretty_names(&path_names);
        for (&index, name) in path_indices.iter().zip(unique_names) {
            items[index].pretty_name = name;
        }
    }
}

fn unique_pretty_names(path_strs: &[String]) -> Vec<String> {
    let paths = path_strs
        .iter()
        .map(|path| path_components(Path::new(path)))
        .collect::<Vec<_>>();
    let mut names = paths
        .iter()
        .map(|components| components.last().cloned().unwrap_or_default())
        .collect::<Vec<_>>();

    let root_entries = paths
        .iter()
        .enumerate()
        .filter_map(|(path_index, components)| (!components.is_empty()).then_some((path_index, components.len() - 1)))
        .collect::<Vec<_>>();
    for edge_entries in build_component_edges(&paths, &root_entries).values() {
        build_unique_pretty_names(&paths, edge_entries, &mut names, false);
    }
    names
}

fn build_unique_pretty_names(
    paths: &[Vec<String>],
    entries: &[(usize, usize)],
    names: &mut [String],
    parent_skipped: bool,
) {
    let edges = build_component_edges(paths, entries);

    if edges.is_empty() {
        if entries.len() > 1 {
            for &(path_index, _) in entries {
                if let Some(first_component) = paths[path_index].first() {
                    prepend(&mut names[path_index], &format!("{first_component}/"));
                }
            }
        }
        return;
    }

    if entries.len() == 1 {
        if !parent_skipped {
            prepend(&mut names[entries[0].0], "...");
        }
        return;
    }

    if edges.len() == 1 {
        if !parent_skipped {
            for &(path_index, _) in entries {
                prepend(&mut names[path_index], ".../");
            }
        }
        for edge_indices in edges.values() {
            build_unique_pretty_names(paths, edge_indices, names, true);
        }
        return;
    }

    for (component, edge_indices) in edges {
        for &(path_index, _) in &edge_indices {
            prepend(&mut names[path_index], &format!("{component}/"));
        }
        build_unique_pretty_names(paths, &edge_indices, names, false);
    }
}

fn build_component_edges(paths: &[Vec<String>], entries: &[(usize, usize)]) -> BTreeMap<String, Vec<(usize, usize)>> {
    let mut edges = BTreeMap::new();
    for &(path_index, component_index) in entries {
        if component_index == 0 {
            continue;
        }
        edges
            .entry(paths[path_index][component_index].clone())
            .or_insert_with(Vec::new)
            .push((path_index, component_index - 1));
    }
    edges
}

fn prepend(s: &mut String, prefix: &str) {
    s.insert_str(0, prefix);
}

fn path_components(path: &Path) -> Vec<String> {
    path.components()
        .map(|component| component.as_os_str().to_string_lossy().into_owned())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn list(names: &[&str]) -> ImageList {
        ImageList::new(names.iter().map(PathBuf::from).collect())
    }

    fn write_test_png(name: &str, rgba: [u8; 4]) -> PathBuf {
        let path = std::env::temp_dir().join(format!("zv-image-list-test-{}-{name}.png", std::process::id()));
        let image = ::image::RgbaImage::from_pixel(1, 1, ::image::Rgba(rgba));
        image.save(&path).expect("write test png");
        path
    }

    fn visible_names(images: &ImageList) -> Vec<&str> {
        images.visible_rows().map(|row| row.name).collect()
    }

    fn selected_visible_index(images: &ImageList) -> Option<usize> {
        images
            .visible_rows()
            .enumerate()
            .find_map(|(visible_index, row)| row.selected.then_some(visible_index))
    }

    #[test]
    fn default_image_when_empty() {
        let images = ImageList::new(Vec::new());
        let rows = images.visible_rows().collect::<Vec<_>>();
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].name, "<<default>>");
        assert_eq!(rows[0].source_path, None);
        assert_eq!(rows[0].size, Some((256, 256)));
    }

    #[test]
    fn initializes_from_paths() {
        let images = list(&["/tmp/a.png", "/tmp/b.png"]);
        assert_eq!(visible_names(&images), ["a.png", "b.png"]);
        assert_eq!(selected_visible_index(&images), Some(0));
    }

    #[test]
    fn selects_by_global_index() {
        let mut images = list(&["a.png", "b.png", "c.png"]);
        images.select_index(2);
        assert_eq!(selected_visible_index(&images), Some(2));
    }

    #[test]
    fn navigates_over_all_rows() {
        let mut images = list(&["a.png", "b.png", "c.png"]);
        images.select_relative(1);
        assert_eq!(images.selected_index(), Some(1));
        images.select_relative(-1);
        assert_eq!(images.selected_index(), Some(0));
        images.select_relative(-1);
        assert_eq!(images.selected_index(), Some(2));
    }

    #[test]
    fn filters_case_insensitive_substrings() {
        let mut images = list(&["Cat.png", "dog.png", "catalog.jpg"]);
        images.set_filter("cat".to_owned());
        assert_eq!(visible_names(&images), ["Cat.png", "catalog.jpg"]);
    }

    #[test]
    fn duplicate_basenames_include_distinguishing_parent() {
        let images = list(&["/tmp/left/frame.png", "/tmp/right/frame.png", "/tmp/other/mask.png"]);
        assert_eq!(
            visible_names(&images),
            ["...left/frame.png", "...right/frame.png", "mask.png"]
        );
    }

    #[test]
    fn duplicate_basenames_skip_common_middle_components() {
        let images = list(&[
            "/common/folderA/same/file1.png",
            "/common/folderB/same/file1.png",
            "/common/folderA/same/file2.png",
            "/common/folderB/same/file2.png",
        ]);
        assert_eq!(
            visible_names(&images),
            [
                "...folderA/.../file1.png",
                "...folderB/.../file1.png",
                "...folderA/.../file2.png",
                "...folderB/.../file2.png",
            ]
        );
    }

    #[test]
    fn navigation_skips_filtered_rows() {
        let mut images = list(&["a.png", "b.png", "aa.png"]);
        images.set_filter("a".to_owned());
        images.select_relative(1);
        assert_eq!(images.selected_index(), Some(2));
    }

    #[test]
    fn move_preserves_selected_id_when_moving_down() {
        let mut images = list(&["a.png", "b.png", "c.png"]);
        images.select_index(1);
        images.move_item(1, 3);
        assert_eq!(visible_names(&images), ["a.png", "c.png", "b.png"]);
        assert_eq!(selected_visible_index(&images), Some(2));
    }

    #[test]
    fn move_preserves_selected_id_when_moving_up() {
        let mut images = list(&["a.png", "b.png", "c.png"]);
        images.select_index(2);
        images.move_item(2, 0);
        assert_eq!(visible_names(&images), ["c.png", "a.png", "b.png"]);
        assert_eq!(selected_visible_index(&images), Some(0));
    }

    #[test]
    fn selected_image_loads_through_cache() {
        let path = write_test_png("selected", [1, 2, 3, 255]);
        let mut images = ImageList::new(vec![path.clone()]);

        assert!(images.selected_view().unwrap().data.is_none());
        let timing = images.ensure_selected_loaded().expect("load selected");
        assert!(timing.succeeded);

        let selected = images.selected_view().unwrap();
        assert!(selected.data.is_some());
        assert!(images.cache.contains(ImageId(1)));

        let _ = fs::remove_file(path);
    }

    #[test]
    fn preloads_next_enabled_image() {
        let first = write_test_png("first", [1, 0, 0, 255]);
        let second = write_test_png("second", [2, 0, 0, 255]);
        let third = write_test_png("third", [3, 0, 0, 255]);
        let mut images = ImageList::new(vec![first.clone(), second.clone(), third.clone()]);

        images.ensure_selected_loaded();
        assert!(images.preload_next_from_selection(|| {}));
        for _ in 0..100 {
            images.poll_preloads();
            if images.cache.contains(ImageId(2)) {
                break;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        assert!(images.cache.contains(ImageId(1)));
        assert!(images.cache.contains(ImageId(2)));
        assert!(!images.cache.contains(ImageId(3)));

        let _ = fs::remove_file(first);
        let _ = fs::remove_file(second);
        let _ = fs::remove_file(third);
    }
}
