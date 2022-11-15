//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#define GL_SILENCE_DEPRECATION 1

#include "ImageWindow.h"
#include "ImageWindowState.h"

#include <libzv/Viewer.h>
#include <libzv/ImageList.h>
#include <libzv/ImageCursorOverlay.h>
#include <libzv/ImguiUtils.h>
#include <libzv/PlatformSpecific.h>
#include <libzv/ImguiGLFWWindow.h>
#include <libzv/ControlsWindow.h>
#include <libzv/Prefs.h>

#define IMGUI_DEFINE_MATH_OPERATORS 1
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include <libzv/Image.h>
#include <libzv/Utils.h>
#include <libzv/MathUtils.h>
#include <libzv/ColorConversion.h>

// Note: need to include that before GLFW3 for some reason.
#include <GL/gl3w.h>
#include <libzv/GLFWUtils.h>

#include <libzv/Platform.h>
#if !PLATFORM_LINUX
#include <nfd.h>
#endif

#include <argparse.hpp>

#include <clip/clip.h>

#include <deque>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace zv
{

std::string viewerModeName (ViewerMode mode)
{
    switch (mode)
    {
        case ViewerMode::None: return "None";
        case ViewerMode::Original: return "Original Image";
        default: return "Invalid";
    }
}

std::string viewerModeFileName (ViewerMode mode)
{
    switch (mode)
    {
        case ViewerMode::None: return "original";
        case ViewerMode::Original: return "original";
        default: return "Invalid";
    }
}

struct ImageLayout
{
    LayoutConfig config;
    std::vector<Rect> imageRects;
    
    ImageLayout ()
    {
        imageRects.push_back(Rect::from_x_y_w_h(0.0,
                                                0.0,
                                                1.0,
                                                1.0));
    }

    Point firstImSizeInRect (Point widgetRectSize, float gridPadding) const
    {
        Point imSize;
        imSize.x = (widgetRectSize.x - (config.numCols - 1) * gridPadding) * imageRects[0].size.x;
        imSize.y = (widgetRectSize.y - (config.numRows - 1) * gridPadding) * imageRects[0].size.y;
        return imSize;
    }

    Point widgetRectForImageSize (Point firstImSize, float gridPadding) const
    {
        const auto& firstRect = imageRects[0];
        Point widgetSize;
        widgetSize.x = firstImSize.x / firstRect.size.x + (config.numCols - 1) * gridPadding;
        widgetSize.y = firstImSize.y / firstRect.size.y + (config.numRows - 1) * gridPadding;
        return widgetSize;
    }

    bool adjustForConfig (const LayoutConfig& config)
    {
        bool layoutChanged = (this->config != config);
        this->config = config;
        imageRects.resize (config.numImages());
        
        for (int r = 0; r < config.numRows; ++r)
        for (int c = 0; c < config.numCols; ++c)
        {
            const int idx = r*config.numCols + c;
            if (idx < config.numImages())
            {
                imageRects[idx] = Rect::from_x_y_w_h(double(c)/config.numCols,
                                                     double(r)/config.numRows,
                                                     1.0/config.numCols,
                                                     1.0/config.numRows);
            }
        }
        return layoutChanged;
    }
};

struct ZoomInfo
{
    int zoomFactor = 1;
    
    // UV means normalized between 0 and 1.
    ImVec2 uvCenter = ImVec2(0.5f,0.5f);
};

struct ImageWindow::Impl
{
    Impl (ImageWindow& that) : that (that)
    {}

    ImageWindow& that;

    ImguiGLFWWindow imguiGlfwWindow;
    Viewer* viewer = nullptr;

    std::vector<ModifiedImagePtr> currentImages;
    ImageLayout currentLayout;
    AnnotationRenderer annotationRenderer;
    
    ImageWindowState mutableState;

    bool enabled = false;

    ImageCursorOverlay inlineCursorOverlay;
    CursorOverlayInfo cursorOverlayInfo;

    std::deque<Command> pendingCommands;

    struct {
        GlobalEventCallbackType callback;
        void* userData;
    } globalCallback;

    struct {
        bool inProgress = false;
        bool needToResize = false;
        int numAlreadyRenderedFrames = 0;
        // This can be higher than 1 on retina displays.
        float screenToImageScale = 1.f;
        zv::Rect targetWindowGeometry;

        void setCompleted () { *this = {}; }
    } updateAfterContentSwitch;
        
    ImVec2 monitorSize = ImVec2(-1,-1);
    
    const int windowBorderSize = 0;
    bool shouldUpdateWindowSize = false;
    const int gridPadding = 1;
    
    struct {
        zv::Rect normal;
        zv::Rect current;
        // Keep track of that guy to avoid shrinking on every call.
        zv::Rect sourceForAspectRatio;
    } imageWidgetRect;
    
    ZoomInfo zoom;

    enum class WindowGeometryMode
    {
        UserDefined,
        Normal,
        AspectRatio,
        ScaleSpect, // scaling while preserving the aspect ratio
        Maxspect,
    } lastGeometryMode = WindowGeometryMode::Normal;
    
    void enterMode (ViewerMode newMode)
    {
        this->mutableState.activeMode = newMode;
    }
    
    void advanceMode (bool backwards)
    {
        advanceEnum(this->mutableState.activeMode, backwards ? -1 : 1, ViewerMode::NumModes);
    }
    
    void onImageWidgetAreaChanged ()
    {
        imguiGlfwWindow.setWindowSize(imageWidgetRect.current.size.x + windowBorderSize * 2,
                                      imageWidgetRect.current.size.y + windowBorderSize * 2);
    }

    void adjustForNewSelection ();

    void adjustAspectRatio ()
    {
        if (lastGeometryMode == WindowGeometryMode::AspectRatio)
        {
            this->imageWidgetRect.current.size = this->imageWidgetRect.sourceForAspectRatio.size;
        }
        else
        {
            this->imageWidgetRect.sourceForAspectRatio.size = this->imageWidgetRect.current.size;
        }

        float ratioX = this->imageWidgetRect.current.size.x / this->imageWidgetRect.normal.size.x;
        float ratioY = this->imageWidgetRect.current.size.y / this->imageWidgetRect.normal.size.y;
        if (ratioX <= ratioY)
        {
            this->imageWidgetRect.current.size.y = int(ratioX * this->imageWidgetRect.normal.size.y + 0.5f);
        }
        else
        {
            this->imageWidgetRect.current.size.x = int(ratioY * this->imageWidgetRect.normal.size.x + 0.5f);
        }
        this->shouldUpdateWindowSize = true;
    }

    bool runAfterCheckingPendingChanges (std::function<void(void)>&& func);
    
    void applyCurrentTool ();
    
    using CreateModifierFunc = std::function<std::unique_ptr<ImageModifier>(void)>;
    void addModifier (const CreateModifierFunc& createModifier);

    ImageWidgetRoi renderImageItem(const ModifiedImagePtr &modImagePtr,
                                   const ImVec2 &imageWidgetTopLeft,
                                   const ImVec2 &imageWidgetSize,
                                   ZoomInfo &zoom,
                                   bool imageSmallerThanNormal,
                                   CursorOverlayInfo *overlayInfo);

    void removeCurrentImageOnDisk ();
};

bool ImageWindow::Impl::runAfterCheckingPendingChanges (std::function<void(void)>&& func)
{
    bool hasPendingChanges = false;
    for (const auto& it : currentImages)
    {
        if (it != nullptr && it->hasPendingChanges())
        {
            hasPendingChanges = true;
            break;
        }
    }

    if (!hasPendingChanges)
    {
        func();
        return true;
    }

    viewer->runAfterConfirmingPendingChanges(std::move(func));
    return false;
}

void ImageWindow::Impl::adjustForNewSelection ()
{
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    this->monitorSize = ImVec2(mode->width, mode->height);

    ImageList& imageList = this->viewer->imageList();
    const auto& selectedRange = imageList.selectedRange();
    const int firstValidSelectionIndex = selectedRange.firstValidIndex();    
    
    // Can't adjust anything if the selection has no valid images.
    if (firstValidSelectionIndex < 0)
        return;

    this->imguiGlfwWindow.enableContexts ();
    
    // It's very important that this gets called while the GL context is bound
    // as it may release some GLTexture in the cache. Would be nice to make this
    // code more robust.
    this->currentImages.resize (this->mutableState.layoutConfig.numImages());
    zv_assert (selectedRange.indices.size() == this->currentImages.size(), "Inconsistent state");
    for (int i = 0; i < this->currentImages.size(); ++i)
    {
        const int selectionIndex = selectedRange.indices[i];
        if (selectionIndex >= 0 && selectionIndex < imageList.numImages())
        {
            const auto& itemPtr = imageList.imageItemFromIndex(selectionIndex);
            // Overwrite the image if the ID changed. Otherwise keep the modified image
            // since it might just have been updated with new modifiers.
            if (!this->currentImages[i] || this->currentImages[i]->item()->uniqueId != itemPtr->uniqueId)
                this->currentImages[i] = std::make_shared<ModifiedImage>(this->annotationRenderer, itemPtr, imageList.getData(itemPtr.get()));
                
            if (this->currentImages[i]->hasValidData())
            {
                this->currentImages[i]->data()->ensureUploadedToGPU ();                
            }
        }
        else
        {
            // Make sure that we clear it.
            zv_assert (i != firstValidSelectionIndex, "We expected data for this one!");
            this->currentImages[i] = {};
        }
    }
    
    Point firstImSizeInRectBefore = this->currentLayout.firstImSizeInRect (this->imageWidgetRect.current.size, gridPadding);
    bool layoutChanged = this->currentLayout.adjustForConfig(this->mutableState.layoutConfig);
        
    // The first image will decide for all the other sizes.
    const auto& firstIm = *this->currentImages[firstValidSelectionIndex]->data()->cpuData;

    if (!this->imageWidgetRect.normal.origin.isValid())
    {
        this->imageWidgetRect.normal.origin.x = this->monitorSize.x * (0.10 + 0.15*(this->viewer->globalIndex()));
        this->imageWidgetRect.normal.origin.y = this->monitorSize.y * 0.10;
    }

    // Handle the case there the cpuImage is empty (e.g. failed to load the file).
    int firstImWidth = firstIm.width() > 0 ? firstIm.width() : 256;
    int firstImHeight = firstIm.height() > 0 ? firstIm.height() : 256;
    this->imageWidgetRect.normal.size = this->currentLayout.widgetRectForImageSize(Point(firstImWidth, firstImHeight), gridPadding);

    // Maintain the size of the first image after changing the layout.
    if (layoutChanged)
    {
        this->imageWidgetRect.current.size = this->currentLayout.widgetRectForImageSize(firstImSizeInRectBefore, gridPadding);
        this->shouldUpdateWindowSize = true;
    }
    else
    {
        switch (this->lastGeometryMode)
        {
            case WindowGeometryMode::Normal:
            {
                that.addCommand (actionCommand(ImageWindowAction::Kind::Zoom_Normal));
                break;
            }

            case WindowGeometryMode::AspectRatio:
            {
                that.addCommand (actionCommand(ImageWindowAction::Kind::Zoom_RestoreAspectRatio));
                break;
            }

            case WindowGeometryMode::Maxspect:
            {
                that.addCommand (actionCommand(ImageWindowAction::Kind::Zoom_Maxspect));
                break;
            }

            case WindowGeometryMode::ScaleSpect:
            {
                // If the user adjusts the size, leave it as is. It's less disturbing.
                // that.addCommand (actionCommand(ImageWindowAction::Kind::Zoom_RestoreAspectRatio));
                break;
            }

            case WindowGeometryMode::UserDefined:
            {
                // do nothing, leave it with the same size.
                break;
            }
        }
    }

    // Keep the current geometry if it was already set before.
    if (!this->imageWidgetRect.current.origin.isValid())
    {
        this->imageWidgetRect.current = this->imageWidgetRect.normal;
        // Don't show it now, but tell it to show the window after
        // updating the content, otherwise we can get annoying flicker.
        this->updateAfterContentSwitch.inProgress = true;
        this->updateAfterContentSwitch.needToResize = true;
        this->updateAfterContentSwitch.numAlreadyRenderedFrames = 0;
        this->updateAfterContentSwitch.targetWindowGeometry.origin.x = this->imageWidgetRect.normal.origin.x - this->windowBorderSize;
        this->updateAfterContentSwitch.targetWindowGeometry.origin.y = this->imageWidgetRect.normal.origin.y - this->windowBorderSize;
        this->updateAfterContentSwitch.targetWindowGeometry.size.x = this->imageWidgetRect.normal.size.x + 2 * this->windowBorderSize;
        this->updateAfterContentSwitch.targetWindowGeometry.size.y = this->imageWidgetRect.normal.size.y + 2 * this->windowBorderSize;
        this->updateAfterContentSwitch.screenToImageScale = 1.0;
        this->viewer->onImageWindowGeometryUpdated(this->updateAfterContentSwitch.targetWindowGeometry);
    }

    this->mutableState.activeMode = ViewerMode::Original;
}

void ImageWindow::Impl::addModifier(const CreateModifierFunc& createModifier)
{
    for (const auto& modImPtr : this->currentImages)
    {
        if (!modImPtr)
            continue;

        modImPtr->addModifier (createModifier());
    }
}

void ImageWindow::Impl::applyCurrentTool()
{
    if (mutableState.activeToolState.kind == ActiveToolState::Kind::None)
        return;
    
    for (const auto& modImPtr : this->currentImages)
    {
        if (!modImPtr)
            continue;
        mutableState.activeToolState.activeTool()->addToImage (*modImPtr);        
    }

    that.setActiveTool (ActiveToolState::Kind::None);
}

ImageWindow::ImageWindow()
: impl (new Impl(*this))
{
}

ImageWindow::~ImageWindow()
{
    shutdown();
}

bool ImageWindow::isEnabled () const
{
    return impl->enabled;
}

void ImageWindow::setEnabled (bool enabled)
{
    if (impl->enabled == enabled)
        return;

    impl->enabled = enabled;

    if (enabled)
    {
        impl->imguiGlfwWindow.setEnabled(true);
    }
    else
    {
        impl->mutableState.activeMode = ViewerMode::None;
        impl->imguiGlfwWindow.setEnabled(false);
    }
}

void ImageWindow::shutdown()
{
    impl->imguiGlfwWindow.enableContexts();
    
    // Make sure that we release any GL stuff here with the context set.
    impl->currentImages.clear();
    impl->cursorOverlayInfo.clear ();
    impl->annotationRenderer.shutdown();

    impl->imguiGlfwWindow.shutdown ();
}

GLFWwindow* ImageWindow::glfwWindow ()
{
    return impl->imguiGlfwWindow.glfwWindow();
}

bool ImageWindow::initialize (GLFWwindow* parentWindow, Viewer* viewer)
{
    impl->viewer = viewer;

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    impl->monitorSize = ImVec2(mode->width, mode->height);
    zv_dbg ("Primary monitor size = %f x %f", impl->monitorSize.x, impl->monitorSize.y);

    // Create window with graphics context.
    // glfwWindowHint(GLFW_RESIZABLE, false); // fixed size.

    zv::Rect windowGeometry;
    windowGeometry.origin.x = 0;
    windowGeometry.origin.y = 0;
    windowGeometry.size.x = 640;
    windowGeometry.size.y = 480;
    
    if (!impl->imguiGlfwWindow.initialize (parentWindow, "Dalton Lens Image Viewer", windowGeometry, false /* viewports */))
        return false;

    impl->annotationRenderer.initializeFromCurrentContext();
    
    impl->imguiGlfwWindow.setWindowSizeChangedCallback([this](int width, int height, bool fromUser) {
        if (fromUser)
        {
            zv_dbg ("Window size was adjusted by the user.");
            impl->lastGeometryMode = Impl::WindowGeometryMode::UserDefined;
        }
    });

    glfwWindowHint(GLFW_RESIZABLE, true); // restore the default.

    checkGLError ();
    
    return true;
}

void ImageWindow::addCommand (Command&& command)
{
    impl->pendingCommands.push_back (std::move(command));
}

ImageWindowState& ImageWindow::mutableState ()
{
    return impl->mutableState;
}

void ImageWindow::checkImguiGlobalImageMouseEvents ()
{
    // Handle the mouse wheel
    auto& io = ImGui::GetIO ();
    if (io.MouseWheel != 0.f)
    {
#if PLATFORM_MACOS
        const float scaleFactor = 0.5f;
#else
        const float scaleFactor = 0.1f;
#endif
        // FIXME: implement a scaling based on that?
    }
}

void ImageWindow::checkImguiGlobalImageKeyEvents ()
{
    // These key events are valid also in the control window.
    auto& io = ImGui::GetIO();

    if (impl->globalCallback.callback)
    {
        impl->globalCallback.callback(impl->globalCallback.userData);
    }

    if (io.WantCaptureKeyboard)
        return;

    for (const auto code : {
            GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, 
            GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN,
            GLFW_KEY_O, GLFW_KEY_S, GLFW_KEY_W, 
            GLFW_KEY_N, GLFW_KEY_A, GLFW_KEY_V, GLFW_KEY_PERIOD, GLFW_KEY_COMMA, GLFW_KEY_M,
            GLFW_KEY_C, GLFW_KEY_Z,            
            GLFW_KEY_SPACE, GLFW_KEY_BACKSPACE, GLFW_KEY_DELETE,
            GLFW_KEY_ESCAPE, GLFW_KEY_ENTER,
        })
    {
        if (ImGui::IsKeyPressed(code))
            processKeyEvent(code);
    }

    // Those don't have direct GLFW keycodes for some reason.
    for (const auto c : {'<', '>'})
    {
        if (io.InputQueueCharacters.contains(c))
            processKeyEvent(c);
    }
}

inline bool CtrlOrCmd(ImGuiIO& io)
{
#if PLATFORM_MACOS
    return io.KeySuper;
#else
    return io.KeyCtrl;
#endif
}

void ImageWindow::processKeyEvent (int keycode)
{
    auto& io = ImGui::GetIO();

    switch (keycode)
    {
        case GLFW_KEY_ESCAPE: enqueueAction(ImageWindowAction::Kind::CancelCurrentTool); break;
        case GLFW_KEY_ENTER: enqueueAction(ImageWindowAction::Kind::ApplyCurrentTool); break;
        
        case GLFW_KEY_UP:
        case GLFW_KEY_BACKSPACE: enqueueAction(ImageWindowAction::Kind::View_PrevImage); break;

        case GLFW_KEY_DELETE: {
            if (io.KeyShift) 
                enqueueAction(ImageWindowAction::Kind::File_DeleteImageOnDisk);
            else 
                enqueueAction(ImageWindowAction::Kind::File_CloseImage);
            break;
        }

        case GLFW_KEY_PAGE_DOWN: enqueueAction(ImageWindowAction::Kind::View_NextPageOfImage); break;
        case GLFW_KEY_PAGE_UP: enqueueAction(ImageWindowAction::Kind::View_PrevPageOfImage); break;
        
        case GLFW_KEY_DOWN:
        case GLFW_KEY_SPACE: enqueueAction(ImageWindowAction::Kind::View_NextImage); break;

        case GLFW_KEY_Z: if (CtrlOrCmd(io)) enqueueAction(ImageWindowAction::Kind::Edit_Undo); break;

        case GLFW_KEY_C: {
            if (CtrlOrCmd(io))
            {
                enqueueAction(ImageWindowAction::Kind::Edit_CopyImageToClipboard);
            }
            else
            {
                enqueueAction(ImageWindowAction::Kind::Edit_CopyCursorInfoToClipboard);
            }            
            break;
        }

        case GLFW_KEY_O:
        {
            // No image saving for now.
            if (CtrlOrCmd(io))
            {
                enqueueAction (ImageWindowAction::Kind::File_OpenImage);
            }
            break;
        }

        case GLFW_KEY_S:
        {
            // No image saving for now.
            if (CtrlOrCmd(io))
            {
                if (io.KeyShift)
                    enqueueAction (ImageWindowAction::Kind::File_SaveImageAs);
                else
                    enqueueAction (ImageWindowAction::Kind::File_SaveImage);
            }
            break;
        }

        // View
        case GLFW_KEY_V: {
            if (CtrlOrCmd(io))
            {
                enqueueAction(ImageWindowAction::Kind::Edit_PasteImageFromClipboard);
            }
            else
            {
                enqueueAction (ImageWindowAction::Kind::View_ToggleOverlay);
            }      
            break;
        }
        
        // Zoom
        case GLFW_KEY_N: enqueueAction(ImageWindowAction::Kind::Zoom_Normal); break;
        case GLFW_KEY_M: enqueueAction(ImageWindowAction::Kind::Zoom_Maxspect); break;
        case GLFW_KEY_A: enqueueAction (ImageWindowAction::Kind::Zoom_RestoreAspectRatio); break;
        case GLFW_KEY_PERIOD: enqueueAction (ImageWindowAction::Kind::Zoom_Inc10p); break;
        case GLFW_KEY_COMMA: enqueueAction (ImageWindowAction::Kind::Zoom_Dec10p); break;
        case '<': enqueueAction (ImageWindowAction::Kind::Zoom_div2); break;
        case '>': enqueueAction (ImageWindowAction::Kind::Zoom_x2); break;

        // Layout
        case GLFW_KEY_1: addCommand(ImageWindow::layoutCommand(1,1)); break;
        case GLFW_KEY_2: addCommand(ImageWindow::layoutCommand(1,2)); break;
        case GLFW_KEY_3: addCommand(ImageWindow::layoutCommand(1,3)); break;
        case GLFW_KEY_4: addCommand(ImageWindow::layoutCommand(2,2)); break;

    }
}

zv::Rect ImageWindow::geometry () const
{
    return impl->imguiGlfwWindow.geometry();
}

const CursorOverlayInfo& ImageWindow::cursorOverlayInfo() const
{
    return impl->cursorOverlayInfo;
}

// void ImageWindow::showImage (const zv::ImageSRGBA& im, const std::string& imagePath, zv::Rect& updatedWindowGeometry)
// {
//     GLFWmonitor* monitor = glfwGetPrimaryMonitor();
//     const GLFWvidmode* mode = glfwGetVideoMode(monitor);
//     impl->monitorSize = ImVec2(mode->width, mode->height);

//     impl->im = im;
//     impl->imagePath = imagePath;

//     impl->imguiGlfwWindow.enableContexts ();
//     impl->gpuTexture.upload(impl->im);
//     if (impl->imageWidgetRect.normal.origin.x < 0) impl->imageWidgetRect.normal.origin.x = impl->monitorSize.x * 0.10;
//     if (impl->imageWidgetRect.normal.origin.y < 0) impl->imageWidgetRect.normal.origin.y = impl->monitorSize.y * 0.10;
//     impl->imageWidgetRect.normal.size.x = currentIm.width();
//     impl->imageWidgetRect.normal.size.y = currentIm.height();
//     impl->imageWidgetRect.current = impl->imageWidgetRect.normal;

//     impl->mutableState.activeMode = ViewerMode::Original;

//     // Don't show it now, but tell it to show the window after
//     // updating the content, otherwise we can get annoying flicker.
//     impl->updateAfterContentSwitch.inProgress = true;
//     impl->updateAfterContentSwitch.needToResize = true;
//     impl->updateAfterContentSwitch.numAlreadyRenderedFrames = 0;
//     impl->updateAfterContentSwitch.targetWindowGeometry.origin.x = impl->imageWidgetRect.normal.origin.x - impl->windowBorderSize;
//     impl->updateAfterContentSwitch.targetWindowGeometry.origin.y = impl->imageWidgetRect.normal.origin.y - impl->windowBorderSize;
//     impl->updateAfterContentSwitch.targetWindowGeometry.size.x = impl->imageWidgetRect.normal.size.x + 2 * impl->windowBorderSize;
//     impl->updateAfterContentSwitch.targetWindowGeometry.size.y = impl->imageWidgetRect.normal.size.y + 2 * impl->windowBorderSize;
//     impl->updateAfterContentSwitch.screenToImageScale = 1.0;

//     updatedWindowGeometry = impl->updateAfterContentSwitch.targetWindowGeometry;
// }

ImageWidgetRoi ImageWindow::Impl::renderImageItem(const ModifiedImagePtr &modImagePtr,
                                                  const ImVec2 &imageWidgetTopLeft,
                                                  const ImVec2 &imageWidgetSize,
                                                  ZoomInfo &zoom,
                                                  bool imageSmallerThanNormal,
                                                  CursorOverlayInfo *overlayInfo)
{
    auto& io = ImGui::GetIO();
    
    ImGui::SetCursorPos (imageWidgetTopLeft);

    ImVec2 uv0 (0,0);
    ImVec2 uv1 (1.f/zoom.zoomFactor,1.f/zoom.zoomFactor);
    ImVec2 uvRoiCenter = (uv0 + uv1) * 0.5f;
    uv0 += zoom.uvCenter - uvRoiCenter;
    uv1 += zoom.uvCenter - uvRoiCenter;
    
    // Make sure the ROI fits in the image.
    ImVec2 deltaToAdd (0,0);
    if (uv0.x < 0) deltaToAdd.x = -uv0.x;
    if (uv0.y < 0) deltaToAdd.y = -uv0.y;
    if (uv1.x > 1.f) deltaToAdd.x = 1.f-uv1.x;
    if (uv1.y > 1.f) deltaToAdd.y = 1.f-uv1.y;
    uv0 += deltaToAdd;
    uv1 += deltaToAdd;

    GLTexture* imageTexture = modImagePtr->data()->textureData.get();

    const bool hasZoom = zoom.zoomFactor != 1;
    const bool useLinearFiltering = imageSmallerThanNormal && !hasZoom;
    // Enable it just for that rendering otherwise the pointer overlay will get filtered too.
    if (useLinearFiltering)
    {
        ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList *parent_list, const ImDrawCmd *cmd)
                                                {
                                                    GLTexture* imageTexture = reinterpret_cast<GLTexture*>(cmd->UserCallbackData);
                                                    imageTexture->setLinearInterpolationEnabled(true);
                                                },
                                                imageTexture);
    }

    ImGui::Image(reinterpret_cast<ImTextureID>(imageTexture->textureId()),
                 imageWidgetSize,
                 uv0,
                 uv1);
    
    if (useLinearFiltering)
    {
        ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList *parent_list, const ImDrawCmd *cmd)
                                                {
                                                    GLTexture* imageTexture = reinterpret_cast<GLTexture*>(cmd->UserCallbackData);
                                                    imageTexture->setLinearInterpolationEnabled(false);
                                                },
                                                imageTexture);
    }

    const auto& currentIm = *modImagePtr->data()->cpuData;

    ImVec2 mousePosInImage (0,0);
    ImVec2 mousePosInTexture (0,0);
    {
        // This 0.5 offset is important since the mouse coordinate is an integer.
        // So when we are in the center of a pixel we'll return 0,0 instead of
        // 0.5,0.5.
        ImVec2 widgetPos = (io.MousePos + ImVec2(0.5f,0.5f)) - imageWidgetTopLeft;
        ImVec2 uv_window = widgetPos / imageWidgetSize;
        mousePosInTexture = (uv1-uv0)*uv_window + uv0;
        mousePosInImage = mousePosInTexture * ImVec2(currentIm.width(), currentIm.height());
    }
    
    bool showCursorOverlay = false;
    const bool pointerOverTheImage = ImGui::IsItemHovered() && currentIm.contains(mousePosInImage.x, mousePosInImage.y);

    if (pointerOverTheImage)
    {
        if (modImagePtr->item()->eventCallback)
        {
            modImagePtr->item()->eventCallback(modImagePtr->item()->uniqueId, mousePosInImage.x, mousePosInImage.y, modImagePtr->item()->eventCallbackData);
        }
    }

    if (pointerOverTheImage && overlayInfo)
    {
        overlayInfo->modImagePtr = modImagePtr;
        overlayInfo->showHelp = false;
        overlayInfo->imageWidgetSize = imageWidgetSize;
        overlayInfo->imageWidgetTopLeft = imageWidgetTopLeft;
        overlayInfo->uvTopLeft = uv0;
        overlayInfo->uvBottomRight = uv1;
        overlayInfo->roiWindowSize = ImVec2(15, 15);
        overlayInfo->mousePos = io.MousePos;
        overlayInfo->mousePosInTexture = mousePosInTexture;
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && io.KeyCtrl)
    {
        if ((currentIm.width() / float(zoom.zoomFactor)) > 16.f
             && (currentIm.height() / float(zoom.zoomFactor)) > 16.f)
        {
            zoom.zoomFactor *= 2;
            zoom.uvCenter = mousePosInTexture;
        }
    }
    
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && io.KeyCtrl)
    {
        if (zoom.zoomFactor >= 2)
            zoom.zoomFactor /= 2;
    }
    
    ImageWidgetRoi roi;
    roi.uv0 = uv0;
    roi.uv1 = uv1;
    return roi;
}

