# C++ ZV Viewer Design Review

This document reviews the current C++ `zv` image viewer design. It is intended as a reference for understanding the existing implementation. Porting notes live separately in [`zv_rust_porting.md`](./zv_rust_porting.md).

## Scope

The C++ project contains three user-facing surfaces:

- `zv`: the desktop image viewer.
- `zv-client` and `libzv/Server`: remote image submission.
- `python/`: Python bindings around the C++ library.

The useful C++ viewer reference surface is:

- App/window lifecycle.
- Image list, selection, layout, lazy loading, and caching.
- Image display, zoom, pixel inspection, and overlays.
- File open/save/refresh/close.
- Basic editing pipeline and annotation model.

Out-of-scope areas for this C++ viewer design note are:

- TCP server/client behavior.
- Python/nanobind API shape.
- OpenGL-specific resource classes.

## High-Level Architecture

The viewer is organized around these main classes:

```text
App
  owns many named Viewer instances

Viewer
  owns one ImageList
  owns one ImageWindow
  owns optional ControlsWindow
  owns optional HelpWindow
  owns ViewerState request flags

ImageWindow
  owns current ModifiedImage instances for visible selection
  owns ImageWindowState
  owns OS window wrapper
  owns zoom/layout/geometry state
  renders the actual images and overlays

ControlsWindow
  owns a second ImGui/GLFW window
  renders menus, image list, modifiers, color editor, dialogs

ImageList
  owns ImageItem records
  owns selection/filter state
  owns lazy ImageItemData cache

ImageItem
  describes one logical image source

ImageItemData
  contains CPU image data and mutable GPU texture data

ModifiedImage
  wraps one ImageItem plus original data, modifier chain, undo, annotations
```

The current C++ design is pragmatic and immediate-mode oriented. It keeps most UI state close to the UI classes, especially `ImageWindow` and `ControlsWindow`, while keeping image ownership and selection in `ImageList`.

## Entry Point and App Lifecycle

### `zv/main.cpp`

The desktop executable is minimal:

```text
main()
  create zv::App
  initialize(argc, argv)
  run()
```

### `App`

Files:

- `libzv/App.h`
- `libzv/App.cpp`

Responsibilities:

- Parse CLI arguments.
- Create a default viewer.
- Add CLI image paths to the default viewer.
- Start the server on `--interface` / `--port`.
- Own a map of named `Viewer`s.
- Drive the frame loop at roughly 30 Hz.
- Remove viewers that requested exit.
- Stop server and terminate GLFW on shutdown.

Important state:

```text
unordered_map<string, unique_ptr<Viewer>> viewers
Server server
RateLimit rateLimit
```

Current flow:

```text
App::initialize(args)
  parse images, --port, --interface, --require-server
  createViewer("default")
  add each CLI image path
  refresh pretty names
  start server

App::run()
  while numViewers() > 0:
    updateOnce()
    sleep to target 30 Hz

App::updateOnce()
  server.updateOnce(callback adding images to named viewers)
  for each viewer:
    if exit requested: shutdown and erase later
    else renderFrame()
```

## Viewer Controller

Files:

- `libzv/Viewer.h`
- `libzv/Viewer.cpp`

Responsibilities:

- Own per-viewer model/UI components.
- Create the main `ImageWindow`.
- Lazily create `ControlsWindow` and `HelpWindow`.
- Translate window requests into controller-level actions.
- Expose public API for adding images, selecting images, setting layout, and running actions.
- Coordinate pending-change confirmation before destructive navigation/actions.

Owned components:

```text
ImageList imageList
ImageWindow imageWindow
ControlsWindow controlsWindow
HelpWindow helpWindow
ViewerState state
```

`ViewerState` is a group of request flags:

```text
helpRequested
toggleControlsRequested
showColorEditorRequested
dismissRequested
openImageRequested
controlsRequestedForConfirmation
pendingChangesConfirmationRequested
funcIfChangesConfirmed
```

The `renderFrame()` method processes these flags, brings up the controls/help windows as needed, renders the image window, then renders controls if enabled.

Important design point: `Viewer` is the mediator between `ImageWindow` and `ControlsWindow`. These classes are friends in C++ and call back into protected methods like `onOpenImage()`, `onToggleControls()`, and `runAfterConfirmingPendingChanges()`.

## Image Data Model

