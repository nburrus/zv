//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Annotations.h"

#include <libzv/ImguiUtils.h>
#include <libzv/MathUtils.h>
#include <libzv/Utils.h>

#include <imgui.h>
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include <GL/gl3w.h>

#include <atomic>
#include <cmath>

namespace zv
{

// ---------------------------------------------------------------------------
// AnnotationId
// ---------------------------------------------------------------------------

AnnotationId AnnotationId::nextId()
{
    static std::atomic<uint64_t> s_counter{0};
    AnnotationId id;
    id.value = ++s_counter;
    return id;
}

float fitTextAnnotationFontSizeToPixelBox(const ImVec2& textExtentAtCurrentFontSize,
                                          float currentFontSize,
                                          const ImVec2& pixelBoxSize,
                                          float minFontSize,
                                          float maxFontSize)
{
    if (currentFontSize <= 0.f || pixelBoxSize.x <= 0.f || pixelBoxSize.y <= 0.f
        || textExtentAtCurrentFontSize.x <= 0.f || textExtentAtCurrentFontSize.y <= 0.f)
        return std::max(minFontSize, std::min(maxFontSize, currentFontSize));

    const float sx = pixelBoxSize.x / textExtentAtCurrentFontSize.x;
    const float sy = pixelBoxSize.y / textExtentAtCurrentFontSize.y;
    const float fitted = currentFontSize * std::min(sx, sy);
    return std::max(minFontSize, std::min(maxFontSize, fitted));
}

// ---------------------------------------------------------------------------
// AnnotationElement
// ---------------------------------------------------------------------------

AnnotationElement::AnnotationElement(AnnotationId id, const LineAnnotationData& data)
    : _id(id), _kind(Kind::Line), _line(data)
{}

AnnotationElement::AnnotationElement(AnnotationId id, const RectangleAnnotationData& data)
    : _id(id), _kind(Kind::Rectangle), _rectangle(data)
{}

AnnotationElement::AnnotationElement(AnnotationId id, const EllipseAnnotationData& data)
    : _id(id), _kind(Kind::Ellipse), _ellipse(data)
{}

AnnotationElement::AnnotationElement(AnnotationId id, const TextAnnotationData& data)
    : _id(id), _kind(Kind::Text), _text(data)
{}

LineAnnotationData& AnnotationElement::asLine()
{
    zv_assert(_kind == Kind::Line, "Element is not a line");
    return _line;
}

const LineAnnotationData& AnnotationElement::asLine() const
{
    zv_assert(_kind == Kind::Line, "Element is not a line");
    return _line;
}

RectangleAnnotationData& AnnotationElement::asRectangle()
{
    zv_assert(_kind == Kind::Rectangle, "Element is not a rectangle");
    return _rectangle;
}

const RectangleAnnotationData& AnnotationElement::asRectangle() const
{
    zv_assert(_kind == Kind::Rectangle, "Element is not a rectangle");
    return _rectangle;
}

EllipseAnnotationData& AnnotationElement::asEllipse()
{
    zv_assert(_kind == Kind::Ellipse, "Element is not an ellipse");
    return _ellipse;
}

const EllipseAnnotationData& AnnotationElement::asEllipse() const
{
    zv_assert(_kind == Kind::Ellipse, "Element is not an ellipse");
    return _ellipse;
}

TextAnnotationData& AnnotationElement::asText()
{
    zv_assert(_kind == Kind::Text, "Element is not a text");
    return _text;
}

const TextAnnotationData& AnnotationElement::asText() const
{
    zv_assert(_kind == Kind::Text, "Element is not a text");
    return _text;
}

int AnnotationElement::numHandles() const
{
    switch (_kind)
    {
        case Kind::Line: return 2; // p1, p2
        case Kind::Rectangle: return 4; // tl, tr, br, bl
        case Kind::Ellipse: return 4; // bounding box corners: tl, tr, br, bl
        case Kind::Text: return 4; // bounding box corners: tl, tr, br, bl
    }
    return 0;
}

Point AnnotationElement::handleTexturePos(int handleIdx) const
{
    switch (_kind)
    {
        case Kind::Line:
            switch (handleIdx)
            {
                case 0: return _line.textureLine.p1;
                case 1: return _line.textureLine.p2;
            }
            break;
        case Kind::Rectangle:
            switch (handleIdx)
            {
                case 0: return _rectangle.textureBox.topLeft();
                case 1: return _rectangle.textureBox.topRight();
                case 2: return _rectangle.textureBox.bottomRight();
                case 3: return _rectangle.textureBox.bottomLeft();
            }
            break;
        case Kind::Ellipse:
            switch (handleIdx)
            {
                case 0: return _ellipse.textureBox.topLeft();
                case 1: return _ellipse.textureBox.topRight();
                case 2: return _ellipse.textureBox.bottomRight();
                case 3: return _ellipse.textureBox.bottomLeft();
            }
            break;
        case Kind::Text:
            switch (handleIdx)
            {
                case 0: return _text.textureBox.topLeft();
                case 1: return _text.textureBox.topRight();
                case 2: return _text.textureBox.bottomRight();
                case 3: return _text.textureBox.bottomLeft();
            }
            break;
    }
    return Point(NAN, NAN);
}

void AnnotationElement::moveHandleTo(int handleIdx, const Point& newTexturePos)
{
    switch (_kind)
    {
        case Kind::Line:
            switch (handleIdx)
            {
                case 0: _line.textureLine.p1 = newTexturePos; break;
                case 1: _line.textureLine.p2 = newTexturePos; break;
            }
            break;
        case Kind::Rectangle:
            switch (handleIdx)
            {
                case 0: _rectangle.textureBox.moveTopLeft(newTexturePos); break;
                case 1: _rectangle.textureBox.moveTopRight(newTexturePos); break;
                case 2: _rectangle.textureBox.moveBottomRight(newTexturePos); break;
                case 3: _rectangle.textureBox.moveBottomLeft(newTexturePos); break;
            }
            break;
        case Kind::Ellipse:
            switch (handleIdx)
            {
                case 0: _ellipse.textureBox.moveTopLeft(newTexturePos); break;
                case 1: _ellipse.textureBox.moveTopRight(newTexturePos); break;
                case 2: _ellipse.textureBox.moveBottomRight(newTexturePos); break;
                case 3: _ellipse.textureBox.moveBottomLeft(newTexturePos); break;
            }
            break;
        case Kind::Text:
            switch (handleIdx)
            {
                case 0: _text.textureBox.moveTopLeft(newTexturePos); break;
                case 1: _text.textureBox.moveTopRight(newTexturePos); break;
                case 2: _text.textureBox.moveBottomRight(newTexturePos); break;
                case 3: _text.textureBox.moveBottomLeft(newTexturePos); break;
            }
            break;
    }
}

void AnnotationElement::moveBy(const Point& d)
{
    switch (_kind)
    {
        case Kind::Line:
            _line.textureLine.p1 += d;
            _line.textureLine.p2 += d;
            break;
        case Kind::Rectangle:
            _rectangle.textureBox.origin += d;
            break;
        case Kind::Ellipse:
            _ellipse.textureBox.origin += d;
            break;
        case Kind::Text:
            _text.textureBox.origin += d;
            break;
    }
}

// ---------------------------------------------------------------------------
// AnnotationDocument
// ---------------------------------------------------------------------------

AnnotationElement& AnnotationDocument::addLine(AnnotationId id, const LineAnnotationData& data)
{
    _elements.emplace_back(id, data);
    return _elements.back();
}

AnnotationElement& AnnotationDocument::addRectangle(AnnotationId id, const RectangleAnnotationData& data)
{
    _elements.emplace_back(id, data);
    return _elements.back();
}

AnnotationElement& AnnotationDocument::addEllipse(AnnotationId id, const EllipseAnnotationData& data)
{
    _elements.emplace_back(id, data);
    return _elements.back();
}

AnnotationElement& AnnotationDocument::addText(AnnotationId id, const TextAnnotationData& data)
{
    _elements.emplace_back(id, data);
    return _elements.back();
}

bool AnnotationDocument::removeById(AnnotationId id)
{
    for (auto it = _elements.begin(); it != _elements.end(); ++it)
    {
        if (it->id() == id)
        {
            _elements.erase(it);
            return true;
        }
    }
    return false;
}

AnnotationElement* AnnotationDocument::findById(AnnotationId id)
{
    for (auto& el : _elements)
        if (el.id() == id) return &el;
    return nullptr;
}

const AnnotationElement* AnnotationDocument::findById(AnnotationId id) const
{
    for (const auto& el : _elements)
        if (el.id() == id) return &el;
    return nullptr;
}

namespace
{

float strokeHitTolerancePx(int strokeWidth, const WidgetToImageTransform& transform,
                           int imageWidth, int imageHeight)
{
    if (imageWidth > 0 && imageHeight > 0)
    {
        const ImVec2 scale = transform.pixelScale(imageWidth, imageHeight);
        return strokeWidth * 0.5f * std::max(scale.x, scale.y);
    }
    return strokeWidth * 0.5f;
}

bool strokedRectContains(const Rect& widgetBox, const Point& widgetPos, float tolerancePx)
{
    Rect outer = Rect::from_x_y_w_h(widgetBox.origin.x - tolerancePx,
                                    widgetBox.origin.y - tolerancePx,
                                    widgetBox.size.x + 2.0 * tolerancePx,
                                    widgetBox.size.y + 2.0 * tolerancePx);
    if (!outer.contains(widgetPos))
        return false;

    Rect inner = Rect::from_x_y_w_h(widgetBox.origin.x + tolerancePx,
                                    widgetBox.origin.y + tolerancePx,
                                    std::max(0.0, widgetBox.size.x - 2.0 * tolerancePx),
                                    std::max(0.0, widgetBox.size.y - 2.0 * tolerancePx));
    return inner.size.x <= 0.0 || inner.size.y <= 0.0 || !inner.contains(widgetPos);
}

bool strokedEllipseContains(const Rect& widgetBox, const Point& widgetPos, float tolerancePx)
{
    const double rx = widgetBox.size.x * 0.5;
    const double ry = widgetBox.size.y * 0.5;
    if (rx <= 0.0 || ry <= 0.0)
        return false;

    const double cx = widgetBox.origin.x + rx;
    const double cy = widgetBox.origin.y + ry;
    const double dx = widgetPos.x - cx;
    const double dy = widgetPos.y - cy;
    const double normalizedDistance = std::sqrt((dx * dx) / (rx * rx) + (dy * dy) / (ry * ry));
    const double normalizedTolerance = tolerancePx / std::max(1.0, std::min(rx, ry));
    return std::abs(normalizedDistance - 1.0) <= normalizedTolerance;
}

} // namespace

AnnotationHitResult AnnotationDocument::hitTest(const Point& widgetPos,
                                                const WidgetToImageTransform& transform,
                                                AnnotationId selectedId,
                                                float handleRadiusPx,
                                                float bodyTolerancePx,
                                                int imageWidth,
                                                int imageHeight) const
{
    AnnotationHitResult result;

    auto handleHitTestFor = [&](const AnnotationElement& el) -> int {
        for (int h = 0; h < el.numHandles(); ++h)
        {
            const Point widgetHandle = transform.textureToWidget(el.handleTexturePos(h));
            if (distanceBetweenPoints(widgetPos, widgetHandle) <= handleRadiusPx)
                return h;
        }
        return -1;
    };

    // Selected element handles take absolute priority.
    if (selectedId.isValid())
    {
        if (const AnnotationElement* el = findById(selectedId))
        {
            int h = handleHitTestFor(*el);
            if (h >= 0)
            {
                result.part = AnnotationHitResult::Part::Handle;
                result.id = selectedId;
                result.handleIdx = h;
                return result;
            }
        }
    }

    // Then walk topmost (last) -> bottommost.
    for (auto it = _elements.rbegin(); it != _elements.rend(); ++it)
    {
        const auto& el = *it;
        int h = handleHitTestFor(el);
        if (h >= 0)
        {
            result.part = AnnotationHitResult::Part::Handle;
            result.id = el.id();
            result.handleIdx = h;
            return result;
        }

        bool bodyHit = false;
        switch (el.kind())
        {
            case AnnotationElement::Kind::Line:
            {
                const auto& ld = el.asLine();
                const Point widgetP1 = transform.textureToWidget(ld.textureLine.p1);
                const Point widgetP2 = transform.textureToWidget(ld.textureLine.p2);
                const double d = distancePointToSegment(widgetPos, widgetP1, widgetP2);
                bodyHit = d <= (bodyTolerancePx + strokeHitTolerancePx(ld.strokeWidth, transform, imageWidth, imageHeight));
                break;
            }
            case AnnotationElement::Kind::Rectangle:
            {
                const auto& rd = el.asRectangle();
                const Point widgetTL = transform.textureToWidget(rd.textureBox.topLeft());
                const Point widgetBR = transform.textureToWidget(rd.textureBox.bottomRight());
                const float tolerance = bodyTolerancePx + strokeHitTolerancePx(rd.strokeWidth, transform, imageWidth, imageHeight);
                bodyHit = strokedRectContains(Rect::from_two_points(widgetTL, widgetBR), widgetPos, tolerance);
                break;
            }
            case AnnotationElement::Kind::Ellipse:
            {
                const auto& ed = el.asEllipse();
                const Point widgetTL = transform.textureToWidget(ed.textureBox.topLeft());
                const Point widgetBR = transform.textureToWidget(ed.textureBox.bottomRight());
                const float tolerance = bodyTolerancePx + strokeHitTolerancePx(ed.strokeWidth, transform, imageWidth, imageHeight);
                bodyHit = strokedEllipseContains(Rect::from_two_points(widgetTL, widgetBR), widgetPos, tolerance);
                break;
            }
            case AnnotationElement::Kind::Text:
            {
                const auto& td = el.asText();
                const Point widgetTL = transform.textureToWidget(td.textureBox.topLeft());
                const Point widgetBR = transform.textureToWidget(td.textureBox.bottomRight());
                bodyHit = Rect::from_two_points(widgetTL, widgetBR).contains(widgetPos);
                break;
            }
        }

        if (bodyHit)
        {
            result.part = AnnotationHitResult::Part::Body;
            result.id = el.id();
            result.handleIdx = -1;
            return result;
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Shared rendering helpers
// ---------------------------------------------------------------------------

namespace {

void drawStyledShaft(ImDrawList* drawList,
                     const ImVec2& p1,
                     const ImVec2& p2,
                     ImU32 color,
                     float thickness,
                     AnnotationStrokeStyle strokeStyle)
{
    const ImVec2 delta = p2 - p1;
    const float len = static_cast<float>(toPoint(delta).length());
    if (len <= 0.5f)
        return;

    if (strokeStyle == AnnotationStrokeStyle::Solid)
    {
        drawList->AddLine(p1, p2, color, thickness);
        return;
    }

    const ImVec2 dir = delta * (1.0f / len);

    if (strokeStyle == AnnotationStrokeStyle::Dashed)
    {
        const float dashLen = std::max(6.0f, thickness * 4.0f);
        const float gapLen = std::max(4.0f, thickness * 2.5f);
        for (float d = 0.0f; d < len; d += dashLen + gapLen)
        {
            const float d2 = std::min(d + dashLen, len);
            drawList->AddLine(p1 + dir * d, p1 + dir * d2, color, thickness);
        }
        return;
    }

    const float spacing = std::max(6.0f, thickness * 3.0f);
    const float radius = std::max(1.0f, thickness * 0.5f);
    for (float d = 0.0f; d <= len; d += spacing)
        drawList->AddCircleFilled(p1 + dir * d, radius, color);
}

void drawArrowHead(ImDrawList* drawList,
                   const ImVec2& tip,
                   const ImVec2& dirTowardTip,
                   ImU32 color,
                   float thickness)
{
    const float headLength = std::max(10.0f, thickness * 4.0f);
    const float headWidth = std::max(7.0f, thickness * 2.5f);
    const ImVec2 perp(-dirTowardTip.y, dirTowardTip.x);
    const ImVec2 base = tip - dirTowardTip * headLength;
    drawList->AddTriangleFilled(tip,
                                base + perp * (headWidth * 0.5f),
                                base - perp * (headWidth * 0.5f),
                                color);
}

void drawLineWithEndpoints(ImDrawList* drawList,
                           const ImVec2& p1,
                           const ImVec2& p2,
                           ImU32 color,
                           float thickness,
                           LineEndpointStyle startStyle,
                           LineEndpointStyle endStyle,
                           AnnotationStrokeStyle strokeStyle)
{
    const ImVec2 delta = p2 - p1;
    const float len = static_cast<float>(toPoint(delta).length());
    if (len <= 0.5f)
        return;

    const ImVec2 dir = delta * (1.0f / len);
    const float headLength = std::max(10.0f, thickness * 4.0f);
    ImVec2 shaftStart = p1;
    ImVec2 shaftEnd = p2;

    if (startStyle == LineEndpointStyle::Arrow)
        shaftStart = p1 + dir * std::min(headLength, len * 0.45f);
    if (endStyle == LineEndpointStyle::Arrow)
        shaftEnd = p2 - dir * std::min(headLength, len * 0.45f);

    drawStyledShaft(drawList, shaftStart, shaftEnd, color, thickness, strokeStyle);

    if (startStyle == LineEndpointStyle::Arrow)
        drawArrowHead(drawList, p1, dir * -1.0f, color, thickness);
    if (endStyle == LineEndpointStyle::Arrow)
        drawArrowHead(drawList, p2, dir, color, thickness);
}

} // namespace

void renderLineAnnotation(ImDrawList* drawList,
                          const LineAnnotationData& data,
                          const AnnotationRenderTransform& transform)
{
    const ImVec2 p1 = transform.textureToScreen(data.textureLine.p1);
    const ImVec2 p2 = transform.textureToScreen(data.textureLine.p2);
    const float thickness = std::max(1.0f, data.strokeWidth * transform.imagePixelToScreenPixel);
    drawLineWithEndpoints(drawList, p1, p2, data.color, thickness,
                          data.startStyle, data.endStyle, data.strokeStyle);
}

void renderRectangleAnnotation(ImDrawList* drawList,
                               const RectangleAnnotationData& data,
                               const AnnotationRenderTransform& transform)
{
    const ImVec2 tl = transform.textureToScreen(data.textureBox.topLeft());
    const ImVec2 br = transform.textureToScreen(data.textureBox.bottomRight());
    const float thickness = std::max(1.0f, data.strokeWidth * transform.imagePixelToScreenPixel);
    drawList->AddRect(tl, br, data.color, 0.0f, 0, thickness);
}

void renderEllipseAnnotation(ImDrawList* drawList,
                             const EllipseAnnotationData& data,
                             const AnnotationRenderTransform& transform)
{
    const ImVec2 tl = transform.textureToScreen(data.textureBox.topLeft());
    const ImVec2 br = transform.textureToScreen(data.textureBox.bottomRight());
    const ImVec2 center((tl.x + br.x) * 0.5f, (tl.y + br.y) * 0.5f);
    const ImVec2 radius(std::abs(br.x - tl.x) * 0.5f, std::abs(br.y - tl.y) * 0.5f);
    const float thickness = std::max(1.0f, data.strokeWidth * transform.imagePixelToScreenPixel);
    drawList->AddEllipse(center, radius, data.color, 0.0f, 0, thickness);
}

void renderTextAnnotation(ImDrawList* drawList,
                          const TextAnnotationData& data,
                          const AnnotationRenderTransform& transform)
{
    const ImVec2 tl = transform.textureToScreen(data.textureBox.topLeft());
    const ImVec2 br = transform.textureToScreen(data.textureBox.bottomRight());
    const float fontSize = std::max(1.0f, data.fontSize * transform.imagePixelToScreenPixel);
    ImFont* font = ImGui::GetFont();
    const ImVec4 clipRect(std::min(tl.x, br.x), std::min(tl.y, br.y),
                          std::max(tl.x, br.x), std::max(tl.y, br.y));
    drawList->AddText(font, fontSize, tl, data.color, data.text.c_str(), nullptr, 0.0f, &clipRect);
}

void renderAnnotationElement(ImDrawList* drawList,
                             const AnnotationElement& element,
                             const AnnotationRenderTransform& transform)
{
    switch (element.kind())
    {
        case AnnotationElement::Kind::Line:
            renderLineAnnotation(drawList, element.asLine(), transform);
            break;
        case AnnotationElement::Kind::Rectangle:
            renderRectangleAnnotation(drawList, element.asRectangle(), transform);
            break;
        case AnnotationElement::Kind::Ellipse:
            renderEllipseAnnotation(drawList, element.asEllipse(), transform);
            break;
        case AnnotationElement::Kind::Text:
            renderTextAnnotation(drawList, element.asText(), transform);
            break;
    }
}

// ---------------------------------------------------------------------------
// AnnotationRenderer
// ---------------------------------------------------------------------------

struct AnnotationRenderer::Impl
{
    ImGuiContext* _sharedImguiContext = nullptr;
    ImGuiContext* _prevContext = nullptr;
    ImageSRGBA _downloadBuffer;
    int imageWidth = -1;
    int imageHeight = -1;
};

AnnotationRenderer::AnnotationRenderer ()
: impl (new Impl())
{}

AnnotationRenderer::~AnnotationRenderer () = default;

void AnnotationRenderer::initializeFromCurrentContext ()
{
    ImGuiContext* prevContext = ImGui::GetCurrentContext();
    zv_assert (prevContext, "This should be called with a parent context set.");

    // This should not happen, but we had this bug when we were initializing the window twice
    if (impl->_sharedImguiContext && impl->_sharedImguiContext->IO.Fonts != prevContext->IO.Fonts)
    {
        zv_dbg("AnnotationRenderer::initializeFromCurrentContext destroy stale annotation font atlas");
        ImGui::DestroyContext(impl->_sharedImguiContext);
        impl->_sharedImguiContext = nullptr;
    }

    if (!impl->_sharedImguiContext)
    {
        // FIXME: use a shared font atlas.
        impl->_sharedImguiContext = ImGui::CreateContext(prevContext->IO.Fonts);
        impl->_sharedImguiContext->IO.BackendRendererUserData = prevContext->IO.BackendRendererUserData;
        impl->_sharedImguiContext->IO.IniFilename = nullptr;
        impl->_sharedImguiContext->IO.BackendRendererName = prevContext->IO.BackendRendererName;
        impl->_sharedImguiContext->IO.BackendFlags = prevContext->IO.BackendFlags;
    }
}

void AnnotationRenderer::shutdown ()
{
    if (impl->_sharedImguiContext)
    {
        impl->_sharedImguiContext->IO.BackendRendererUserData = nullptr;
        impl->_sharedImguiContext->IO.BackendPlatformUserData = nullptr;
        ImGui::DestroyContext(impl->_sharedImguiContext);
        impl->_sharedImguiContext = nullptr;
    }
}

void AnnotationRenderer::enableContext ()
{
    impl->_prevContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(impl->_sharedImguiContext);
}

void AnnotationRenderer::disableContext ()
{
    ImGui::SetCurrentContext(impl->_prevContext);
    impl->_prevContext = nullptr;
}

void AnnotationRenderer::beginRendering (const ImageItemData& input)
{
    const int inW = input.cpuData->width();
    const int inH = input.cpuData->height();

    impl->imageWidth = inW;
    impl->imageHeight = inH;

    input.ensureUploadedToGPU();

    enableContext ();
    ImGui::GetIO().DisplaySize = ImVec2(inW, inH);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(inW, inH), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("#empty", nullptr, windowFlagsWithoutAnything());
    ImGui::Image(reinterpret_cast<void*>(input.textureData->textureId()), ImVec2(inW, inH));
    // ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(10, 10), ImVec2(64, 64), IM_COL32(0, 0, 255, 255));
}

void AnnotationRenderer::endRendering (ImageItemData& output)
{
    ImGui::PopStyleVar(2);
    ImGui::End();
    ImGui::Render();

    auto& io = ImGui::GetIO();
    const int outW = io.DisplaySize.x;
    const int outH = io.DisplaySize.y;

    output.cpuData = std::make_shared<ImageSRGBA>(outW, outH);
    if (!output.textureData)
    {
        output.textureData = std::make_shared<GLTexture>();
        output.textureData->initialize();
    }

    GLFrameBuffer frameBuffer (output.textureData);
    frameBuffer.enable(outW, outH);
    checkGLError();
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    checkGLError();
    if (!output.cpuData)
        output.cpuData = std::make_shared<ImageSRGBA>();
    frameBuffer.downloadBuffer(impl->_downloadBuffer);
    frameBuffer.disable();

    output.cpuData->ensureAllocatedBufferForSize(outW, outH);
    for (int r = 0; r < outH; ++r)
    {
        PixelSRGBA* outRowPtr = output.cpuData->atRowPtr(r);
        const PixelSRGBA* inRowPtr = impl->_downloadBuffer.atRowPtr(outH - r - 1);
        memcpy (outRowPtr, inRowPtr, outW * sizeof(PixelSRGBA));
    }

    checkGLError();

    disableContext ();
    output.status = ImageItemData::Status::Ready;
}

} // namespace zv