void ImageWindow::renderFrame ()
{    
    for (Command& command : impl->pendingCommands)
        command.execFunc (*this);
    impl->pendingCommands.clear ();

    ImageList& imageList = impl->viewer->imageList();

    bool contentChanged = (impl->mutableState.layoutConfig != impl->currentLayout.config);
    contentChanged |= impl->currentImages.empty();
    if (!contentChanged)
    {
        const auto& selectionRange = imageList.selectedRange();
        for (int idx = 0; idx < impl->currentImages.size(); ++idx)
        {
            if (idx >= selectionRange.indices.size())
                continue;
            
            const int imageListIdx = selectionRange.indices[idx];
            if (imageListIdx < 0)
            {
                // If we have data for this guy, we need to clear it.
                contentChanged = impl->currentImages[idx] && impl->currentImages[idx]->data();
                continue;
            }
            
            if (!impl->currentImages[idx] || !impl->currentImages[idx]->data())
            {
                // Was a new image added?
                if (imageListIdx < imageList.numImages())
                {
                    contentChanged = true;
                    break;
                }
                else
                {
                    continue;
                }
            }
            
            const ImageItemPtr& item = imageList.imageItemFromIndex (imageListIdx);
            if (impl->currentImages[idx]->item()->uniqueId != item->uniqueId)
            {
                contentChanged = true;
                break;
            }
        }
    }

    // Try to update any item data that might have changed (async network load
    // finished, file changed..).
    for (int idx = 0; idx < impl->currentImages.size(); ++idx)
    {
        if (impl->currentImages[idx] && impl->currentImages[idx]->update ())
        {
            impl->currentImages[idx]->data()->textureData.reset ();
            contentChanged = true;
        }
    }

    if (contentChanged)
    {
        impl->adjustForNewSelection ();
    }

    if (impl->updateAfterContentSwitch.needToResize)
    {
        impl->imguiGlfwWindow.enableContexts();

        impl->imguiGlfwWindow.setWindowSize (impl->updateAfterContentSwitch.targetWindowGeometry.size.x, 
                                             impl->updateAfterContentSwitch.targetWindowGeometry.size.y);        

        impl->updateAfterContentSwitch.needToResize = false;
    }

    const auto frameInfo = impl->imguiGlfwWindow.beginFrame ();
    const auto& controlsWindowState = impl->viewer->controlsWindow()->inputState();
    
    // If we do not have a pending resize request, then adjust the content size to the
    // actual window size. The framebuffer might be bigger depending on the retina scale
    // factor.
    if (!impl->shouldUpdateWindowSize)
    {
        impl->imageWidgetRect.current.size.x = frameInfo.windowContentWidth;
        impl->imageWidgetRect.current.size.y = frameInfo.windowContentHeight;
    }
  
    auto& io = ImGui::GetIO();
    const float monoFontSize = ImguiGLFWWindow::monoFontSize(io);
    
    impl->mutableState.inputState.shiftIsPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);

    if (!io.WantCaptureKeyboard)
    {
        if (ImGui::IsKeyPressed(GLFW_KEY_Q) || impl->imguiGlfwWindow.closeRequested())
        {
            impl->mutableState.activeMode = ViewerMode::None;
            impl->imguiGlfwWindow.cancelCloseRequest ();
        }

        checkImguiGlobalImageKeyEvents ();
    }
    checkImguiGlobalImageMouseEvents ();

    // Might get filled later on.
    impl->cursorOverlayInfo.clear ();
    
    impl->mutableState.modeForCurrentFrame = impl->mutableState.activeMode;

    if (impl->shouldUpdateWindowSize)
    {
        impl->onImageWidgetAreaChanged();
        impl->shouldUpdateWindowSize = false;
    }
    
    zv::Rect platformWindowGeometry = impl->imguiGlfwWindow.geometry();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(frameInfo.windowContentWidth, frameInfo.windowContentHeight), ImGuiCond_Always);

    ImGuiWindowFlags flags = (ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoScrollbar
                            // ImGuiWindowFlags_NoScrollWithMouse
                            // | ImGuiWindowFlags_NoCollapse
                            // | ImGuiWindowFlags_NoBackground
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_HorizontalScrollbar
                            // | ImGuiWindowFlags_NoDocking
                            | ImGuiWindowFlags_NoNav);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1,1,1,1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    bool isOpen = true;
    
    int firstValidImageIndex = -1;
    for (firstValidImageIndex = 0; firstValidImageIndex < impl->currentImages.size(); ++firstValidImageIndex)
        if (impl->currentImages[firstValidImageIndex])
            break;

    // Since we add a default image, this should never happen.
    zv_assert (firstValidImageIndex >= 0, "We should always have at least one valid image.");

    std::string mainWindowName = "zv - " + impl->currentImages[firstValidImageIndex]->item()->prettyName;
    if (impl->currentImages[firstValidImageIndex]->hasPendingChanges())
        mainWindowName += " [edited]";
    glfwSetWindowTitle(impl->imguiGlfwWindow.glfwWindow(), mainWindowName.c_str());

    if (ImGui::Begin((mainWindowName + "###Image").c_str(), &isOpen, flags))
    {
        if (!isOpen)
        {
            impl->mutableState.activeMode = ViewerMode::None;
        }
        
        const ImVec2 globalImageWidgetTopLeft = ImGui::GetCursorScreenPos();
        const auto globalImageWidgetSize = imSize(impl->imageWidgetRect.current);
        const auto globalImageWidgetContentSize = globalImageWidgetSize - ImVec2(impl->currentLayout.config.numCols-1, impl->currentLayout.config.numRows-1)*impl->gridPadding;
        const bool imageSmallerThanNormal = int(impl->imageWidgetRect.current.size.x) < int(impl->imageWidgetRect.normal.size.x);
        
        std::vector<Rect> widgetGeometries (impl->currentImages.size());

        for (int r = 0; r < impl->currentLayout.config.numRows; ++r)
        for (int c = 0; c < impl->currentLayout.config.numCols; ++c)
        {
            const int idx = r*impl->currentLayout.config.numCols + c;
            if (idx < impl->currentImages.size() && impl->currentImages[idx] && impl->currentImages[idx]->hasValidData())
            {
                const auto& rect = impl->currentLayout.imageRects[idx];
                const Point imageWidgetSize = Point(globalImageWidgetContentSize.x * rect.size.x,
                                                      globalImageWidgetContentSize.y * rect.size.y);
                const Point imageWidgetTopLeft = Point(globalImageWidgetContentSize.x * rect.origin.x + c*impl->gridPadding,
                                                         globalImageWidgetContentSize.y * rect.origin.y + r*impl->gridPadding);
                widgetGeometries[idx].origin = imageWidgetTopLeft;
                widgetGeometries[idx].size = imageWidgetSize;
            }
            
            // Option: show it next to the mouse.
            // if (impl->mutableState.inputState.shiftIsPressed || controlsWindowState.shiftIsPressed)
            // {
            //     impl->inlineCursorOverlay.showTooltip(impl->cursorOverlayInfo);
            // }
        }

        for (int idx = 0; idx < impl->currentImages.size(); ++idx)
        {
            if (!impl->currentImages[idx])
                continue;
            
            if (!impl->currentImages[idx]->data()->cpuData->hasData())
            {
                ImGui::SetCursorScreenPos (imVec2(widgetGeometries[idx].topLeft()));
                switch (impl->currentImages[idx]->data()->status)
                {
                    case ImageItemData::Status::FailedToLoad: {
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "ERROR: could not load the image %s.\nPath: %s",
                                   impl->currentImages[idx]->item()->prettyName.c_str(),
                                   impl->currentImages[idx]->item()->sourceImagePath.c_str());
                        break;
                    }

                    case ImageItemData::Status::StillLoading: {
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Loading the image...");
                        break;
                    }
                    
                    default:
                        break;
                }
                
            }
            else
            {
                ImageWidgetRoi uvRoi = impl->renderImageItem(impl->currentImages[idx],
                                                             imPos(widgetGeometries[idx]),
                                                             imSize(widgetGeometries[idx]),
                                                             impl->zoom,
                                                             imageSmallerThanNormal,
                                                             &impl->cursorOverlayInfo);

                WidgetToImageTransform transform(uvRoi, widgetGeometries[idx]);
                
                if (impl->mutableState.activeToolState.kind != ActiveToolState::Kind::None)
                {
                    InteractiveToolRenderingContext context;
                    context.widgetToImageTransform = transform;
                    const auto &im = *impl->currentImages[idx]->data()->cpuData;
                    context.imageWidth = im.width();
                    context.imageHeight = im.height();
                    context.firstValidImageIndex = (idx == firstValidImageIndex);
                    impl->mutableState.activeToolState.activeTool()->renderAsActiveTool (context);
                }
            }
        }

        if (impl->cursorOverlayInfo.valid())
        {            
            for (int idx = 0; idx < impl->currentImages.size(); ++idx)
            {
                if (!impl->currentImages[idx] || !impl->currentImages[idx]->hasValidData())
                    continue;

                if (impl->cursorOverlayInfo.modImagePtr->item()->uniqueId == impl->currentImages[idx]->item()->uniqueId)
                    continue;
                
                ImVec2 deltaFromTopLeft = impl->cursorOverlayInfo.mousePos - impl->cursorOverlayInfo.imageWidgetTopLeft;
                // FIXME: replace this with an image of a cross-hair texture. Filled black with a white outline.
                ImGui::GetForegroundDrawList()->AddCircle(imPos(widgetGeometries[idx]) + deltaFromTopLeft, 4.0, IM_COL32(255,255,255,180), 0, 2.0f);
                ImGui::GetForegroundDrawList()->AddCircle(imPos(widgetGeometries[idx]) + deltaFromTopLeft, 5.0, IM_COL32(0,0,0,180), 0, 1.f);
                ImGui::GetForegroundDrawList()->AddCircle(imPos(widgetGeometries[idx]) + deltaFromTopLeft, 3.0, IM_COL32(0,0,0,180), 0, 1.f);
            }

            // const bool showStatusBar = (impl->mutableState.inputState.shiftIsPressed || controlsWindowState.shiftIsPressed);
            const bool showStatusBar = impl->mutableState.infoOverlayEnabled;
            // ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyCtrl;
            if (showStatusBar)
            {
                impl->imguiGlfwWindow.PushMonoSpaceFont(io);

                float mouseYinWidget = (impl->cursorOverlayInfo.mousePos.y - impl->cursorOverlayInfo.imageWidgetTopLeft.y);
                const bool showOnBottom = (impl->cursorOverlayInfo.imageWidgetSize.y - mouseYinWidget) > monoFontSize*2.2;

                for (int idx = 0; idx < impl->currentImages.size(); ++idx)
                {
                    if (!impl->currentImages[idx] || !impl->currentImages[idx]->hasValidData())
                        continue;
                    
                    const auto& im = *impl->currentImages[idx]->data()->cpuData;
                    const ImVec2 imSize (im.width(), im.height());
                    ImVec2 mousePosInImage = impl->cursorOverlayInfo.mousePosInTexture * imSize;
                    const int cInImage = int(mousePosInImage.x);
                    const int rInImage = int(mousePosInImage.y);

                    PixelSRGBA sRgba = im(cInImage, rInImage);
                    const auto hsv = zv::convertToHSV(sRgba);
                    std::string caption = formatted("%s\n%4d, %4d (sRGB %3d %3d %3d) (HSV %3d %3d %3d)",
                                                    impl->currentImages[idx]->item()->prettyName.c_str(),
                                                    cInImage, rInImage,
                                                    sRgba.r, sRgba.g, sRgba.b,
                                                    intRnd(hsv.x*360.f), intRnd(hsv.y*100.f), intRnd(hsv.z*100.f/255.f));

                    ImVec2 textStart, textAreaStart, textAreaEnd;

                    if (showOnBottom)
                    {
                        textStart = imPos(widgetGeometries[idx]);
                        textStart.x += monoFontSize*0.5;
                        
                        textAreaStart = imVec2(widgetGeometries[idx].topLeft());
                        textAreaEnd = imVec2(widgetGeometries[idx].bottomRight());

                        textStart.y += widgetGeometries[idx].size.y - monoFontSize*2.1;
                        textAreaStart.y = textStart.y - monoFontSize*0.1;
                    }
                    else
                    {
                        textStart = imPos(widgetGeometries[idx]);
                        textStart.x += monoFontSize*0.5;
                        textStart.y += monoFontSize*0.15;
                        
                        textAreaStart = imPos(widgetGeometries[idx]);
                        textAreaEnd = textAreaStart + ImVec2(widgetGeometries[idx].size.x, monoFontSize*2.2);
                    }
                    
                    auto* drawList = ImGui::GetWindowDrawList();
                    ImVec4 clip_rect(textAreaStart.x, textAreaStart.y, textAreaEnd.x, textAreaEnd.y);
                    drawList->AddRectFilled(textAreaStart, textAreaEnd, IM_COL32(0,0,0,127));
                    // DrawList::AddText to be able to clip it, not sure if there is a simpler way,
                    // but PushItemWidth / SetNextItemWidth do not seem to apply to Text.
                    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textStart, IM_COL32_WHITE, caption.c_str(), NULL, 0.0f, &clip_rect);
                }
                ImGui::PopFont();
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.KeyCtrl)
        {
            // xv-like controls focus.
            if (impl->viewer) impl->viewer->onToggleControls();
        }
    }
        
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    impl->imguiGlfwWindow.endFrame ();
    
    if (impl->updateAfterContentSwitch.inProgress)
    {
        ++impl->updateAfterContentSwitch.numAlreadyRenderedFrames;
        
        if (impl->updateAfterContentSwitch.numAlreadyRenderedFrames >= 2)
        {
            setEnabled(true);
            // Make sure that even if the viewer was already enabled, then we'll focus it.
            glfwFocusWindow(impl->imguiGlfwWindow.glfwWindow());
            impl->imguiGlfwWindow.setWindowPos(impl->updateAfterContentSwitch.targetWindowGeometry.origin.x,
                                               impl->updateAfterContentSwitch.targetWindowGeometry.origin.y);
            impl->updateAfterContentSwitch.setCompleted(); // not really needed, just to be explicit.
        }
    }
    
    // User pressed q, escape or closed the window. We need to do an empty rendering to
    // make sure the platform windows will get hidden and won't stay as ghosts and create
    // flicker when we enable this again.
    if (impl->mutableState.activeMode == ViewerMode::None)
    {
        if (impl->viewer) impl->viewer->onDismissRequested ();
    }

    // Sync persistent settings that might have changed. This is no-op is none changed.
    // DaltonLensPrefs::setDaltonizeDeficiencyKind((int)impl->mutableState.daltonizeParams.kind);
}

