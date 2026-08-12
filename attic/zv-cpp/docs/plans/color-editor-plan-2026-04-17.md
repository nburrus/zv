# Color Editor Plan

## Goal

Add a modern color editor tab inspired by xv, opened with `e`, for fast visual inspection and lightweight image transformations in ZV.

The editor should support immediate preview, clean undo behavior, histogram inspection, levels adjustment, histogram equalization, simple RGB operations, and label-map colorization for gray-like images.

The first version should optimize for practical CV workflows rather than faithfully reproducing xv's full color editor. In particular, ZV should not implement direct colormap cell editing unless a later use case makes it worth the complexity.

## Current State (as of 2026-04-19)

**Completed phases: 1, 2, 3, 4A, 4B, 4C, 4D, 5, 6, 6B. The planned first version is complete.**

The color editor supports levels editing and hue shift with live GPU preview, and one-click one-shot color operations.

### What is working

- `e` opens the Controls Window and activates the Color Editor tab.
- Menu item `Tools > Color Editor` does the same.
- Histogram rendered with ImPlot (bar plot, hover bin tooltip with count/pct/cumulative).
- Channel selector (Luma / R / G / B) controls both the histogram and levels parameters being edited.
- Log y-scale toggle (right-aligned in the header row).
- Inline per-channel stats (min / max / mean) in the header row.
- Levels handles on the histogram: input black (triangle), gamma/midtone (diamond), input white (triangle). Dragging shows tooltip with current value. Handles are colored per channel.
- Darkened regions outside the input range overlay on the histogram.
- Live GPU preview via `overrideImageRendering()` hook: levels LUT compiled to a 256×1 RGB texture, rendered through a small FBO + LUT shader. Preview updates on every handle drag.
- Apply Mapping commits the current levels as a `LevelsModifier` (one undoable entry per image). Reset clears all channels. Auto Levels commits a per-image 0.1%-clipped luma stretch.
- `?` help marker explains drag interaction.
- Multi-image: preview correctly applies to each image independently via per-image framebuffer map (keyed by texture ID, evicted when map grows beyond 16 to handle scrolling through long lists).
- `Ctrl+Z` reverts an applied levels edit.
- One-Shot Actions section: Swap R/B, Swap R/G, Swap G/B, Invert, Grayscale, HistEq, Auto Levels, and Label Colorize. Each commits immediately as an undoable `OneShotColorModifier`. Actions disabled when levels preview is dirty.
- HistEq commits a per-image luma histogram equalization LUT as grayscale RGB output while preserving alpha.
- Label Colorize one-shot action for gray-like label maps: deterministic seed-based palette with a compact seed field next to the button. The modifier re-checks gray-likeness per image and leaves non-gray-like images unchanged, preserving alpha for colored labels.

### Dear ImGui upgrade (done, not originally planned here)

ZV was upgraded from ImGui 1.88 WIP to **v1.92.7-docking** (tagged release). Key migration notes:

- `IMGUI_DEFINE_MATH_OPERATORS` must be defined in `imconfig.h`, not per-file.
- `PushFont(font)` single-arg is obsolete; use `PushFont(font, size)` with explicit pixel size.
- `ImFont::FontSize` is gone; store font pixel size in static members on `ImguiGLFWWindow` (`s_monoFontPixelSize`, `s_smallMonoFontPixelSize`).
- `ImTextureID` is now `uint64_t`; cast with `(ImTextureID)(uint64_t)(textureId)`.
- Key events now use `ImGuiKey` enum throughout (`processKeyEvent` takes `ImGuiKey`); GLFW key constants removed from event handling.
- ImPlot vendored under `deps/implot` and wired into the `libzv` CMake build.
- `AnnotationRenderer` shared ImGui context: must clear `BackendRendererUserData` and `BackendPlatformUserData` before `DestroyContext()` to avoid an assert in 1.92.

### Design deviations from original plan

- **No output levels strip.** Output black/white handles were removed. The UI is intentionally simpler.
- **No compact numeric input fields.** Dragging the histogram handles is the only way to adjust levels. This may be revisited if precision editing proves necessary.
- **Reset resets all channels**, not just the current channel. This matches the expected "start over" semantics.
- **Histogram height is 72 px** (plan implied ~145 px). Halved to keep the panel compact.
- **Tooltip only while dragging**, not on hover. This avoids tooltip clutter during inspection.
- **Single buttons row below histogram:** `Level Mapping: [Apply] [Reset] [Auto] [?]` — no separate Apply/Cancel/Reset row at the bottom of a levels section.
- **Cancel is not exposed as a button.** Pressing the `e` key or switching away cancels implicitly via `onCancel()`. This keeps the panel minimal.

### Known limitations / deferred

- Dirty preview state intentionally persists across image navigation. This makes it useful to inspect a collection of images with the same pending levels or hue transform before deciding whether to apply.
- Label Colorize button does not show affected image count in multi-image mode. The modifier still skips non-gray-like images independently.

## User-Facing Scope

### Open And Close

- Add a `Color Editor` tab to the existing Controls Window.
- Press `e` to show/focus the Controls Window and activate the `Color Editor` tab.
- Add a menu item under `Tools > Color Editor` that performs the same action.
- The histogram should describe the active/focused image, even when edits apply to multiple selected images.

