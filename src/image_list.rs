use std::collections::{BTreeMap, HashMap, HashSet, VecDeque};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc::{self, Receiver, TryRecvError};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime};

use crate::color_image::{ImageSRGBA, PixelSRGBA};
use crate::image_io::{load_rgba_image, load_rgba_image_from_memory};
use crate::image_item_data::ImageItemData;
use crate::modified_image::ModifiedImage;
use crate::networking::RemoteImageRef;
use crate::protocol::ImageOffer;

// A large grid should stay responsive without decoding every visible image at
// once and multiplying peak memory use.
const MAX_CONCURRENT_IMAGE_LOADS: usize = 4;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash)]
pub struct ImageId(u64);

pub struct ImageListRow<'a> {
    pub index: usize,
    pub selected: bool,
    pub name: &'a str,
    pub display_path: Option<&'a Path>,
    pub size: Option<(u32, u32)>,
    /// The in-memory size differs from the file on disk because of unsaved edits.
    pub size_changed: bool,
    pub has_changes: bool,
}

#[derive(Clone)]
pub struct SelectedImageView {
    pub id: ImageId,
    pub name: String,
    pub data: Option<Arc<Mutex<ModifiedImage>>>,
    pub error: Option<String>,
}

#[derive(Clone)]
pub struct PendingImageChange {
    pub index: usize,
    pub name: String,
    pub data: Arc<Mutex<ModifiedImage>>,
}

pub struct ImageLoadTiming {
    pub path: PathBuf,
    pub elapsed: Duration,
    pub succeeded: bool,
}

pub struct ImageList {
    items: Vec<ImageItem>,
    selection_start: usize,
    selection_count: usize,
    filter_text: String,
    next_pasted_image_number: u64,
    cache: ImageItemCache,
    pending_preloads: HashMap<ImageId, Receiver<PreloadResult>>,
}

struct ImageItem {
    id: ImageId,
    source: ImageSource,
    pretty_name: String,
    metadata: Option<(u32, u32)>,
    error: Option<String>,
    source_mtime: Option<SystemTime>,
}

#[derive(Clone)]
enum ImageSource {
    LocalPath(PathBuf),
    Remote {
        remote: RemoteImageRef,
        remote_path: String,
        format_hint: Option<String>,
    },
    InMemory,
    Default,
}

struct PreloadResult {
    id: ImageId,
    source_label: String,
    local_path: Option<PathBuf>,
    elapsed: Duration,
    result: Result<ImageSRGBA, String>,
}

struct ImageItemCache {
    max_size: usize,
    entries: HashMap<ImageId, Arc<Mutex<ModifiedImage>>>,
    lru: VecDeque<ImageId>,
    protected: HashSet<ImageId>,
    pinned: HashSet<ImageId>,
}

impl ImageItemCache {
    fn new(max_size: usize) -> Self {
        Self {
            max_size,
            entries: HashMap::new(),
            lru: VecDeque::new(),
            protected: HashSet::new(),
            pinned: HashSet::new(),
        }
    }

    fn get_cached(&self, id: ImageId) -> Option<Arc<Mutex<ModifiedImage>>> {
        self.entries.get(&id).cloned()
    }

    fn get(&mut self, id: ImageId) -> Option<Arc<Mutex<ModifiedImage>>> {
        let data = self.entries.get(&id).cloned()?;
        self.touch(id);
        Some(data)
    }

    fn put(&mut self, id: ImageId, data: Arc<Mutex<ModifiedImage>>) {
        self.entries.insert(id, data);
        self.touch(id);
        self.trim_to_max(Some(id));
    }

    fn set_pinned(&mut self, pinned: HashSet<ImageId>) {
        self.pinned = pinned;
        self.trim_to_max(None);
    }

    fn trim_to_max(&mut self, preserved: Option<ImageId>) {
        while self.entries.len() > self.max_size {
            // Evict the oldest entry that is clean and not the one we just
            // inserted. Dirty, protected, and currently visible entries must
            // never be evicted; if nothing else can go, the cache temporarily
            // exceeds max_size.
            let Some(victim) = self
                .lru
                .iter()
                .copied()
                .find(|&candidate| Some(candidate) != preserved && self.is_evictable(candidate))
            else {
                break;
            };
            self.remove(victim);
        }
    }

    fn is_evictable(&self, id: ImageId) -> bool {
        if self.protected.contains(&id) || self.pinned.contains(&id) {
            return false;
        }
        // Treat an unreadable entry as dirty so a poisoned/locked mutex never
        // causes us to drop unsaved work.
        self.entries
            .get(&id)
            .and_then(|entry| entry.lock().ok().map(|entry| !entry.has_pending_changes()))
            .unwrap_or(false)
    }

    fn remove(&mut self, id: ImageId) {
        self.entries.remove(&id);
        self.lru.retain(|&existing| existing != id);
        self.protected.remove(&id);
        self.pinned.remove(&id);
    }

    fn protect(&mut self, id: ImageId) {
        self.protected.insert(id);
    }

