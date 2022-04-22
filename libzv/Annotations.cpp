//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Annotations.h"

#include <libzv/ImguiUtils.h>
#include <libzv/Utils.h>

#define IMGUI_DEFINE_MATH_OPERATORS 1
#include <imgui.h>
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include <GL/gl3w.h>

namespace zv
{

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
    if (!impl->_sharedImguiContext)
    {
        // FIXME: use a shared font atlas.
        impl->_sharedImguiContext = ImGui::CreateContext(prevContext->IO.Fonts);
        impl->_sharedImguiContext->IO.BackendRendererUserData = prevContext->IO.BackendRendererUserData;
    }
}

void AnnotationRenderer::shutdown ()
{
    if (impl->_sharedImguiContext)
    {
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

void AnnotationModifier::apply (const ImageItemData& inputData, ImageItemData& outputData, AnnotationRenderer& annotationRenderer)
{   
    const int w = inputData.cpuData->width();
    const int h = inputData.cpuData->height();

    annotationRenderer.beginRendering (inputData);
    renderAnnotation (w, h);
    annotationRenderer.endRendering (outputData);
}

void LineAnnotation::renderAnnotation (int imageWidth, int imageHeight)
{
    Line imageLine = _params.validImageLineForSize (imageWidth, imageHeight);
    ImGui::GetWindowDrawList()->AddLine(imVec2(imageLine.p1), imVec2(imageLine.p2), _params.color, _params.lineWidth);
}

Line LineAnnotation::Params::imageAlignedTextureLine (int width, int height) const
{
    Line rounded;
    rounded.p1 = uvToRoundedPixel (textureLine.p1, width, height);
    rounded.p2 = uvToRoundedPixel (textureLine.p2, width, height);
    return rounded;
}

Line LineAnnotation::Params::validImageLineForSize(int width, int height) const
{
    Line alignedLine = imageAlignedTextureLine(width, height);
    alignedLine.scale (width, height);
    alignedLine.p1.x = keepInRange(alignedLine.p1.x, 0., width-1.0);
    alignedLine.p1.y = keepInRange(alignedLine.p1.y, 0., height-1.0);
    alignedLine.p2.x = keepInRange(alignedLine.p2.x, 0., width-1.0);
    alignedLine.p2.y = keepInRange(alignedLine.p2.y, 0., height-1.0);
    return alignedLine;
}

Point LineAnnotation::Params::controlPointPos (int idx, const Line& imageAlignedTextureLine)
{
    switch (idx)
    {
        case 0: return imageAlignedTextureLine.p1;
        case 1: return imageAlignedTextureLine.p2;
    }
    return Point(-1,-1);
}

void LineAnnotation::Params::updateControlPoint (int idx, const Point& p, int imageWidth, int imageHeight)
{
    switch (idx)
    {
        case 0: textureLine.p1 = p; break;
        case 1: textureLine.p2 = p; break;
    }
}

} // namespace zv