void copyToClipboard (const ImageSRGBA& im)
{
    clip::image_spec spec;
    spec.width = im.width();
    spec.height = im.height();
    spec.bits_per_pixel = 32;
    spec.bytes_per_row = im.bytesPerRow();
    spec.red_mask = 0xff;
    spec.green_mask = 0xff00;
    spec.blue_mask = 0xff0000;
    spec.alpha_mask = 0xff000000;
    spec.red_shift = 8*0;
    spec.green_shift = 8*1;
    spec.blue_shift = 8*2;
    spec.alpha_shift = 8*3;
    clip::set_image (clip::image(im.rawBytes(), spec));
}

void ImageWindow::runAction (const ImageWindowAction& action)
{
    switch (action.kind)
    {
        case ImageWindowAction::Kind::Zoom_Normal: {
            impl->lastGeometryMode = Impl::WindowGeometryMode::Normal;
            impl->imageWidgetRect.current = impl->imageWidgetRect.normal;
            impl->shouldUpdateWindowSize = true;
            break;
        }

        case ImageWindowAction::Kind::Zoom_RestoreAspectRatio: {            
            impl->adjustAspectRatio (); 
            impl->lastGeometryMode = Impl::WindowGeometryMode::AspectRatio;
            break;
        }

        case ImageWindowAction::Kind::Zoom_x2: {
            impl->imageWidgetRect.current.size.x *= 2.f;
            impl->imageWidgetRect.current.size.y *= 2.f;
            impl->shouldUpdateWindowSize = true;
            if (impl->lastGeometryMode != Impl::WindowGeometryMode::UserDefined)
                impl->lastGeometryMode = Impl::WindowGeometryMode::ScaleSpect;
            break;
        }

        case ImageWindowAction::Kind::Zoom_div2: {
            if (impl->imageWidgetRect.current.size.x > 64 && impl->imageWidgetRect.current.size.y > 64)
            {
                impl->imageWidgetRect.current.size.x *= 0.5f;
                impl->imageWidgetRect.current.size.y *= 0.5f;
                impl->shouldUpdateWindowSize = true;
                if (impl->lastGeometryMode != Impl::WindowGeometryMode::UserDefined)
                    impl->lastGeometryMode = Impl::WindowGeometryMode::ScaleSpect;
            }
            break;
        }

        case ImageWindowAction::Kind::Zoom_Inc10p: {
            impl->imageWidgetRect.current.size.x *= 1.1f;
            impl->imageWidgetRect.current.size.y *= 1.1f;
            impl->shouldUpdateWindowSize = true;
            if (impl->lastGeometryMode != Impl::WindowGeometryMode::UserDefined)
                impl->lastGeometryMode = Impl::WindowGeometryMode::ScaleSpect;
            break;
        }

        case ImageWindowAction::Kind::Zoom_Dec10p: {
            if (impl->imageWidgetRect.current.size.x > 64 && impl->imageWidgetRect.current.size.y > 64)
            {
                impl->imageWidgetRect.current.size.x *= 0.9f;
                impl->imageWidgetRect.current.size.y *= 0.9f;
                impl->shouldUpdateWindowSize = true;
                if (impl->lastGeometryMode != Impl::WindowGeometryMode::UserDefined)
                    impl->lastGeometryMode = Impl::WindowGeometryMode::ScaleSpect;
            }
            break;
        }

        case ImageWindowAction::Kind::Zoom_Maxspect: {
            impl->imageWidgetRect.current.size.x = impl->monitorSize.x;
            impl->imageWidgetRect.current.size.y = impl->monitorSize.y;
            impl->adjustAspectRatio ();
            impl->lastGeometryMode = Impl::WindowGeometryMode::Maxspect;
            break;
        }

        case ImageWindowAction::Kind::File_OpenImage: {
            impl->viewer->onOpenImage();
            break;
        }

        case ImageWindowAction::Kind::File_SaveImage: {
            impl->viewer->onSavePendingChangesConfirmed(Confirmation::Ok, false /* don't force path selection */);
            break;
        }

        case ImageWindowAction::Kind::File_SaveImageAs: {
            impl->viewer->onSavePendingChangesConfirmed(Confirmation::Ok, true /* force path selection */);
            break;
        }
        
        case ImageWindowAction::Kind::File_DeleteImageOnDisk: {
            impl->removeCurrentImageOnDisk ();
            break;
        }

        case ImageWindowAction::Kind::File_DeleteImageOnDisk_Confirmed: {
            auto& imageList = impl->viewer->imageList();
            int idx = imageList.firstSelectedAndEnabledIndex();
            if (idx < 0)
            {
                zv_dbg ("No selected image.");
                break;
            }

            const ImageItemPtr& itemPtr = imageList.imageItemFromIndex (idx);
            if (itemPtr->sourceImagePath.empty())
            {
                zv_dbg ("No image path.");
                break;
            }

            if (!fs::remove(itemPtr->sourceImagePath))
            {
                zv_dbg ("Failed to remove %s", itemPtr->sourceImagePath.c_str());
                break;
            }

            imageList.removeImage (idx);
            if (imageList.numImages() == 0)
            {
                imageList.addImage(defaultImageItem(), 0, false);
            }
            break;
        }

        case ImageWindowAction::Kind::File_CloseImage: {
            auto& imageList = impl->viewer->imageList();
            int idx = imageList.firstSelectedAndEnabledIndex();
            if (idx >= 0)
            {
                imageList.removeImage (idx);
                if (imageList.numImages() == 0)
                {
                    imageList.addImage(defaultImageItem(), 0, false);
                }
            }
            break;
        }

        case ImageWindowAction::Kind::View_ToggleOverlay: {
            impl->mutableState.infoOverlayEnabled = !impl->mutableState.infoOverlayEnabled;
            break;
        }

        case ImageWindowAction::Kind::View_NextPageOfImage:
        case ImageWindowAction::Kind::View_PrevPageOfImage: {
            auto& imageList = impl->viewer->imageList();
            const auto& range = impl->viewer->imageList().selectedRange();
            const int n = imageList.numEnabledImages();
            const int count = range.indices.size();
            const int step = 2 + ((n * 0.1f) / count); // advance by 10% each time, at least 2
            const int finalStep = range.indices.size() * step;
            bool forward = (action.kind == ImageWindowAction::Kind::View_NextPageOfImage);
            impl->viewer->imageList().advanceCurrentSelection (forward ? finalStep : -finalStep);
            break;
        }

        case ImageWindowAction::Kind::View_NextImage: {
            impl->runAfterCheckingPendingChanges ([this]() {
                const auto& range = impl->viewer->imageList().selectedRange();
                impl->viewer->imageList().advanceCurrentSelection (range.indices.size());
            });
            break;
        }

        case ImageWindowAction::Kind::View_PrevImage: {
            impl->runAfterCheckingPendingChanges ([this]() {
                const auto& range = impl->viewer->imageList().selectedRange();
                impl->viewer->imageList().advanceCurrentSelection (-range.indices.size());
            });
            break;
        }

        case ImageWindowAction::Kind::View_SelectImage: {
            impl->runAfterCheckingPendingChanges ([this, action]() {
                impl->viewer->imageList().setSelectionStart (action.paramsPtr->intParams[0]);
            });
            break;
        }

        case ImageWindowAction::Kind::Edit_Undo: {
            for (auto& it : impl->currentImages)
            {
                if (it.get() && it->hasValidData())
                    it->undoLastChange ();
            }
            impl->cursorOverlayInfo.clear();
            break;
        }

        case ImageWindowAction::Kind::Edit_RevertToOriginal: {
            discardAllChanges ();
            impl->cursorOverlayInfo.clear();
            break;
        }

        case ImageWindowAction::Kind::Edit_PasteImageFromClipboard: {
            impl->viewer->addPastedImage ();
            break;
        }

        case ImageWindowAction::Kind::Edit_CopyImageToClipboard: {
            for (int i = 0; i < impl->currentImages.size(); ++i)
            {
                if (impl->currentImages[i] && impl->currentImages[i]->hasValidData())
                {                    
                    copyToClipboard (*impl->currentImages[i]->data()->cpuData);
                    break;
                }
            }
            break;
        }

        case ImageWindowAction::Kind::Edit_CopyCursorInfoToClipboard: { 
            if (!impl->cursorOverlayInfo.valid())
                break;
            
            const auto& image = *impl->cursorOverlayInfo.modImagePtr->data()->cpuData;
            ImVec2 mousePosInImage = impl->cursorOverlayInfo.mousePosInImage();

            if (!image.contains(mousePosInImage.x, mousePosInImage.y))
                break;

            const auto sRgb = image((int)mousePosInImage.x, (int)mousePosInImage.y);

            std::string clipboardText;
            clipboardText += formatted("[%d, %d]\n", (int)mousePosInImage.x, (int)mousePosInImage.y);
            clipboardText += formatted("sRGB %d %d %d\n", sRgb.r, sRgb.g, sRgb.b);

            const PixelLinearRGB lrgb = zv::convertToLinearRGB(sRgb);
            clipboardText += formatted("linearRGB %.1f %.1f %.1f\n", lrgb.r, lrgb.g, lrgb.b);

            const auto hsv = zv::convertToHSV(sRgb);
            clipboardText += formatted("HSV %.1fº %.1f%% %.1f%%\n", hsv.x * 360.f, hsv.y * 100.f, hsv.z * 100.f / 255.f);

            PixelLab lab = zv::convertToLab(sRgb);
            clipboardText += formatted("L*a*b %.1f %.1f %.1f\n", lab.l, lab.a, lab.b);

            PixelXYZ xyz = convertToXYZ(sRgb);
            clipboardText += formatted("XYZ %.1f %.1f %.1f\n", xyz.x, xyz.y, xyz.z);

            // glfwSetClipboardString(nullptr, clipboardText.c_str());
            clip::set_text(clipboardText.c_str());
            impl->cursorOverlayInfo.timeOfLastCopyToClipboard = currentDateInSeconds();
            break;
        }

        case ImageWindowAction::Kind::Modify_Rotate90:
        case ImageWindowAction::Kind::Modify_Rotate180:
        case ImageWindowAction::Kind::Modify_Rotate270: {
            RotateImageModifier::Angle angle;
            switch (action.kind)
            {
                case ImageWindowAction::Kind::Modify_Rotate90: angle = RotateImageModifier::Angle::Angle_90; break;
                case ImageWindowAction::Kind::Modify_Rotate180: angle = RotateImageModifier::Angle::Angle_180; break;
                case ImageWindowAction::Kind::Modify_Rotate270: angle = RotateImageModifier::Angle::Angle_270; break;
                default: zv_assert(false, "invalid action"); break;
            }

            impl->addModifier ([angle]() { return std::make_unique<RotateImageModifier>(angle); });
            break;
        }
            
        case ImageWindowAction::Kind::ApplyCurrentTool: {
            impl->applyCurrentTool ();
            break;
        }

        case ImageWindowAction::Kind::CancelCurrentTool: {
            setActiveTool (ActiveToolState::Kind::None);
            break;
        }
    }
}