    fn unprotect(&mut self, id: ImageId) {
        self.protected.remove(&id);
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

fn file_mtime(path: &Path) -> Option<SystemTime> {
    std::fs::metadata(path).and_then(|metadata| metadata.modified()).ok()
}

fn next_image_id() -> ImageId {
    static NEXT: AtomicU64 = AtomicU64::new(1);
    ImageId(NEXT.fetch_add(1, Ordering::Relaxed))
}

impl ImageList {
    pub fn new(paths: Vec<PathBuf>) -> Self {
        let mut items = if paths.is_empty() {
            vec![ImageItem::default_image(next_image_id())]
        } else {
            paths
                .into_iter()
                .map(|path| ImageItem::from_path(next_image_id(), path))
                .collect::<Vec<_>>()
        };

        refresh_pretty_names(&mut items);
        Self {
            items,
            selection_start: 0,
            selection_count: 1,
            filter_text: String::new(),
            next_pasted_image_number: 1,
            cache: ImageItemCache::new(16),
            pending_preloads: HashMap::new(),
        }
    }

    pub fn filter_text(&self) -> &str {
        &self.filter_text
    }

    pub fn num_enabled_images(&self) -> usize {
        self.enabled_indices().len()
    }

    pub fn visible_rows(&self) -> impl Iterator<Item = ImageListRow<'_>> {
        let selected_indices = self.selected_indices();
        self.items.iter().enumerate().filter_map(move |(index, item)| {
            let (has_changes, cached_size) = self
                .cache
                .get_cached(item.id)
                .and_then(|data| {
                    data.lock().ok().map(|d| {
                        let [w, h] = d.image_size();
                        (d.has_pending_changes(), Some((w, h)))
                    })
                })
                .unwrap_or((false, None));
            let size = cached_size.or(item.metadata);
            self.item_enabled(index).then_some(ImageListRow {
                index,
                selected: selected_indices.contains(&Some(index)),
                name: &item.pretty_name,
                display_path: item.naming_path(),
                size,
                size_changed: has_changes && size != item.metadata,
                has_changes,
            })
        })
    }

    pub fn remove_at(&mut self, index: usize) {
        if index >= self.items.len() {
            return;
        }
        let id = self.items[index].id;
        self.items.remove(index);
        self.pending_preloads.remove(&id);
        self.cache.remove(id);
        // Keep the viewer usable after removing the final image, matching the C++ viewer's fallback.
        if self.items.is_empty() {
            self.items.push(ImageItem::default_image(next_image_id()));
        }
        refresh_pretty_names(&mut self.items);
        let enabled = self.enabled_indices();
        if enabled.is_empty() {
            self.selection_start = 0;
        } else {
            self.selection_start = self.selection_start.min(enabled.len() - 1);
        }
    }

    pub fn add_image_paths(&mut self, paths: Vec<PathBuf>) {
        for path in paths {
            let id = next_image_id();
            let item = ImageItem::from_path(id, path);
            self.items.push(item);
        }
        refresh_pretty_names(&mut self.items);
        // Navigate to the last added image.
        let enabled = self.enabled_indices();
        if !enabled.is_empty() {
            self.selection_start = enabled.len() - 1;
        }
    }

    pub fn replace_image_paths(&mut self, paths: Vec<PathBuf>) {
        *self = Self::new(paths);
    }

    pub fn local_paths(&self) -> Vec<PathBuf> {
        self.items
            .iter()
            .filter_map(|item| item.local_path().map(Path::to_path_buf))
            .collect()
    }

    pub fn add_remote_image(&mut self, offer: ImageOffer, remote: RemoteImageRef) {
        if self.items.len() == 1 && self.items[0].is_default() {
            let default_id = self.items.remove(0).id;
            self.cache.remove(default_id);
        }
        let metadata = offer.width.zip(offer.height);
        self.items.push(ImageItem {
            id: next_image_id(),
            source: ImageSource::Remote {
                remote,
                remote_path: offer.remote_path,
                format_hint: offer.format_hint,
            },
            pretty_name: offer.name,
            metadata,
            error: None,
            source_mtime: None,
        });
        refresh_pretty_names(&mut self.items);
        let enabled = self.enabled_indices();
        if enabled.len() == 1 {
            self.selection_start = 0;
        }
    }

    pub fn add_in_memory_image_data(
        &mut self,
        image: ImageSRGBA,
        name: impl Into<String>,
        insert_position: usize,
    ) -> ImageId {
        if self.items.len() == 1 && self.items[0].is_default() {
            let default_id = self.items.remove(0).id;
            self.cache.remove(default_id);
        }

        let id = next_image_id();
        let metadata = Some((image.width(), image.height()));
        let item = ImageItem {
            id,
            source: ImageSource::InMemory,
            pretty_name: name.into(),
            metadata,
            error: None,
            source_mtime: None,
        };
        let position = insert_position.min(self.items.len());
        self.items.insert(position, item);
        // In-memory images have no source to reload after eviction. Keep them
        // protected until a successful save promotes them to a local path.
        self.cache.protect(id);
        self.cache.put(
            id,
            Arc::new(Mutex::new(ModifiedImage::new_unsaved(ImageItemData::new(image)))),
        );
        refresh_pretty_names(&mut self.items);
        self.select_closest_enabled_index(position);
        id
    }

    pub fn add_pasted_image_data(&mut self, image: ImageSRGBA) -> ImageId {
        let number = self.next_pasted_image_number;
        self.next_pasted_image_number = number.checked_add(1).expect("pasted image counter overflow");
        self.add_in_memory_image_data(image, format!("(pasted image {number})"), 0)
    }

    pub fn set_filter(&mut self, filter_text: String) {
        if self.filter_text == filter_text {
            return;
        }
        let selected_index = self.selected_index().unwrap_or(0);
        self.filter_text = filter_text;
        self.select_closest_enabled_index(selected_index);
    }

    pub fn select_index(&mut self, index: usize) {
        if index < self.items.len() && self.item_enabled(index) {
            self.selection_start = self
                .enabled_indices()
                .iter()
                .position(|&enabled_index| enabled_index == index)
                .unwrap_or(0);
        }
    }

    pub fn select_relative(&mut self, offset: isize) {
        let enabled = self.enabled_indices();
        if enabled.is_empty() {
            return;
        }

        self.selection_start = self
            .selection_start
            .saturating_add_signed(offset)
            .min(enabled.len() - 1);
    }

    pub fn move_item(&mut self, from: usize, to: usize) {
        if from >= self.items.len() {
            return;
        }

        let mut target = to.min(self.items.len());
        if from == target || from + 1 == target {
            return;
        }

        let selected_id = self.selected_item().map(|item| item.id);
        let item = self.items.remove(from);
        if from < target {
            target -= 1;
        }
        self.items.insert(target, item);
        if let Some(selected_id) = selected_id {
            if let Some(index) = self.items.iter().position(|item| item.id == selected_id) {
                self.select_closest_enabled_index(index);
            }
        }
    }

    pub fn selection_count(&self) -> usize {
        self.selection_count
    }

    pub fn set_selection_count(&mut self, count: usize) {
        self.selection_count = count.max(1);
        let enabled_count = self.enabled_indices().len();
        if enabled_count > 0 && self.selection_start >= enabled_count {
            self.selection_start = enabled_count - 1;
        }
    }

    /// Check every local source, including filtered and uncached images. Decoding
    /// remains lazy: visible images reload on the next update, others on demand.
    pub fn reload_changed_images(&mut self) -> Vec<String> {
        let mut issues = Vec::new();
        for index in 0..self.items.len() {
            if let Err(issue) = self.invalidate_changed_image(index) {
                issues.push(issue);
            }
        }
        issues
    }

    /// Shared invalidation point for explicit refresh and future file events.
    /// Never discard unsaved edits, and keep the old image if stat fails.
    fn invalidate_changed_image(&mut self, index: usize) -> Result<(), String> {
        let item = &mut self.items[index];
        let Some(path) = item.local_path() else {
            return Ok(());
        };
        // Pending edits are silently excluded, even if the source is unavailable.
        if self
            .cache
            .get_cached(item.id)
            .is_some_and(|data| data.lock().map(|data| data.has_pending_changes()).unwrap_or(true))
        {
            return Ok(());
        }
        let mtime = std::fs::metadata(path)
            .and_then(|metadata| metadata.modified())
            .map_err(|error| format!("{}: {error}", path.display()))?;
        if item.source_mtime == Some(mtime) && item.error.is_none() {
            return Ok(());
        }
        let metadata = ::image::image_dimensions(path).ok();
        // Dropping the receiver prevents an older in-flight decode from restoring
        // stale pixels after invalidation. A new load gets its own receiver.
        self.pending_preloads.remove(&item.id);
        self.cache.remove(item.id);
        item.metadata = metadata;
        item.error = None;
        item.source_mtime = Some(mtime);
        Ok(())
    }

    pub fn poll_preloads(&mut self) -> Option<ImageLoadTiming> {
        let mut first_timing = None;
        let pending_ids = self.pending_preloads.keys().copied().collect::<Vec<_>>();
        for id in pending_ids {
            let Some(receiver) = self.pending_preloads.get(&id) else {
                continue;
            };
            match receiver.try_recv() {
                Ok(result) => {
                    self.pending_preloads.remove(&id);
                    let timing = result.local_path.clone().map(|path| ImageLoadTiming {
                        path,
                        elapsed: result.elapsed,
                        succeeded: result.result.is_ok(),
                    });
                    self.apply_preload_result(result);
                    if let Some(timing) = timing {
                        first_timing.get_or_insert(timing);
                    }
                }
                Err(TryRecvError::Empty) => {}
                Err(TryRecvError::Disconnected) => {
                    self.pending_preloads.remove(&id);
                    if let Some(item) = self.items.iter_mut().find(|item| item.id == id) {
                        item.error = Some("image load worker stopped unexpectedly".to_owned());
                    }
                }
            }
        }
        first_timing
    }

    pub fn ensure_selected_loaded(&mut self, on_done: impl FnOnce() + Send + Clone + 'static) {
        self.sync_visible_cache_pins();
        self.drop_obsolete_preloads();
        for index in self.selected_indices().into_iter().flatten() {
            let id = self.items[index].id;
            if self.cache.get(id).is_some() {
                continue;
            }
            let _ = self.start_preload_for_index(index, on_done.clone());
        }
    }

    /// A finished decode for an old selection must not consume a slot needed
    /// by the current selection. Dropping its receiver also discards its result.
    /// The worker already decoding may run to completion.
    fn drop_obsolete_preloads(&mut self) {
        let enabled = self.enabled_indices();
        let mut needed: HashSet<_> = self
            .selected_indices()
            .into_iter()
            .flatten()
            .map(|index| self.items[index].id)
            .collect();
        if enabled.len() >= 2 && needed.len() < self.cache.max_size {
            let next = (self.selection_start + self.selection_count) % enabled.len();
            needed.insert(self.items[enabled[next]].id);
        }
        self.pending_preloads.retain(|id, _| needed.contains(id));
    }

    pub fn preload_next_from_selection(&mut self, on_done: impl FnOnce() + Send + 'static) -> bool {
        let enabled = self.enabled_indices();
        let visible_count = self.selected_indices().into_iter().flatten().count();
        if enabled.len() < 2 || visible_count >= self.cache.max_size {
            return false;
        }

        let next_enabled_index = (self.selection_start + self.selection_count) % enabled.len();
        let next_index = enabled[next_enabled_index];
        self.start_preload_for_index(next_index, on_done)
    }

    pub fn selected_range_views(&self) -> Vec<Option<SelectedImageView>> {
        self.selected_indices()
            .into_iter()
            .map(|index| {
                let item = self.items.get(index?)?;
                Some(SelectedImageView {
                    id: item.id,
                    name: item.pretty_name.clone(),
                    data: self.cache.get_cached(item.id),
                    error: item.error.clone(),
                })
            })
            .collect()
    }

    pub fn first_selected_index(&self) -> Option<usize> {
        self.selected_index()
    }

    pub fn source_path_at(&self, index: usize) -> Option<&Path> {
        self.items.get(index)?.local_path()
    }

    pub fn set_source_path_at(&mut self, index: usize, path: PathBuf) {
        let Some(item) = self.items.get_mut(index) else {
            return;
        };
        item.source_mtime = file_mtime(&path);
        item.source = ImageSource::LocalPath(path);
        item.error = None;
        // The saved file carries the edited dimensions.
        if let Some(data) = self.cache.get_cached(item.id)
            && let Ok(data) = data.lock()
        {
            let [w, h] = data.image_size();
            item.metadata = Some((w, h));
        }
        self.cache.unprotect(item.id);
        refresh_pretty_names(&mut self.items);
    }

    pub fn modified_image_at(&self, index: usize) -> Option<Arc<Mutex<ModifiedImage>>> {
        let id = self.items.get(index)?.id;
        self.cache.get_cached(id)
    }

    pub fn selected_pending_change_images(&self) -> Vec<PendingImageChange> {
        self.selected_indices()
            .into_iter()
            .flatten()
            .filter_map(|index| self.pending_change_image_at(index))
            .collect()
    }

    pub fn pending_change_images(&self) -> Vec<PendingImageChange> {
        self.items
            .iter()
            .enumerate()
            .filter_map(|(index, item)| {
                let data = self.cache.get_cached(item.id)?;
                let has_changes = data.lock().ok().is_some_and(|data| data.has_pending_changes());
                has_changes.then(|| PendingImageChange {
                    index,
                    name: item.pretty_name.clone(),
                    data,
                })
            })
            .collect()
    }

    pub fn pending_change_image_at(&self, index: usize) -> Option<PendingImageChange> {
        let item = self.items.get(index)?;
        let data = self.cache.get_cached(item.id)?;
        let has_changes = data.lock().ok().is_some_and(|data| data.has_pending_changes());
        has_changes.then(|| PendingImageChange {
            index,
            name: item.pretty_name.clone(),
            data,
        })
    }

    pub fn has_pending_changes_at(&self, index: usize) -> bool {
        self.modified_image_at(index)
            .and_then(|data| data.lock().ok().map(|data| data.has_pending_changes()))
            .unwrap_or(false)
    }

    fn selected_item(&self) -> Option<&ImageItem> {
        self.items.get(self.selected_index()?)
    }

    fn selected_index(&self) -> Option<usize> {
        self.selected_indices().into_iter().flatten().next()
    }

    fn selected_indices(&self) -> Vec<Option<usize>> {
        let enabled = self.enabled_indices();
        (0..self.selection_count)
            .map(|offset| enabled.get(self.selection_start + offset).copied())
            .collect()
    }

    /// Keep every image represented by a visible layout cell resident, and
    /// release pins for images that left the visible range.
    fn sync_visible_cache_pins(&mut self) {
        let visible_ids = self
            .selected_indices()
            .into_iter()
            .flatten()
            .filter_map(|index| self.items.get(index).map(|item| item.id))
            .collect();
        self.cache.set_pinned(visible_ids);
    }

    fn select_closest_enabled_index(&mut self, index: usize) {
        let enabled = self.enabled_indices();
        if enabled.is_empty() {
            self.selection_start = 0;
            return;
        }
        self.selection_start = enabled
            .iter()
            .position(|&enabled_index| enabled_index >= index)
            .unwrap_or(enabled.len() - 1);
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

    fn start_preload_for_index(&mut self, index: usize, on_done: impl FnOnce() + Send + 'static) -> bool {
        let Some(item) = self.items.get_mut(index) else {
            return false;
        };
        if self.cache.get_cached(item.id).is_some()
            || self.pending_preloads.contains_key(&item.id)
            || item.error.is_some()
        {
            return false;
        }
        let id = item.id;
        let source = item.source.clone();
        if matches!(&source, ImageSource::Default | ImageSource::InMemory) {
            if matches!(&source, ImageSource::Default) {
                let data = Arc::new(Mutex::new(ModifiedImage::new(
                    ImageItemData::new(default_image()),
                    None,
                )));
                self.cache.put(item.id, data);
                return true;
            }
            return false;
        }
        if self.pending_preloads.len() >= MAX_CONCURRENT_IMAGE_LOADS {
            return false;
        }
        item.source_mtime = item.local_path().and_then(file_mtime);
        let (sender, receiver) = mpsc::channel();
        std::thread::spawn(move || {
            let start = Instant::now();
            let preload = match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| match source {
                ImageSource::LocalPath(path) => {
                    let result = load_rgba_image(&path).map_err(|err| format!("{err:#}"));
                    (path.display().to_string(), Some(path), result)
                }
                ImageSource::Remote {
                    remote,
                    remote_path,
                    format_hint,
                } => {
                    let result = remote.request_encoded_bytes().and_then(|encoded| {
                        load_rgba_image_from_memory(&encoded, format_hint.as_deref())
                            .map_err(|error| format!("{error:#}"))
                    });
                    (format!("{} (remote id {})", remote_path, remote.id()), None, result)
                }
                ImageSource::InMemory | ImageSource::Default => unreachable!("handled above"),
            })) {
                Ok((source_label, local_path, result)) => PreloadResult {
                    id,
                    source_label,
                    local_path,
                    elapsed: start.elapsed(),
                    result,
                },
                Err(_) => PreloadResult {
                    id,
                    source_label: "image load worker".to_owned(),
                    local_path: None,
                    elapsed: start.elapsed(),
                    result: Err("image decoder stopped unexpectedly".to_owned()),
                },
            };
            let _ = sender.send(preload);
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
                self.cache.put(
                    result.id,
                    Arc::new(Mutex::new(ModifiedImage::new(
                        ImageItemData::new(image),
                        result.local_path.clone(),
                    ))),
                );
                tracing::debug!(
                    elapsed_ms = result.elapsed.as_millis(),
                    image = %result.source_label,
                    "preloaded image"
                );
            }
            Err(error) => {
                self.items[index].error = Some(error);
                tracing::debug!(
                    elapsed_ms = result.elapsed.as_millis(),
                    image = %result.source_label,
                    "image preload failed"
                );
            }
        }
    }
}

