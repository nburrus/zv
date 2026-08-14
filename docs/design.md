# Rust ZV Viewer Design

This document describes the current Rust ZV implementation. It is an
implementation note, not a stable API specification. The archived C++ design
review lives in `attic/zv-cpp/docs/zv_cpp_design.md`.

## Scope

The active Rust project targets the desktop viewer and its built-in client/server
mode. The former standalone C++ `zv-client` and Python bindings are archived.

The current implementation is one crate:

```text
Cargo.toml
Justfile
src/
debug-scripts/
  hover-controls.json
docs/
```

The package and executable are both named `zv`. Future reusable Rust components
can be split into crates only when clear ownership boundaries emerge.

## Technology Stack

The current viewer uses:

- Rust 2024 edition
- `eframe` with the `wgpu` renderer
- `egui` for UI and viewport management
- `egui-wgpu` callbacks for custom WGPU drawing inside egui
- `image` for image file loading
- `serde` and `serde_json` for debug automation scripts and state artifacts
- `clap` for CLI parsing
- `tracing` for logging

The intended graphics direction remains `egui + wgpu`.
The prototype already proves a custom WGPU shader path for the image texture instead of using a regular `egui::Image`.

## Main Types

### `ZvApp`

`ZvApp` is the `eframe::App` implementation.
It owns:

- `Viewer`
- optional `RuntimeDebug`

At construction time it registers `WgpuImageRenderer` in egui-wgpu callback resources.
This gives image drawing code access to a shared WGPU pipeline, bind group layout, and sampler.

`ZvApp::raw_input_hook` is used by debug mode to filter real user input and inject scripted input before egui derives pointer and keyboard state.

### `Viewer`

`Viewer` is the top-level controller for the image viewer.
It owns:

- `ImageWindow`
- `ControlsWindow`
- `ImageWindowGeometryState`
- image entries
- selected image index
- pending app actions
- controls action queue shared with the controls viewport
- shared cursor pixel info
- applied image geometry bookkeeping

`Viewer::update` runs the viewer frame:

1. Observe root viewport geometry and reconcile any pending programmatic resize.
2. Collect shortcuts from the root viewport.
3. Collect shortcuts from the controls viewport queue.
4. Apply pending app actions centrally.
5. Load the selected image lazily.
6. Apply initial image window geometry.
7. Render the image window.
8. Toggle controls on image right-click.
9. Update controls placement.
10. Render the controls viewport.
11. Return a `ViewerDebugState` snapshot for debug automation.

### `ImageWindow`

`ImageWindow` renders the main image viewport.

Current behavior:

- Uses a black central panel.
- Allocates the full available content rectangle for the image.
- Does not add internal aspect-fit padding; the image always covers its whole cell, and there is never a letterbox.
- The visible region is defined by a zoom factor, a UV center, and a *region aspect ratio* relative to the image's own:
  - `1` (the default) is the whole image, so nothing changes unless a command says otherwise.
  - above `1` keeps the full width and crops the height; below `1` keeps the full height and crops the width.
  - the half-extent in UV is `0.5 * (min(1, k), min(1, 1/k)) / zoom_factor`.
- `a` and `s` are duals: both make the visible region match the window pixel for pixel, `a` by reshaping the window to the image, `s` by reshaping the region to the window. `s` is what shows an image far taller or wider than the window undistorted, scrolling whatever no longer fits.
- Both are one-shot commands, so both go stale when the window is resized afterwards; press them again. `n`, `a` and `m` reset the region aspect to `1`, since they reshape the window around the whole image.
- Scrolls with the mouse wheel, a middle-button drag, a primary drag the annotation tool did not claim, and the arrow keys.
- Adds an `egui_wgpu::Callback` over that rectangle.
- Updates cursor pixel info from hover coordinates.
- Draws a compact in-image status overlay with image size and hover sRGBA values.
- Reports secondary clicks so `Viewer` can toggle the controls window.

The image is not submitted through an egui texture handle.
Instead, the callback renders a WGPU texture managed by `ImageItemData`.

### `ImageView` and `Minimap`

`image_view.rs` owns the visible region: its zoom factor, UV center and region aspect ratio, the commands acting on them, and the pending ones that need a laid-out cell. It resolves to one `CellView` per grid cell, which is all `ImageWindow` needs in order to paint. It knows nothing about painting.

`minimap.rs` draws a thumbnail of the whole image with the visible region marked on it, whenever a `CellView` hides part of its image. It fades out shortly after the view stops moving, and reports whether it is still animating so the caller can keep frames coming.

### `ControlsWindow`

