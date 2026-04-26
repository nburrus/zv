//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "AnnotationTool.h"

#include <libzv/ImguiUtils.h>

#include <imgui.h>

#include <cfloat>
#include <cmath>

namespace zv
{

AnnotationTool::AnnotationTool()
    : InteractiveTool(Kind::Annotation)
{}

bool AnnotationTool::handleKeyEvent(ImGuiKey key, const ImGuiIO& /*io*/)
{
    if ((key == ImGuiKey_Backspace || key == ImGuiKey_Delete) && _selectedId.isValid())
    {
        deleteSelected();
        return true;
    }
    return false;
}

void AnnotationTool::setMode(Mode mode)
{
    if (_mode == mode)
        return;
    cancelDragCreation();
    cancelEditDrag();
    _propertyEditActive = false;
    _mode = mode;
    _selectedId = {};
    _selectedLineDataValid = false;
    _selectedRectangleDataValid = false;
    _selectedEllipseDataValid = false;
    _selectedTextDataValid = false;
}

bool AnnotationTool::cancelCurrentAction()
{
    bool consumed = false;

    if (_creatingShape)
    {
        cancelDragCreation();
        consumed = true;
    }

    if (_mode != Mode::Select)
    {
        setMode(Mode::Select);
        consumed = true;
    }

    return consumed;
}

bool AnnotationTool::selectionChangedSince(AnnotationId& lastSeen) const
{
    const AnnotationId current = _selectedId;
    const bool changed = current.isValid() && current != lastSeen;
    lastSeen = current;
    return changed;
}

void AnnotationTool::deleteSelected()
{
    if (!_selectedId.isValid() || !_applyFunc)
        return;

    // Drop any in-progress drag or property edit on the element being deleted.
    cancelEditDrag();
    _propertyEditActive = false;

    const AnnotationId idToDelete = _selectedId;
    _applyFunc([idToDelete](ModifiedImage& im) {
        AnnotationElement* el = im.annotations().findById(idToDelete);
        if (!el)
            return;

        const AnnotationElement::Kind kind = el->kind();
        LineAnnotationData lineData;
        TextAnnotationData textData;
        RectangleAnnotationData rectangleData;
        EllipseAnnotationData ellipseData;
        if (kind == AnnotationElement::Kind::Line)
            lineData = el->asLine();
        else if (kind == AnnotationElement::Kind::Rectangle)
            rectangleData = el->asRectangle();
        else if (kind == AnnotationElement::Kind::Ellipse)
            ellipseData = el->asEllipse();
        else
            textData = el->asText();

        im.annotations().removeById(idToDelete);
        im.markAnnotationsDirty();

        im.pushUndoAction([&im, idToDelete, kind, lineData, rectangleData, ellipseData, textData]() {
            if (kind == AnnotationElement::Kind::Line)
                im.annotations().addLine(idToDelete, lineData);
            else if (kind == AnnotationElement::Kind::Rectangle)
                im.annotations().addRectangle(idToDelete, rectangleData);
            else if (kind == AnnotationElement::Kind::Ellipse)
                im.annotations().addEllipse(idToDelete, ellipseData);
            else
                im.annotations().addText(idToDelete, textData);
            im.markAnnotationsDirty();
        });
    });

    _selectedId = {};
}

// ---------------------------------------------------------------------------
// Pre-edit snapshots + undo (interactive drag and style edits)
// ---------------------------------------------------------------------------

void AnnotationTool::captureSelectedEditSnapshots()
{
    if (!_applyFunc)
        return;

    _selectedEditSnapshots.clear();
    const AnnotationId id = _selectedId;
    _applyFunc([this, id](ModifiedImage& im) {
        ElementSnapshot snap;
        if (const AnnotationElement* el = im.annotations().findById(id))
        {
            snap.valid = true;
            snap.kind  = el->kind();
            if (snap.kind == AnnotationElement::Kind::Line)
                snap.lineData = el->asLine();
            else if (snap.kind == AnnotationElement::Kind::Rectangle)
                snap.rectangleData = el->asRectangle();
            else if (snap.kind == AnnotationElement::Kind::Ellipse)
                snap.ellipseData = el->asEllipse();
            else
                snap.textData = el->asText();
        }
        _selectedEditSnapshots[im.item()->uniqueId] = snap;
    });
}

void AnnotationTool::pushSelectedEditUndo()
{
    if (!_applyFunc || _selectedEditSnapshots.empty())
        return;

    const AnnotationId id = _selectedId;
    auto snapshots = std::make_shared<std::unordered_map<ImageId, ElementSnapshot>>(
        std::move(_selectedEditSnapshots));
    _selectedEditSnapshots.clear();

    _applyFunc([id, snapshots](ModifiedImage& im) {
        auto it = snapshots->find(im.item()->uniqueId);
        if (it == snapshots->end() || !it->second.valid)
            return;
        const ElementSnapshot snap = it->second;
        im.pushUndoAction([&im, id, snap]() {
            AnnotationElement* el = im.annotations().findById(id);
            if (!el)
                return;
            if (snap.kind == AnnotationElement::Kind::Line)
                el->asLine() = snap.lineData;
            else if (snap.kind == AnnotationElement::Kind::Rectangle)
                el->asRectangle() = snap.rectangleData;
            else if (snap.kind == AnnotationElement::Kind::Ellipse)
                el->asEllipse() = snap.ellipseData;
            else
                el->asText() = snap.textData;
            im.markAnnotationsDirty();
        });
    });
}

void AnnotationTool::cancelEditDrag()
{
    _editDragActive   = false;
    _editDragKind     = EditDragKind::None;
    _editDragHandleIdx = -1;
    _editDragMoved    = false;
    _selectedEditSnapshots.clear();
}

// ---------------------------------------------------------------------------
// Drag creation
// ---------------------------------------------------------------------------

AnnotationId AnnotationTool::commitNewLine(const LineAnnotationData& data)
{
    const AnnotationId createdId = AnnotationId::nextId();

    if (_applyFunc)
    {
        _applyFunc([createdId, data](ModifiedImage& im) {
            im.annotations().addLine(createdId, data);
            im.markAnnotationsDirty();
            im.pushUndoAction([&im, createdId]() {
                im.annotations().removeById(createdId);
                im.markAnnotationsDirty();
            });
        });
    }

    _selectedId = createdId;
    return createdId;
}

AnnotationId AnnotationTool::commitNewRectangle(const RectangleAnnotationData& data)
{
    const AnnotationId createdId = AnnotationId::nextId();

    if (_applyFunc)
    {
        _applyFunc([createdId, data](ModifiedImage& im) {
            im.annotations().addRectangle(createdId, data);
            im.markAnnotationsDirty();
            im.pushUndoAction([&im, createdId]() {
                im.annotations().removeById(createdId);
                im.markAnnotationsDirty();
            });
        });
    }

    _selectedId = createdId;
    return createdId;
}

AnnotationId AnnotationTool::commitNewEllipse(const EllipseAnnotationData& data)
{
    const AnnotationId createdId = AnnotationId::nextId();

    if (_applyFunc)
    {
        _applyFunc([createdId, data](ModifiedImage& im) {
            im.annotations().addEllipse(createdId, data);
            im.markAnnotationsDirty();
            im.pushUndoAction([&im, createdId]() {
                im.annotations().removeById(createdId);
                im.markAnnotationsDirty();
            });
        });
    }

    _selectedId = createdId;
    return createdId;
}

void AnnotationTool::startDragCreation(CreationKind kind, const Point& textureStart)
{
    _creatingShape          = true;
    _creationKind           = kind;
    _creationId             = AnnotationId::nextId();
    _creationStartTexture   = textureStart;
    _creationCurrentTexture = textureStart;
}

void AnnotationTool::updateDragCreation(const Point& textureCurrent)
{
    if (!_creatingShape)
        return;
    _creationCurrentTexture = textureCurrent;
}

void AnnotationTool::finishDragCreation()
{
    if (!_creatingShape)
        return;

    const double dx = _creationCurrentTexture.x - _creationStartTexture.x;
    const double dy = _creationCurrentTexture.y - _creationStartTexture.y;
    if (dx * dx + dy * dy < 1e-8)
    {
        cancelDragCreation();
        return;
    }

    const Rect box = Rect::from_two_points(_creationStartTexture, _creationCurrentTexture);
    if (_creationKind == CreationKind::Line)
    {
        LineAnnotationData lineData = _defaultLineStyle;
        lineData.textureLine = Line(_creationStartTexture, _creationCurrentTexture);
        commitNewLine(lineData);
    }
    else if (_creationKind == CreationKind::Rectangle)
    {
        RectangleAnnotationData rectangleData = _defaultRectangleStyle;
        rectangleData.textureBox = box;
        commitNewRectangle(rectangleData);
    }
    else if (_creationKind == CreationKind::Ellipse)
    {
        EllipseAnnotationData ellipseData = _defaultEllipseStyle;
        ellipseData.textureBox = box;
        commitNewEllipse(ellipseData);
    }

    _creatingShape = false;
    _creationKind  = CreationKind::None;
    _creationId    = {};
    _mode = Mode::Select;
}

void AnnotationTool::cancelDragCreation()
{
    _creatingShape = false;
    _creationKind  = CreationKind::None;
    _creationId    = {};
}

// ---------------------------------------------------------------------------
// Text creation
// ---------------------------------------------------------------------------

// Compute the textureBox size from actual font metrics.
// Must be called inside an ImGui frame (CalcTextSizeA needs the font atlas).
// `imagePixelToScreenPixel` is the display zoom factor (widget_px / image_px).
static ImVec2 textExtentFromFontMetrics(const TextAnnotationData& data,
                                        float imagePixelToScreenPixel)
{
    zv_assert(ImGui::GetCurrentContext() != nullptr, "textExtentFromFontMetrics must be called inside an ImGui frame");
    const float wFontSize = std::max(1.0f, data.fontSize * imagePixelToScreenPixel);
    const char* text = data.text.empty() ? " " : data.text.c_str();
    return ImGui::GetFont()->CalcTextSizeA(wFontSize, FLT_MAX, 0.f, text);
}

static void tuneTextFontSizeToBox(TextAnnotationData& data,
                                  int imageWidth,
                                  int imageHeight,
                                  float imagePixelToScreenPixel,
                                  bool growToFit)
{
    if (imageWidth <= 0 || imageHeight <= 0)
        return;

    const float scale = imagePixelToScreenPixel > 0.f ? imagePixelToScreenPixel : 1.f;
    const ImVec2 extent = textExtentFromFontMetrics(data, scale);
    const ImVec2 pixelBoxSize((float)(data.textureBox.size.x * imageWidth * scale),
                              (float)(data.textureBox.size.y * imageHeight * scale));
    const float currentScreenFontSize = std::max(1.0f, data.fontSize * scale);
    const float fittedScreenFontSize = fitTextAnnotationFontSizeToPixelBox(
        extent, currentScreenFontSize, pixelBoxSize, 1.f, 512.f * scale);
    const float fittedImageFontSize = fittedScreenFontSize / scale;

    if (growToFit || fittedImageFontSize < data.fontSize)
        data.fontSize = fittedImageFontSize;
}

static void imageSizeForAnnotationFit(const ModifiedImage& im,
                                      int fallbackWidth,
                                      int fallbackHeight,
                                      int& imageWidth,
                                      int& imageHeight)
{
    imageWidth = fallbackWidth;
    imageHeight = fallbackHeight;

    const ImageItemDataPtr& data = im.preAnnotationData();
    if (data && data->cpuData && data->cpuData->hasData())
    {
        imageWidth = data->cpuData->width();
        imageHeight = data->cpuData->height();
        return;
    }

    if (im.item() && im.item()->metadata.width > 0 && im.item()->metadata.height > 0)
    {
        imageWidth = im.item()->metadata.width;
        imageHeight = im.item()->metadata.height;
    }
}

static Rect textBoxFromFontMetrics(const Point& origin,
                                   const TextAnnotationData& data,
                                   float imagePixelToScreenPixel,
                                   int imageWidth,
                                   int imageHeight)
{
    zv_assert(ImGui::GetCurrentContext() != nullptr, "textBoxFromFontMetrics must be called inside an ImGui frame");
    const ImVec2 ext = textExtentFromFontMetrics(data, imagePixelToScreenPixel);
    // Convert widget-pixel extent back to texture coordinates.
    const float scale = imagePixelToScreenPixel > 0.f ? imagePixelToScreenPixel : 1.f;
    // Fallbacks (~20% width, ~8% height) are used when image dimensions are
    // not yet known (e.g. the image is still loading).
    const float boxW  = (imageWidth  > 0) ? ext.x / (scale * (float)imageWidth)  : 0.2f;
    const float boxH  = (imageHeight > 0) ? ext.y / (scale * (float)imageHeight) : 0.08f;
    return Rect::from_x_y_w_h(origin.x, origin.y, boxW, boxH);
}

AnnotationId AnnotationTool::commitNewText(const TextAnnotationData& data)
{
    const AnnotationId createdId = AnnotationId::nextId();

    if (_applyFunc)
    {
        _applyFunc([createdId, data](ModifiedImage& im) {
            im.annotations().addText(createdId, data);
            im.markAnnotationsDirty();
            im.pushUndoAction([&im, createdId]() {
                im.annotations().removeById(createdId);
                im.markAnnotationsDirty();
            });
        });
    }

    _selectedId = createdId;
    return createdId;
}

void AnnotationTool::createTextAtPosition(const Point& texturePos, int imageWidth, int imageHeight,
                                          float imagePixelToScreenPixel)
{
    TextAnnotationData data = _defaultTextStyle;
    // Compute the initial box using exact font metrics so the hit area matches
    // the visible text from the very first frame.
    const Rect box = textBoxFromFontMetrics(texturePos, data, imagePixelToScreenPixel,
                                            imageWidth, imageHeight);
    data.textureBox = Rect::from_x_y_w_h(
        texturePos.x - box.size.x * 0.5,
        texturePos.y - box.size.y * 0.5,
        box.size.x,
        box.size.y
    );
    commitNewText(data);
    _mode = Mode::Select;
}

// ---------------------------------------------------------------------------
// Per-frame rendering and input
// ---------------------------------------------------------------------------

void AnnotationTool::renderAsActiveTool(const InteractiveToolRenderingContext& context)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Build the render transform (texture → screen/widget coordinates).
    AnnotationRenderTransform renderTransform;
    renderTransform.imageWidth  = context.imageWidth;
    renderTransform.imageHeight = context.imageHeight;
    renderTransform.imagePixelToScreenPixel =
        context.widgetToImageTransform.pixelScale(context.imageWidth, context.imageHeight).x;
    const auto widgetTransform = context.widgetToImageTransform;
    renderTransform.textureToScreen = [widgetTransform](const Point& p) {
        return imVec2(widgetTransform.textureToWidget(p));
    };

