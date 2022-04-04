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

    int lastSelectedIdx = 0;
    
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

    bool saveAllChanges = false;
    bool askToConfirmPendingChanges = false;

    ModifiedImagePtr modImToSave = nullptr;
    void saveNextModifiedImage ();
};

void ControlsWindow::Impl::saveNextModifiedImage ()
{
    auto* imageWindow = viewer->imageWindow();
    modImToSave = imageWindow->getFirstModifiedImage();
    if (!modImToSave)
    {
        viewer->onAllChangesSaved (false /* not cancelled */);
        return;
    }

    ImGuiFileDialog::Instance()->OpenModal("SaveImageDlgKey",
                                           "Save Image",
                                           ".png,.bmp,.gif,.jpg,.jpeg,.pnm,.pgm",
                                           modImToSave->item()->sourceImagePath.empty() ? "new_image.png" : modImToSave->item()->sourceImagePath,
                                           1, /* vCountSelectionMax */
                                           nullptr,
                                           ImGuiFileDialogFlags_ConfirmOverwrite);
}

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
                                           "Image files (*.png *.bmp *.gif *.jpg *.jpeg *.pnm){.png,.bmp,.gif,.jpg,.jpeg,.pnm,.pgm}",
                                           ".",
                                           10000 /* vCountSelectionMax */);
}

void ControlsWindow::saveAllChanges ()
{
    impl->saveNextModifiedImage ();
}

void ControlsWindow::confirmPendingChanges ()
{
    impl->askToConfirmPendingChanges = true;
}

#if PLATFORM_MACOS
# define CtrlOrCmd_Str "Cmd"
#else
# define CtrlOrCmd_Str "Ctrl"
#endif