### `Image<T>`

File:

- `libzv/Image.h`

`Image<T>` is a templated image container with:

- Width and height.
- Raw byte pointer.
- 16-byte row alignment.
- `bytesPerRow`.
- Custom release callback.
- Copy/move semantics.
- Row access helpers and pixel indexing.

Common concrete types:

```text
PixelSRGBA       u8 RGBA
PixelXYZ         f32 triplet with aliases
PixelLinearRGB   strong alias over PixelXYZ
PixelLMS         strong alias over PixelXYZ
PixelYCbCr       strong alias over PixelXYZ
PixelHSV         strong alias over PixelXYZ
PixelLab         strong alias over PixelXYZ
ImageSRGBA       Image<PixelSRGBA>
```

I/O functions:

```text
readImageFile(path, ImageSRGBA&, error)
readJpegFile(path, ImageSRGBA&, error)
writeImageFile(path, ImageSRGBA&)
```

The C++ loader uses TurboJPEG for `.jpg`/`.jpeg` first, falls back to `stb_image`, and writes JPEG with TurboJPEG or PNG with stb.

## Image Items and Lazy Loading

### `ImageItem`

Files:

- `libzv/ImageList.h`
- `libzv/ImageList.cpp`

`ImageItem` describes one logical image:

```text
Source:
  Invalid
  FilePath
  Data
  Callback

uniqueId
errorString
sourceImagePath
prettyName
viewerName
sourceData
loadDataCallback
eventCallback
metadata width/height
disabled
alreadyModifiedAndSaved
```

The item may be backed by:

- A file path.
- In-memory image data.
- A callback returning `ImageItemData`.

`ImageItemData` contains:

```text
Status:
  FailedToLoad
  Unknown
  Ready
  StillLoading

cpuData: shared_ptr<ImageSRGBA>
textureData: mutable GLTexturePtr
update(): bool
ensureUploadedToGPU()
```

Important design point: CPU data and GPU texture data are stored together in `ImageItemData`. Texture upload happens lazily when an image becomes visible.

## ImageList, Selection, Filtering, and Cache

### `ImageList`

Files:

- `libzv/ImageList.h`
- `libzv/ImageList.cpp`

Responsibilities:

- Own ordered `ImageItem`s.
- Maintain filtered/enabled indices.
- Maintain current selection range.
- Add/remove/swap/move images.
- Refresh duplicate pretty names.
- Lazy-load image data through an LRU cache.
- Preload the next callback-backed image.
- Release GL resources on shutdown.

Important state:

```text
entries: vector<ImageItemPtr>
enabledEntries: vector<int>
filter: function<bool(name)>
selection: SelectionRange
selectionStart: index into enabledEntries
selectionCount: number of visible grid cells
globalSelectionStart: index into entries
cache: ImageItemCache
```

Selection is represented as a vector of global entry indices, one per layout cell. Invalid cells are `-1`.

Key behaviors:

- A generated default image is inserted when the list is empty.
- Adding the first real image removes the default image.
- `replaceExisting` replaces by source path when possible, else by pretty name.
- Selection advances by the selection count, so a 2x2 layout pages four images at a time.
- Filtering rebuilds `enabledEntries` and selects the closest enabled item.
- `refreshPrettyFileNames()` disambiguates duplicate basenames.
- Cache size is currently fixed at 8.

## Modified Images and Editing Pipeline

### `ModifiedImage`

Files:

- `libzv/Modifiers.h`
- `libzv/Modifiers.cpp`

`ModifiedImage` wraps a visible image and tracks user edits:

```text
ImageItemPtr _item
ImageItemDataPtr _originalData
deque<unique_ptr<ImageModifier>> _modifiers
deque<ImageAction> _actions
unique_ptr<AnnotationDocument> _annotations
ImageItemDataPtr _annotatedData
bool _modifiersChangedSinceLastUpdate
bool _annotationsDirty
```

Important methods:

- `finalData()`: annotation-composited output if annotations exist, else modifier output.
- `preAnnotationData()`: modifier output before annotation compositing.
- `updateModifiers()`: recompute modifier pipeline when dirty.
- `updateAnnotations()`: recompute annotation layer when dirty.
- `addModifier()`, `removeLastModifier()`.
- `saveChanges()`, `discardChanges()`, `undoLastChange()`.
- `resetToNewData()` for refresh-from-disk without transient null state.
- `pushUndoAction()` for tools that mutate annotation state.