    // ---- Rendering (every image) ----

    // Draw handles for the selected annotation so the user can see what is
    // selected and grab endpoints. The annotation body is already composited
    // into the image texture; we only need the interactive handle overlay.
    // Also refresh the cached line data used by renderControls (only when the
    // user is not actively editing a property, to avoid overwriting live edits).
    if (_selectedId.isValid() && context.annotationDocument)
    {
        if (const AnnotationElement* sel = context.annotationDocument->findById(_selectedId))
        {
            // Text: draw the editable bounding box. Font size is adjusted to fit
            // this box when users resize it; body drags preserve the geometry.
            if (sel->kind() == AnnotationElement::Kind::Text)
            {
                const auto& td = sel->asText();
                const ImVec2 tlW = imVec2(widgetTransform.textureToWidget(td.textureBox.topLeft()));
                const ImVec2 brW = imVec2(widgetTransform.textureToWidget(td.textureBox.bottomRight()));

                // Thin outline
                drawList->AddRect(tlW, brW, IM_COL32(255, 255, 255, 160), 0.f, 0, 1.5f);
            }

            for (int h = 0; h < sel->numHandles(); ++h)
            {
                const ImVec2 hp = imVec2(widgetTransform.textureToWidget(sel->handleTexturePos(h)));
                drawList->AddCircleFilled(hp, kHandleRadius, IM_COL32(255, 255, 255, 220));
                drawList->AddCircle(hp, kHandleRadius, IM_COL32(0, 0, 0, 220), 0, 1.5f);
            }

            if (!_propertyEditActive)
            {
                if (sel->kind() == AnnotationElement::Kind::Line)
                {
                    _selectedLineData = sel->asLine();
                    _selectedLineDataValid = true;
                    _selectedRectangleDataValid = false;
                    _selectedEllipseDataValid = false;
                    _selectedTextDataValid = false;
                }
                else if (sel->kind() == AnnotationElement::Kind::Rectangle)
                {
                    _selectedRectangleData = sel->asRectangle();
                    _selectedRectangleDataValid = true;
                    _selectedLineDataValid = false;
                    _selectedEllipseDataValid = false;
                    _selectedTextDataValid = false;
                }
                else if (sel->kind() == AnnotationElement::Kind::Ellipse)
                {
                    _selectedEllipseData = sel->asEllipse();
                    _selectedEllipseDataValid = true;
                    _selectedLineDataValid = false;
                    _selectedRectangleDataValid = false;
                    _selectedTextDataValid = false;
                }
                else if (sel->kind() == AnnotationElement::Kind::Text)
                {
                    _selectedTextData = sel->asText();
                    _selectedTextDataValid = true;
                    _selectedLineDataValid = false;
                    _selectedRectangleDataValid = false;
                    _selectedEllipseDataValid = false;
                    // Sync edit buffer when selection/content changes externally.
                    snprintf(_textEditBuffer, sizeof(_textEditBuffer), "%s",
                             _selectedTextData.text.c_str());
                }
            }
        }
        else
        {
            _selectedLineDataValid = false;
            _selectedRectangleDataValid = false;
            _selectedEllipseDataValid = false;
            _selectedTextDataValid = false;
        }
    }
    else
    {
        _selectedLineDataValid = false;
        _selectedRectangleDataValid = false;
        _selectedEllipseDataValid = false;
        _selectedTextDataValid = false;
    }

