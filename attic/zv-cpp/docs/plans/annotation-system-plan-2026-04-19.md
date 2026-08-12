# Annotation System Rework Plan

## Goal

Rework annotations into a Preview-style editable annotation layer.

Users should be able to create multiple annotations, select them, move them,
resize them, edit their style/content, delete them, undo edits, and save the
result as pixels. Annotations should behave like editable objects while the app
session is active, instead of being baked immediately as independent modifier
stack entries.

The Rust viewer currently focuses on line, arrow, rectangle, and ellipse
annotations. Text, freehand, and richer editing can be added once the core
object model and interaction model are stable.

Because ZV is still pre-alpha, this rework does not need to preserve or migrate
the existing annotation implementation. The old one-shot `LineTool` /
`LineAnnotation` path can be removed as soon as the new annotation model replaces
it.

## Current State

The current annotation implementation treats each applied line as an
`ImageModifier`:

- `LineTool` owns a single editable line while active.
- Pressing Apply creates a `LineAnnotation` modifier.
- The modifier rasterizes the line through `AnnotationRenderer`.
- The line is then baked into the modifier chain and is no longer editable.

This works for simple cumulative drawing, but it does not support modern
annotation behavior:

- There is no persistent annotation selection model.
- Existing annotations cannot be moved, resized, restyled, or deleted
  individually.
- Each annotation is ordered wherever it was added in the normal modifier
  stack, not as a final overlay.
- Undo only removes the last modifier, which is too coarse for object-level
  annotation edits.

## Chosen Direction

Use one editable annotation layer per image, rendered and applied after all
normal image modifiers.

Conceptually:

```text
original image
  -> normal modifiers: crop, resize, rotate, color edits, ...
  -> annotation layer modifier: all editable annotations
  -> displayed/saved image
```

The annotation layer is not repeatedly appended as one modifier per shape.
Instead, each `ModifiedImage` owns or references an `AnnotationDocument`, and a
single pinned final annotation compositor produces the final displayed/saved
pixels whenever the document changes.

This is the best fit for Preview-style editing because the annotation document
remains structured and editable until save/discard, while the existing
`ModifiedImage::data()` and save pipeline can still expose a fully composited
image.

## User-Facing Behavior

### Tools

V1 annotation modes:

- Select/edit annotations.
- Add line.
- Add arrow.
- Add rectangle.
- Add ellipse.
- Add text.

Line behavior:

- Click-drag creates a new line.
- Clicking an existing line selects it.
- Dragging the line body moves the whole line.
- Dragging endpoint handles resizes/repositions the line.
- Selected line controls expose color and line width.

Text behavior:

- Clicking in add-text mode creates a text annotation with default text
  `Text`.
- Clicking an existing text annotation selects it.
- Dragging the text body moves it.
- Dragging handles resizes the text box.
- Selected text controls expose text content, color, and font size.

Selection behavior:

- Only one annotation is selected at a time in V1.
- Hit-testing prioritizes selected handles first, then annotations from
  topmost to bottommost.
- Clicking empty image space clears selection.
- Delete/backspace deletes the selected annotation while the annotation tool is
  active.
- Escape exits annotation mode and clears selection.

### Multi-Image Behavior

V1 keeps the current comparison-oriented behavior: annotation creation and edits
apply to all currently visible valid images.

- Creating a new annotation inserts corresponding annotations into all visible
  valid images.
- Corresponding annotations share a stable annotation id across those images.
- Moving, resizing, editing style/content, or deleting the selected annotation
  applies to every visible image that has the same annotation id.
- Images shown later are not retroactively annotated.
- If an annotation id is missing from one visible image, the edit is skipped for
  that image.

If the user changes which images are visible while editable annotations exist,
they are prompted to apply (bake) the current annotations before the change takes
effect. This matches the existing behavior for modifiers and avoids the
complexity of retroactively mirroring or merging annotations across different
sets of visible images.

This keeps CV comparison workflows useful while still moving toward an editable
object model.

### Save, Discard, Undo

Editable annotation state is session-only in V1.

- Save writes the composited image pixels with annotations included.
- After a successful save, normal modifiers and editable annotation state are
  cleared, matching the current destructive save model.
- Discard clears normal modifiers and editable annotations.
- Undo supports annotation operations as first-class undoable actions.
- Undo restores annotation document snapshots for the affected visible images.

No sidecar file, embedded metadata, or project/document persistence format is
introduced in V1.

## Core Design

### Annotation Document

