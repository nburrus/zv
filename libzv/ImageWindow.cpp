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

#include <cstdio>

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

struct ImageWindow::Impl
{
    ImguiGLFWWindow imguiGlfwWindow;
    Viewer* viewer = nullptr;

    const ImageEntry* lastImageEntry = nullptr;
    
    ImageWindowState mutableState;

    bool enabled = false;

    ImageCursorOverlay inlineCursorOverlay;
    CursorOverlayInfo cursorOverlayInfo;

    struct {
        bool requested = false;
        std::string outPath;
    } saveToFile;

    struct {
        bool inProgress = false;
        bool needToResize = false;
        int numAlreadyRenderedFrames = 0;
        // This can be higher than 1 on retina displays.
        float screenToImageScale = 1.f;
        zv::Rect targetWindowGeometry;

        void setCompleted () { *this = {}; }
    } updateAfterContentSwitch;
    
    GLTexture gpuTexture;
    std::shared_ptr<zv::ImageSRGBA> im;
    std::string imagePath;
    
    ImVec2 monitorSize = ImVec2(-1,-1);
    
    const int windowBorderSize = 0;
    bool shouldUpdateWindowSize = false;
    
    struct {
        zv::Rect normal;
        zv::Rect current;
    } imageWidgetRect;
    
    struct {
        int zoomFactor = 1;
        
        // UV means normalized between 0 and 1.
        ImVec2 uvCenter = ImVec2(0.5f,0.5f);
    } zoom;
    
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

    void adjustForNewImage (ImageEntryData& imData);

    void adjustAspectRatio ()
    {
        float ratioX = this->imageWidgetRect.current.size.x / this->imageWidgetRect.normal.size.x;
        float ratioY = this->imageWidgetRect.current.size.y / this->imageWidgetRect.normal.size.y;
        if (ratioX < ratioY)
        {
            this->imageWidgetRect.current.size.y = ratioX * this->imageWidgetRect.normal.size.y;
        }
        else
        {
            this->imageWidgetRect.current.size.x = ratioY * this->imageWidgetRect.normal.size.x;
        }
        this->shouldUpdateWindowSize = true;
    }
};

void ImageWindow::Impl::adjustForNewImage (ImageEntryData& imData)
{
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    this->monitorSize = ImVec2(mode->width, mode->height);

    this->im = imData.cpuData;
    this->imagePath = imData.entry->sourceImagePath;

    this->imguiGlfwWindow.enableContexts ();
    // FIXME: not using the cache yet! GPU data releasing can be tricky.
    this->gpuTexture.upload(*(this->im));
    if (!this->imageWidgetRect.normal.origin.isValid())
    {
        this->imageWidgetRect.normal.origin.x = this->monitorSize.x * 0.10;
        this->imageWidgetRect.normal.origin.y = this->monitorSize.y * 0.10;
    }
    this->imageWidgetRect.normal.size.x = this->im->width();
    this->imageWidgetRect.normal.size.y = this->im->height();
    
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
        this->viewer->onImageWindowGeometryUpdated (this->updateAfterContentSwitch.targetWindowGeometry);
    }

    this->mutableState.activeMode = ViewerMode::Original;    
}

// void Viewer::addImageData (const ImageSRGBA& image, const std::string& imageName)
// {
//     zv::Rect updatedViewerWindowGeometry;
//     impl->imageWindow.showImage (image, imageName, updatedViewerWindowGeometry);
//     impl->controlsWindow.repositionAfterNextRendering (updatedViewerWindowGeometry, true /* show by default */);
// }

ImageWindow::ImageWindow()
: impl (new Impl())
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
    impl->imguiGlfwWindow.shutdown ();
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

    glfwWindowHint(GLFW_RESIZABLE, true); // restore the default.

    impl->gpuTexture.initialize();

    checkGLError ();
    
    return true;
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

    for (const auto code : {GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_S, GLFW_KEY_W, GLFW_KEY_N, GLFW_KEY_A, GLFW_KEY_SPACE })
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