impl ImageItem {
    fn is_default(&self) -> bool {
        matches!(self.source, ImageSource::Default)
    }

    fn local_path(&self) -> Option<&Path> {
        match &self.source {
            ImageSource::LocalPath(path) => Some(path),
            _ => None,
        }
    }

    fn naming_path(&self) -> Option<&Path> {
        match &self.source {
            ImageSource::LocalPath(path) => Some(path),
            ImageSource::Remote { remote_path, .. } => Some(Path::new(remote_path)),
            ImageSource::InMemory | ImageSource::Default => None,
        }
    }

    fn from_path(id: ImageId, path: PathBuf) -> Self {
        let source_mtime = file_mtime(&path);
        let metadata = ::image::image_dimensions(&path).ok();
        Self {
            id,
            pretty_name: display_name(&path),
            source: ImageSource::LocalPath(path),
            metadata,
            error: None,
            source_mtime,
        }
    }

    fn default_image(id: ImageId) -> Self {
        Self {
            id,
            source: ImageSource::Default,
            pretty_name: "<<default>>".to_owned(),
            metadata: Some((256, 256)),
            error: None,
            source_mtime: None,
        }
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
        if let Some(name) = item.naming_path().map(display_name) {
            item.pretty_name = name;
        }
    }

    let mut grouped_names: BTreeMap<String, Vec<usize>> = BTreeMap::new();
    for (index, item) in items.iter().enumerate() {
        let Some(path) = item.naming_path() else {
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
            .filter_map(|&index| items[index].naming_path())
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

    fn selected_visible_indices(images: &ImageList) -> Vec<usize> {
        images
            .visible_rows()
            .enumerate()
            .filter_map(|(visible_index, row)| row.selected.then_some(visible_index))
            .collect()
    }

    fn cached_test_image() -> Arc<Mutex<ModifiedImage>> {
        Arc::new(Mutex::new(ModifiedImage::new(
            ImageItemData::new(default_image()),
            None,
        )))
    }

    #[test]
    fn removing_last_image_restores_default_image() {
        let mut images = list(&["/tmp/a.png"]);

        images.remove_at(0);

        let rows = images.visible_rows().collect::<Vec<_>>();
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].name, "<<default>>");
        assert_eq!(rows[0].display_path, None);
        assert!(rows[0].selected);
    }