    // Live in-progress shape overlay — drawn in every visible image so the
    // drag preview mirrors where the committed annotation will land.
    if (_creatingShape)
    {
        if (_creationKind == CreationKind::Line)
        {
            LineAnnotationData ld = _defaultLineStyle;
            ld.textureLine = Line(_creationStartTexture, _creationCurrentTexture);
            renderLineAnnotation(drawList, ld, renderTransform);
        }
        else if (_creationKind == CreationKind::Rectangle)
        {
            RectangleAnnotationData rd = _defaultRectangleStyle;
            rd.textureBox = Rect::from_two_points(_creationStartTexture, _creationCurrentTexture);
            renderRectangleAnnotation(drawList, rd, renderTransform);
        }
        else if (_creationKind == CreationKind::Ellipse)
        {
            EllipseAnnotationData ed = _defaultEllipseStyle;
            ed.textureBox = Rect::from_two_points(_creationStartTexture, _creationCurrentTexture);
            renderEllipseAnnotation(drawList, ed, renderTransform);
        }
    }

    // ---- Input (first valid image only) ----

    if (!context.firstValidImageIndex)
        return;

    _lastImageWidth              = context.imageWidth;
    _lastImageHeight             = context.imageHeight;
    _lastImagePixelToScreenPixel = renderTransform.imagePixelToScreenPixel;