void ImageWindow::processKeyEvent (int keycode)
{
    auto& io = ImGui::GetIO();

    switch (keycode)
    {
        case GLFW_KEY_UP:
        {
            impl->viewer->imageList().selectImage (impl->viewer->imageList().selectedIndex() - 1);
            break;
        }

        case GLFW_KEY_DOWN:
        {
            impl->viewer->imageList().selectImage (impl->viewer->imageList().selectedIndex() + 1);
            break;
        }

        case GLFW_KEY_S:
        {
            if (io.KeyCtrl)
            {
                saveCurrentImage();
            }
            break;
        }

        case GLFW_KEY_N: 
        {
            impl->imageWidgetRect.current = impl->imageWidgetRect.normal;
            impl->shouldUpdateWindowSize = true;
            break;
        }

        case GLFW_KEY_A:
        {
            impl->adjustAspectRatio ();
            break;
        }

        case '<':
        {
            if (impl->imageWidgetRect.current.size.x > 64 && impl->imageWidgetRect.current.size.y > 64)
            {
                impl->imageWidgetRect.current.size.x *= 0.5f;
                impl->imageWidgetRect.current.size.y *= 0.5f;
                impl->shouldUpdateWindowSize = true;
            }
            break;
        }

        case '>':
        {
            impl->imageWidgetRect.current.size.x *= 2.f;
            impl->imageWidgetRect.current.size.y *= 2.f;
            impl->shouldUpdateWindowSize = true;
            break;
        }
    }
}