Add an annotation document model in `libzv/Annotations.h/.cpp`.

Core types:

- `AnnotationId`: stable id shared by corresponding annotations across visible
  images. Implemented as a monotonically increasing integer per session.
- `AnnotationDocument`: ordered collection of annotation elements.
- `AnnotationElement`: tagged variant or polymorphic base for V1 element types.
- `LineAnnotationElement`: normalized endpoints plus stroke style.
- `TextAnnotationElement`: normalized text box plus text style/content.
- `AnnotationSelection`: optional selected annotation id plus selected handle,
  if any. No-selection state is represented as an absent id, not a sentinel
  value.

Geometry should be stored in normalized texture coordinates so annotations
survive zoom and display-size changes. Style values that must match final pixel
output, such as line width and font size, should be stored in image-space units
or explicitly converted during render.

Document operations:

- Add line/text.
- Remove by id.
- Find by id.
- Move by normalized delta.
- Resize through named handles.
- Update style/content.
- Hit-test in widget space using a `WidgetToImageTransform`.
- Render final pixels through `AnnotationRenderer`.
- Render editable overlays through the active image window draw list.

### Final Annotation Layer

Replace single-shape committed annotations with a final annotation layer.

The final layer should:

- Composite the complete `AnnotationDocument`.
- Run after all normal modifiers.
- Recompute when either the input image data changes or the annotation document
  is dirty.
- Use the existing `AnnotationRenderer` path for native-resolution rasterized
  output.
- Reuse the same element rendering helpers as the live overlay, so preview and
  saved output stay visually consistent.

Implementation can be either:

- a concrete `AnnotationLayerModifier` pinned outside the normal modifier deque,
  or
- equivalent final-layer logic directly inside `ModifiedImage`.

The important invariant is that normal modifiers never get appended after the
annotation compositor. Crop, resize, rotate, and color edits must always feed
the annotation layer, not consume already annotated pixels.

### ModifiedImage Integration

Extend `ModifiedImage` with annotation state and final-layer recomputation.

Expected responsibilities:

- Own an `AnnotationDocument`.
- Track whether the annotation document is dirty.
- Return composited data from `data()` when annotations exist.
- Include annotations in `hasPendingChanges()`.
- Clear annotations on successful save and discard.
- Register undo actions for annotation edits.
- Recompute final annotation output after normal modifiers are recomputed.

Undo should snapshot the annotation document for each affected image before an
operation, then restore those snapshots if the action is undone.

### Annotation Tool

Replace `LineTool` with an `AnnotationTool`.

The tool should be a persistent annotation editor/controller, not a one-shot
modifier tool:

- It owns the current annotation mode: select, add line, add text.
- It owns transient interaction state: active drag, selected handle, drag start
  document snapshots, and pending text creation.
- It renders handles and selection outlines over each image.
- It routes input events for annotation editing when active.
- It requests document edits on `ModifiedImage` rather than appending normal
  modifiers.

The old Apply/Cancel flow should not be used for annotation edits. Edits happen
live and become undoable at gesture boundaries.

### ImGui Interaction Strategy

Use Dear ImGui as the input and drawing substrate, not as the annotation object
model.

V1 should use bare ImGui plus `ImDrawList`:

- Do not create one ImGui widget per annotation or per handle.
- Do not introduce a canvas/vector-editor dependency for V1.
- Render live overlays with `ImDrawList` calls such as `AddLine`, `AddRect`,
  `AddCircleFilled`, and `AddText`.
- Perform manual hit-testing in widget/screen space from the annotation
  document.
- Keep selection, active handle, drag kind, drag start position, and undo
  snapshots in `AnnotationTool`.
- Treat ImGui item state from the underlying `ImGui::Image()` as the input
  boundary: image item rect, hover state, click state, mouse position, UV ROI,
  and `WidgetToImageTransform`.

Hit-testing should use screen-space tolerances so annotations remain selectable
at different zoom levels:

- Line endpoints: small circular handle radius.
- Line body: distance to segment plus stroke/tolerance.
- Text body: transformed text box bounds.
- Text resize handles: corner or edge handles.

For V1, edit text content in the Controls Window for the selected text
annotation. Inline text editing on top of the image is deferred because it adds
focus, keyboard routing, clipping, selection, zoom, and multi-image mirroring
complexity.

## UI Plan

- Keep resize, crop, rotate, and color editor behavior unchanged.
- Keep the annotation tool active whenever no other tool, such as crop, occupies
  the tool slot. This lets the user click an existing annotation from any tab
  without first navigating to Modifiers.
