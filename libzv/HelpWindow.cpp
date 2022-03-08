//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "HelpWindow.h"

#include <libzv/OpenGL.h>
#include <libzv/ImguiUtils.h>
#include <libzv/ImguiGLFWWindow.h>
#include <libzv/Prefs.h>
#include <libzv/PlatformSpecific.h>

#include <libzv/Platform.h>

#include <libzv/OpenGL.h>

#include <libzv/Utils.h>

#include <stb/stb_image.h>

#include "PlatformSpecific.h"

#define IMGUI_DEFINE_MATH_OPERATORS 1
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include "GLFWUtils.h"

#include <cstdio>

namespace zv
{

struct HelpWindow::Impl
{
    // Debatable, but decided to use composition for more flexibility and explicit code.
    ImguiGLFWWindow imguiGlfwWindow;
};

HelpWindow::HelpWindow()
: impl (new Impl())
{}

HelpWindow::~HelpWindow() = default;

bool HelpWindow::isInitialized () const
{
    return impl->imguiGlfwWindow.isInitialized();
}

bool HelpWindow::initialize (GLFWwindow* parentWindow)
{
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    ImVec2 monitorSize = ImVec2(mode->width, mode->height);

    zv::Rect geometry;
    // Tweaked manually by letting ImGui auto-resize the window.
    // 20 vertical pixels per new line.
    geometry.size.x = 1150/2;
    geometry.size.y = 900/2 + 42;

    const zv::Point dpiScale = ImguiGLFWWindow::primaryMonitorContentDpiScale();
    geometry.size.x *= dpiScale.x;
    geometry.size.y *= dpiScale.y;

    geometry.origin.x = (monitorSize.x - geometry.size.x)/2;
    geometry.origin.y = (monitorSize.y - geometry.size.y)/2;

    bool ok = impl->imguiGlfwWindow.initialize (parentWindow, "zv Help", geometry);
    if (!ok)
    {
        return false;
    }
    
    // This leads to issues with the window going to the back after a workspace switch.
    // setWindowFlagsToAlwaysShowOnActiveDesktop(impl->imguiGlfwWindow.glfwWindow());

    // No resize for the help.
    glfwSetWindowAttrib(impl->imguiGlfwWindow.glfwWindow(), GLFW_RESIZABLE, false);
       
    return true;
}

void HelpWindow::shutdown () 
{ 
    impl->imguiGlfwWindow.shutdown (); 
}

void HelpWindow::setEnabled (bool enabled)
{
    impl->imguiGlfwWindow.setEnabled (enabled);
}

bool HelpWindow::isEnabled () const
{
    return impl->imguiGlfwWindow.isEnabled ();
}

static void AddUnderLine( ImColor col_ )
{
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    min.y = max.y;
    ImGui::GetWindowDrawList()->AddLine( min, max, col_, 1.0f );
}

// From https://gist.github.com/dougbinks/ef0962ef6ebe2cadae76c4e9f0586c69#file-imguiutils-h-L228-L262
static void TextURL( const char* name_, const char* URL_, bool SameLineBefore_, bool SameLineAfter_ )
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

void HelpWindow::renderFrame ()
{
    const auto frameInfo = impl->imguiGlfwWindow.beginFrame ();    
    const auto& io = ImGui::GetIO();
    const float monoFontSize = ImguiGLFWWindow::monoFontSize(io);

    if (ImGui::IsKeyPressed(GLFW_KEY_Q) || ImGui::IsKeyPressed(GLFW_KEY_ESCAPE) || impl->imguiGlfwWindow.closeRequested())
    {
        setEnabled(false);
    }

    ImGuiWindowFlags flags = (ImGuiWindowFlags_NoTitleBar
                              | ImGuiWindowFlags_NoResize
                              | ImGuiWindowFlags_NoMove
                              | ImGuiWindowFlags_NoScrollbar
                              | ImGuiWindowFlags_NoScrollWithMouse
                              | ImGuiWindowFlags_NoCollapse
                              | ImGuiWindowFlags_NoBackground
                              | ImGuiWindowFlags_NoSavedSettings
                              | ImGuiWindowFlags_HorizontalScrollbar
                              // | ImGuiWindowFlags_NoDocking
                              | ImGuiWindowFlags_NoNav);
    

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(frameInfo.windowContentWidth, frameInfo.windowContentHeight), ImGuiCond_Always);
    if (ImGui::Begin("zv Help Context", nullptr, flags))
    {
        static std::string appVersion;
        static std::string buildNumber;
        if (appVersion.empty())
        {
            getVersionAndBuildNumber(appVersion, buildNumber);
        }
        
        bool showOnStartup = Prefs::showHelpOnStartup();
        if (ImGui::Checkbox("Always show on startup", &showOnStartup))
        {
            Prefs::setShowHelpOnStartupEnabled(showOnStartup);
        }

        ImGui::SameLine(monoFontSize * 22, 0);
        ImGui::BeginChild("About");        
        ImGui::Text("zv %s (build ", appVersion.c_str());
            TextURL(buildNumber.c_str(), ("https://github.com/nburrus/zv/commit/" +  buildNumber).c_str(), true, true);
            ImGui::Text(")");
        TextURL("Report issues", "https://github.com/nburrus/zv", false, true);
        ImGui::EndChild();
    }
    ImGui::End();
    
    impl->imguiGlfwWindow.endFrame ();
}

} // zv