void ImageWindow::saveCurrentImage ()
{
#if !PLATFORM_LINUX
    nfdchar_t *outPath = NULL;
    std::string default_name = formatted("daltonlens_%s.png", viewerModeFileName(impl->mutableState.activeMode).c_str());
    nfdfilteritem_t filterItems[] = { { "Images", "png" } };
    nfdresult_t result = NFD_SaveDialogU8 (&outPath, filterItems, 1, nullptr, default_name.c_str());

    if ( result == NFD_OKAY )
    {
        zv_dbg ("Saving to %s", outPath);
        impl->saveToFile.requested = true;
        impl->saveToFile.outPath = outPath;
        NFD_FreePathU8(outPath);
    }
    else if ( result == NFD_CANCEL ) 
    {
        zv_dbg ("Save image cancelled");
    }
    else 
    {
        fprintf(stderr, "Error: %s\n", NFD_GetError() );
    }
#else
    // FIXME: implement with the ImGui file dialog.
#endif
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
//     impl->imageWidgetRect.normal.size.x = impl->im->width();
//     impl->imageWidgetRect.normal.size.y = impl->im->height();
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

void ImageWindow::renderFrame ()
{
    ImageList& imageList = impl->viewer->imageList();
    
    const ImageEntry* imageEntry = imageList.imageEntryFromIndex (imageList.selectedIndex());
    std::shared_ptr<ImageEntryData> imdata = imageList.getData(imageEntry);
    
    if (impl->lastImageEntry != imageEntry)
    {
        impl->adjustForNewImage (*imdata);
        impl->lastImageEntry = imageEntry;
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

    // Might get filled later on.
    impl->cursorOverlayInfo = {};
    
    // If we do not have a pending resize request, then adjust the content size to the
    // actual window size. The framebuffer might be bigger depending on the retina scale
    // factor.
    if (!impl->shouldUpdateWindowSize)
    {
        impl->imageWidgetRect.current.size.x = frameInfo.windowContentWidth;
        impl->imageWidgetRect.current.size.y = frameInfo.windowContentHeight;
    }
  
    auto& io = ImGui::GetIO();    
    
    impl->mutableState.inputState.shiftIsPressed = ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift);

    if (!io.WantCaptureKeyboard)
    {
        if (ImGui::IsKeyPressed(GLFW_KEY_Q) || ImGui::IsKeyPressed(GLFW_KEY_ESCAPE) || impl->imguiGlfwWindow.closeRequested())
        {
            impl->mutableState.activeMode = ViewerMode::None;
            impl->imguiGlfwWindow.cancelCloseRequest ();
        }

        checkImguiGlobalImageKeyEvents ();
    }
    checkImguiGlobalImageMouseEvents ();
    
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
                            | ImGuiWindowFlags_NoBackground
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_HorizontalScrollbar
                            // | ImGuiWindowFlags_NoDocking
                            | ImGuiWindowFlags_NoNav);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    bool isOpen = true;
    
    std::string mainWindowName = impl->imagePath;
    glfwSetWindowTitle(impl->imguiGlfwWindow.glfwWindow(), mainWindowName.c_str());

    if (ImGui::Begin((mainWindowName + "###Image").c_str(), &isOpen, flags))
    {
        if (!isOpen)
        {
            impl->mutableState.activeMode = ViewerMode::None;
        }
                      
        const ImVec2 imageWidgetTopLeft = ImGui::GetCursorScreenPos();
        
        ImVec2 uv0 (0,0);
        ImVec2 uv1 (1.f/impl->zoom.zoomFactor,1.f/impl->zoom.zoomFactor);
        ImVec2 uvRoiCenter = (uv0 + uv1) * 0.5f;
        uv0 += impl->zoom.uvCenter - uvRoiCenter;
        uv1 += impl->zoom.uvCenter - uvRoiCenter;
        
        // Make sure the ROI fits in the image.
        ImVec2 deltaToAdd (0,0);
        if (uv0.x < 0) deltaToAdd.x = -uv0.x;
        if (uv0.y < 0) deltaToAdd.y = -uv0.y;
        if (uv1.x > 1.f) deltaToAdd.x = 1.f-uv1.x;
        if (uv1.y > 1.f) deltaToAdd.y = 1.f-uv1.y;
        uv0 += deltaToAdd;
        uv1 += deltaToAdd;
    
        GLTexture* imageTexture = &impl->gpuTexture;

        if (impl->saveToFile.requested)
        {
            impl->saveToFile.requested = false;
            ImageSRGBA im;
            imageTexture->download (im);
            writePngImage (impl->saveToFile.outPath, im);
        }
        
        const bool imageHasNonMultipleSize = int(impl->imageWidgetRect.current.size.x) % int(impl->imageWidgetRect.normal.size.x) != 0;
        const bool hasZoom = impl->zoom.zoomFactor != 1;
        const bool useLinearFiltering = imageHasNonMultipleSize && !hasZoom;
        // Enable it just for that rendering otherwise the pointer overlay will get filtered too.
        if (useLinearFiltering)
        {
            ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList *parent_list, const ImDrawCmd *cmd)
                                                    {
                                                        ImageWindow *that = reinterpret_cast<ImageWindow *>(cmd->UserCallbackData);
                                                        that->impl->gpuTexture.setLinearInterpolationEnabled(true);
                                                    },
                                                    this);
        }

        const auto imageWidgetSize = imSize(impl->imageWidgetRect.current);
        ImGui::Image(reinterpret_cast<ImTextureID>(imageTexture->textureId()),
                     imageWidgetSize,
                     uv0,
                     uv1);        

        if (useLinearFiltering)
        {
            ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList *parent_list, const ImDrawCmd *cmd)
                                                    {
                                                        ImageWindow *that = reinterpret_cast<ImageWindow *>(cmd->UserCallbackData);
                                                        that->impl->gpuTexture.setLinearInterpolationEnabled(false);
                                                    },
                                                    this);
        }

        ImVec2 mousePosInImage (0,0);
        ImVec2 mousePosInTexture (0,0);
        {
            // This 0.5 offset is important since the mouse coordinate is an integer.
            // So when we are in the center of a pixel we'll return 0,0 instead of
            // 0.5,0.5.
            ImVec2 widgetPos = (io.MousePos + ImVec2(0.5f,0.5f)) - imageWidgetTopLeft;
            ImVec2 uv_window = widgetPos / imageWidgetSize;
            mousePosInTexture = (uv1-uv0)*uv_window + uv0;
            mousePosInImage = mousePosInTexture * ImVec2(impl->im->width(), impl->im->height());
        }
        
        bool showCursorOverlay = false;
        const bool pointerOverTheImage = ImGui::IsItemHovered() && impl->im->contains(mousePosInImage.x, mousePosInImage.y);
        if (pointerOverTheImage)
        {
            showCursorOverlay = impl->mutableState.activeMode == ViewerMode::Original;
        }
        
        if (showCursorOverlay)
        {
            impl->cursorOverlayInfo.image = impl->im.get();
            impl->cursorOverlayInfo.imageTexture = &impl->gpuTexture;
            impl->cursorOverlayInfo.showHelp = false;
            impl->cursorOverlayInfo.imageWidgetSize = imageWidgetSize;
            impl->cursorOverlayInfo.imageWidgetTopLeft = imageWidgetTopLeft;
            impl->cursorOverlayInfo.uvTopLeft = uv0;
            impl->cursorOverlayInfo.uvBottomRight = uv1;
            impl->cursorOverlayInfo.roiWindowSize = ImVec2(15, 15);
            impl->cursorOverlayInfo.mousePos = io.MousePos;
            // Option: show it next to the mouse.
            if (impl->mutableState.inputState.shiftIsPressed || controlsWindowState.shiftIsPressed)
            {
                impl->inlineCursorOverlay.showTooltip(impl->cursorOverlayInfo);
            }
        }

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && io.KeyCtrl)
        {
            if ((impl->im->width() / float(impl->zoom.zoomFactor)) > 16.f
                 && (impl->im->height() / float(impl->zoom.zoomFactor)) > 16.f)
            {
                impl->zoom.zoomFactor *= 2;
                impl->zoom.uvCenter = mousePosInTexture;
            }
        }    
        
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            if (io.KeyCtrl)
            {
                if (impl->zoom.zoomFactor >= 2)
                    impl->zoom.zoomFactor /= 2;
            }
            else
            {
                // xv-like controls focus.
                if (impl->viewer) impl->viewer->onControlsRequested();
            }
        }
    }
        
    ImGui::End();
    ImGui::PopStyleVar();
    
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

} // zv