- Add toolbar buttons for line and text creation in the Modifiers tab.
- Treat Select mode as the implicit default. Clicking the line or text button
  enters a one-shot creation mode; after creation, the tool returns to Select
  mode with the new annotation selected.
- Show annotation controls below the modifier toolbar only when a new annotation
  is being created or an existing annotation is selected.
- Line controls expose color and stroke width.
- Text controls expose content, color, and font size.
- When a new annotation is selected while the Modifiers tab is not visible, bring
  the Modifiers tab to front using the same `SetSelected` mechanism used by the
  Color Editor tab.
- Add `Tools > Annotate > Add Line` and `Tools > Annotate > Add Text`.
- Delete/backspace deletes the selected annotation while annotation mode is
  active.
- Escape exits annotation mode and clears selection.
- Enter does not apply annotations because annotation edits are already live.
- Keep the existing crop tool on the old Apply/Cancel active-tool model.

## Implementation Phases

### Phase 1: Data Model And Rendering Helpers

Introduce the editable annotation document without changing UI behavior yet.

Deliverables:

- Add `AnnotationDocument` and V1 element types.
- Add stable annotation ids.
- Add model operations for add, remove, find, move, resize, and style/content
  updates.
- Add hit-test helpers with screen-space tolerances.
- Add shared rendering helpers for line and text elements.
- Remove or bypass the existing `LineTool`/`LineAnnotation` path as soon as the
  new model is ready to own line annotations. No migration compatibility layer
  is required.

Tests:

- Add/remove/find elements.
- Stable ids are preserved through copies/snapshots.
- Move and resize update normalized geometry correctly.
- Hit-testing prioritizes handles over bodies and topmost elements over older
  elements.

### Phase 2: Final Annotation Layer In ModifiedImage

Add the pinned final annotation compositor and connect it to `ModifiedImage`.

Deliverables:

- Add per-image annotation document ownership.
- Add final annotation output storage and dirty tracking.
- Ensure `data()` returns annotated output when annotations exist.
- Ensure normal modifiers recompute before annotation output.
- Update save/discard/pending-change behavior to include annotations.
- Do not preserve the old line annotation UI for compatibility. It can be
  disabled or removed while the new annotation tool is being wired.

Tests:

- Annotation layer composites after color/geometry modifiers.
- Adding a normal modifier after annotations still leaves annotations last.
- `hasPendingChanges()` is true for annotation-only edits.
- Save clears editable annotation state after promoting composited pixels.
- Discard clears annotations.

### Phase 3a: AnnotationTool Infrastructure And Line Creation

Wire up the tool, input routing, and line creation. No move/resize yet.

Note: between Phase 1 and this phase, no annotation UI exists. That is
intentional for a pre-alpha codebase.

Deliverables:

- Add `AnnotationTool` with select and add-line modes.
- Route image-window mouse input to the tool when annotation mode is active.
- Create mirrored line annotations across visible valid images on click-drag.
- Render line overlays (no selection handles yet).
- Add undo snapshots for create and delete.
- Remove the old `LineTool` Apply path.

Tests:

- Creating a line adds matching ids to all visible valid images.
- Delete removes the selected id from all visible documents.
- Undo restores the document after create and delete.

### Phase 3b: Line Selection, Move, And Resize

Add interactive editing of existing line annotations.

Deliverables:

- Select lines by hit-test.
- Move selected line body.
- Drag endpoint handles to resize/reposition.
- Render selection handles and line overlays.
- Add undo snapshots for move, resize, and style edit.

Tests:

- Move/resize applies to visible images with the selected id.
- Undo restores all affected visible documents.

Manual checks:

- Line thickness looks consistent between live overlay and saved rasterization.
- Ctrl-click zoom behavior does not conflict with annotation dragging.
- Cursor overlay remains useful when not actively dragging annotations.

### Phase 4: Text Annotations (Planned)

Add text annotations on top of the same model/tool pipeline.

Deliverables:

- Add text annotation creation.
- Add selected text content editing in controls.
- Add color and font-size controls.
- Add text-box move support. Text resize handles are intentionally omitted for
  V1: text boxes auto-size to content, so corner handles do not provide a useful
  editing behavior until wrapping/fixed-width text boxes exist.
- Composite text in the final annotation layer.
- Include text edits in undo snapshots.

Tests:

- Text creation mirrors across visible valid images.
- Text content/style edits apply to corresponding visible annotations.
- Text move updates normalized text box geometry.
- Undo restores text content, style, and geometry.

