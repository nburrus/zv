//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImguiUtils.h"

#include <libzv/Utils.h>
#include <libzv/PlatformSpecific.h>

#include "imgui_internal.h"

#include <GLFW/glfw3.h>

namespace zv
{

bool IsItemHovered(ImGuiHoveredFlags flags, float delaySeconds)
{
    return ImGui::IsItemHovered(flags) && ImGui::GetCurrentContext()->HoveredIdTimer > delaySeconds;
}

zv::Point ImGui_primaryMonitorContentDpiScale ()
{
    float dpiScale_x = 1.f, dpiScale_y = 1.f;

    // On macOS, content scaling will be done automatically. Instead the
    // framebuffers will get resized.
#if !PLATFORM_MACOS
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    glfwGetMonitorContentScale(monitor, &dpiScale_x, &dpiScale_y);
#endif

    return zv::Point(dpiScale_x, dpiScale_y);
}

zv::Point ImGui_primaryMonitorRetinaFrameBufferScale ()
{
    float dpiScale_x = 1.f, dpiScale_y = 1.f;

    // This framebuffer scaling only happens on macOS.
#if PLATFORM_MACOS
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    glfwGetMonitorContentScale(monitor, &dpiScale_x, &dpiScale_y);
#endif

    return zv::Point(dpiScale_x, dpiScale_y);
}

void ImGui_PushMonoSpaceFont (const ImGuiIO& io, bool small)
{
    ImGui::PushFont(io.Fonts->Fonts[small ? 2 : 1]); 
}

float ImGui_MonoFontSize (const ImGuiIO& io)
{
    return io.Fonts->Fonts[1]->FontSize * io.Fonts->Fonts[1]->Scale;
}

static void AddUnderLine( ImColor col_ )
{
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    min.y = max.y;
    ImGui::GetWindowDrawList()->AddLine( min, max, col_, 1.0f );
}

// From https://gist.github.com/dougbinks/ef0962ef6ebe2cadae76c4e9f0586c69#file-imguiutils-h-L228-L262
void TextURL( const char* name_, const char* URL_, bool SameLineBefore_, bool SameLineAfter_ )
{
    if( SameLineBefore_ ){ ImGui::SameLine( 0.0f, ImGui::GetStyle().ItemInnerSpacing.x ); }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
    ImGui::Text("%s", name_);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
    {
        if( ImGui::IsMouseClicked(0) )
        {
            zv::openURLInBrowser( URL_ );
        }
        AddUnderLine( ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered] );
        // ImGui::SetTooltip( ICON_FA_LINK "  Open in browser\n%s", URL_ );
    }
    else
    {
        AddUnderLine( ImGui::GetStyle().Colors[ImGuiCol_Button] );
    }
    if( SameLineAfter_ ){ ImGui::SameLine( 0.0f, ImGui::GetStyle().ItemInnerSpacing.x ); }
}

void ControlPoint::update (Point pos, const std::function<void(Point)>& onDragUpdate)
{
    _pos = pos;

    auto& io = ImGui::GetIO();
    const bool isLeftButtonDragged = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (_dragged)
    {
        if (isLeftButtonDragged)
        {
            onDragUpdate(toPoint(io.MousePos));
            return;
        }
        else
        {
            _dragged = false;
            return;
        }
    }

    if (isLeftButtonDragged && (toPoint(io.MouseClickedPos[0]) - _pos).length() < _radius*1.5)
    {
        _dragged = true;
        onDragUpdate (toPoint(io.MousePos));
    }
}

void ControlPoint::render () const
{
    ImGui::GetWindowDrawList()->AddCircleFilled(imVec2(_pos), _radius, IM_COL32(255,215,0,255));
}

} // zv
