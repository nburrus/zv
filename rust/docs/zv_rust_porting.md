# Rust Porting Notes

This is a WIP planning document for a possible Rust port of the `zv` image viewer. Nothing here is decided yet. The goal is to capture hypotheses, options, and early preferences so they can be validated progressively against prototypes.

The C++ implementation reference is [`zv_cpp_design.md`](./zv_cpp_design.md). That document should stay focused on what the current C++ viewer does; this document should hold the Rust-specific interpretation.

## Current Direction

The working idea is a side-by-side Rust implementation under `rust/`, initially covering only the desktop image viewer:

- No `zv-client`.
- No Python bindings.
- No server/client protocol.
- Keep the C++ implementation as the behavior and design reference.
- Start with viewer fundamentals before editing, annotations, or color tools.

The currently imagined stack is `egui` + `wgpu`, likely through `eframe` first for faster iteration. This is not final; it should be validated with a small rendering and interaction prototype.

## Initial Questions to Validate

- Is `eframe` enough for ZV's windowing needs, or do we need direct `egui-winit` + `egui-wgpu` control?
- Can `egui` texture APIs give us adequate UV cropping, nearest sampling, and large image behavior?
- Should the first Rust viewer use one native window with panels instead of the C++ model of separate image/control/help windows?
- Should image loading be synchronous at first, or do we need async/background loading immediately?
- How much of the C++ editing pipeline should exist in the first Rust data model even if UI tools are deferred?

Current validation notes:

- `eframe` is sufficient for the current single-image viewer, controls viewport, custom WGPU image drawing, and C++-style window resize commands.
- `eframe`/egui does not expose a portable monitor work-area rectangle. The current geometry code estimates drawable inner area from `inner_rect`/`outer_rect` decorations and handles OS clamp feedback after maxspect. Keep this fallback even if platform-specific work-area helpers are added, because work-area queries are not universally available, notably on Wayland.
- The first custom WGPU image path is now in use rather than an `egui::Image` texture widget.

## Tentative Stack

Potential first-pass crates:

```toml
[dependencies]
eframe = "..."
egui = "..."
image = "..."
clap = { version = "...", features = ["derive"] }
lru = "..."
rfd = "..."
arboard = "..."
anyhow = "..."
thiserror = "..."
tracing = "..."
tracing-subscriber = "..."
serde = { version = "...", features = ["derive"] }
directories = "..."
```

If lower-level rendering becomes necessary:

```toml
wgpu = "..."
egui-wgpu = "..."
egui-winit = "..."
winit = "..."
bytemuck = { version = "...", features = ["derive"] }
```

Do not lock exact versions from this note. Pick current stable versions when initializing the Rust crate.

## C++ Concepts Likely Worth Preserving

- Lazy loading and a small LRU cache.
- Stable `ImageId`s.
- Selection count tied to layout cell count.
- Default generated image when no files are loaded.
- Normalized UV geometry for zoom, crop, and annotations.
- A current/visible image wrapper that can preserve pending edits across layout and selection refreshes.
- An action vocabulary for keyboard, menu, and tool commands.
- Pixel-level inspection as a first-class workflow.

These are candidates, not commitments.

## C++ Concepts Likely Worth Replacing

- Friend-class coupling between `Viewer`, `ImageWindow`, and `ControlsWindow`.
- Storing GPU texture handles inside image data objects.
- Direct ports of `ImguiGLFWWindow` and `OpenGL` classes.
- Separate native controls/help windows in the first prototype.
- Function-object command queues where plain enum actions would be clearer.

## Tentative Rust Architecture

Start with a Cargo workspace, but only one implementation crate: `zv-viewer`. The goal is to avoid moving the project root later while still keeping the first milestone simple.

```text
rust/
  Cargo.toml
  docs/
    zv_cpp_design.md
    zv_rust_porting.md
  crates/
    zv-viewer/
      Cargo.toml
      src/
        main.rs
        app.rs
        viewer.rs
        image_window.rs
        image/
          mod.rs
          rgba.rs
          io.rs
        render/
          mod.rs
          texture.rs
        geometry.rs
        actions.rs
```

Later, once boundaries start making sense, split shared logic out of `zv-viewer`:

```text
rust/
  Cargo.toml
  crates/
    zv-core/      # shared image/domain logic
    zv-viewer/    # desktop GUI app
    zv-client/    # future CLI client app
    zv-python/    # future Python extension module
```

No separate `zv-protocol` crate is planned. If protocol/message code is ported later, keep it in `zv-core` until there is a concrete reason to revisit that.

## State Model Sketch

This is an early sketch, not an API decision:

```rust
struct ZvApp {
    viewer: Viewer,
}

struct Viewer {
    images: ImageList,
    visible: VisibleImages,
    layout: LayoutConfig,
    zoom: ZoomState,
    controls: ControlsState,
    pending_actions: Vec<ImageWindowAction>,
}

struct VisibleImages {
    slots: Vec<Option<VisibleImage>>,
}

struct VisibleImage {
    image_id: ImageId,
    loaded: Arc<LoadedImage>,
    texture_id: Option<egui::TextureId>,
    version: u64,
}

struct TextureCache {
    textures: LruCache<TextureKey, egui::TextureHandle>,
}
```

Frame flow hypothesis:

```text
eframe update(ctx, frame)
  collect keyboard/mouse/menu commands
  apply viewer actions
  sync visible images from image list + loaded cache
  upload missing textures
  render image panel
  render controls panel/window
  request repaint if loading or animations require it
```

## Image Data Hypothesis

