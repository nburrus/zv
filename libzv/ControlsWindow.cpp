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
#include <libzv/Platform.h>
#include <libzv/Viewer.h>

#include <libzv/Utils.h>

#define IMGUI_DEFINE_MATH_OPERATORS 1
#include "imgui.h"

#include <ImGuiFileDialog/ImGuiFileDialog.h>

#include <FontIcomoon.h>

#include <cstdio>

#if PLATFORM_MACOS
# define CtrlOrCmd_Str "Cmd"
#else
# define CtrlOrCmd_Str "Ctrl"
#endif

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

    ActionToConfirm currentActionToConfirm;

    std::deque<ModifiedImagePtr> modImagesToSave;
    ModifiedImagePtr currentModImageToSave;
    bool forcePathSelectionOnSave = false;
    void saveNextModifiedImage ();

    void renderMenu ();
    void maybeRenderOpenImage ();
    void maybeRenderSaveImage ();
    void maybeRenderConfirmPendingChanges ();
    
    void renderActiveTool (const ModifiedImagePtr& firstModIm);
    void renderImageList (float cursorOverlayHeight);
    void renderModifiersTab (float cursorOverlayHeight);
    void renderCursorInfo (const CursorOverlayInfo& cursorOverlayInfo, float footerHeight, float overlayHeight);
};

void ControlsWindow::Impl::saveNextModifiedImage ()
{
    auto* imageWindow = viewer->imageWindow();    
    if (this->modImagesToSave.empty())
    {
        this->currentModImageToSave = nullptr;
        viewer->onAllChangesSaved (false /* not cancelled */);
        return;
    }

    this->currentModImageToSave = this->modImagesToSave.front();
    this->modImagesToSave.pop_front();

    // If we already saved it before, just save it to the current filepath.
    if (!forcePathSelectionOnSave && this->currentModImageToSave->item()->alreadyModifiedAndSaved)
    {
        zv_assert (this->currentModImageToSave->item()->source == ImageItem::Source::FilePath, "Expected filepath source since it was already saved.");
        this->currentModImageToSave->saveChanges (this->currentModImageToSave->item()->sourceImagePath);
        this->saveNextModifiedImage ();
    }
    else
    {
        ImGuiFileDialog::Instance()->OpenModal("SaveImageDlgKey",
                                               "Save Image",
                                               ".png,.bmp,.gif,.jpg,.jpeg,.pnm,.pgm",
                                               this->currentModImageToSave->item()->sourceImagePath.empty() ? "new_image.png" : this->currentModImageToSave->item()->sourceImagePath,
                                               1, /* vCountSelectionMax */
                                               nullptr,
                                               ImGuiFileDialogFlags_ConfirmOverwrite);
    }    
}

void ControlsWindow::Impl::maybeRenderOpenImage ()
{
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
                this->viewer->imageList().addImage(imageItemFromPath (it.second), -1 /* end */, false /* replace */);
            }
            this->viewer->imageList().setSelectionStart (this->viewer->imageList().numImages() - 1);
        }
        // close
        ImGuiFileDialog::Instance()->Close();
    }
}

void ControlsWindow::Impl::maybeRenderSaveImage ()
{
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    if (ImGuiFileDialog::Instance()->Display("SaveImageDlgKey", ImGuiWindowFlags_NoCollapse, contentSize, contentSize))
    {
        if (ImGuiFileDialog::Instance()->IsOk() == true)
        {
            std::string outputPath = ImGuiFileDialog::Instance()->GetFilePathName();
            zv_dbg ("outputPath: %s", outputPath.c_str());
            this->currentModImageToSave->saveChanges(outputPath);
            ImGuiFileDialog::Instance()->Close();
            this->saveNextModifiedImage ();
        }
        else
        {
            ImGuiFileDialog::Instance()->Close();
            this->currentModImageToSave = nullptr;
            this->modImagesToSave.clear ();
            this->viewer->onAllChangesSaved (true /* cancelled */);
        }
    }
}