### Two-Tier Commit Model

The editor is split into two clearly separated sections with different commit semantics.

**Preview section** — histogram-driven controls with deferred commit:

- Levels (black/white/gamma per channel).
- Levels are adjusted primarily with handles on the histogram.
- Changes preview immediately via GPU LUT while the editor is active.
- `Apply` commits the current levels adjustment as one undoable modifier.
- `Reset` resets all levels parameters while keeping the editor open.
- Handle drags must not create undo entries until `Apply`.
- `Ctrl+Z` should undo an applied color edit using the existing undo path.

**One-shot actions section** — operations that commit immediately:

- Invert (with channel target).
- Convert to grayscale (with mode selector).
- Swap R/B, Swap R/G, Swap G/B.
- Auto-levels.
- Histogram equalization.
- Label Colorize with compact seed field.

Each one-shot action constructs and commits its own undoable modifier immediately when clicked. There is no pending state. `Ctrl+Z` reverts them like any other modifier.

**Interaction between sections:** if the preview section has uncommitted changes (dirty preview) when a one-shot action is clicked, the action is blocked. A tooltip explains that the user must Apply or Reset first.

### Multi-Image Behavior

- The preview section (levels) applies the same transform to all currently displayed images. The histogram and stats reflect the focused image only.
- One-shot actions are image-adaptive where needed (HistEq, label colorize, auto-levels compute per-image transforms) and apply independently to each image.
- Label colorize is silently skipped for non-gray-like images in a multi-image selection.
- One-shot actions always operate on the current committed image state, not on any active preview.

## Initial Feature Set

### Histogram, Levels Canvas, And Stats

Show high-quality input-image statistics for the active image. The main histogram is also the levels adjustment canvas.

The main levels canvas has one channel selector that controls both the histogram being shown and the levels parameters being edited:

- Luma
- Red
- Green
- Blue

Useful interaction:

- Hover a bin to show bin index, value range, count, percentage, and cumulative percentage.
- Drag input black, gamma/midtone, and input white handles directly on the histogram.
- Show min, max, mean inline in the channel selector row.
- Offer linear and log y-scale.
- Compute the histogram and stats from the editor input image, not from the current preview output.

MVP uses 256 bins for 8-bit sRGB data rendered with ImPlot bar plots.

### Levels

GIMP-inspired levels in a compact UI.

Controls (handle-only, no numeric fields in current implementation):

- Input black point
- Input white point
- Gamma / midtone

Channel targets: Luma, Red, Green, Blue.

Buttons: Apply Mapping, Reset (all channels), Auto.

Levels compile to 256-entry channel LUTs for speed and deterministic CPU output. Same LUTs uploaded for GPU preview.

`Luma` applies the same LUT to R, G, and B. Per-channel overrides accumulate independently.

### Histogram Equalization ✅ DONE

Add global histogram equalization as a one-shot action (Phase 5).

MVP: Luma/grayscale HistEq, preserve alpha, per-image LUT in multi-image mode.

### Quick Operations (Phase 4C ✅ DONE)

Add simple one-click one-shot operations:

- Invert (RGB, Red, Green, or Blue target).
- Convert to grayscale (sRGB luma, or single-channel extraction).
- Swap R/B, Swap R/G, Swap G/B.

Each commits immediately as its own `ImageModifier`. Undo reverts it.

### Hue Shift (Phase 4D)

Add a hue shift slider as a live-preview control (same two-tier model as levels: preview via GPU while dragging, Apply commits).

- Slider range: −180° to +180°, default 0°.
- Operates in HSV or HSL space; check `~/Perso/DaltonLens` for the reference implementation before deciding on color space and the exact shift algorithm.
- Apply commits as a `HueShiftModifier`. Reset returns to 0°.
- Multi-image: same shift applied to all displayed images (same policy as levels).
- GPU preview: extend the existing LUT shader or add a separate hue-shift shader — evaluate after checking DaltonLens.

### Label Colorize (Phase 6)

Support random colors for grayscale-looking label maps as a one-shot action.

Gray-like detection: `R == G == B` across a ~10k-pixel random sample, tolerance ±2.

Controls: one `Label Colorize` button with a compact seed integer next to it.

## Color Space Policy

Public scalar helpers in `ColorConversion.h` (already done):

```cpp
double srgbToLinear(double x);
double linearToSrgb(double x);
float srgbToLinear(float x);
float linearToSrgb(float x);
```

Use color spaces deliberately:

- Levels: sRGB by default.
- Histogram and stats display: input-image sRGB bins.
- HistEq: sRGB/luma for MVP.
- Label colorize: categorical palette mapping.

## Internal Design

### Parameters

```cpp
struct LevelsParams
{
    int inputBlack = 0;
    int inputWhite = 255;
    float gamma = 1.0f;
    int outputBlack = 0;
    int outputWhite = 255;
};

struct LevelsAdjustmentParams
{
    LevelsChannel target = LevelsChannel::Luma;
    LevelsParams lumaLevels;
    LevelsParams redLevels;
    LevelsParams greenLevels;
    LevelsParams blueLevels;
};
```

