//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageList.h>
#include <libzv/ImguiUtils.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace zv
{

// ---------------------------------------------------------------------------
// Editable annotation document model.
// Geometry is stored in normalized texture coordinates ([0,1]) so the same
// data can be rendered at any zoom or be composited at native image
// resolution. Stroke widths and font sizes are stored in image-space pixels;
// callers convert to widget-space pixels when rendering live overlays.
// ---------------------------------------------------------------------------

struct AnnotationId
{
    uint64_t value = 0;

    bool isValid() const { return value != 0; }
    bool operator==(AnnotationId rhs) const { return value == rhs.value; }
    bool operator!=(AnnotationId rhs) const { return !(*this == rhs); }

    // Returns a new globally-unique annotation id.
    static AnnotationId nextId();
};

struct LineAnnotationData
{
    Line textureLine = Line(Point(0.1, 0.1), Point(0.5, 0.5));
    ImColor color = ImColor(ImVec4(1, 1, 0, 1));
    int strokeWidth = 2; // image-space pixels
};

struct TextAnnotationData
{
    Rect textureBox = Rect::from_x_y_w_h(0.4, 0.4, 0.2, 0.1);
    std::string text = "Text";
    ImColor color = ImColor(ImVec4(1, 1, 0, 1));
    float fontSize = 24.f; // image-space pixels
};

class AnnotationElement
{
public:
    enum class Kind { Line, Text };

    AnnotationElement(AnnotationId id, const LineAnnotationData& data);
    AnnotationElement(AnnotationId id, const TextAnnotationData& data);

    AnnotationId id() const { return _id; }
    Kind kind() const { return _kind; }

    LineAnnotationData& asLine();
    const LineAnnotationData& asLine() const;
    TextAnnotationData& asText();
    const TextAnnotationData& asText() const;

    int numHandles() const;
    Point handleTexturePos(int handleIdx) const;
    void moveHandleTo(int handleIdx, const Point& newTexturePos);
    void moveBy(const Point& textureDelta);

private:
    AnnotationId _id;
    Kind _kind;
    LineAnnotationData _line;
    TextAnnotationData _text;
};

// Hit-test result. Handle hits take priority over body hits, and topmost
// elements take priority over older ones (see AnnotationDocument::hitTest).
struct AnnotationHitResult
{
    enum class Part { None, Body, Handle };

    Part part = Part::None;
    AnnotationId id;
    int handleIdx = -1;

    bool isValid() const { return part != Part::None && id.isValid(); }
};

class AnnotationDocument
{
public:
    AnnotationDocument() = default;

    // Adds an element with the supplied id (so corresponding annotations
    // across multiple visible images can share a stable id).
    AnnotationElement& addLine(AnnotationId id, const LineAnnotationData& data);
    AnnotationElement& addText(AnnotationId id, const TextAnnotationData& data);

    bool removeById(AnnotationId id);

    AnnotationElement* findById(AnnotationId id);
    const AnnotationElement* findById(AnnotationId id) const;

    const std::vector<AnnotationElement>& elements() const { return _elements; }
    std::vector<AnnotationElement>& mutableElements() { return _elements; }

    bool empty() const { return _elements.empty(); }
    size_t size() const { return _elements.size(); }
    void clear() { _elements.clear(); }

    // Hit-test in widget space. selectedId is preferred when its handles win.
    // handleRadiusPx and bodyTolerancePx are in widget pixels.
    AnnotationHitResult hitTest(const Point& widgetPos,
                                const WidgetToImageTransform& transform,
                                AnnotationId selectedId,
                                float handleRadiusPx,
                                float bodyTolerancePx) const;

private:
    std::vector<AnnotationElement> _elements;
};

// ---------------------------------------------------------------------------
// Shared rendering helpers, used both by the live overlay (drawing into the
// active ImGui window) and by the offscreen final-layer rasterizer.
// `textureToScreen` maps a normalized texture coordinate into the target
// drawing surface. `imagePixelToScreenPixel` scales image-space stroke widths
// and font sizes into the target surface's pixels (1.0 for offscreen native
// rendering, derived from WidgetToImageTransform for live overlay).
// ---------------------------------------------------------------------------

struct AnnotationRenderTransform
{
    std::function<ImVec2(const Point&)> textureToScreen;
    float imagePixelToScreenPixel = 1.0f;
    int imageWidth = 0;
    int imageHeight = 0;
};

void renderLineAnnotation(ImDrawList* drawList,
                          const LineAnnotationData& data,
                          const AnnotationRenderTransform& transform);

void renderTextAnnotation(ImDrawList* drawList,
                          const TextAnnotationData& data,
                          const AnnotationRenderTransform& transform);

void renderAnnotationElement(ImDrawList* drawList,
                             const AnnotationElement& element,
                             const AnnotationRenderTransform& transform);

// ---------------------------------------------------------------------------
// Offscreen ImGui → texture rasterization for the final annotation layer
// (see ModifiedImage::compositeAnnotationLayer).
// ---------------------------------------------------------------------------

class AnnotationRenderer
{
public:
    AnnotationRenderer ();
    ~AnnotationRenderer ();

public:
    void initializeFromCurrentContext ();
    void shutdown ();

    void beginRendering (const ImageItemData& inputData);
    void endRendering (ImageItemData& outputData);

private:
    void enableContext ();
    void disableContext ();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace zv