C++ uses `Image<T>` with custom allocation, row padding, and a release callback. The Rust image type should keep an explicit row stride as part of the CPU-side representation so padding is handled consistently before upload:

```rust
struct RgbaImage {
    width: u32,
    height: u32,
    bytes_per_row: usize,
    pixels: Vec<u8>, // RGBA8 rows, including any row padding
}
```

Tentative notes:

- Preserve bytes-per-row in the core image model, even when rows happen to be tightly packed.
- Allocate CPU-side padding deliberately so `wgpu` uploads can use the image's row layout directly.
- Keep row access helpers explicit, similar to the C++ `atRowPtr()` pattern, so image algorithms never assume tight packing accidentally.
- Use the `image` crate first, then evaluate faster JPEG decoders only if needed.

## Image List Hypothesis

The C++ `ImageList` maps well to a Rust data model:

```rust
struct ImageList {
    items: Vec<ImageItem>,
    enabled_indices: Vec<usize>,
    selection_start: usize,
    selection_count: usize,
    global_selection_start: usize,
    filter: Option<Filter>,
    loaded_cache: LruCache<ImageId, LoadedImage>,
}
```

Use `Option<usize>` for empty grid cells instead of the C++ `-1` sentinel.

## Rendering Hypothesis

Do not directly port the C++ OpenGL layer. For the first prototype, test:

```rust
egui::ColorImage::from_rgba_unmultiplied([w, h], rgba_bytes)
ctx.load_texture(name, color_image, TextureOptions::NEAREST)
ui.image(...)
```

If this is insufficient for large images, exact sampling, partial updates, or advanced previews, introduce a renderer-owned `wgpu` texture cache:

```rust
struct GpuImage {
    texture: wgpu::Texture,
    view: wgpu::TextureView,
    sampler_nearest: wgpu::Sampler,
    sampler_linear: wgpu::Sampler,
    size: UVec2,
}
```

Renderer resources should not live in the core image model.

Current rendering status:

- `ImageItemData` owns CPU image data plus optional WGPU texture/bind group state.
- `WgpuImageRenderer` is registered as an egui-wgpu callback resource and renders image rectangles through a custom shader.
- The shader currently displays sampled texture color directly.
- The sampler uses nearest filtering for magnification and linear filtering for minification. True anti-aliased downsampling still needs mipmaps or a custom reconstruction path.

## Action Model Hypothesis

Keep the C++ action vocabulary, but represent it as enums:

```rust
enum ImageWindowAction {
    Zoom(ZoomAction),
    File(FileAction),
    Edit(EditAction),
    View(ViewAction),
    Modify(ModifyAction),
    Tool(ToolAction),
}
```

This should be easier to test and inspect than a queue of closures. Deferred confirmation may still need a small `PendingAction` representation.

## First Milestone Hypothesis

A useful first viewer-only milestone might include:

1. CLI opens one or more image files.
2. Single native window.
3. Image list and selected image.
4. Original-size display, fit-to-window/maxspect behavior, and basic zoom.
5. Keyboard next/previous.
6. Grid layout for multiple visible images.
7. Pixel-perfect nearest sampling by default.
8. Mouse pixel inspection overlay with coordinates and RGBA.
9. Basic file-open dialog.

Progress so far:

- CLI opens images and falls back to a generated default image.
- Single native image viewport plus a secondary controls viewport are implemented.
- Lazy CPU load and lazy WGPU upload are implemented.
- The image viewport fills the OS window content area without internal aspect padding.
- C++-style resize commands are implemented: `n`, `a`, `m`, `<`, `>`, `.`, and `,`.
- Pixel hover reporting is implemented in both the controls viewport and a compact image overlay.
- A JSON debug runner can inject input and write state/screenshot artifacts.

Likely deferred:

- Multiple native viewer windows.
- Separate controls window.
- Remote server/client.
- Python API.
- Editing pipeline.
- Annotations.
- Color editor.
- Clipboard image support.
- Saving modified images.

## Porting Difficulty Guess

This table is a planning guess only.

| Area | Guess | Notes |
| --- | --- | --- |
| CLI app setup | Easy | `clap`; no server initially. |
| Image container | Easy | Tight `Vec<u8>` RGBA first. |
| Image I/O | Easy | `image` crate covers first needs. |
| ImageList selection/filtering | Easy-Medium | Ports cleanly; use `Option<usize>`. |
| Layout grid | Easy | Pure geometry. |
| Zoom ROI math | Easy-Medium | Math is easy; texture sampler details need validation. |
| Pixel overlay | Easy-Medium | `egui::Painter` should handle it, but verify clipping/coordinates. |
| Controls window | Medium | Prefer side panel/floating `egui::Window` before multi-window. |
| ModifiedImage pipeline | Medium | Probably better as enums/data than a direct trait-object port. |
| Annotations | Medium-Hard | Data model easy; interactive editing/compositing harder. |
| Color editor | Medium-Hard | GPU preview needs redesign if/when ported. |
| OpenGL layer | Replace | Do not port directly. |
| GLFW/ImGui wrapper | Replace | `eframe` or `egui-winit`/`egui-wgpu`. |
| Server/client | Out of scope | Keep C++ as reference only. |
| Python bindings | Out of scope | Ignore for now. |

## Validation Plan

Near-term validation should be prototype-driven:

1. Initialize a minimal Rust app.
2. Load one RGBA image and show it at native size.
3. Verify nearest sampling and UV ROI zoom.
4. Verify a large image's memory and upload behavior.
5. Add multi-image grid layout.
6. Add pixel inspection and compare behavior against C++ `zv`.
7. Revisit whether `eframe` is enough before building more UI on top.