Manual checks:

- Text remains legible at common zoom levels.
- Saved text position and size match the live overlay closely.
- Long text does not crash or corrupt drawing; clipping/wrapping behavior is
  acceptable for V1.

### Phase 5: UI Polish And Cleanup (In Progress)

Remove obsolete annotation paths and make the workflow coherent.

Deliverables:

- Remove old `LineTool` and one-shape `LineAnnotation` modifier path if no
  longer needed.
- Update `docs/DESIGN.md` to describe the new annotation layer architecture.
- Add menu entries for annotation modes.
- Audit keyboard handling for Escape, Enter, Delete, Backspace, and Ctrl+Z.
- Ensure no annotation-specific state leaks into crop or color editor behavior.

Tests:

- Existing modifier tests still pass.
- Existing color editor behavior still works with annotations present.
- Undo works across mixed operations: color edit, line create, text create,
  line move, normal modifier.

### Phase 6: Rectangle And Ellipse Extensions (Rust: Complete)

Rectangle and ellipse annotations are implemented in the Rust viewer.

Deliverables:

- Add rectangle and ellipse element variants with normalized bounds.
- Create rectangle and ellipse annotations by click-drag and mirror them across
  visible valid images.
- Select and move rectangle and ellipse annotations; resize them with four
  corner handles.
- Keep the opposite corner fixed throughout a resize so the dragged corner can
  cross it and continue growing in the other direction.
- Hold Shift while creating or resizing to constrain rectangles to squares and
  ellipses to circles; constrain lines and arrows to 45-degree increments.
- Hit-test rectangle and ellipse strokes in screen space.
- Render the same outline geometry in the live overlay and final compositor.
- Include creation, deletion, movement, resizing, rotation, and undo.
- Expose toolbar/menu actions and Shift+R / Shift+E shortcuts.
- Expose color and line-width controls for new and selected rectangles and
  ellipses, with live mirrored updates and gesture-level undo.
- Share stroke style, named resize handles, annotation-kind metadata, rendering
  helpers, controls, and whole-element undo across annotation types.
- Carry any edited annotation stroke color and width forward as the shared
  default for subsequently created lines, arrows, rectangles, and ellipses.

Tests:

- Rectangle corner resizing preserves the opposite corner.
- Ellipse hit-testing selects its border rather than its interior.

### Phase 7: Deferred Extensions

These are intentionally out of V1 unless the core system proves stable:

- Additional arrowhead and callout styles.
- Freehand drawing.
- Multi-select and group move.
- Annotation z-order controls.
- Copy/paste annotations.
- Fixed-width/wrapping text boxes with resize handles.

## Open Risks

- `ModifiedImage::data()` currently assumes the last modifier owns final output.
  The final annotation layer must preserve that mental model for callers.
- There is no migration requirement for old annotation behavior or saved
  annotation state; this is an intentional pre-alpha simplification.
- Text rendering through the existing offscreen ImGui renderer may need careful
  font-size handling to match live overlay output.
- Undo snapshots are simple and robust for V1, but large annotation documents
  could eventually need delta-based undo.
- Mirrored multi-image edits require stable ids and clear skip behavior when a
  visible image lacks the selected id.
- Crop/resize after annotations does not move or remap annotation coordinates.
  Annotations are in normalized texture coordinates of the final composited
  image; adding a geometry modifier beneath them leaves the annotation document
  untouched. The visual result may shift if the geometry modifier changes the
  image's aspect ratio or crop, but this is acceptable for V1.
- Text rendering through the existing offscreen ImGui renderer requires the
  ImGui font atlas to be built at a scale appropriate for the native image
  resolution, not the display resolution. If the atlas was built for display
  DPI, rasterized text at high-resolution images will appear small. This needs
  careful font-size handling — possibly a separate atlas or explicit scaling —
  to match the live overlay output.

## Current Rust Acceptance Criteria

- Users can create multiple line, arrow, rectangle, and ellipse annotations.
- Users can select, move, resize, delete, and undo those annotations.
- Line, arrow, rectangle, and ellipse stroke color/width can be edited.
- Annotation edits mirror across currently visible valid images.
- Annotations are always displayed and saved after normal image modifiers.
- Saving bakes annotations into pixels and clears editable annotation state.
- Existing crop, resize, rotate, color editor, save, discard, and undo behavior
  remains intact for non-annotation workflows.

Text-specific acceptance criteria remain part of planned Phase 4.
