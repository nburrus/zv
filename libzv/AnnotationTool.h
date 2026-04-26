//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/Annotations.h>
#include <libzv/InteractiveTool.h>
#include <libzv/MathUtils.h>
#include <libzv/Modifiers.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace zv
{

// Persistent annotation editor. Owns the current annotation mode, transient
// in-progress drag state, and routes mouse events into AnnotationDocument
// edits across all visible valid images.
class AnnotationTool : public InteractiveTool
{
public:
    enum class Mode
    {
        Select,   // click existing = select; click empty = deselect
        AddLine,  // click always starts a new line (no selecting existing)
        AddRectangle, // drag always starts a new rectangle
        AddEllipse, // drag always starts a new ellipse
        AddText,  // click always places new text (no selecting existing)
    };

    // Applies an operation to every visible image with valid data. The
    // ImageWindow rebinds this each frame so the tool can fan edits across
    // the current selection without owning lifetime of the ModifiedImages.
    using ApplyToVisibleImagesFunc = std::function<void(const std::function<void(ModifiedImage&)>&)>;

    AnnotationTool();

    Mode mode() const { return _mode; }
    void setMode(Mode mode);
    bool cancelCurrentAction();

    void setApplyToVisibleImagesFunc(ApplyToVisibleImagesFunc f) { _applyFunc = std::move(f); }

    AnnotationId selectedId() const { return _selectedId; }
    void clearSelection() { _selectedId = {}; }

    // Returns true if the selection changed to a new valid id since lastSeen,
    // and updates lastSeen to the current selection.
    bool selectionChangedSince(AnnotationId& lastSeen) const;
    void deleteSelected();

    bool isCreating() const { return _creatingShape; }

    // Commits a new line annotation to every visible image (via _applyFunc),
    // returns its newly-allocated id, and selects it. Used by the in-tool
    // drag flow and also by tests that want to exercise the create path
    // without simulating an ImGui drag.
    AnnotationId commitNewLine(const LineAnnotationData& data);
    AnnotationId commitNewRectangle(const RectangleAnnotationData& data);
    AnnotationId commitNewEllipse(const EllipseAnnotationData& data);

    // Commits a new text annotation to every visible image (via _applyFunc),
    // returns its newly-allocated id, and selects it.
    AnnotationId commitNewText(const TextAnnotationData& data);

    // InteractiveTool interface.
    void renderAsActiveTool(const InteractiveToolRenderingContext& context) override;
    void renderControls(const ImageSRGBA& firstIm) override;
    // Annotation edits happen live on the documents — there's no Apply step.
    void addToImage(ModifiedImage&) override {}
    bool handleKeyEvent(ImGuiKey key, const ImGuiIO& io) override;

private:
    // --- Drag creation for line/rectangle/ellipse ---
    enum class CreationKind { None, Line, Rectangle, Ellipse };
    void startDragCreation(CreationKind kind, const Point& textureStart);
    void updateDragCreation(const Point& textureCurrent);
    void finishDragCreation();
    void cancelDragCreation();

    // --- Text creation ---
    void createTextAtPosition(const Point& texturePos, int imageWidth, int imageHeight,
                              float imagePixelToScreenPixel);

    // Apply color/width from style to the currently selected line across all images.
    void applySelectedLineStyle(const LineAnnotationData& style);
    void applySelectedRectangleStyle(const RectangleAnnotationData& style);
    void applySelectedEllipseStyle(const EllipseAnnotationData& style);

    // Apply text/color/fontSize from style to the currently selected text across all images.
    void applySelectedTextStyle(const TextAnnotationData& style,
                                bool growFontToExistingBox,
                                bool shrinkFontToExistingBox);

    // --- Edit drag (move / handle-resize an existing annotation) ---
    enum class EditDragKind { None, Body, Handle };

    // Snapshot of a single annotation element captured before an edit.
    struct ElementSnapshot
    {
        bool valid = false;
        AnnotationElement::Kind kind = AnnotationElement::Kind::Line;
        LineAnnotationData lineData;
        RectangleAnnotationData rectangleData;
        EllipseAnnotationData ellipseData;
        TextAnnotationData textData;
    };

    // Capture per-image snapshots of the selected element into _selectedEditSnapshots,
    // keyed by ImageItem::uniqueId, for the next committed edit (drag or style edit).
    void captureSelectedEditSnapshots();

    // Push per-image undo actions that restore the snapshots from the last
    // captureSelectedEditSnapshots() (call before clearing _selectedEditSnapshots).
    void pushSelectedEditUndo();

    // Cancel the current edit drag without committing any undo action (e.g.,
    // on mode switch or when the gesture produced no movement).
    void cancelEditDrag();

    static constexpr float kHandleRadius  = 6.0f;
    static constexpr float kBodyTolerance = 4.0f;

    // --- State ---
    Mode _mode = Mode::Select;
    AnnotationId _selectedId;
    ApplyToVisibleImagesFunc _applyFunc;
    // Cached image dimensions and pixel scale from the last rendered frame
    // (first valid image). Used to keep the text hit-box in sync with content.
    int   _lastImageWidth              = 0;
    int   _lastImageHeight             = 0;
    float _lastImagePixelToScreenPixel = 1.0f;

    // Drag creation
    bool _creatingShape = false;
    CreationKind _creationKind = CreationKind::None;
    AnnotationId _creationId;
    Point _creationStartTexture;
    Point _creationCurrentTexture;
    LineAnnotationData _defaultLineStyle;
    RectangleAnnotationData _defaultRectangleStyle;
    EllipseAnnotationData _defaultEllipseStyle;

    // Cached properties of the currently selected annotation (updated each frame
    // in renderAsActiveTool so renderControls can read and edit them).
    LineAnnotationData _selectedLineData;
    bool _selectedLineDataValid = false;
    RectangleAnnotationData _selectedRectangleData;
    bool _selectedRectangleDataValid = false;
    EllipseAnnotationData _selectedEllipseData;
    bool _selectedEllipseDataValid = false;
    TextAnnotationData _selectedTextData;
    bool _selectedTextDataValid = false;
    // Buffer for InputText editing of selected text content.
    char _textEditBuffer[1024] = {};
    bool _propertyEditActive = false;

    // Default styles for new annotations.
    TextAnnotationData _defaultTextStyle;

    // Edit drag
    EditDragKind _editDragKind    = EditDragKind::None;
    bool         _editDragActive  = false;
    bool         _editDragMoved   = false;
    int          _editDragHandleIdx = -1;
    Point        _editDragPrevTexture;
    // Per-image pre-edit snapshots, keyed by ImageItem::uniqueId.
    std::unordered_map<ImageId, ElementSnapshot> _selectedEditSnapshots;
};

} // zv
