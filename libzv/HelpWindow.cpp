//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "HelpWindow.h"

#include <libzv/OpenGL.h>
#include <libzv/ImguiUtils.h>
#include <libzv/ImguiGLFWWindow.h>
#include <libzv/ImguiCanvasContainer.h>
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
    Impl()
    {
#if ZV_IMGUI_WINDOW_CONTAINER_TYPE_CANVAS
        windowContainer = std::make_unique<ImguiCanvasContainer>();
#else
        windowContainer = std::make_unique<ImguiGLFWWindow>();
#endif
    }

    std::unique_ptr<ImguiWindowContainer> windowContainer;
};

HelpWindow::HelpWindow()
: impl (new Impl())
{}

HelpWindow::~HelpWindow() = default;

bool HelpWindow::isInitialized () const
{
    return impl->windowContainer->isInitialized();
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

    const zv::Point dpiScale = ImGui_primaryMonitorContentDpiScale();
    geometry.size.x *= dpiScale.x;
    geometry.size.y *= dpiScale.y;

    geometry.origin.x = (monitorSize.x - geometry.size.x)/2;
    geometry.origin.y = (monitorSize.y - geometry.size.y)/2;

    bool ok = false;
#if ZV_IMGUI_WINDOW_CONTAINER_TYPE_CANVAS
    ImguiCanvasContainer* imguiCanvasContainer = dynamic_cast<ImguiCanvasContainer*>(impl->windowContainer.get());
    zv_assert (imguiCanvasContainer, "ImguiCanvasContainer is expected.");
    ok = imguiCanvasContainer->initialize ("zv Help", geometry);
#else
    ImguiGLFWWindow* imguiGlfwWindow = dynamic_cast<ImguiGLFWWindow*>(impl->windowContainer.get());
    zv_assert (imguiGlfwWindow, "ImguiGLFWWindow is expected.");
    ok = imguiGlfwWindow->initialize (parentWindow, "zv Help", geometry, false /* viewports */);
#endif
    if (!ok)
    {
        return false;
    }
    
    // This leads to issues with the window going to the back after a workspace switch.
    // setWindowFlagsToAlwaysShowOnActiveDesktop(impl->imguiGlfwWindow.glfwWindow());

    // No resize for the help.
    impl->windowContainer->setResizable(false);
    
    return true;
}

void HelpWindow::shutdown () 
{ 
    impl->windowContainer->shutdown (); 
}

void HelpWindow::setEnabled (bool enabled)
{
    impl->windowContainer->setEnabled (enabled);
}

bool HelpWindow::isEnabled () const
{
    return impl->windowContainer->isEnabled ();
}

void HelpWindow::renderFrame ()
{
    const auto frameInfo = impl->windowContainer->beginFrame ();    
    const auto& io = ImGui::GetIO();
    const float monoFontSize = ImGui_MonoFontSize(io);

    if (ImGui::IsKeyPressed(GLFW_KEY_Q) || ImGui::IsKeyPressed(GLFW_KEY_ESCAPE) || impl->windowContainer->closeRequested())
    {
        setEnabled(false);
    }

    ImGuiWindowFlags extraFlags = ImGuiWindowFlags_NoResize;

    bool isOpen = true;
    if (impl->windowContainer->ImGuiBegin(frameInfo, &isOpen, extraFlags))
    {
        if (!isOpen)
        {
            setEnabled(false);
        }

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
    
    impl->windowContainer->endFrame ();
}

} // zv