    const bool  hovered    = ImGui::IsItemHovered();
    const Point texturePos = widgetTransform.widgetToTexture(toPoint(io.MousePos));

    // (A) Start a new interaction on a fresh click (not already dragging/creating).
    if (!_editDragActive && !_creatingShape
        && hovered
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !io.KeyCtrl)
    {
        switch (_mode)
        {
        case Mode::Select: {
            AnnotationHitResult hit;
            if (context.annotationDocument && !context.annotationDocument->empty())
            {
                hit = context.annotationDocument->hitTest(
                    toPoint(io.MousePos),
                    widgetTransform,
                    _selectedId,
                    kHandleRadius,
                    kBodyTolerance,
                    context.imageWidth,
                    context.imageHeight);
            }

            if (hit.isValid())
            {
                if (_selectedId != hit.id)
                {
                    _propertyEditActive = false;
                    _selectedEditSnapshots.clear();
                }
                _selectedId = hit.id;
                captureSelectedEditSnapshots();
                _editDragActive      = true;
                _editDragKind        = (hit.part == AnnotationHitResult::Part::Handle)
                                           ? EditDragKind::Handle
                                           : EditDragKind::Body;
                _editDragHandleIdx   = hit.handleIdx;
                _editDragPrevTexture = texturePos;
                _editDragMoved       = false;
            }
            else
            {
                _selectedId           = {};
                _propertyEditActive   = false;
                _selectedEditSnapshots.clear();
            }
            break;
        }
        case Mode::AddLine:
        case Mode::AddRectangle:
        case Mode::AddEllipse:
        case Mode::AddText:
            // Placement modes: never select or edit existing annotations; add new only.
            _propertyEditActive = false;
            _selectedEditSnapshots.clear();
            _selectedId = {};
            if (_mode == Mode::AddLine)
                startDragCreation(CreationKind::Line, texturePos);
            else if (_mode == Mode::AddRectangle)
                startDragCreation(CreationKind::Rectangle, texturePos);
            else if (_mode == Mode::AddEllipse)
                startDragCreation(CreationKind::Ellipse, texturePos);
            else
                createTextAtPosition(texturePos, context.imageWidth, context.imageHeight,
                                     renderTransform.imagePixelToScreenPixel);
            break;
        }
    }

