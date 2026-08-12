# Rust Annotations Handoff

This note is the handoff state for the Rust line-annotation work. The goal is to keep the Rust design close to the C++ annotation path while using Rust/egui/wgpu idioms where needed.

## Relevant Commits

- `d1935476b` `rust: start implementing the annotation flow`
  - Introduces `ModifiedImage`, `AnnotationDocument`, line annotations, undo/save/discard, egui-based offscreen compositing, shortcuts, controls, and debug harness support.
- `bca0a2b0b` `rust: reuse annotation composite resources`
  - Reuses the offscreen WGPU composite texture/view/readback buffer instead of reallocating every dirty composite.
- `bbde201cd` `rust: cover modified image edit state`
  - Adds focused `ModifiedImage` tests for save/discard/undo state.
- `dad9be3f4` `rust: extend annotation debug coverage`
  - Adds annotation debug state, Ctrl-Z debug input, repo-local `tmp/` convention, and stale-selection cleanup after undo/discard/save.
- `87395dd02` `rust: polish line annotation interactions`
  - Makes AddLine one-shot like C++ and scales interactive stroke width/hit testing through the image-to-widget transform.

## Main Files

- `src/annotations.rs`
  - Annotation data model, hit testing, interactive drawing helpers, and final-image egui/wgpu compositor.
- `src/annotation_tool.rs`
  - Select/AddLine tool state, creation drag, edit drag, undo snapshot capture, selected handles, and selection cleanup helpers.
- `src/modified_image.rs`
  - Original/annotated image state, undo stack, dirty tracking, save/discard, and pre-frame annotation update.
- `src/viewer.rs`
  - Applies actions, runs pre-frame annotation compositing using `frame.wgpu_render_state()`, and exposes debug annotation state.
- `src/render.rs`
  - Paint callback now only uploads/binds/paints current image data. Annotation compositing no longer happens inside the callback.
- `src/image_window.rs`
  - Renders visible images and routes pointer input into `AnnotationTool`.
- `src/debug.rs`
  - JSON debug script runner, screenshots, state snapshots, and synthetic input.

## Current Behavior

- Line annotations are functional in Rust:
  - Shift-L enters AddLine.
  - A primary-button drag creates one line.
  - AddLine is one-shot: after successful creation, mode returns to Select, matching C++.
  - Select mode supports selecting line body or handles.
  - Body drag moves the line.
  - Endpoint drag moves one endpoint.
  - Delete removes the selected annotation.
  - Ctrl-Z undoes line creation, deletion, and line edits.
  - Ctrl-S saves visible image edits.
  - Discard clears annotations and undo state.
- Creation/selection starts on primary-button press, not egui click release.
  - This matches C++ `IsMouseClicked`.
  - Plain AddLine clicks no longer leave dangling create state.
- Selection handles are display-only overlays.
  - They are not baked into final image data.
- Final image compositing is done through egui/epaint and WGPU.
  - The compositor renders annotation shapes into an offscreen texture, reads back to CPU, and stores the result as the final image data.
  - This is intentionally synchronous, like C++ `AnnotationRenderer::endRendering`.
- Annotation compositing runs as a pre-frame update in `Viewer::update(...)`.
  - This matches the C++ `ImageWindow::renderFrame` split: update annotations first, then paint frame.
  - Save runs directly during action handling after forcing annotation update.
- `save_changes(...)` is a no-op when there are no annotations.
  - This intentionally avoids lossy re-encode/overwrite on untouched images.
- Interactive stroke width and body hit tolerance use image-pixel-to-widget scale.
  - This matches the C++ `strokeWidth * imagePixelToScreenPixel` behavior.

## Verification

Last verified commands:

```bash
cd rust
cargo check
cargo test
```

`cargo test` passes with `43` tests.

GUI debug was exercised with repo-local temporary artifacts under `tmp/`. These scripts are not checked in.

**Single-image flows** (under `tmp/zv-line-annotation-debug/`):
- Plain AddLine click: no dangling `creating` or `editing` state.
- Create line: `mode: "select"`, `count: 1`, selected line geometry present.
- Endpoint drag: selected line endpoint geometry changes.
- Body drag: selected line geometry moves.
- Undo sequence: restores body drag, endpoint drag, removes created line; final state `count: 0`, `selected: false`.
- Save: Ctrl-S clears annotation state; on `rgbgrid.png` changed 421 pixels.