void ImageWindow::enqueueAction (const ImageWindowAction& action)
{
    addCommand(actionCommand(action));
}

ImageWindow::Command ImageWindow::actionCommand (const ImageWindowAction& action)
{
    return Command([action](ImageWindow& window) {
        window.runAction (action);
    });
}

ImageWindow::Command ImageWindow::layoutCommand(int numRows, int numCols)
{
    return Command([numRows,numCols](ImageWindow &window) {
        window.impl->runAfterCheckingPendingChanges([numRows,numCols,&window]() {
            LayoutConfig config;
            config.numCols = numCols;
            config.numRows = numRows;
            window.impl->mutableState.layoutConfig = config; 
            window.impl->viewer->imageList().setSelectionCount(config.numImages());
        });
    });
}

void ImageWindow::discardAllChanges ()
{
    for (auto& it : impl->currentImages)
    {
        if (it)
            it->discardChanges();
    }
}

ModifiedImagePtr ImageWindow::getFirstValidImage(bool modifiedOnly)
{
    for (auto& imPtr : impl->currentImages)
    {
        if (imPtr && (!modifiedOnly || imPtr->hasPendingChanges()))
            return imPtr;
    }

    return nullptr;
}

void ImageWindow::applyOverValidImages(bool modifiedOnly, const std::function<void(const ModifiedImagePtr&)>& onImage)
{
    for (auto& imPtr : impl->currentImages)
    {
        if (imPtr && (!modifiedOnly || imPtr->hasPendingChanges()))
            onImage(imPtr);
    }
}