    // (B) Continue / finish an in-progress edit drag.
    if (_editDragActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (_editDragKind == EditDragKind::Body)
        {
            const Point delta = texturePos - _editDragPrevTexture;
            if (delta.x * delta.x + delta.y * delta.y > 1e-12)
            {
                _editDragMoved = true;
                const AnnotationId id = _selectedId;
                if (_applyFunc)
                {
                    _applyFunc([id, delta](ModifiedImage& im) {
                        AnnotationElement* el = im.annotations().findById(id);
                        if (el) { el->moveBy(delta); im.markAnnotationsDirty(); }
                    });
                }
                _editDragPrevTexture = texturePos;
            }
        }
        else if (_editDragKind == EditDragKind::Handle)
        {
            const AnnotationId id = _selectedId;
            const int hi = _editDragHandleIdx;
            const int imageWidth = context.imageWidth;
            const int imageHeight = context.imageHeight;
            const float imagePixelToScreenPixel = renderTransform.imagePixelToScreenPixel;
            if (_applyFunc)
            {
                _applyFunc([id, hi, texturePos, imageWidth, imageHeight, imagePixelToScreenPixel](ModifiedImage& im) {
                    AnnotationElement* el = im.annotations().findById(id);
                    if (el)
                    {
                        el->moveHandleTo(hi, texturePos);
                        if (el->kind() == AnnotationElement::Kind::Text)
                        {
                            int fitW = imageWidth;
                            int fitH = imageHeight;
                            imageSizeForAnnotationFit(im, imageWidth, imageHeight, fitW, fitH);
                            tuneTextFontSizeToBox(el->asText(), fitW, fitH,
                                                  imagePixelToScreenPixel, true);
                        }
                        im.markAnnotationsDirty();
                    }
                });
            }
            _editDragMoved = true;
        }
    }