    #[test]
    fn adding_in_memory_image_data_replaces_default_and_caches_selected_pixels() {
        let mut images = ImageList::new(Vec::new());
        let image = ImageSRGBA::from_tightly_packed_bytes(1, 1, &[1, 2, 3, 4]);

        let id = images.add_in_memory_image_data(image, "(pasted)", 0);

        let rows = images.visible_rows().collect::<Vec<_>>();
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].name, "(pasted)");
        assert_eq!(rows[0].display_path, None);
        assert_eq!(rows[0].size, Some((1, 1)));
        assert!(rows[0].selected);
        let pasted = images.cache.get_cached(id).unwrap();
        let pasted = pasted.lock().unwrap();
        assert_eq!(pasted.final_data().pixel_rgba(0, 0), Some([1, 2, 3, 4]));
        assert!(pasted.has_pending_changes());
    }

    #[test]
    fn pasted_images_receive_distinct_monotonic_names() {
        let mut images = list(&["a.png"]);

        images.add_pasted_image_data(ImageSRGBA::from_tightly_packed_bytes(1, 1, &[1, 2, 3, 255]));
        images.add_pasted_image_data(ImageSRGBA::from_tightly_packed_bytes(1, 1, &[4, 5, 6, 255]));

        assert_eq!(
            visible_names(&images),
            ["(pasted image 2)", "(pasted image 1)", "a.png"]
        );
    }

    #[test]
    fn selection_range_keeps_empty_slots_past_end() {
        let mut images = list(&["a.png", "b.png"]);
        images.set_selection_count(4);
        let range = images.selected_range_views();
        assert_eq!(range.len(), 4);
        assert!(range[0].is_some());
        assert!(range[1].is_some());
        assert!(range[2].is_none());
        assert!(range[3].is_none());
    }

    #[test]
    fn grid_navigation_advances_by_selection_count() {
        let mut images = list(&["a.png", "b.png", "c.png", "d.png", "e.png"]);
        images.set_selection_count(2);
        let step = images.selection_count() as isize;

        images.select_relative(step);
        assert_eq!(selected_visible_indices(&images), [2, 3]);

        images.select_relative(-step);
        assert_eq!(selected_visible_indices(&images), [0, 1]);

        images.select_relative(-step);
        assert_eq!(selected_visible_indices(&images), [0, 1]);

        images.select_relative(10);
        assert_eq!(selected_visible_indices(&images), [4]);
        images.select_relative(step);
        assert_eq!(selected_visible_indices(&images), [4]);
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
    fn remote_duplicate_basenames_use_local_path_disambiguation() {
        let mut images = ImageList::new(Vec::new());
        for (id, remote_path) in [
            (1, "/workspace/tests/books_4k.jpg"),
            (2, "/workspace/rust/my-copy/books_4k.jpg"),
        ] {
            images.add_remote_image(
                ImageOffer {
                    id,
                    name: "books_4k.jpg".to_owned(),
                    remote_path: remote_path.to_owned(),
                    width: None,
                    height: None,
                    format_hint: Some("jpg".to_owned()),
                },
                crate::networking::remote_image_ref_for_test(id),
            );
        }

        assert_eq!(
            visible_names(&images),
            ["...tests/books_4k.jpg", "...my-copy/books_4k.jpg"]
        );
        assert_eq!(
            images.visible_rows().map(|row| row.display_path).collect::<Vec<_>>(),
            [
                Some(Path::new("/workspace/tests/books_4k.jpg")),
                Some(Path::new("/workspace/rust/my-copy/books_4k.jpg")),
            ]
        );
    }

    #[test]
    fn navigation_skips_filtered_rows() {
        let mut images = list(&["a.png", "b.png", "aa.png"]);
        images.set_filter("a".to_owned());
        images.select_relative(1);
        assert_eq!(images.selected_index(), Some(2));
        images.select_relative(1);
        assert_eq!(images.selected_index(), Some(2));
    }

    #[test]
    fn filtering_repairs_multi_selection_to_nearest_enabled_row() {
        let mut images = list(&["a.png", "b.png", "c.png", "d.png"]);
        images.set_selection_count(2);
        images.select_index(2);
        images.set_filter("a".to_owned());
        assert_eq!(selected_visible_indices(&images), [0]);
        assert_eq!(images.selected_range_views().len(), 2);
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

    fn change_test_png(path: &Path, original_mtime: SystemTime) {
        ::image::RgbaImage::from_pixel(3, 2, ::image::Rgba([9, 8, 7, 255]))
            .save(path)
            .unwrap();
        // Use an older timestamp too: changes are inequality, not just increases.
        fs::File::options()
            .write(true)
            .open(path)
            .unwrap()
            .set_times(fs::FileTimes::new().set_modified(original_mtime - Duration::from_secs(10)))
            .unwrap();
    }

    fn wait_for_cached_images(images: &mut ImageList, indices: &[usize]) {
        images.ensure_selected_loaded(|| {});
        for _ in 0..100 {
            images.poll_preloads();
            if indices
                .iter()
                .all(|&index| images.cache.contains(images.items[index].id))
            {
                return;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        panic!("selected images did not finish loading");
    }

    #[test]
    fn refresh_invalidates_filtered_images_but_preserves_unchanged_cache_and_selection() {
        let first = write_test_png("refresh-first", [1, 2, 3, 255]);
        let second = write_test_png("refresh-second", [4, 5, 6, 255]);
        let mut images = ImageList::new(vec![first.clone(), second.clone()]);
        images.set_selection_count(2);
        wait_for_cached_images(&mut images, &[0, 1]);
        let unchanged = images.modified_image_at(0).unwrap();
        let changed = images.modified_image_at(1).unwrap();
        let id = images.items[1].id;
        images.set_filter("refresh-first".to_owned());
        change_test_png(&second, images.items[1].source_mtime.unwrap());

        assert!(images.reload_changed_images().is_empty());
        assert!(Arc::ptr_eq(&unchanged, &images.modified_image_at(0).unwrap()));
        assert!(!images.cache.contains(id));
        assert_eq!(images.items[1].metadata, Some((3, 2)));
        assert_eq!(images.first_selected_index(), Some(0));
        images.set_filter(String::new());
        wait_for_cached_images(&mut images, &[1]);
        let reloaded = images.modified_image_at(1).unwrap();
        assert!(!Arc::ptr_eq(&changed, &reloaded));
        assert_eq!(
            reloaded.lock().unwrap().final_data().cpu_data().pixel(0, 0),
            Some(PixelSRGBA {
                r: 9,
                g: 8,
                b: 7,
                a: 255
            })
        );
        assert!(images.reload_changed_images().is_empty());
        assert!(Arc::ptr_eq(&reloaded, &images.modified_image_at(1).unwrap()));
        fs::remove_file(first).unwrap();
        fs::remove_file(second).unwrap();
    }

    #[test]
    fn refresh_preserves_edits_and_missing_files_and_can_retry_after_discard() {
        let path = write_test_png("refresh-edits", [1, 2, 3, 255]);
        let mut images = ImageList::new(vec![path.clone()]);
        wait_for_cached_images(&mut images, &[0]);
        let original = images.modified_image_at(0).unwrap();
        original.lock().unwrap().rotate_cw();
        change_test_png(&path, images.items[0].source_mtime.unwrap());
        assert!(images.reload_changed_images().is_empty());
        assert!(Arc::ptr_eq(&original, &images.modified_image_at(0).unwrap()));
        original.lock().unwrap().discard_changes();
        assert!(images.reload_changed_images().is_empty());
        wait_for_cached_images(&mut images, &[0]);
        let reloaded = images.modified_image_at(0).unwrap();
        fs::remove_file(&path).unwrap();
        assert_eq!(images.reload_changed_images().len(), 1);
        assert!(Arc::ptr_eq(&reloaded, &images.modified_image_at(0).unwrap()));
        reloaded.lock().unwrap().rotate_cw();
        assert!(images.reload_changed_images().is_empty());
        assert!(Arc::ptr_eq(&reloaded, &images.modified_image_at(0).unwrap()));
    }

    #[test]
    fn refresh_cancels_stale_preloads_and_retries_decode_errors_with_same_mtime() {
        let path = write_test_png("refresh-preload", [1, 2, 3, 255]);
        let mut images = ImageList::new(vec![path.clone()]);
        let id = images.items[0].id;
        let (sender, receiver) = mpsc::channel();
        images.pending_preloads.insert(id, receiver);
        change_test_png(&path, images.items[0].source_mtime.unwrap());
        assert!(images.reload_changed_images().is_empty());
        assert!(
            sender
                .send(PreloadResult {
                    id,
                    source_label: String::new(),
                    local_path: Some(path.clone()),
                    elapsed: Duration::ZERO,
                    result: Err("stale error".to_owned()),
                })
                .is_err()
        );
        images.items[0].error = Some("previous decode error".to_owned());
        assert!(images.reload_changed_images().is_empty());
        wait_for_cached_images(&mut images, &[0]);
        assert!(images.items[0].error.is_none());
        assert!(images.cache.contains(id));
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn selected_group_starts_loading_without_populating_cache_on_caller() {
        let paths = [
            write_test_png("group-a", [1, 0, 0, 255]),
            write_test_png("group-b", [2, 0, 0, 255]),
            write_test_png("group-c", [3, 0, 0, 255]),
        ];
        let mut images = ImageList::new(paths.to_vec());
        images.set_selection_count(paths.len());
        let ids = images.items.iter().map(|item| item.id).collect::<Vec<_>>();

        images.ensure_selected_loaded(|| {});

        assert!(ids.iter().all(|id| images.pending_preloads.contains_key(id)));
        assert!(ids.iter().all(|id| !images.cache.contains(*id)));

        for _ in 0..100 {
            images.poll_preloads();
            if images.pending_preloads.is_empty() {
                break;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        for path in paths {
            let _ = fs::remove_file(path);
        }
    }

    #[test]
    fn preloads_next_enabled_image() {
        let first = write_test_png("first", [1, 0, 0, 255]);
        let second = write_test_png("second", [2, 0, 0, 255]);
        let third = write_test_png("third", [3, 0, 0, 255]);
        let mut images = ImageList::new(vec![first.clone(), second.clone(), third.clone()]);

        let id0 = images.items[0].id;
        let id1 = images.items[1].id;
        let id2 = images.items[2].id;
        images.ensure_selected_loaded(|| {});
        assert!(images.preload_next_from_selection(|| {}));
        for _ in 0..100 {
            images.poll_preloads();
            if images.cache.contains(id1) {
                break;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        assert!(images.cache.contains(id0));
        assert!(images.cache.contains(id1));
        assert!(!images.cache.contains(id2));

        let _ = fs::remove_file(first);
        let _ = fs::remove_file(second);
        let _ = fs::remove_file(third);
    }

    #[test]
    fn disconnected_preload_worker_marks_image_as_failed() {
        let mut images = list(&["a.png"]);
        let id = images.items[0].id;
        let (sender, receiver) = mpsc::channel::<PreloadResult>();
        images.pending_preloads.insert(id, receiver);
        drop(sender);

        images.poll_preloads();

        assert!(images.items[0].error.as_deref().unwrap().contains("worker stopped"));
        assert!(!images.pending_preloads.contains_key(&id));
    }

    #[test]
    fn saving_filtered_selection_updates_each_selected_source_and_clears_changes() {
        let mut images = list(&["hidden.png", "selected-a.png", "other.png", "selected-b.png"]);
        for item in &images.items {
            let data = cached_test_image();
            data.lock().unwrap().rotate_cw();
            images.cache.put(item.id, data);
        }
        images.set_filter("selected".to_owned());
        images.set_selection_count(3);
        let pending = images.selected_pending_change_images();
        assert_eq!(pending.iter().map(|image| image.index).collect::<Vec<_>>(), [1, 3]);

        let dir = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tmp/save-selection-tests");
        fs::create_dir_all(&dir).unwrap();
        for image in pending {
            let path = dir.join(format!("saved-{}-{}.png", std::process::id(), image.index));
            image.data.lock().unwrap().save_changes(Some(&path)).unwrap();
            images.set_source_path_at(image.index, path.clone());
            assert_eq!(images.source_path_at(image.index), Some(path.as_path()));
            assert_eq!(image.data.lock().unwrap().source_path(), Some(path.as_path()));
            assert!(images.items[image.index].pretty_name.contains("saved-"));
            assert!(images.pending_change_image_at(image.index).is_none());
            assert!(::image::open(&path).is_ok());
            fs::remove_file(path).unwrap();
        }
        assert_eq!(images.source_path_at(0), Some(Path::new("hidden.png")));
        assert_eq!(images.source_path_at(2), Some(Path::new("other.png")));
        assert_eq!(
            images
                .pending_change_images()
                .iter()
                .map(|image| image.index)
                .collect::<Vec<_>>(),
            [0, 2]
        );
    }

    #[test]
    fn saved_in_memory_image_reloads_after_cache_eviction() {
        let path = std::env::temp_dir().join(format!("zv-saved-paste-{}.png", std::process::id()));
        let mut images = ImageList::new(Vec::new());
        let id = images.add_in_memory_image_data(
            ImageSRGBA::from_tightly_packed_bytes(1, 1, &[1, 2, 3, 255]),
            "pasted",
            0,
        );
        images
            .cache
            .get_cached(id)
            .unwrap()
            .lock()
            .unwrap()
            .save_changes(Some(&path))
            .unwrap();
        images.set_source_path_at(0, path.clone());
        images.cache.remove(id);

        images.ensure_selected_loaded(|| {});
        for _ in 0..100 {
            images.poll_preloads();
            if images.cache.contains(id) {
                break;
            }
            std::thread::sleep(Duration::from_millis(10));
        }

        assert!(images.cache.contains(id));
        let _ = fs::remove_file(path);
    }

    #[test]
    fn in_memory_image_is_protected_until_it_has_a_path() {
        let mut images = ImageList::new(Vec::new());
        let id = images.add_in_memory_image_data(
            ImageSRGBA::from_tightly_packed_bytes(1, 1, &[1, 2, 3, 255]),
            "pasted",
            0,
        );
        images.cache.get_cached(id).unwrap().lock().unwrap().discard_changes();

        assert!(!images.cache.is_evictable(id));

        images.set_source_path_at(0, PathBuf::from("saved.png"));
        assert!(images.cache.is_evictable(id));
    }

    #[test]
    fn cache_does_not_evict_dirty_images() {
        let mut cache = ImageItemCache::new(1);
        let dirty_id = ImageId(10);
        let clean_id = ImageId(11);
        let dirty = cached_test_image();
        dirty.lock().unwrap().rotate_cw();

        cache.put(dirty_id, dirty);
        cache.put(clean_id, cached_test_image());

        assert!(cache.contains(dirty_id));
        assert!(cache.contains(clean_id));
    }

    #[test]
    fn visible_range_is_pinned_without_growing_the_normal_cache_permanently() {
        const VISIBLE_RANGE_SIZE: usize = 64;
        let paths = (0..VISIBLE_RANGE_SIZE)
            .map(|index| PathBuf::from(format!("image-{index}.png")))
            .collect();
        let mut images = ImageList::new(paths);
        images.set_selection_count(VISIBLE_RANGE_SIZE);
        let ids = images.items.iter().map(|item| item.id).collect::<Vec<_>>();
        images.sync_visible_cache_pins();

        for &id in &ids {
            images.cache.put(id, cached_test_image());
        }

        assert!(
            images
                .selected_range_views()
                .into_iter()
                .all(|view| view.is_some_and(|view| view.data.is_some()))
        );
        assert_eq!(images.cache.entries.len(), VISIBLE_RANGE_SIZE);

        images.set_selection_count(1);
        images.sync_visible_cache_pins();

        assert!(images.cache.contains(ids[0]));
        assert_eq!(images.cache.entries.len(), images.cache.max_size);
    }

    #[test]
    fn lookahead_does_not_exceed_a_full_visible_cache_budget() {
        let mut images = list(&[
            "0.png", "1.png", "2.png", "3.png", "4.png", "5.png", "6.png", "7.png", "8.png", "9.png", "10.png",
            "11.png", "12.png", "13.png", "14.png", "15.png", "16.png",
        ]);
        images.set_selection_count(images.cache.max_size);

        assert!(!images.preload_next_from_selection(|| {}));
    }
}
