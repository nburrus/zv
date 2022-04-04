//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Modifiers.h"

#include <libzv/ImguiUtils.h>
#include <libzv/Utils.h>

#define IMGUI_DEFINE_MATH_OPERATORS 1
#include <imgui.h>
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include <GL/gl3w.h>

namespace zv
{

void AnnotationRenderer::initializeFromCurrentContext ()
{
    ImGuiContext* prevContext = ImGui::GetCurrentContext();
    zv_assert (prevContext, "This should be called with a parent context set.");
    if (!_sharedImguiContext)
    {
        // FIXME: use a shared font atlas.
        _sharedImguiContext = ImGui::CreateContext(prevContext->IO.Fonts);
        _sharedImguiContext->IO.BackendRendererUserData = prevContext->IO.BackendRendererUserData;
    }
}

void AnnotationRenderer::shutdown ()
{
    if (_sharedImguiContext)
    {
        ImGui::DestroyContext(_sharedImguiContext);
        _sharedImguiContext = nullptr;
    }
}

void AnnotationRenderer::enableContext ()
{
    _prevContext = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(_sharedImguiContext);
}

void AnnotationRenderer::disableContext ()
{
    ImGui::SetCurrentContext(_prevContext);
    _prevContext = nullptr;
}

void AnnotationRenderer::beginRendering (const ImageItemDataPtr& inputData)
{
    const int outW = input.cpuData->width();
    const int outH = input.cpuData->height();
        
    enableContext ();
    ImGui::GetIO().DisplaySize = ImVec2(outW, outH);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(outW, outH), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("#empty", nullptr, windowFlagsWithoutAnything());
    ImGui::Image(reinterpret_cast<void*>(input.textureData->textureId()), ImVec2(outW, outH));
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(10, 10) + screenFromWindow, ImVec2(64, 64) + screenFromWindow, IM_COL32(0, 0, 255, 255));    
}

void AnnotationRenderer::endRendering (ImageItemDataPtr& outputData)
{
    ImGui::PopStyleVar(2);
    ImGui::End();
    ImGui::Render();

    output.cpuData = std::make_shared<ImageSRGBA>(outW, outH);
    if (!output.textureData)
        output.textureData = std::make_shared<GLTexture>();

    GLFrameBuffer frameBuffer (output.textureData);
    frameBuffer.enable(outW, outH);
    checkGLError();
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    checkGLError();
    if (!output.cpuData)
        output.cpuData = std::make_shared<ImageSRGBA>();
    frameBuffer.downloadBuffer(*output.cpuData);
    frameBuffer.disable();
    
    checkGLError();
    
    disableContext ();
    output.status = ImageItemData::Status::Ready;
}

void LineAnnotation::render ()
{
    ImGui::GetWindowDrawList()->AddLine(imVec2(_p1), imVec2(_p2), IM_COL32(255, 0, 0, 255));
}

} // namespace zv