void ControlsWindow::renderFrame ()
{
    const auto frameInfo = impl->imguiGlfwWindow.beginFrame ();
    const auto& io = ImGui::GetIO();
    const float monoFontSize = ImguiGLFWWindow::monoFontSize(io);
    auto* imageWindow = impl->viewer->imageWindow();
    auto& imageWindowState = imageWindow->mutableState();

    if (impl->imguiGlfwWindow.closeRequested())
    {
        setEnabled (false);
    }

    if (!io.WantCaptureKeyboard)
    {
        if (ImGui::IsKeyPressed(GLFW_KEY_Q))
        {
            impl->viewer->onDismissRequested();
        }

        if (ImGui::IsKeyPressed(GLFW_KEY_ESCAPE))
        {
            impl->viewer->onToggleControls();
        }
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
                              | ImGuiWindowFlags_MenuBar
                              // | ImGuiWindowFlags_NoDocking
                              | ImGuiWindowFlags_NoNav);
    
    // flags = 0;

    // Always show the ImGui window filling the GLFW window.
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(frameInfo.windowContentWidth, frameInfo.windowContentHeight), ImGuiCond_Always);
    // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(1,1));
    if (ImGui::Begin("zv controls", nullptr, flags))
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                // if (ImGui::MenuItem("Save Image", "Ctrl + s", false))
                // {
                //     imageWindow->saveCurrentImage ();
                // }

                if (ImGui::MenuItem("Open Image", CtrlOrCmd_Str "+o", false))
                {
                    impl->viewer->onOpenImage();
                }

                if (ImGui::MenuItem("Close", "q", false))
                {
                    impl->viewer->onDismissRequested();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Copy to clipboard", CtrlOrCmd_Str "+c", false))
                {
                    imageWindow->processKeyEvent(GLFW_KEY_C);
                }
                if (ImGui::MenuItem("Paste from clipboard", CtrlOrCmd_Str "+v", false))
                {
                    imageWindow->processKeyEvent(GLFW_KEY_V);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Transform"))
            {
                if (ImGui::MenuItem("Rotate Left (-90)", "", false))
                {
                    imageWindow->addCommand (ImageWindow::actionCommand(ImageWindowAction::Modify_Rotate270));
                }
                if (ImGui::MenuItem("Rotate Right (+90)", "", false))
                {
                    imageWindow->addCommand (ImageWindow::actionCommand(ImageWindowAction::Modify_Rotate90));
                }
                if (ImGui::MenuItem("Rotate UpsideDown (180)", "", false))
                {
                    imageWindow->addCommand (ImageWindow::actionCommand(ImageWindowAction::Modify_Rotate180));
                }
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Annotate"))
            {
                if (ImGui::MenuItem("Add Line", "", false))
                {
                    imageWindow->addCommand (ImageWindow::actionCommand(ImageWindowAction::Annotate_AddLine));
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Info overlay", "v", &imageWindowState.infoOverlayEnabled);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::MenuItem("Original", "n", false))
                    imageWindow->processKeyEvent(GLFW_KEY_N);
                if (ImGui::MenuItem("Maxspect", "m", false))
                    imageWindow->processKeyEvent(GLFW_KEY_M);
                if (ImGui::MenuItem("Double size", ">", false))
                    imageWindow->processKeyEvent('>');
                if (ImGui::MenuItem("Half size", "<", false))
                    imageWindow->processKeyEvent('<');
                if (ImGui::MenuItem("10% larger", ".", false))
                    imageWindow->processKeyEvent(GLFW_KEY_PERIOD);
                if (ImGui::MenuItem("10% smaller", ",", false))
                    imageWindow->processKeyEvent(GLFW_KEY_COMMA);
                if (ImGui::MenuItem("Restore aspect ratio", "a", false))
                    imageWindow->processKeyEvent(GLFW_KEY_A);
                if (ImGui::BeginMenu("Layout"))
                {
                    if (ImGui::MenuItem("Single image", "1"))
                    {
                        imageWindow->addCommand(ImageWindow::layoutCommand(1, 1));
                    }
                    if (ImGui::MenuItem("2 columns", "2"))
                    {
                        imageWindow->addCommand(ImageWindow::layoutCommand(1, 2));
                    }
                    if (ImGui::MenuItem("3 columns", "3"))
                    {
                        imageWindow->addCommand(ImageWindow::layoutCommand(1, 3));
                    }
                    if (ImGui::MenuItem("2 rows"))
                    {
                        imageWindow->addCommand(ImageWindow::layoutCommand(2, 1));
                    }
                    if (ImGui::MenuItem("3 rows"))
                    {
                        imageWindow->addCommand(ImageWindow::layoutCommand(3, 1));
                    }
                    if (ImGui::MenuItem("2x2"))
                    {
                        imageWindow->addCommand(ImageWindow::layoutCommand(2, 2));
                    }
                    if (ImGui::MenuItem("2x3"))
                    {
                        imageWindow->addCommand(ImageWindow::layoutCommand(2, 3));
                    }
                    if (ImGui::MenuItem("3x4"))
                    {
                        imageWindow->addCommand(ImageWindow::layoutCommand(3, 4));
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
            ImGui::EndMenuBar();
        }

        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        if (ImGuiFileDialog::Instance()->Display("ChooseImageDlgKey", ImGuiWindowFlags_NoCollapse, contentSize, contentSize))
        {
            if (ImGuiFileDialog::Instance()->IsOk() == true)
            {
                // map<FileName, FilePathName>
                std::map<std::string, std::string> files = ImGuiFileDialog::Instance()->GetSelection();
                // Add image will keep adding to the top, so process them in the reverse order.
                for (const auto& it  : files)
                {
                    impl->viewer->imageList().addImage(imageItemFromPath (it.second), -1 /* end */, false /* replace */);
                }
                impl->viewer->imageList().setSelectionStart (impl->viewer->imageList().numImages() - 1);
            }
            // close
            ImGuiFileDialog::Instance()->Close();
        }

        if (ImGuiFileDialog::Instance()->Display("SaveImageDlgKey", ImGuiWindowFlags_NoCollapse, contentSize, contentSize))
        {
            if (ImGuiFileDialog::Instance()->IsOk() == true)
            {
                std::string outputPath = ImGuiFileDialog::Instance()->GetFilePathName();
                zv_dbg ("outputPath: %s", outputPath.c_str());
                // impl->modImToSave->saveChanges (outputPath);
                // FIXME: TEMP TEMP!
                impl->modImToSave->discardChanges();
                ImGuiFileDialog::Instance()->Close();
                impl->saveNextModifiedImage ();
            }
            else
            {
                ImGuiFileDialog::Instance()->Close();
                impl->viewer->onAllChangesSaved (true /* cancelled */);
            }
        }

        if (impl->askToConfirmPendingChanges)
        {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::OpenPopup("Confirm pending changes?");
            if (ImGui::BeginPopupModal("Confirm pending changes?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("The current image has been modified.\n Save the pending changes?\n\n");
                ImGui::Separator();

                if (ImGui::Button("OK", ImVec2(120, 0)))
                {
                    impl->askToConfirmPendingChanges = false;
                    impl->viewer->onPendingChangedConfirmed(Viewer::Confirmation::Ok);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SetItemDefaultFocus();

                ImGui::SameLine();
                if (ImGui::Button("Discard", ImVec2(120, 0)))
                {
                    impl->askToConfirmPendingChanges = false;
                    impl->viewer->onPendingChangedConfirmed(Viewer::Confirmation::Discard);
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if (ImGui::Button("Cancel Action", ImVec2(120, 0)))
                {
                    impl->askToConfirmPendingChanges = false;
                    impl->viewer->onPendingChangedConfirmed(Viewer::Confirmation::Cancel);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        float overlayHeight = 0.f;
        const auto* cursorOverlayInfo = &imageWindow->cursorOverlayInfo();
        if (cursorOverlayInfo->valid())
        {
            overlayHeight = monoFontSize*13.5;
        }

        ImageList& imageList = impl->viewer->imageList();
        
        static ImGuiTextFilter filter;
        const std::string filterTitle = "Filter files";
        const float filterWidth = ImGui::GetFontSize() * 16;
        // const float filterWidth = contentSize.x - ImGui::CalcTextSize(filterTitle.c_str()).x;        
        if (filter.Draw(filterTitle.c_str(), filterWidth))
        {
            imageList.setFilter ([](const std::string& s) {
                return filter.PassFilter (s.c_str());
            });
        }

        contentSize = ImGui::GetContentRegionAvail();
        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("Images", 2, flags, ImVec2(0,contentSize.y - overlayHeight)))
        {
            const float availableWidth = contentSize.x;
            const SelectionRange& selectionRange =  imageList.selectedRange();

            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (int idx = 0; idx < imageList.numImages(); ++idx)
            {
                const ImageItemPtr& itemPtr = imageList.imageItemFromIndex(idx);
                if (itemPtr->disabled) // from the filter.
                    continue;

                const int firstValidSelectionIndex = selectionRange.firstValidIndex();
                const int minSelectedImageIndex = firstValidSelectionIndex >= 0 ? selectionRange.indices[firstValidSelectionIndex] : -1;
                bool selected = selectionRange.isSelected(idx);
                const std::string& name = itemPtr->prettyName;                

                if (selected && impl->lastSelectedIdx != idx && idx == minSelectedImageIndex)
                {
                    ImGui::SetScrollHereY();
                    impl->lastSelectedIdx = idx;
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(idx);
                if (ImGui::Selectable(name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                {
                    // Always trigger this since the global index might change if the current filter
                    // limited the options.
                    imageList.setSelectionStart (idx);
                    impl->lastSelectedIdx = idx;
                }
                ImGui::PopID();

                if (!itemPtr->sourceImagePath.empty()
                    && zv::IsItemHovered(ImGuiHoveredFlags_RectOnly, 0.5))
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(availableWidth);
                    ImGui::TextUnformatted(itemPtr->sourceImagePath.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }

                ImGui::TableNextColumn();
                if (itemPtr->metadata.width >= 0)
                {
                    ImGui::Text("%dx%d", itemPtr->metadata.width, itemPtr->metadata.height);
                }
                else
                {
                    ImGui::Text("(?x?)");
                }
            }
            ImGui::EndTable();
        }
                
        if (cursorOverlayInfo->valid())
        {
            const float padding = monoFontSize*0.25;
            const float overlayWidth = monoFontSize*21;
            ImGui::SetCursorPosY (ImGui::GetWindowHeight() - overlayHeight - padding);
            ImGui::SetCursorPosX ((contentSize.x - overlayWidth)/2.f);
            // ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.85));
            ImGui::BeginChild("CursorOverlay", ImVec2(overlayWidth, overlayHeight), false /* border */, windowFlagsWithoutAnything());
            ImGui::SetCursorPos (ImVec2(monoFontSize*0.25, monoFontSize*0.25));
            impl->cursorOverlay.showTooltip(*cursorOverlayInfo, false /* not as tooltip */);
            ImGui::EndChild();
            ImGui::PopStyleColor();
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
    // ImGui::PopStyleVar();

    ImGui::End();
    impl->imguiGlfwWindow.endFrame ();
    
    if (impl->updateAfterContentSwitch.showAfterNextRendering)
    {
        setEnabled(true);
    }
}

} // zv
