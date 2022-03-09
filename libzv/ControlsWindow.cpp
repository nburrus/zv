//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ControlsWindow.h"

#include <libzv/ImguiUtils.h>
#include <libzv/ImguiGLFWWindow.h>
#include <libzv/ImageWindow.h>
#include <libzv/ImageWindowState.h>
#include <libzv/GLFWUtils.h>
#include <libzv/ImageCursorOverlay.h>
#include <libzv/PlatformSpecific.h>
#include <libzv/Viewer.h>

#include <libzv/Utils.h>

#define IMGUI_DEFINE_MATH_OPERATORS 1
#include "imgui.h"

#include <ImGuiFileDialog/ImGuiFileDialog.h>

#include <cstdio>

namespace zv
{

struct ControlsWindow::Impl
{
    Viewer* viewer = nullptr;
    
    ControlsWindowInputState inputState;

    // Debatable, but since we don't need polymorphism I've decided to use composition
    // for more flexibility, encapsulation (don't need to expose all the methods)
    // and explicit code.
    ImguiGLFWWindow imguiGlfwWindow;

    struct {
        bool showAfterNextRendering = false;
        bool needRepositioning = false;
        zv::Point targetPosition;

        void setCompleted () { *this = {}; }
    } updateAfterContentSwitch;

    ImVec2 monitorSize = ImVec2(0,0);

    // Tweaked manually by letting ImGui auto-resize the window.
    // 20 vertical pixels per new line.
    ImVec2 windowSizeAtDefaultDpi = ImVec2(640, 382 + 20 + 20);
    ImVec2 windowSizeAtCurrentDpi = ImVec2(-1,-1);
    