bool ImageWindow::canUndo() const
{
    for (auto& it : impl->currentImages)
    {
        if (it && it->canUndo())
            return true;
    }
    return false;
}

void ImageWindow::setActiveTool (ActiveToolState::Kind kind)
{
    if (kind == impl->mutableState.activeToolState.kind)
        return;

    impl->mutableState.activeToolState.kind = kind;
}

void ImageWindow::setGlobalEventCallback (const GlobalEventCallbackType& callback, void* userData)
{
    impl->globalCallback.callback = callback;
    impl->globalCallback.userData = userData;
}

void ImageWindow::Impl::removeCurrentImageOnDisk ()
{
    auto& imageList = viewer->imageList();
    int idx = imageList.firstSelectedAndEnabledIndex();
    if (idx < 0)
    {
        return;
    }

    const ImageItemPtr& itemPtr = imageList.imageItemFromIndex (idx);
    if (itemPtr->sourceImagePath.empty())
    {
        return;
    }

    const std::string& imagePath = itemPtr->sourceImagePath;

    ActionToConfirm actionToConfirm;
    actionToConfirm.title = "Delete Image on Disk?";            
    actionToConfirm.renderDialog = [imagePath](Confirmation& confirmation) -> bool {
        ImGui::TextWrapped("%s will be deleted.\nThis operation cannot be undone!\n\n", imagePath.c_str());
        ImGui::Separator();

        bool gotAnswer = false;

        if (ImGui::Button("OK", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
        { 
            confirmation = Confirmation::Ok;
            gotAnswer = true;
        }

        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) 
        { 
            confirmation = Confirmation::Cancel;
            gotAnswer = true;
        }

        return gotAnswer;
    };
    actionToConfirm.onOk = [this]() {
        that.enqueueAction (ImageWindowAction::Kind::File_DeleteImageOnDisk_Confirmed);
    };
    
    viewer->controlsWindow()->setCurrentActionToConfirm (actionToConfirm);
    viewer->onControlsRequestedForConfirmation ();
}

} // zv
