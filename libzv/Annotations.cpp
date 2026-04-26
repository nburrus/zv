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

// ---------------------------------------------------------------------------
// AnnotationElement
// ---------------------------------------------------------------------------

AnnotationElement::AnnotationElement(AnnotationId id, const LineAnnotationData& data)
    : _id(id), _kind(Kind::Line), _line(data)
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
        case Kind::Text: return 0; // text boxes are body-drag only in V1
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
        case Kind::Text:
            break; // text boxes are body-drag only in V1; numHandles() returns 0
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
        case Kind::Text:
            break; // text boxes are body-drag only in V1; numHandles() returns 0
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


AnnotationHitResult AnnotationDocument::hitTest(const Point& widgetPos,
                                                const WidgetToImageTransform& transform,
                                                AnnotationId selectedId,
                                                float handleRadiusPx,
                                                float bodyTolerancePx) const
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
                bodyHit = d <= (bodyTolerancePx + ld.strokeWidth * 0.5);
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

void renderLineAnnotation(ImDrawList* drawList,
                          const LineAnnotationData& data,
                          const AnnotationRenderTransform& transform)
{
    const ImVec2 p1 = transform.textureToScreen(data.textureLine.p1);
    const ImVec2 p2 = transform.textureToScreen(data.textureLine.p2);
    const float thickness = std::max(1.0f, data.strokeWidth * transform.imagePixelToScreenPixel);
    drawList->AddLine(p1, p2, data.color, thickness);
}

void renderTextAnnotation(ImDrawList* drawList,
                          const TextAnnotationData& data,
                          const AnnotationRenderTransform& transform)
{
    const ImVec2 tl = transform.textureToScreen(data.textureBox.topLeft());
    const float fontSize = std::max(1.0f, data.fontSize * transform.imagePixelToScreenPixel);
    ImFont* font = ImGui::GetFont();
    drawList->AddText(font, fontSize, tl, data.color, data.text.c_str());
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