**Multi-visible-image flows** (under `tmp/verify-multi-image/`):
- Switched to 2-up layout (rgbgrid.png + baboon.png) via Num2 shortcut.
- Confirmed both images loaded: `counts_by_image: [0, 0]`.
- Created one line: `counts_by_image: [1, 1]` — fan-out confirmed.
- Body drag: both images updated; geometry moves by correct UV delta.
- Ctrl-Z undo of body drag: both images restore original geometry.
- Ctrl-Z undo of create: `counts_by_image: [0, 0]`, `selected: false`.

**Zoomed interaction flows** (under `tmp/verify-zoom/`):
- Ctrl+click zoom-in to 2x works; subsequent states show UV coords in zoomed sub-region.
- Line creation at 2x zoom: UV coords correctly span the zoomed UV window.
- Body drag at 2x zoom: UV delta matches widget delta scaled by inverse zoom factor.
- Handle drag at 2x zoom: handle hit test succeeds within 6px radius; p1 moves to correct UV; p2 unchanged.
- Undo of handle drag at 2x zoom: p1 restored to pre-drag UV.
- Handle miss at 2x zoom: correctly deselects (no spurious state left).

## Local Debug Artifact Convention

- Put temporary GUI debug scripts, screenshots, and generated target images under repo-local `tmp/`.
- `tmp/` is ignored by Git.
- Do not use `/private/tmp` for this workflow unless there is a specific reason; repo-local `tmp/` avoids permission prompts for cleanup.
- Do not check in the debug regression scripts yet. The decision was to keep them ad hoc for now.

## C++ Parity Notes

- C++ reference files:
  - `libzv/AnnotationTool.cpp`
  - `libzv/AnnotationTool.h`
  - `libzv/Annotations.cpp`
  - `libzv/Annotations.h`
  - `libzv/ImageWindow.cpp`
  - `libzv/ControlsWindow.cpp`
- Important C++ behaviors already matched:
  - AddLine is one-shot and returns to Select after successful creation.
  - Placement modes do not select existing annotations.
  - Selection handles have radius `6.0`.
  - Body hit base tolerance is `4.0`.
  - Stroke hit tolerance is half of stroke width scaled into widget pixels.
  - Annotation bodies are baked into final image data, but handles are overlay-only.
  - Annotation update is a pre-frame step, not paint-callback work.
- One intentional Rust simplification:
  - Rust currently has one cached composite resource bundle per `AnnotationRenderer`. This is fine for the current single-renderer flow.

## Known Gaps

- ~~Multi-visible-image behavior needs explicit GUI verification.~~ **Verified** (see below).
- ~~Non-1x zoom behavior needs explicit GUI verification.~~ **Verified** (see below).
- Debug scripts are ad hoc.
  - If CI-style regression coverage becomes desirable, add tracked scripts under `debug-scripts/` and keep generated artifacts under `tmp/`.
- Line and arrow annotations exist in Rust so far.
  - Rectangle/ellipse/text should be added only after line behavior is considered solid.
- Color editor is intentionally deferred.

## Recommended Next Steps

1. Add the next annotation type (Rectangle or Ellipse).
   - Line behavior is now verified: single-image, multi-image, and zoomed flows all pass.
   - Prefer extending `AnnotationElement`, `AnnotationDocument::hit_test`, `AnnotationTool`, and `AnnotationRenderer`.
   - Keep egui as both the interactive overlay renderer and final-image compositor.
   - Keyboard shortcuts: Shift-R for rectangle, Shift-O for ellipse (matching C++ plain R / O with the Rust shift-prefix convention).

2. Decide whether to formalize debug scripts.
   - Current decision: do not check them in.
   - The `debug.rs` infrastructure now also supports `Num1`/`Num2` layout keys and `CtrlPointerDown`/`CtrlPointerUp` for zoom testing.
   - If scripts are formalized, add a small validator that reads state JSON and asserts geometry/counts.

3. Keep color editing separate.
   - The line tool already exercises `ModifiedImage`, undo, save, discard, CPU final image generation, and render-phase separation.
   - Color editing should layer on this infrastructure later, not be mixed into the annotation bring-up.