void ControlsWindow::Impl::maybeRenderConfirmPendingChanges ()
{
    if (this->askToConfirmPendingChanges)
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
                this->askToConfirmPendingChanges = false;
                this->viewer->onSavePendingChangesConfirmed(Confirmation::Ok, false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();

            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(120, 0)))
            {
                this->askToConfirmPendingChanges = false;
                this->viewer->onSavePendingChangesConfirmed(Confirmation::Discard, false);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel Action", ImVec2(120, 0)))
            {
                this->askToConfirmPendingChanges = false;
                this->viewer->onSavePendingChangesConfirmed(Confirmation::Cancel, false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}

void ControlsWindow::Impl::renderActiveTool (const ModifiedImagePtr& firstModIm)
{    
    const auto& firstIm = *(firstModIm->data()->cpuData);

    auto* imageWindow = this->viewer->imageWindow();
    auto& state = imageWindow->mutableState();

    if (state.activeToolState.kind == ActiveToolState::Kind::None)
        return;

    InteractiveTool* activeTool = state.activeToolState.activeTool();

    if (state.activeToolState.kind != ActiveToolState::Kind::None)
    {
        ImGui::Spacing();
        ImGui::Separator();

        activeTool->renderControls(firstIm);

        if (ImGui::Button("Apply"))
        {
            imageWindow->addCommand(ImageWindow::actionCommand(ImageWindowAction::Kind::ApplyCurrentTool));
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            imageWindow->addCommand(ImageWindow::actionCommand(ImageWindowAction::Kind::CancelCurrentTool));
        }
    }
}

void ControlsWindow::Impl::renderModifiersTab (float cursorOverlayHeight)
{
    auto* imageWindow = this->viewer->imageWindow();
    ModifiedImagePtr firstModIm = imageWindow->getFirstValidImage(false /* not only modified */);
    auto& state = imageWindow->mutableState();
    
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    ImGui::Spacing();

    if (ImGui::Button(ICON_ROTATE_LEFT))
        imageWindow->addCommand (ImageWindow::actionCommand(ImageWindowAction::Kind::Modify_Rotate270));
    helpMarker ("Rotate Left (-90º)", contentSize.x * 0.8, false /* no extra question mark */);

    ImGui::SameLine();

    if (ImGui::Button(ICON_ROTATE_RIGHT))
        imageWindow->addCommand (ImageWindow::actionCommand(ImageWindowAction::Kind::Modify_Rotate90));
    helpMarker ("Rotate Right (+90º)", contentSize.x * 0.8, false /* no extra question mark */);

    ImGui::SameLine();

    if (ImGui::Button(ICON_CROP))
        imageWindow->setActiveTool (ActiveToolState::Kind::Transform_Crop);
    helpMarker ("Crop", contentSize.x * 0.8, false /* no extra question mark */);
    
    ImGui::SameLine();

    if (ImGui::Button(ICON_FLOW_LINE))
        imageWindow->setActiveTool (ActiveToolState::Kind::Annotate_Line);
    helpMarker ("Add Line", contentSize.x * 0.8, false /* no extra question mark */);

    ImGui::SameLine();

    if (ImGui::Button(ICON_RECTANGLE))
        imageWindow->setActiveTool (ActiveToolState::Kind::Annotate_Line);
    helpMarker ("Add Rectangle", contentSize.x * 0.8, false /* no extra question mark */);

    ImGui::SameLine();

    if (ImGui::Button(ICON_CIRCLE))
        imageWindow->setActiveTool (ActiveToolState::Kind::Annotate_Line);
    helpMarker ("Add Circle", contentSize.x * 0.8, false /* no extra question mark */);

    ImGui::SameLine();

    if (ImGui::Button(ICON_TEXT))
        imageWindow->setActiveTool (ActiveToolState::Kind::Annotate_Line);
    helpMarker ("Add Text", contentSize.x * 0.8, false /* no extra question mark */);

    if (!firstModIm->hasValidData())
        return;
    
    renderActiveTool (firstModIm);
}

void ControlsWindow::Impl::renderImageList (float cursorOverlayHeight)
{
    auto* imageWindow = this->viewer->imageWindow();
    ImageList& imageList = this->viewer->imageList();
        
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

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("Images", 2, flags, ImVec2(0,contentSize.y - cursorOverlayHeight)))
    {
        const float availableWidth = contentSize.x;
        const SelectionRange& selectionRange =  imageList.selectedRange();

        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        std::pair<int,int> dragAndDropped { -1, -1 };

        for (int idx = 0; idx < imageList.numImages(); ++idx)
        {
            const ImageItemPtr& itemPtr = imageList.imageItemFromIndex(idx);
            if (itemPtr->disabled) // from the filter.
                continue;

            const int firstValidSelectionIndex = selectionRange.firstValidIndex();
            const int minSelectedImageIndex = firstValidSelectionIndex >= 0 ? selectionRange.indices[firstValidSelectionIndex] : -1;
            bool selected = selectionRange.isSelected(idx);
            const std::string& name = itemPtr->prettyName;                

            if (selected && this->lastSelectedIdx != idx && idx == minSelectedImageIndex)
            {
                ImGui::SetScrollHereY();
                this->lastSelectedIdx = idx;
            }                      

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(idx);
            if (ImGui::Selectable(name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                // Always trigger this since the global index might change if the current filter
                // limited the options.
                auto paramsPtr = std::make_shared<ImageWindowAction::Params>();
                paramsPtr->intParams[0] = idx;
                imageWindow->addCommand (ImageWindow::actionCommand(ImageWindowAction::Kind::View_SelectImage, paramsPtr));
                this->lastSelectedIdx = idx;
            }

            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("_IMAGE_ITEM", reinterpret_cast<void*>(&idx), sizeof(int));
                ImGui::Text("%s", name.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("_IMAGE_ITEM"))
                {
                    IM_ASSERT(payload->DataSize == sizeof(int));
                    int sourceIndex = *reinterpret_cast<int*>(payload->Data);
                    dragAndDropped.first = sourceIndex;
                    dragAndDropped.second = idx;
                }
                ImGui::EndDragDropTarget();
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

        if (dragAndDropped.first >= 0)
        {
            imageList.swapItems (dragAndDropped.first, dragAndDropped.second);
        }
        ImGui::EndTable();
    }
}

void ControlsWindow::Impl::renderCursorInfo (const CursorOverlayInfo& cursorOverlayInfo, 
                                             float footerHeight,
                                             float overlayHeight)
{
    auto& io = ImGui::GetIO();
    const float monoFontSize = ImguiGLFWWindow::monoFontSize(io);

    if (cursorOverlayInfo.valid())
    {
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        const float padding = monoFontSize*0.25;
        const float overlayWidth = monoFontSize*21;
        ImGui::SetCursorPosY (ImGui::GetWindowHeight() - footerHeight - padding);
        ImGui::SetCursorPosX ((contentSize.x - overlayWidth)/2.f);
        // ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.85));
        ImGui::BeginChild("CursorOverlay", ImVec2(overlayWidth, overlayHeight), false /* border */, windowFlagsWithoutAnything());
        ImGui::SetCursorPos (ImVec2(monoFontSize*0.25, monoFontSize*0.25));
        this->cursorOverlay.showTooltip(cursorOverlayInfo, false /* not as tooltip */);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}

void ControlsWindow::Impl::renderMenu ()
{
    auto* imageWindow = this->viewer->imageWindow();
    auto& imageWindowState = imageWindow->mutableState();

    const bool hasChanges = imageWindow->getFirstValidImage(true /* modified only */) != nullptr;

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Image", CtrlOrCmd_Str "+o", false))
            {
                imageWindow->addCommand(ImageWindow::actionCommand(ImageWindowAction::Kind::File_OpenImage));
            }

            if (ImGui::MenuItem("Save Image", CtrlOrCmd_Str "+s", false, hasChanges))
            {
                imageWindow->addCommand(ImageWindow::actionCommand(ImageWindowAction::Kind::File_SaveImage));
            }

            if (ImGui::MenuItem("Save Image As...", CtrlOrCmd_Str "+Shift+s", false))
            {
                imageWindow->addCommand(ImageWindow::actionCommand(ImageWindowAction::Kind::File_SaveImageAs));
            }

            if (ImGui::MenuItem("Close Image", "DEL", false))
            {
                imageWindow->addCommand(ImageWindow::actionCommand(ImageWindowAction::Kind::File_CloseImage));
            }

            if (ImGui::MenuItem("Delete Image on Disk", "Shift+DEL", false))
            {
                imageWindow->addCommand(ImageWindow::actionCommand(ImageWindowAction::Kind::File_DeleteImageOnDisk));
            }

            if (ImGui::MenuItem("Close", "q", false))
            {
                this->viewer->onDismissRequested();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", CtrlOrCmd_Str "+z", false, imageWindow->canUndo()))
            {
                imageWindow->addCommand(ImageWindow::actionCommand(ImageWindowAction::Kind::Edit_Undo));
            }
            if (ImGui::MenuItem("Revert to Original", "", false, hasChanges))
            {
                imageWindow->addCommand(ImageWindow::actionCommand(ImageWindowAction::Kind::Edit_RevertToOriginal));
            }
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

        if (ImGui::BeginMenu("Tools"))
        {
            if (ImGui::BeginMenu("Transform"))
            {
                if (ImGui::MenuItem("Rotate Left (-90)", "", false))
                {
                    imageWindow->addCommand (ImageWindow::actionCommand(ImageWindowAction::Kind::Modify_Rotate270));
                }
                if (ImGui::MenuItem("Rotate Right (+90)", "", false))
                {
                    imageWindow->addCommand (ImageWindow::actionCommand(ImageWindowAction::Kind::Modify_Rotate90));
                }
                if (ImGui::MenuItem("Rotate UpsideDown (180)", "", false))
                {
                    imageWindow->addCommand (ImageWindow::actionCommand(ImageWindowAction::Kind::Modify_Rotate180));
                }
                if (ImGui::MenuItem("Crop Image", "", false))
                {
                    imageWindowState.activeToolState.kind = ActiveToolState::Kind::Transform_Crop;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Annotate"))
            {
                if (ImGui::MenuItem("Add Line", "", false))
                {
                    imageWindowState.activeToolState.kind = ActiveToolState::Kind::Annotate_Line;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Window"))
        {
            ImGui::MenuItem("Info overlay", "v", &imageWindowState.infoOverlayEnabled);
            if (ImGui::BeginMenu("Size"))
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
                ImGui::EndMenu();
            }
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
                this->viewer->onHelpRequested();
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
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

void ControlsWindow::saveAllChanges (bool forcePathSelectionOnSave)
{
    impl->forcePathSelectionOnSave = forcePathSelectionOnSave;
    const bool modifiedOnly = !forcePathSelectionOnSave;
    impl->modImagesToSave.clear ();
    impl->viewer->imageWindow()->applyOverValidImages(modifiedOnly, [this](const ModifiedImagePtr& modIm) {
        impl->modImagesToSave.push_back (modIm);
    });
    impl->saveNextModifiedImage ();
}

void ControlsWindow::confirmPendingChanges ()
{
    impl->askToConfirmPendingChanges = true;
}

void ControlsWindow::setCurrentActionToConfirm (const ActionToConfirm& actionToConfirm)
{
    zv_assert (!impl-> currentActionToConfirm.isActive(), "Already an active confirmation!");
    impl->currentActionToConfirm = actionToConfirm;    
}

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
        impl->renderMenu ();

        impl->maybeRenderOpenImage ();
        impl->maybeRenderSaveImage ();
        impl->maybeRenderConfirmPendingChanges ();        

        if (impl->currentActionToConfirm.isActive())
        {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(frameInfo.windowContentWidth, frameInfo.windowContentHeight)*0.8, ImGuiCond_Appearing);
            ImGui::OpenPopup(impl->currentActionToConfirm.title.c_str());
            if (ImGui::BeginPopupModal(impl->currentActionToConfirm.title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                Confirmation confirmation;
                if (impl->currentActionToConfirm.renderDialog(confirmation))
                {
                    switch (confirmation)
                    {
                        case Confirmation::Cancel: {
                            if (impl->currentActionToConfirm.onCancelled)
                                impl->currentActionToConfirm.onCancelled();
                            break;
                        }

                        case Confirmation::Ok: {
                            if (impl->currentActionToConfirm.onOk)
                                impl->currentActionToConfirm.onOk();
                            break;
                        }

                        case Confirmation::Discard: {
                            if (impl->currentActionToConfirm.onDiscard)
                                impl->currentActionToConfirm.onDiscard();
                            break;
                        }

                        default:
                            zv_assert (false, "Invalid confirmation");
                    }
                    ImGui::CloseCurrentPopup();
                    impl->currentActionToConfirm = {};
                }
                ImGui::EndPopup();
            }
        }

        const auto& cursorOverlayInfo = imageWindow->cursorOverlayInfo();
        const bool showCursorOverlay = cursorOverlayInfo.valid();

        const float windowSizeWidgetsHeight = monoFontSize*1.75;
        float footerHeight = windowSizeWidgetsHeight;
        const float cursorOverlayHeight = monoFontSize*13.5;

        if (showCursorOverlay)
        {
            footerHeight += cursorOverlayHeight;
        }

        ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
        if (ImGui::BeginTabBar("TabBar", tab_bar_flags))
        {
            if (ImGui::BeginTabItem("Image List"))
            {
                impl->renderImageList (footerHeight);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Modifiers"))
            {
                impl->renderModifiersTab (footerHeight);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }        
                        
        if (showCursorOverlay)
            impl->renderCursorInfo (cursorOverlayInfo, footerHeight, cursorOverlayHeight);

        // FIXME: add tooltips. Add commands when the size is actually changed.

        ImGui::SetCursorPosY (ImGui::GetWindowHeight() - windowSizeWidgetsHeight);
        Rect imageRect = imageWindow->imageWidgetGeometry();
        int width = imageRect.size.x;
        int height = imageRect.size.y;
        ImGui::SetNextItemWidth (monoFontSize * 3);
        if (ImGui::InputInt ("##Window width", &width, -1, -1))
        {

        }
        ImGui::SameLine ();
        static bool lockRatio = true;
        ImGui::Checkbox ("##LockRatio", &lockRatio); 
        ImGui::SameLine (); ImGui::SetNextItemWidth (monoFontSize * 3);
        if (ImGui::InputInt ("##Window height", &height, -1, -1))
        {

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
