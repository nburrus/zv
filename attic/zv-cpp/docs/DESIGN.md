# Design Notes

## Interactive Tools vs. Image Modifiers

ZV intentionally separates live editing UI from committed image changes.

An `InteractiveTool` is the live editor and preview layer. It owns editable
parameters, renders overlays in the viewer with `renderAsActiveTool()`, renders
controls in the controls window with `renderControls()`, and handles draggable
control points. This state is UI-only: while the tool is active, it does not
change the image pixels.

An `ImageModifier` is the committed image-processing layer. When the user
presses Apply, the active tool creates a modifier from its current parameters by
calling `addToImage()`. The modifier is appended to the `ModifiedImage` pipeline
and produces the image data that `ModifiedImage::preAnnotationData()` and
`ModifiedImage::finalData()` expose.

Most tools follow the preview-then-Apply pattern. For example, crop owns its
transient rectangle while active; pressing Apply appends a crop modifier to the
`ModifiedImage` pipeline.

Annotations are the exception. The annotation UI is persistent and edits a
per-image `AnnotationDocument` directly — there is no Apply step.

## Annotation System

### Data model

`AnnotationDocument` owns a flat list of `AnnotationElement` objects. Each
element has a stable `AnnotationId` (a process-global `uint64_t` counter),
a kind (`Line` or `Text`), and the corresponding data struct
(`LineAnnotationData` or `TextAnnotationData`). All geometry is stored in
**normalized texture coordinates** ([0, 1] in each axis) so the same data can
be rendered at any zoom level. Stroke widths and font sizes are stored in
**image-space pixels**; callers convert to widget-space pixels when rendering
live overlays.

### ModifiedImage pipeline

Each `ModifiedImage` owns an `AnnotationDocument` and a separate composited
output buffer. The data pipeline is:

```
originalData
  → modifier chain (crop, resize, levels, …)  → preAnnotationData()
  → annotation compositor                     → annotatedData (cached)
                                               → finalData()
```

`finalData()` returns `annotatedData` if it exists and is ready; otherwise it
falls back to `preAnnotationData()`. `ModifiedImage::updateAnnotations()` drives
the compositor each frame via `AnnotationRenderer` (an offscreen ImGui context
at native image resolution). Callers set `markAnnotationsDirty()` after mutating
the document; the compositor reruns on the next `updateAnnotations()` call.
Saving bakes `finalData()` to disk and clears the editable annotation state.

### AnnotationTool fan-out and undo

`AnnotationTool` edits every currently-visible valid `ModifiedImage` in lockstep
through an `ApplyToVisibleImagesFunc` callback that `ImageWindow` rebinds each
frame. The tool allocates a single `AnnotationId` and fans it to all images so
the same logical annotation has the same id everywhere.

Before any interactive drag or style edit begins, `captureSelectedEditSnapshots()`
records the pre-edit state of the selected element into a
`std::unordered_map<ImageId, ElementSnapshot>` keyed by each image's stable
`ImageItem::uniqueId`. When the gesture ends, `pushSelectedEditUndo()` looks up
each image's snapshot by `uniqueId` and registers a per-image undo action, so
images that became valid or invalid mid-gesture are handled gracefully.

### Rendering

Live overlays (selection handles, in-progress line preview) are drawn directly
into the active ImGui window's draw list inside `renderAsActiveTool()`.
Committed annotation pixels are produced by `compositeAnnotationLayer()`, which
uses the offscreen `AnnotationRenderer` (a separate ImGui context sharing the
main font atlas) to render the annotation document at native image resolution
into `ModifiedImage::_annotatedData`.

The shared rendering helpers `renderLineAnnotation()`, `renderTextAnnotation()`,
and `renderAnnotationElement()` (declared in `Annotations.h`, defined in
`Annotations.cpp`) are used by both the live overlay and the offscreen
compositor so the two paths stay in sync.

## ActiveToolState and the Annotation Invariant

`ActiveToolState` (in `ImageWindowState.h`) is a **modal, one-at-a-time** tool
slot. Only one kind can be active at a time.

`AnnotationTool` is always kept in this slot (`Kind::Annotate`). `ControlsWindow`
re-activates it every frame whenever the slot would otherwise be idle, so
annotation selection and hit-testing work globally regardless of which Controls
Window tab is visible. `Kind::None` is therefore a transient state that
immediately becomes `Kind::Annotate` on the next frame.

Other tools (`Transform_Crop`, etc.) take over the slot for their duration.
Pressing Escape cancels in-progress annotation gestures (line draw, mode change);
if nothing is in progress the slot is set to `None`, which immediately reverts to
`Annotate`. Pressing Escape while in Crop mode exits crop.

Persistent panel tools like `ColorEditorTool` are **not** stored in
`ActiveToolState`. They are direct members of `ImageWindowState` and are always
present; their controls live in a dedicated Controls Window tab.

## Controls Window Tab Architecture

The Controls Window (`ControlsWindow.cpp`) owns the tab bar. Tabs are rendered
in `renderFrame()` using `ImGui::BeginTabBar("TabBar", ...)`. To programmatically
switch to a tab (e.g. when an annotation is selected while on the Images tab),
set a `bool requestXxxTab` flag on `ControlsWindow::Impl` and pass
`ImGuiTabItemFlags_SetSelected` to `BeginTabItem` on the next frame, then clear
the flag.

The public entry point for the color editor is
`ControlsWindow::requestColorEditorTab()`. It is called from
`Viewer::renderFrame()` when `ViewerState::showColorEditorRequested` is set.
Showing the controls window and requesting a tab are two separate concerns
handled in sequence in `Viewer::renderFrame()`.

## Image Rendering Override Hook

`InteractiveTool` has a non-pure virtual method:

```cpp
virtual ImageRenderingOverride overrideImageRendering(const ImageRenderingContext&);
```

`ImageWindow::Impl::renderImageItem()` calls this before `ImGui::Image()` and
substitutes the texture ID if the override returns a non-zero `overrideTextureId`.
This lets a persistent tool (e.g. `ColorEditorTool`) render a GPU-processed
version of the image without modifying `ModifiedImage` or the committed modifier
stack.

## Key Shortcuts → Actions → Viewer Flow

Keyboard shortcuts in `ImageWindow.cpp` enqueue an `ImageWindowAction::Kind`
value. The action handler in `ImageWindow::runAction()` calls methods on
`impl->viewer` for anything that requires cross-window coordination. The
`Viewer` translates these into state flags on `ViewerState` or direct calls on
`ControlsWindow`. New keyboard shortcuts follow this pattern:

```
GLFW key handler → enqueueAction(Kind::Foo)
  → ImageWindow::runAction → viewer->onFoo()
    → ViewerState::fooRequested = true
      → Viewer::renderFrame() handles the flag
```

Delete and Backspace are intercepted before the action queue when an annotation
is selected: they call `AnnotationTool::deleteSelected()` directly and do not
propagate to the image-navigation actions.

## ImGui Version Note

The vendored ImGui does not include `ImGui::SeparatorText()` (added in ImGui
1.89.6). Use `ImGui::Separator()` + `ImGui::Text()` instead for labelled
section dividers.
