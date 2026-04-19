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
and produces the image data that `ModifiedImage::data()` exposes.

For example, the line tool has two related but separate objects:

```text
LineTool
  owns editable LineAnnotation::Params
  draws the preview overlay in widget/screen space
  owns and updates draggable control points

Apply

LineAnnotation : ImageModifier
  receives a copy of the line parameters
  rasterizes the line into image-sized output
  becomes part of the ModifiedImage modifier chain
```

Before Apply, `LineTool::renderAsActiveTool()` draws the preview with ImGui's
window draw list on top of the displayed image. The line is vector UI rendering
in screen coordinates, scaled through `WidgetToImageTransform` so its apparent
thickness follows the image zoom and matches the intended image-space line
width.

After Apply, `LineAnnotation` is added as an `ImageModifier`. Annotation
modifiers use `AnnotationRenderer`: it renders the input image and annotation
offscreen at the image's native resolution, then downloads the framebuffer back
into `output.cpuData`. At that point the annotation is baked into the modified
image data.

This split keeps interaction responsive and high quality while editing, while
preserving the same modifier pipeline used for save, undo, discard, and
multi-image application.

Current naming can be slightly confusing: `LineAnnotation` is the committed
modifier, not the interactive tool. The interactive object is `LineTool`.

## ActiveToolState vs. Persistent Tools

`ActiveToolState` (in `ImageWindowState.h`) is a **modal, one-at-a-time** tool
slot for transient editing modes like crop and line annotation. Only one can be
active at a time; pressing Escape or Enter exits the mode.

Persistent panel tools like `ColorEditorTool` are **not** stored in
`ActiveToolState`. They are direct members of `ImageWindowState` and are always
present. Their controls are rendered in a dedicated Controls Window tab.

## Controls Window Tab Architecture

The Controls Window (`ControlsWindow.cpp`) owns the tab bar. Tabs are rendered
in `renderFrame()` using `ImGui::BeginTabBar("TabBar", ...)`. To programmatically
switch to a tab (e.g. when the user presses `e`), set a `bool requestXxxTab`
flag on `ControlsWindow::Impl` and pass `ImGuiTabItemFlags_SetSelected` to
`BeginTabItem` on the next frame, then clear the flag.

The public entry point is `ControlsWindow::requestColorEditorTab()`. It is
called from `Viewer::renderFrame()` when `ViewerState::showColorEditorRequested`
is set. Showing the controls window and requesting a tab are two separate
concerns handled in sequence in `Viewer::renderFrame()`.

## Image Rendering Override Hook

`InteractiveTool` has a new non-pure virtual method:

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

## ImGui Version Note

The vendored ImGui does not include `ImGui::SeparatorText()` (added in ImGui
1.89.6). Use `ImGui::Separator()` + `ImGui::Text()` instead for labelled
section dividers.