One-shot action params are consolidated in `OneShotColorParams`, including invert, grayscale, swizzle, HistEq, and Label Colorize.

### GPU Preview

`ColorEditorTool::overrideImageRendering()` is called by `ImageWindow` before drawing each image. When levels preview is active, it renders each image's committed texture through its LUT shader into a **per-image** FBO (keyed by `ctx.textureId`).

**Important implementation note:** a single shared FBO is wrong for multi-image layouts. Each unique `ctx.textureId` gets its own `GLFrameBuffer` in `_previewFrameBuffers`. The map is evicted (cleared) when it exceeds 16 entries (handles scrolling through long image lists) and also on `resetAllLevels()`.

`BeginDisabled/EndDisabled` pairs must capture the condition into a `const bool` before the block — never re-evaluate between begin and end.

### Histogram And Stats State

`ImageColorStats` with per-channel stats (r, g, b, a, luma), pixel count, and detection flags. Cached by raw data pointer; recomputed when the pointer changes (i.e., when `ModifiedImage` produces new output).

## Implementation Phases

### Phase 1: Skeleton And Preview Infrastructure ✅ DONE

### Phase 2: Public Color Transfer Helpers ✅ DONE

### Phase 3: Histogram And Stats ✅ DONE

### Phase 4A: Unified Luma Levels UI ✅ DONE

### Phase 4B: Levels Preview And Commit ✅ DONE

### Phase 4C: One-Shot Operations ✅ DONE

- `OneShotColorParams` + `OneShotColorModifier` in `Modifiers.h/.cpp`.
- Invert always targets all RGB channels; Grayscale always uses BT.709 luma.
- Buttons laid out in a 3-column table (swaps row 1, invert/grayscale row 2).
- One-shot actions blocked when `_previewDirty`; tooltip explains why.
- Tests cover all operations, alpha preservation, and channel targeting.

### Phase 4D: Hue Shift ✅ DONE

- `HueShiftModifier` with `float hueDegrees`; CPU uses HSV on sRGB (BT.709 / Sam Hocevar formulas).
- GPU preview shader: compact HSV↔RGB in GLSL, `HueShift` uniform (degrees/360). Separate shader from levels; `overrideImageRendering` dispatches based on which preview is dirty (levels takes priority).
- Slider −180°..+180° below the levels section; Apply/Reset follow the same pattern as levels.
- One-shot actions remain blocked when levels preview is dirty; hue shift section blocked when levels preview is dirty.
- Tests: identity at 0° and 360°, alpha preservation, gray invariance, pure-red→cyan at 180°.

### Phase 5: HistEq

- Implement luma/grayscale HistEq with CPU histogram/CDF/LUT.
- Per-image LUT in multi-image mode.
- Add UI button (unblock the disabled HistEq button).
- Add tests on ramps and representative images.

### Phase 6: Label Colorize ✅ DONE

- Add gray-like detection (`ImageColorStats::rgbChannelsEqual`).
- Implement deterministic random palette (seed → color per gray value).
- Add compact label colorize UI (button plus seed field).
- Button is disabled for non-gray-like focused images; the modifier still skips non-gray-like images when applied to a multi-image set.
- Add tests for determinism, alpha preservation, background value behavior.

### Phase 6B: Auto Levels ✅ DONE

- Auto button computes per-image black/white points from the luma histogram with 0.1% clipping at both ends.
- Commits immediately as a one-shot color action, preserving alpha and using the existing undo path.
- Flat images are left unchanged.

## Testing Plan

Add focused unit tests for pure CPU behavior.

Color conversion:
- Known sRGB transfer values around `0`, `1`, `0.04045`, and `0.0031308`.
- Round-trip tolerances for representative values.

Adjustment modifier (Phase 4C+):
- Identity params produce identical RGB and alpha.
- Invert maps `0 <-> 255` and preserves alpha.
- Single-channel invert only changes the targeted channel.
- Grayscale conversion preserves alpha and computes expected RGB values.
- Channel swaps produce expected channels.

Levels (already implemented, tests not yet written):
- Black/white/gamma mappings are deterministic.
- Single-channel levels only change the targeted channel.
- LUT output matches CPU `LevelsModifier` output.

HistEq (Phase 5):
- Output is monotonic for grayscale ramps.

Label colorize (Phase 6):
- Deterministic for a fixed seed.
- Equal labels map to equal colors.
- Background value behavior is correct.
- Alpha preserved.

Integration/manual:
- Open editor with `e`.
- Preview changes image immediately.
- Reset restores identity for all channels.
- Apply creates one undo entry per image.
- `Ctrl+Z` restores pre-apply image.
- Save writes committed output.
- Multi-image: each image gets independent preview FBO; no bleed between images.

## Non-Goals For The First Version

- Direct colormap cell editing.
- Full xv HSV wheel UI.
- Arbitrary curve editor.
- 16-bit, float, or true scalar image preservation.
- CPU preview fallback (GPU-only preview for levels).
- Output black/white level handles.
- Compact numeric input fields for levels (handle-only UI).
- Python API exposure.