Modifier classes include:

- `LevelsModifier`
- `OneShotColorModifier`
- `HueShiftModifier`
- `RotateImageModifier`
- `CropImageModifier`
- `ResizeImageModifier`

## Windowing, Rendering, and GPU Resources

### `ImguiGLFWWindow`

Files:

- `libzv/ImguiGLFWWindow.h`
- `libzv/ImguiGLFWWindow.cpp`

This is a GLFW-backed window with its own ImGui and OpenGL context. It handles:

- Creating platform windows.
- ImGui context setup.
- Frame begin/end.
- Window geometry.
- DPI/framebuffer scale.
- Enable/disable/show/hide.
- Close requests.
- Context activation.

### `OpenGL`

Files:

- `libzv/OpenGL.h`
- `libzv/OpenGL.cpp`
- `libzv/OpenGL_Shaders.cpp`

OpenGL abstractions include:

- `GLShader`
- `GLTexture`
- `GLContext`
- `GLFrameBuffer`
- `GLImageRenderer`

`GLTexture` owns the texture ID, dimensions, upload/download helpers, and linear/nearest filtering toggles. `ImageItemData::ensureUploadedToGPU()` creates and uploads textures lazily.

## ImageWindow

Files:

- `libzv/ImageWindow.h`
- `libzv/ImageWindow.cpp`
- `libzv/ImageWindowState.h`
- `libzv/ImageWindowState.cpp`

`ImageWindow` is the central viewer surface. It currently combines:

- Main image OS window.
- Current visible image slots.
- Layout grid.
- Zoom ROI.
- Window size and monitor-fit logic.
- Keyboard/mouse input handling.
- Rendering of images.
- Rendering of pixel info overlay and cross-image cursor markers.
- Active tool overlay rendering.
- Action queue and command execution.
- Current image modifications and pending-change checks.

Important state:

```text
ImguiGLFWWindow imguiGlfwWindow
Viewer* viewer
vector<ModifiedImagePtr> currentImages
ImageLayout currentLayout
AnnotationRenderer annotationRenderer
ImageWindowState mutableState
ImageCursorOverlay inlineCursorOverlay
CursorOverlayInfo cursorOverlayInfo
deque<Command> pendingCommands
ZoomInfo zoom
imageWidgetRect.normal/current/sourceForAspectRatio
WindowGeometryMode lastGeometryMode
```

### ImageWindowState

`ImageWindowState` contains:

```text
activeMode
modeForCurrentFrame
controlsInputState
inputState
activeToolState
colorEditorTool
layoutConfig
infoOverlayEnabled
timeOfLastCopyToClipboard
```

`ActiveToolState` owns concrete tool instances:

```text
CropTool cropTool
AnnotationTool annotationTool
```

### Actions

`ImageWindowAction::Kind` includes:

- Zoom: normal, restore aspect ratio, x2, half, +/-10%, maxspect, custom.
- File: open, save, save as, refresh, delete, close.
- Edit: copy cursor info, copy image, paste image, undo, revert.
- View: toggle overlay, next/previous image, paging, select image.
- Modify: resize to window, rotate.
- Tools: apply/cancel current tool, show color editor.

Actions are wrapped as `ImageWindow::Command`, a move-only function object queued in `pendingCommands` and executed at the start of `renderFrame()`.

### Layout

`LayoutConfig` is simple:

```text
numRows
numCols
numImages() = rows * cols
```

`ImageLayout` converts it into normalized grid rectangles. `bestLayoutForImageCount()` chooses a layout based on target aspect ratio.

### Selection-to-Visible Update

`adjustForNewSelectionOrUpdatedContent()` is a key method:

1. Read selected indices from `ImageList`.
2. Resize `currentImages` to the layout cell count.
3. For each selected image:
   - Load from `ImageList` cache if necessary.
   - Wrap as `ModifiedImage`.
   - Upload texture if valid.
4. Preload next image.
5. Update layout geometry and normal image widget size.
6. Apply previous window geometry mode.

### Zoom and Sampling

The C++ zoom model is:

```text
zoomFactor: integer, starts at 1
uvCenter: normalized center
uv0/uv1: computed as a 1/zoomFactor ROI around uvCenter, clamped to [0,1]
Ctrl-left click: zoom in at cursor
Ctrl-right click: zoom out
```