    if (_editDragActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (_editDragMoved)
            pushSelectedEditUndo();
        cancelEditDrag();
    }

    // (C) Continue / finish an in-progress drag creation.
    if (_creatingShape)
    {
        updateDragCreation(texturePos);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            finishDragCreation();
    }
}

// ---------------------------------------------------------------------------
// Controls panel
// ---------------------------------------------------------------------------

void AnnotationTool::applySelectedTextStyle(const TextAnnotationData& style,
                                            bool growFontToExistingBox,
                                            bool shrinkFontToExistingBox)
{
    if (!_selectedId.isValid() || !_applyFunc)
        return;

    const AnnotationId id  = _selectedId;
    const ImColor color    = style.color;
    const float fontSize   = style.fontSize;
    const std::string text = style.text;
    const int   imW   = _lastImageWidth;
    const int   imH   = _lastImageHeight;
    const float scale = _lastImagePixelToScreenPixel;

    _applyFunc([id, color, fontSize, text, imW, imH, scale,
                growFontToExistingBox, shrinkFontToExistingBox](ModifiedImage& im) {
        AnnotationElement* el = im.annotations().findById(id);
        if (el && el->kind() == AnnotationElement::Kind::Text)
        {
            auto& td    = el->asText();
            td.color    = color;
            td.fontSize = fontSize;
            td.text     = text;
            if (growFontToExistingBox || shrinkFontToExistingBox)
            {
                int fitW = imW;
                int fitH = imH;
                imageSizeForAnnotationFit(im, imW, imH, fitW, fitH);
                tuneTextFontSizeToBox(td, fitW, fitH, scale, growFontToExistingBox);
            }
            im.markAnnotationsDirty();
        }
    });
}

