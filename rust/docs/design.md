# Rust ZV Viewer Design

This document describes the current Rust viewer prototype under `rust/`.
It is a work-in-progress implementation note, not a final architecture decision.
The C++ design review remains in `docs/zv_cpp_design.md`, and the broader porting notes remain in `docs/zv_rust_porting.md`.

## Scope

The Rust work currently targets only the image viewer.
`zv-client`, the Python bindings, and any later crate split are intentionally out of the first implementation step.

The current implementation is one crate:

```text
rust/
  Cargo.toml
  Justfile
  crates/
    zv-viewer/
      Cargo.toml
      src/
  debug-scripts/
    hover-controls.json
  docs/
```

The plan is to keep everything in `zv-viewer` until there is real pressure to split.
Possible later crates are still provisional: viewer core, client app, and Python bindings can be split out when the ownership boundaries become clearer.

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
- image entries
- selected image index
- pending app actions
- controls action queue shared with the controls viewport
- shared cursor pixel info
- applied image geometry bookkeeping

`Viewer::update` runs the viewer frame:

1. Collect shortcuts from the root viewport.
2. Collect shortcuts from the controls viewport queue.
3. Apply pending app actions centrally.
4. Load the selected image lazily.
5. Apply initial image window geometry.
6. Render the image window.
7. Toggle controls on image right-click.
8. Update controls placement.
9. Render the controls viewport.
10. Return a `ViewerDebugState` snapshot for debug automation.

### `ImageWindow`

`ImageWindow` renders the main image viewport.

Current behavior:

- Uses a black central panel.
- Shows image name, size, and CPU row stride.
- Allocates an image-sized rectangle.
- Adds an `egui_wgpu::Callback` over that rectangle.
- Updates cursor pixel info from hover coordinates.
- Reports secondary clicks so `Viewer` can toggle the controls window.

The image is not submitted through an egui texture handle.
Instead, the callback renders a WGPU texture managed by `ImageItemData`.

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

The current shader deliberately modifies color output so the custom path is visibly proven.
It is a placeholder for the real viewer shader work: zooming, sampling modes, image display transforms, and annotation composition.

## Window Geometry

Window geometry is implemented in `geometry.rs`.
It ports the relevant C++ policy at a high level:

- initial image window size matches the image size
- image window is clamped to monitor size if needed
- aspect ratio is preserved when clamped
- controls window is placed to the left of the image window when possible, otherwise to the right

Egui exposes viewport commands and monitor size, but it does not expose all of the same low-level window/work-area details as GLFW.
So this is a faithful policy port using the available egui APIs, not a byte-for-byte platform port.

## Input and Quit Behavior

Current runtime input:

- `q` quits from either root or controls viewport through the shared shortcut router.
- right-click on the image toggles controls visibility.
- arrow/space/backspace navigation works from either viewport through shared app actions.

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
- `right_click`
- `key`
- `state`
- `screenshot`

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

- how zoom/pan state should be represented
- where GPU shader parameters should live
- how annotations should be modeled independently from rendering
- whether annotations should be drawn with egui painters, WGPU passes, or both
- how to render annotations to a texture for final apply/export
- when to split `zv-viewer` into smaller crates
- how to preserve C++ behavior where it matters while accepting Rust/egui-specific structure

