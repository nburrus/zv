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

struct ImageLayout
{
    LayoutConfig config;
    std::vector<Rect> imageRects;
    
    void adjustForConfig (const LayoutConfig& config)
    {
        this->config = config;
        imageRects.resize (config.numImages);
        
        for (int r = 0; r < config.numRows; ++r)
        for (int c = 0; c < config.numCols; ++c)
        {
            const int idx = r*config.numCols + c;
            if (idx < config.numImages)
            {
                imageRects[idx] = Rect::from_x_y_w_h(double(c)/config.numCols,
                                                     double(r)/config.numRows,
                                                     1.0/config.numCols,
                                                     1.0/config.numRows);
            }
        }
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
    ImguiGLFWWindow imguiGlfwWindow;
    Viewer* viewer = nullptr;

    std::vector<ImageItemAndData> currentImages;
    ImageLayout currentLayout;
    
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
        
    ImVec2 monitorSize = ImVec2(-1,-1);
    
    const int windowBorderSize = 0;
    bool shouldUpdateWindowSize = false;
    const int gridPadding = 1;
    
    struct {
        zv::Rect normal;
        zv::Rect current;
    } imageWidgetRect;
    
    ZoomInfo zoom;
    
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

void ImageWindow::Impl::adjustForNewSelection ()
{
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    this->monitorSize = ImVec2(mode->width, mode->height);

    ImageList& imageList = this->viewer->imageList();
    const int firstIndex = imageList.selectedIndex();
    
    this->imguiGlfwWindow.enableContexts ();
    
    // It's very important that this gets called while the GL context is bound
    // as it may release some GLTexture in the cache. Would be nice to make this
    // code more robust.
    this->currentImages.resize (this->mutableState.layoutConfig.numImages);
    for (int i = 0; i < this->currentImages.size(); ++i)
    {
        const int imgIndex = firstIndex + i;
        if (imgIndex < imageList.numImages())
        {
            this->currentImages[i].item = imageList.imageItemFromIndex(firstIndex + i);
            this->currentImages[i].data = imageList.getData(this->currentImages[i].item.get());
            if (this->currentImages[i].data->cpuData->hasData())
            {
                auto &textureData = this->currentImages[i].data->textureData;
                if (!textureData)
                {
                    textureData = std::make_unique<GLTexture>();
                    textureData->initialize();
                    // FIXME: not using the cache yet! GPU data releasing can be tricky.
                    textureData->upload(*this->currentImages[i].data->cpuData);
                }
            }
        }
        else
        {
            // Make sure that we clear it.
            this->currentImages[i] = {};
        }
    }
    
    this->currentLayout.adjustForConfig(this->mutableState.layoutConfig);
        
    // The first image will decide for all the other sizes.
    const auto& firstIm = *this->currentImages[0].data->cpuData;

    if (!this->imageWidgetRect.normal.origin.isValid())
    {
        this->imageWidgetRect.normal.origin.x = this->monitorSize.x * 0.10;
        this->imageWidgetRect.normal.origin.y = this->monitorSize.y * 0.10;
    }

    const auto& firstRect = this->currentLayout.imageRects[0];
    // Handle the case there the cpuImage is empty (e.g. failed to load the file).
    int firstImWidth = firstIm.width() > 0 ? firstIm.width() : 256;
    int firstImHeight = firstIm.height() > 0 ? firstIm.height() : 256;
    this->imageWidgetRect.normal.size.x = firstImWidth / firstRect.size.x + (this->currentLayout.config.numCols - 1) * gridPadding;
    this->imageWidgetRect.normal.size.y = firstImHeight / firstRect.size.y + (this->currentLayout.config.numRows - 1) * gridPadding;
    
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

    glfwWindowHint(GLFW_RESIZABLE, true); // restore the default.

    checkGLError ();
    
    return true;
}

void ImageWindow::setLayout (int numImages, int numRows, int numCols)
{
    LayoutConfig config;
    config.numImages = numImages;
    config.numCols = numCols;
    config.numRows = numRows;
    impl->mutableState.layoutConfig = config;
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

    for (const auto code : {
            GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, 
            GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT, GLFW_KEY_RIGHT,
            GLFW_KEY_O, GLFW_KEY_S, GLFW_KEY_W, GLFW_KEY_N, GLFW_KEY_A,
            GLFW_KEY_SPACE, GLFW_KEY_BACKSPACE
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

void ImageWindow::processKeyEvent (int keycode)
{
    auto& io = ImGui::GetIO();

    switch (keycode)
    {
        case GLFW_KEY_UP:
        case GLFW_KEY_BACKSPACE:
        {
            impl->viewer->imageList().selectImage (impl->viewer->imageList().selectedIndex() - impl->currentLayout.config.numImages);
            break;
        }

        case GLFW_KEY_DOWN:
        case GLFW_KEY_SPACE:
        {
            impl->viewer->imageList().selectImage (impl->viewer->imageList().selectedIndex() + impl->currentLayout.config.numImages);
            break;
        }

        case GLFW_KEY_S:
        {
            // No image saving for now.
            // if (io.KeyCtrl)
            // {
            //     saveCurrentImage();
            // }
            break;
        }

        case GLFW_KEY_O:
        {
            // No image saving for now.
            if (io.KeyCtrl)
            {
                impl->viewer->onOpenImage();
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

        case GLFW_KEY_1: setLayout(1,1,1); break;
        case GLFW_KEY_2: setLayout(2,1,2); break;
        case GLFW_KEY_3: setLayout(3,1,3); break;
        case GLFW_KEY_4: setLayout(4,2,2); break;

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

void renderImageItem (const ImageItemAndData& item,
                      const ImVec2& imageWidgetTopLeft,
                      const ImVec2& imageWidgetSize,
                      ZoomInfo& zoom,
                      bool imageHasNonMultipleSize,
                      CursorOverlayInfo* overlayInfo)
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

    GLTexture* imageTexture = item.data->textureData.get();
    
    const bool hasZoom = zoom.zoomFactor != 1;
    const bool useLinearFiltering = imageHasNonMultipleSize && !hasZoom;
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

    const auto& currentIm = *item.data->cpuData;

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

    if (pointerOverTheImage && overlayInfo)
    {
        overlayInfo->itemAndData = item;
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
}

void ImageWindow::renderFrame ()
{    
    ImageList& imageList = impl->viewer->imageList();
       
    bool contentChanged = (impl->mutableState.layoutConfig != impl->currentLayout.config);
    contentChanged |= impl->currentImages.empty();
    if (!contentChanged)
    {
        for (int idx = 0; idx < impl->currentImages.size(); ++idx)
        {
            const ImageItemPtr& item = imageList.imageItemFromIndex (imageList.selectedIndex() + idx);
            if (impl->currentImages[idx].item->uniqueId != item->uniqueId)
            {
                contentChanged = true;
                break;
            }
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
    const float monoFontSize = ImguiGLFWWindow::monoFontSize(io);
    
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
                            // | ImGuiWindowFlags_NoBackground
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_HorizontalScrollbar
                            // | ImGuiWindowFlags_NoDocking
                            | ImGuiWindowFlags_NoNav);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1,1,1,1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    bool isOpen = true;
    
    std::string mainWindowName = "zv - " + impl->currentImages[0].item->prettyName();
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
        const bool imageHasNonMultipleSize = int(impl->imageWidgetRect.current.size.x) % int(impl->imageWidgetRect.normal.size.x) != 0;
        
        struct ImageItemGeometry
        {
            ImVec2 topLeft;
            ImVec2 size;
        };
        std::vector<ImageItemGeometry> widgetGeometries (impl->currentImages.size());

        for (int r = 0; r < impl->currentLayout.config.numRows; ++r)
        for (int c = 0; c < impl->currentLayout.config.numCols; ++c)
        {
            const int idx = r*impl->currentLayout.config.numCols + c;
            if (idx < impl->currentImages.size() && impl->currentImages[idx].data)
            {
                const auto& rect = impl->currentLayout.imageRects[idx];
                const ImVec2 imageWidgetSize = ImVec2(globalImageWidgetContentSize.x * rect.size.x, 
                                                      globalImageWidgetContentSize.y * rect.size.y);
                const ImVec2 imageWidgetTopLeft = ImVec2(globalImageWidgetContentSize.x * rect.origin.x + c*impl->gridPadding, 
                                                         globalImageWidgetContentSize.y * rect.origin.y + r*impl->gridPadding);
                widgetGeometries[idx].topLeft = imageWidgetTopLeft;
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
            if (!impl->currentImages[idx].data)
                continue;
            
            if (!impl->currentImages[idx].data->cpuData->hasData())
            {
                ImGui::SetCursorScreenPos (widgetGeometries[idx].topLeft);
                ImGui::TextColored (ImVec4(1,0,0,1), "ERROR: could not load the image.\nPath: %s", impl->currentImages[idx].item->prettyName().c_str());
            }
            else
            {
                renderImageItem(impl->currentImages[idx],
                                widgetGeometries[idx].topLeft,
                                widgetGeometries[idx].size,
                                impl->zoom,
                                imageHasNonMultipleSize,
                                &impl->cursorOverlayInfo);
            }
        }

        if (impl->cursorOverlayInfo.valid())
        {            
            for (int idx = 0; idx < impl->currentImages.size(); ++idx)
            {
                if (!impl->currentImages[idx].data)
                    continue;

                if (impl->cursorOverlayInfo.itemAndData.item->uniqueId == impl->currentImages[idx].item->uniqueId)
                    continue;
                
                ImVec2 deltaFromTopLeft = impl->cursorOverlayInfo.mousePos - impl->cursorOverlayInfo.imageWidgetTopLeft;
                // FIXME: replace this with an image of a cross-hair texture. Filled black with a white outline.
                ImGui::GetForegroundDrawList()->AddCircle(widgetGeometries[idx].topLeft + deltaFromTopLeft, 4.0, IM_COL32(255,255,255,180), 0, 2.0f);
                ImGui::GetForegroundDrawList()->AddCircle(widgetGeometries[idx].topLeft + deltaFromTopLeft, 5.0, IM_COL32(0,0,0,180), 0, 1.f);
                ImGui::GetForegroundDrawList()->AddCircle(widgetGeometries[idx].topLeft + deltaFromTopLeft, 3.0, IM_COL32(0,0,0,180), 0, 1.f);
            }

            const bool showStatusBar = ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyCtrl;
            if (showStatusBar)
            {
                impl->imguiGlfwWindow.PushMonoSpaceFont(io);

                const bool showOnBottom = impl->cursorOverlayInfo.mousePosInTexture.y < 0.75;

                for (int idx = 0; idx < impl->currentImages.size(); ++idx)
                {
                    if (!impl->currentImages[idx].data)
                        continue;
                    
                    const auto& im = *impl->currentImages[idx].data->cpuData;
                    const ImVec2 imSize (im.width(), im.height());
                    ImVec2 mousePosInImage = impl->cursorOverlayInfo.mousePosInTexture * imSize;
                    const int cInImage = int(mousePosInImage.x);
                    const int rInImage = int(mousePosInImage.y);

                    PixelSRGBA sRgba = im(cInImage, rInImage);
                    const auto hsv = zv::convertToHSV(sRgba);
                    std::string caption = formatted("%4d, %4d (sRGB %3d %3d %3d) (HSV %3d %3d %3d)",
                                                    cInImage, rInImage,
                                                    sRgba.r, sRgba.g, sRgba.b,
                                                    intRnd(hsv.x*360.f), intRnd(hsv.y*100.f), intRnd(hsv.z*100.f/255.f));

                    ImVec2 textStart, textAreaStart, textAreaEnd;

                    if (showOnBottom)
                    {
                        textStart = widgetGeometries[idx].topLeft;
                        textStart.x += monoFontSize*0.5;
                        
                        textAreaStart = widgetGeometries[idx].topLeft;
                        textAreaEnd = widgetGeometries[idx].topLeft + widgetGeometries[idx].size;

                        textStart.y += widgetGeometries[idx].size.y - monoFontSize*1.1;
                        textAreaStart.y = textStart.y - monoFontSize*0.1;
                    }
                    else
                    {
                        textStart = widgetGeometries[idx].topLeft;
                        textStart.x += monoFontSize*0.5;
                        textStart.y += monoFontSize*0.15;
                        
                        textAreaStart = widgetGeometries[idx].topLeft;
                        textAreaEnd = textAreaStart + ImVec2(widgetGeometries[idx].size.x, monoFontSize*1.2);
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
            if (impl->viewer) impl->viewer->onControlsRequested();
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

} // zv