`ControlsWindow` is a secondary egui viewport.

Current behavior:

- Hidden initially.
- Toggled by right-clicking the image.
- Positioned next to the image window only on first show; afterward it keeps the user-managed position across hide/show cycles.
- Right-click show requests focus for the controls viewport so it comes to front.
- Displays the pixel under the mouse:
  - image name
  - pixel coordinates
  - sRGBA values
- Sends shortcut actions through the same shared action path as the root viewport.

`Viewer` owns and manages `ControlsWindow`; the controls window does not own viewer state.

## Image Data

### `RgbaImage`

`RgbaImage` is the current CPU image container.
It stores:

- width
- height
- `bytes_per_row`
- pixel bytes

Rows are padded to `RgbaImage::ROW_ALIGNMENT`, currently 256 bytes.
This is intentional: CPU-side padding is part of the image representation so WGPU uploads can use the image's real row stride directly.

The important invariant is that `bytes_per_row` is not assumed to be `width * 4`.
Pixel access and texture upload both respect the stored stride.

### Image Loading

Image loading is implemented in `image_io.rs`.
JPEG paths (`.jpg` and `.jpeg`, case-insensitive) are tried with the Rust `turbojpeg` crate first, which links to libjpeg-turbo/TurboJPEG.
If TurboJPEG rejects the file, loading falls back to the generic `image` crate path.
This preserves compatibility with mislabeled files, such as PNG data stored in a `.jpg` file.

Non-JPEG formats continue to use the `image` crate directly.
When both the TurboJPEG path and the fallback path fail for a JPEG extension, the returned error includes context from both attempts.

The `turbojpeg` crate defaults enable `pkg-config`, `cmake`, and `require-simd`.
On macOS, Linux, and Windows it can link against a system libturbojpeg discovered by pkg-config, or build libjpeg-turbo from source through CMake when a system library is not available.
Source builds require the platform C/C++ toolchain, CMake, and an assembler for SIMD-capable builds such as NASM on x86/x86_64.
This keeps the Rust viewer portable, but it does add native build prerequisites when prebuilt/system TurboJPEG is not found.

### `ImageItemData`

`ImageItemData` is modeled after the C++ image item data role.
It owns:

- CPU image data
- optional WGPU texture data

The GPU side is uploaded lazily by `ensure_uploaded_to_gpu`.
Upload uses `wgpu::TexelCopyBufferLayout` with `bytes_per_row` taken from the CPU image.

This gives one object responsibility for the image's CPU data and its associated GPU texture/bind group.

## WGPU Rendering

`WgpuImageRenderer` owns the shared WGPU resources needed by image callbacks:

- render pipeline
- bind group layout
- sampler

`WgpuImageCallback` is attached to the image rectangle by egui.
During prepare it uploads `ImageItemData` to the GPU if needed.
During paint it binds the image texture and draws a full-viewport quad.

The current shader samples and displays the image texture directly.
The sampler uses nearest filtering for magnification and linear filtering for minification.
This is faithful for color display and gives better downsampling than nearest-only, but it still does not implement true anti-aliased minification through mipmaps or custom reconstruction.

## Window Geometry

Window geometry is implemented in `image_window_geometry.rs`, with raw egui
viewport measurements normalized in `viewport_geometry.rs`.
It ports the relevant C++ policy at a high level:

- initial image window size matches the image size
- image window is clamped to the available inner image area if needed
- aspect ratio is preserved when clamped
- normal resize uses the OS window content size directly
- `n`, `a`, `m`, `<`, `>`, `.`, and `,` resize the OS window rather than adding padding inside the image viewport
- controls window is placed to the left of the image window when possible, otherwise to the right

`ImageWindowGeometryState` owns the C++-style geometry mode tracking:

- normal size
- aspect-ratio source size
- last requested programmatic inner size
- inferred user-defined geometry
- cached platform maximum inner size for maxspect

Egui exposes viewport commands, monitor size, inner rect, and outer rect, but it does not expose a portable monitor work area.
The implementation estimates drawable inner area from the monitor size and observed window decoration size.
If the OS clamps a maxspect request because a menu bar, dock, taskbar, or Wayland compositor work-area policy reduces the usable area, the next frame observes the granted size and treats it as platform max-size feedback rather than a user resize.
This mirrors the intent of C++ ZV's `glfwGetMonitorWorkarea()` path while keeping a fallback for platforms where work area cannot be queried.

## Input and Quit Behavior

Current runtime input:

- `q` quits from either root or controls viewport through the shared shortcut router.
- right-click on the image toggles controls visibility.
- space/backspace navigation works from either viewport through shared app actions.
- arrow keys scroll the image whenever their axis is scrollable, and navigate the image list only when that axis shows the whole image. A scrollable axis keeps its keys at both ends of its travel, so scrolling to the bottom of a tall image never spills over into the next image.
- `s` scales the image content to the window, the dual of `a` scaling the window to the image content.
- `n` restores normal image size.
- `a` restores the image aspect ratio by resizing the OS window.
- `m` applies maxspect.
- `<` / `>` halve or double the OS window size.
- `.` / `,` increase or decrease the OS window size by ten percent.

Shortcut handling is centralized in `shortcuts.rs`:

- both viewports call the same shortcut collector
- actions are executed in one place by `Viewer`
- global shortcuts are gated by `ctx.wants_keyboard_input()`, so text-edit widgets can consume keyboard input without firing global commands
- controls viewport shortcut collection explicitly requests a root viewport repaint when it enqueues actions, so root-driven viewer updates continue even when controls is focused
- after applying actions, `Viewer` explicitly requests repaint for both root and controls viewports so focused-window shortcuts still refresh the other viewport
- scoped shortcut kinds already exist for future extension (`GlobalAlways`, `GlobalWhenNotTyping`, and `ViewportOnly`)

A previous `q` deadlock was fixed by avoiding viewport close commands inside `ctx.input` closures.
The code now records the intent inside the input closure and sends viewport commands afterward.

## Debug Automation

The viewer has a built-in JSON debug runner.
This is intended to let an agent or test process drive the native app without OS-level screenshots.

Debug mode is enabled with:

```bash
cargo run -- \
  --debug-script-json debug-scripts/hover-controls.json \
  --debug-artifact-dir /tmp/zv-debug
```

The runner uses `eframe::App::raw_input_hook` to:

- remove real mouse/keyboard/touch input during debug runs
- inject synthetic egui input events

This makes debug scripts deterministic.

### JSON Actions

The script is an ordered list of actions:

```json
{
  "wait_frames_default": 60,
  "actions": [
    { "type": "wait_for_image" },
    { "type": "right_click", "target": "image", "at": [0.5, 0.5] },
    { "type": "wait_frames", "frames": 0 },
    { "type": "state", "name": "controls_open" },
    { "type": "hover", "target": "image", "at": [0.25, 0.25] },
    { "type": "wait_frames", "frames": 0 },
    { "type": "state", "name": "hover_1" },
    { "type": "hover", "target": "image", "at": [0.75, 0.75] },
    { "type": "wait_frames", "frames": 0 },
    { "type": "state", "name": "hover_2" },
    { "type": "screenshot", "name": "hover_controls", "viewport": "controls" },
    { "type": "key", "viewport": "root", "key": "q" }
  ]
}
```

Supported actions currently include:

- `wait_for_image`
- `wait_frames`
- `hover`
- `click`
- `right_click`
- `key`
- `state`
- `screenshot`
- `assert_cursor`
- `discard_changes`

`wait_frames` with `frames: 0` uses `wait_frames_default`.
The CLI option `--debug-wait-frames N` overrides the JSON default so scripts can be slowed down without editing the file.

### Artifacts

Debug artifacts are written to `--debug-artifact-dir`.

State actions write `<name>.json`.
Screenshot actions write `<name>.png`.
Every generated artifact is recorded in `trace.json`.

State snapshots currently include:

- frame number
- selected image name, size, and bytes per row
- image window visibility and image rect
- controls window visibility and target position
- cursor image, pixel coordinates, and RGBA value when available

Screenshots use `egui::ViewportCommand::Screenshot`.
This captures the rendered egui viewport through eframe/wgpu, not an OS-level screenshot.

Native GUI debug runs need normal GUI permissions.
In practice, the app must be run outside the command sandbox for macOS to create windows and WGPU surfaces correctly.

## Current Verification

The current implementation has been verified with:

```bash
cargo check
cargo run -- \
  --debug-script-json debug-scripts/hover-controls.json \
  --debug-artifact-dir /private/tmp/zv-debug-json-only \
  --debug-wait-frames 10
```

That run produced:

- `controls_open.json`
- `hover_1.json`
- `hover_2.json`
- `hover_controls.png`
- `trace.json`

## Near-Term Design Questions

Open questions for the next milestones:

- where GPU shader parameters should live
- how annotations should be modeled independently from rendering
- whether annotations should be drawn with egui painters, WGPU passes, or both
- how to render annotations to a texture for final apply/export
- when to split `zv` into smaller crates
- how to preserve C++ behavior where it matters while accepting Rust/egui-specific structure