void AnnotationTool::applySelectedLineStyle(const LineAnnotationData& style)
{
    if (!_selectedId.isValid() || !_applyFunc)
        return;

    const AnnotationId id = _selectedId;
    const ImColor color   = style.color;
    const int strokeWidth = style.strokeWidth;
    _applyFunc([id, color, strokeWidth](ModifiedImage& im) {
        AnnotationElement* el = im.annotations().findById(id);
        if (el && el->kind() == AnnotationElement::Kind::Line)
        {
            el->asLine().color = color;
            el->asLine().strokeWidth = strokeWidth;
            im.markAnnotationsDirty();
        }
    });
}

void AnnotationTool::applySelectedRectangleStyle(const RectangleAnnotationData& style)
{
    if (!_selectedId.isValid() || !_applyFunc)
        return;

    const AnnotationId id = _selectedId;
    const ImColor color   = style.color;
    const int strokeWidth = style.strokeWidth;
    _applyFunc([id, color, strokeWidth](ModifiedImage& im) {
        AnnotationElement* el = im.annotations().findById(id);
        if (el && el->kind() == AnnotationElement::Kind::Rectangle)
        {
            el->asRectangle().color = color;
            el->asRectangle().strokeWidth = strokeWidth;
            im.markAnnotationsDirty();
        }
    });
}

void AnnotationTool::applySelectedEllipseStyle(const EllipseAnnotationData& style)
{
    if (!_selectedId.isValid() || !_applyFunc)
        return;

    const AnnotationId id = _selectedId;
    const ImColor color   = style.color;
    const int strokeWidth = style.strokeWidth;
    _applyFunc([id, color, strokeWidth](ModifiedImage& im) {
        AnnotationElement* el = im.annotations().findById(id);
        if (el && el->kind() == AnnotationElement::Kind::Ellipse)
        {
            el->asEllipse().color = color;
            el->asEllipse().strokeWidth = strokeWidth;
            im.markAnnotationsDirty();
        }
    });
}