When the image is smaller than its natural size and not zoomed, C++ temporarily enables linear filtering. Otherwise it uses nearest sampling.

### Pixel Inspection Overlay

Files:

- `libzv/ImageCursorOverlay.h`
- `libzv/ImageCursorOverlay.cpp`
- parts of `ImageWindow.cpp`

The C++ viewer computes:

- Mouse position in widget coordinates.
- UV position in displayed ROI.
- Pixel coordinate in each visible image.
- sRGBA and HSV values.

It renders:

- A dark status strip with filename, pixel coordinate, sRGBA, HSV.
- Cross-image cursor circles at corresponding positions.
- Optional tooltip-style cursor overlay in controls window.
- Clipboard text with sRGB, linear RGB, HSV, Lab, XYZ.

## ControlsWindow

Files:

- `libzv/ControlsWindow.h`
- `libzv/ControlsWindow.cpp`

Responsibilities:

- Optional secondary window sharing the main GL context.
- File open/save dialogs.
- Pending-change confirmation dialogs.
- Delete confirmation dialog.
- Menu bar.
- Image list table with filter.
- Modifier toolbar.
- Active tool controls.
- Color editor tab.
- Annotation controls.
- Cursor overlay tooltip/status.

Controls are split into tabs:

- Image List.
- Modifiers.
- Color Editor.
- Annotations.

Important behavior:

- Right-click in image window toggles controls.
- Many menu items enqueue `ImageWindowAction`s.
- Open/save uses ImGuiFileDialog and platform-specific native dialogs where available.
- Save all changes walks `ImageWindow` visible modified images.

## Interactive Tools

### `InteractiveTool`

Files:

- `libzv/InteractiveTool.h`
- `libzv/InteractiveTool.cpp`

Tool interface:

```text
renderAsActiveTool(context)
renderControls(firstImage)
addToImage(modifiedImage)
overrideImageRendering(context) -> optional replacement texture
handleKeyEvent(key, io) -> consumed
```

Tools currently include:

- `CropTool`
- `AnnotationTool`
- `ColorEditorTool`

## Annotation Model

Files:

- `libzv/Annotations.h`
- `libzv/Annotations.cpp`
- `libzv/AnnotationTool.h`
- `libzv/AnnotationTool.cpp`

The data model is strong:

- Geometry is stored in normalized texture coordinates.
- Stroke widths and font sizes are stored in image-space pixels.
- Elements have stable `AnnotationId`s.
- Supported elements: line, rectangle, ellipse, text.
- Hit-testing works in widget space through `WidgetToImageTransform`.
- Rendering helpers are shared between live overlay and offscreen compositing.

`AnnotationTool` owns:

- Current mode.
- Selected annotation ID.
- Creation drag state.
- Edit drag state.
- Default styles.
- Per-image undo snapshots.
- Callback to apply edits to all visible images.

## Color Editor and Color Conversion

Files:

- `libzv/ColorEditorTool.h`
- `libzv/ColorEditorTool.cpp`
- `libzv/ColorConversion.h`
- `libzv/ColorConversion.cpp`
- `libzv/ImageColorStats.h`
- `libzv/ImageColorStats.cpp`

The color editor includes:

- Histogram/statistics.
- Levels adjustment by luma/R/G/B.
- GPU preview via LUT texture and shader.
- Hue shift preview via shader.
- One-shot color actions: invert, grayscale, channel swaps, histogram equalization, label colorize.
- Commit-to-modifier pipeline.

## Help, Preferences, Icons, Platform Glue

Files:

- `HelpWindow.*`
- `Prefs.*`
- `Icon.*`
- `PlatformSpecific_*`
- `GLFWUtils.*`
- `ImguiUtils.*`

These support the current ImGui/GLFW app:

- Help window.
- Persistent preferences.
- Icon resources.
- Platform-specific filesystem/app behavior.
- ImGui helper widgets.

## Dependencies in the Current C++ Viewer

The viewer currently vendors or builds:

- GLFW
- Dear ImGui
- ImPlot
- gl3w
- stb_image/stb_image_write
- libjpeg-turbo
- clip
- ImGuiFileDialog
- nativefiledialog-extended on macOS/Windows
- cppuserprefs
- znet for server