    ImageCursorOverlay cursorOverlay;
};

ControlsWindow::ControlsWindow()
: impl (new Impl ())
{}

ControlsWindow::~ControlsWindow() = default;

const ControlsWindowInputState& ControlsWindow::inputState () const
{
    return impl->inputState;
}

void ControlsWindow::shutdown() { impl->imguiGlfwWindow.shutdown(); }

void ControlsWindow::setEnabled(bool enabled)
{ 
    impl->imguiGlfwWindow.setEnabled(enabled);
    
    if (enabled)
    {
        if (impl->updateAfterContentSwitch.needRepositioning)
        {
            // Needs to be after on Linux.
            impl->imguiGlfwWindow.setWindowPos(impl->updateAfterContentSwitch.targetPosition.x,
                                               impl->updateAfterContentSwitch.targetPosition.y);
        }
        impl->updateAfterContentSwitch.setCompleted ();
    }
    else
    {
        // Make sure to reset the input state when the window gets dismissed.
        impl->inputState = {};
    }
}

bool ControlsWindow::isEnabled() const { return impl->imguiGlfwWindow.isEnabled(); }

// Warning: may be ignored by some window managers on Linux.. 
// Working hack would be to call 
// sendEventToWM(window, _glfw.x11.NET_ACTIVE_WINDOW, 2, 0, 0, 0, 0);
// in GLFW (notice the 2 instead of 1 in the source code).
// Kwin ignores that otherwise.
void ControlsWindow::bringToFront ()
{
    glfw_reliableBringToFront (impl->imguiGlfwWindow.glfwWindow());
}

bool ControlsWindow::isInitialized () const
{
    return impl->imguiGlfwWindow.isInitialized();
}

bool ControlsWindow::initialize (GLFWwindow* parentWindow, Viewer* viewer)
{
    zv_assert (viewer, "Cannot be null, we don't check it everywhere.");
    impl->viewer = viewer;

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    impl->monitorSize = ImVec2(mode->width, mode->height);

    const zv::Point dpiScale = ImguiGLFWWindow::primaryMonitorContentDpiScale();

    impl->windowSizeAtCurrentDpi = impl->windowSizeAtDefaultDpi;
    impl->windowSizeAtCurrentDpi.x *= dpiScale.x;
    impl->windowSizeAtCurrentDpi.y *= dpiScale.y;

    zv::Rect geometry;
    geometry.size.x = impl->windowSizeAtCurrentDpi.x;
    geometry.size.y = impl->windowSizeAtCurrentDpi.y;
    geometry.origin.x = (impl->monitorSize.x - geometry.size.x)/2;
    geometry.origin.y = (impl->monitorSize.y - geometry.size.y)/2;

    glfwWindowHint(GLFW_RESIZABLE, true);
    bool ok = impl->imguiGlfwWindow.initialize (parentWindow, "zv controls", geometry);
    if (!ok)
    {
        return false;
    }
    
    glfwWindowHint(GLFW_RESIZABLE, true); // restore the default.

    // This leads to issues with the window going to the back after a workspace switch.
    // setWindowFlagsToAlwaysShowOnActiveDesktop(impl->imguiGlfwWindow.glfwWindow());
    
    // This is tricky, but with GLFW windows we can't have multiple windows waiting for
    // vsync or we'll end up with the framerate being 60 / numberOfWindows. So we'll
    // just keep the image window with the vsync, and skip it for the controls window.
    // Another option would be multi-threading or use a single OpenGL context,
    // but I don't want to introduce that complexity.
    glfwSwapInterval (0);

    return ok;
}

void ControlsWindow::repositionAfterNextRendering (const zv::Rect& viewerWindowGeometry, bool showRequested)
{
    // FIXME: padding probably depends on the window manager
    const int expectedHighlightWindowWidthWithPadding = impl->windowSizeAtCurrentDpi.x + 12;
    // Try to put it on the left first.
    if (viewerWindowGeometry.origin.x > expectedHighlightWindowWidthWithPadding)
    {
        impl->updateAfterContentSwitch.needRepositioning = true;
        impl->updateAfterContentSwitch.targetPosition.x = viewerWindowGeometry.origin.x - expectedHighlightWindowWidthWithPadding;
        impl->updateAfterContentSwitch.targetPosition.y = viewerWindowGeometry.origin.y;
    }
    else if ((impl->monitorSize.x - viewerWindowGeometry.origin.x - viewerWindowGeometry.size.x) > expectedHighlightWindowWidthWithPadding)
    {
        impl->updateAfterContentSwitch.needRepositioning = true;
        impl->updateAfterContentSwitch.targetPosition.x = viewerWindowGeometry.origin.x + viewerWindowGeometry.size.x + 8;
        impl->updateAfterContentSwitch.targetPosition.y = viewerWindowGeometry.origin.y;
    }
    else
    {
        // Can't fit along side the image window, so just leave it to its default position.
        impl->updateAfterContentSwitch.needRepositioning = false;
    }

    impl->updateAfterContentSwitch.showAfterNextRendering = showRequested;
}

void ControlsWindow::openImage ()
{
    ImGuiFileDialog::Instance()->OpenModal("ChooseImageDlgKey",
                                           "Open Image",
                                           ".png,.jpg,.jpeg,.bmp,.tiff",
                                           ".",
                                           10000 /* vCountSelectionMax */);
}

void ControlsWindow::renderFrame ()
{
    const auto frameInfo = impl->imguiGlfwWindow.beginFrame ();
    const auto& io = ImGui::GetIO();
    const float monoFontSize = ImguiGLFWWindow::monoFontSize(io);
    auto* imageWindow = impl->viewer->imageWindow();

    if (impl->imguiGlfwWindow.closeRequested())
    {
        setEnabled (false);
    }

    if (ImGui::IsKeyPressed(GLFW_KEY_Q) || ImGui::IsKeyPressed(GLFW_KEY_ESCAPE))
    {
        impl->viewer->onDismissRequested();
    }

    int menuBarHeight = 0;
    if (ImGui::BeginMainMenuBar())
    {
        menuBarHeight = ImGui::GetWindowSize().y;

        if (ImGui::BeginMenu("File"))
        {
            // if (ImGui::MenuItem("Save Image", "Ctrl + s", false))
            // {
            //     imageWindow->saveCurrentImage ();
            // }

            if (ImGui::MenuItem("Open Image", "Ctrl + o", false))
            {
                impl->viewer->onOpenImage();
            }

            if (ImGui::MenuItem("Close", "q", false))
            {
                impl->viewer->onDismissRequested();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Original size", "n", false)) imageWindow->processKeyEvent (GLFW_KEY_N);
            if (ImGui::MenuItem("Double size", ">", false)) imageWindow->processKeyEvent ('>');
            if (ImGui::MenuItem("Half size", "<", false)) imageWindow->processKeyEvent ('<');
            if (ImGui::MenuItem("Restore aspect ratio", "a", false)) imageWindow->processKeyEvent (GLFW_KEY_A);
            if (ImGui::BeginMenu("Grid"))
            {
                if (ImGui::MenuItem("Single image", "1"))
                {
                    imageWindow->setLayout(1,1,1);
                }
                if (ImGui::BeginMenu("2 images"))
                {
                    if (ImGui::MenuItem("1x2", "2")) imageWindow->setLayout(2,1,2);
                    if (ImGui::MenuItem("2x1")) imageWindow->setLayout(2,2,1);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("3 images"))
                {
                    if (ImGui::MenuItem("1x3", "3")) imageWindow->setLayout(3,1,3);
                    if (ImGui::MenuItem("3x1")) imageWindow->setLayout(3,3,1);
                    if (ImGui::MenuItem("2x2")) imageWindow->setLayout(3,2,2);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("4 images"))
                {
                    if (ImGui::MenuItem("2x2", "4")) imageWindow->setLayout(4,2,2);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Help", NULL, false))
                impl->viewer->onHelpRequested();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // ImGui::ShowDemoWindow();
    
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
    
    // flags = 0;

    // Always show the ImGui window filling the GLFW window.
    ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(frameInfo.windowContentWidth, frameInfo.windowContentHeight), ImGuiCond_Always);
    if (ImGui::Begin("zv controls", nullptr, flags))
    {
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        if (ImGuiFileDialog::Instance()->Display("ChooseImageDlgKey", ImGuiWindowFlags_NoCollapse, contentSize, contentSize))
        {
            if (ImGuiFileDialog::Instance()->IsOk() == true)
            {
                // map<FileName, FilePathName>
                std::map<std::string, std::string> files = ImGuiFileDialog::Instance()->GetSelection();
                // Add image will keep adding to the top, so process them in the reverse order.
                for (auto it = files.rbegin(); it != files.rend(); ++it)
                {
                    impl->viewer->imageList().addImage(imageItemFromPath (it->second));
                }
                impl->viewer->imageList().selectImage (impl->viewer->imageList().numImages() - 1);
            }
            // close
            ImGuiFileDialog::Instance()->Close();
        }

        auto& imageWindowState = imageWindow->mutableState();
                
        ImageList& imageList = impl->viewer->imageList();
        if (ImGui::BeginListBox("##filelist", ImVec2(-FLT_MIN, 4 * ImGui::GetTextLineHeightWithSpacing())))
        {
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const int minSelectedIndex = imageList.selectedIndex();
            const int maxSelectedIndex = minSelectedIndex + imageWindowState.layoutConfig.numImages;
            for (int idx = 0; idx < imageList.numImages(); ++idx)
            {
                bool selected = (idx >= minSelectedIndex && idx < maxSelectedIndex);
                std::string name = imageList.imageItemFromIndex(idx)->prettyName();
                
                // Make sure that the last part of the filename will be visible.
                ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
                if (textSize.x > availableWidth)
                {
                    ImGui::SetCursorPosX(availableWidth - textSize.x);
                }
                if (ImGui::Selectable(name.c_str(), selected) && idx != minSelectedIndex)
                {
                    imageList.selectImage (idx);
                }
            }
            ImGui::EndListBox();
        }
        
        const auto* cursorOverlayInfo = &imageWindow->cursorOverlayInfo();
        if (cursorOverlayInfo->valid())
        {
            ImGui::SetCursorPosY (ImGui::GetWindowHeight() - monoFontSize*15.5);
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            // ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1,0,0,1));
            ImGui::BeginChild("CursorOverlay", ImVec2(monoFontSize*21, monoFontSize*14), false /* no border */, windowFlagsWithoutAnything());
            impl->cursorOverlay.showTooltip(*cursorOverlayInfo, false /* not as tooltip */);
            ImGui::EndChild();
            // ImGui::PopStyleColor();
        }

        imageWindow->checkImguiGlobalImageKeyEvents ();
        imageWindow->checkImguiGlobalImageMouseEvents ();
        
        // Debug: show the FPS.
        if (ImGui::IsKeyPressed(GLFW_KEY_F))
        {
            ImGui::Text("%.1f FPS", io.Framerate);
        }

        impl->inputState.shiftIsPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);
    }

    ImGui::End();

    // ImGui::ShowDemoWindow();
    // ImGui::ShowUserGuide();
    
    impl->imguiGlfwWindow.endFrame ();
    
    if (impl->updateAfterContentSwitch.showAfterNextRendering)
    {
        setEnabled(true);
    }
}

} // zv