void AnnotationTool::renderControls(const ImageSRGBA& /*firstIm*/)
{
    ImGuiColorEditFlags colorFlags = ImGuiColorEditFlags_NoAlpha;

    if (_selectedId.isValid() && _selectedLineDataValid)
    {
        // --- Properties of the selected line ---
        ImGui::TextDisabled("Selected line  |  Delete / Backspace to remove");

        bool anyChanged = false;
        if (ImGui::ColorEdit4("Color##line", (float*)&_selectedLineData.color.Value, colorFlags))
            anyChanged = true;
        const bool colorDone = ImGui::IsItemDeactivatedAfterEdit();

        if (ImGui::SliderInt("Width", &_selectedLineData.strokeWidth, 1, 10))
            anyChanged = true;
        const bool widthDone = ImGui::IsItemDeactivatedAfterEdit();

        if (anyChanged)
        {
            if (!_propertyEditActive)
            {
                captureSelectedEditSnapshots();
                _propertyEditActive = true;
            }
            applySelectedLineStyle(_selectedLineData);
        }

        if (_propertyEditActive && (colorDone || widthDone))
        {
            pushSelectedEditUndo();
            _selectedEditSnapshots.clear();
            _propertyEditActive = false;
        }
    }
    else if (_selectedId.isValid() && _selectedRectangleDataValid)
    {
        // --- Properties of the selected rectangle ---
        ImGui::TextDisabled("Selected rectangle  |  Delete / Backspace to remove");

        bool anyChanged = false;
        if (ImGui::ColorEdit4("Color##rectangle", (float*)&_selectedRectangleData.color.Value, colorFlags))
            anyChanged = true;
        const bool colorDone = ImGui::IsItemDeactivatedAfterEdit();

        if (ImGui::SliderInt("Width##rectangle", &_selectedRectangleData.strokeWidth, 1, 10))
            anyChanged = true;
        const bool widthDone = ImGui::IsItemDeactivatedAfterEdit();

        if (anyChanged)
        {
            if (!_propertyEditActive)
            {
                captureSelectedEditSnapshots();
                _propertyEditActive = true;
            }
            applySelectedRectangleStyle(_selectedRectangleData);
        }

        if (_propertyEditActive && (colorDone || widthDone))
        {
            pushSelectedEditUndo();
            _selectedEditSnapshots.clear();
            _propertyEditActive = false;
        }
    }
    else if (_selectedId.isValid() && _selectedEllipseDataValid)
    {
        // --- Properties of the selected ellipse ---
        ImGui::TextDisabled("Selected ellipse  |  Delete / Backspace to remove");

        bool anyChanged = false;
        if (ImGui::ColorEdit4("Color##ellipse", (float*)&_selectedEllipseData.color.Value, colorFlags))
            anyChanged = true;
        const bool colorDone = ImGui::IsItemDeactivatedAfterEdit();

        if (ImGui::SliderInt("Width##ellipse", &_selectedEllipseData.strokeWidth, 1, 10))
            anyChanged = true;
        const bool widthDone = ImGui::IsItemDeactivatedAfterEdit();

        if (anyChanged)
        {
            if (!_propertyEditActive)
            {
                captureSelectedEditSnapshots();
                _propertyEditActive = true;
            }
            applySelectedEllipseStyle(_selectedEllipseData);
        }

        if (_propertyEditActive && (colorDone || widthDone))
        {
            pushSelectedEditUndo();
            _selectedEditSnapshots.clear();
            _propertyEditActive = false;
        }
    }
    else if (_selectedId.isValid() && _selectedTextDataValid)
    {
        // --- Properties of the selected text annotation ---
        ImGui::TextDisabled("Selected text  |  Delete / Backspace to remove");

        bool anyChanged = false;

        ImGui::SetNextItemWidth(-FLT_MIN);
        bool textChanged = false;
        if (ImGui::InputTextMultiline("Text", _textEditBuffer, sizeof(_textEditBuffer),
                                      ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f)))
        {
            _selectedTextData.text = _textEditBuffer;
            textChanged = true;
            anyChanged = true;
        }
        const bool textDone = ImGui::IsItemDeactivatedAfterEdit();

        if (ImGui::ColorEdit4("Color##text", (float*)&_selectedTextData.color.Value, colorFlags))
            anyChanged = true;
        const bool colorDone = ImGui::IsItemDeactivatedAfterEdit();

        const float fontSizeBefore = _selectedTextData.fontSize;
        if (ImGui::SliderFloat("Font Size", &_selectedTextData.fontSize, 8.f, 96.f))
            anyChanged = true;
        const bool fontSizeDone = ImGui::IsItemDeactivatedAfterEdit();

        if (anyChanged)
        {
            if (!_propertyEditActive)
            {
                captureSelectedEditSnapshots();
                _propertyEditActive = true;
            }
            const bool fontSizeChanged = std::abs(_selectedTextData.fontSize - fontSizeBefore) >= 1e-6f;
            applySelectedTextStyle(_selectedTextData,
                                   textChanged,
                                   textChanged || fontSizeChanged);
        }

        if (_propertyEditActive && (textDone || colorDone || fontSizeDone))
        {
            pushSelectedEditUndo();
            _selectedEditSnapshots.clear();
            _propertyEditActive = false;
        }
    }
    else if (_mode == Mode::AddLine)
    {
        // --- Default style shown briefly while drawing a new line ---
        ImGui::TextDisabled("New line style");
        ImGui::ColorEdit4("Color##newline", (float*)&_defaultLineStyle.color.Value, colorFlags);
        ImGui::SliderInt("Width", &_defaultLineStyle.strokeWidth, 1, 10);
    }
    else if (_mode == Mode::AddRectangle)
    {
        // --- Default style shown briefly while drawing a new rectangle ---
        ImGui::TextDisabled("New rectangle style");
        ImGui::ColorEdit4("Color##newrectangle", (float*)&_defaultRectangleStyle.color.Value, colorFlags);
        ImGui::SliderInt("Width##newrectangle", &_defaultRectangleStyle.strokeWidth, 1, 10);
    }
    else if (_mode == Mode::AddEllipse)
    {
        // --- Default style shown briefly while drawing a new ellipse ---
        ImGui::TextDisabled("New ellipse style");
        ImGui::ColorEdit4("Color##newellipse", (float*)&_defaultEllipseStyle.color.Value, colorFlags);
        ImGui::SliderInt("Width##newellipse", &_defaultEllipseStyle.strokeWidth, 1, 10);
    }
    else if (_mode == Mode::AddText)
    {
        // --- Default style shown briefly while in text-placement mode ---
        ImGui::TextDisabled("Click image to place text");
        ImGui::ColorEdit4("Color##newtext", (float*)&_defaultTextStyle.color.Value, colorFlags);
        ImGui::SliderFloat("Font Size##new", &_defaultTextStyle.fontSize, 8.f, 96.f);
    }
    // Select mode + nothing selected → show nothing.
}

} // zv
