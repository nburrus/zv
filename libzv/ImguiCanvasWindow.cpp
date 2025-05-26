//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImguiCanvasWindow.h"

#include <libzv/ImguiUtils.h>
#include <libzv/Icon.h>

#include <libzv/ProggyVector_font.hpp>
#include <libzv/Arimo_font.hpp>
#include <libzv/FontIcomoon_data.hpp>

#include <FontIcomoon.h>

#include <libzv/OpenGL.h>
#include <libzv/Utils.h>
#include <libzv/Platform.h>

#include "PlatformSpecific.h"

#define IMGUI_DEFINE_MATH_OPERATORS 1
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <imgui/misc/freetype/imgui_freetype.h>

#if PLATFORM_EMSCRIPTEN
#include <emscripten.h>
#define GL_GLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES
#else
#include <GL/gl3w.h>
#endif

#include <GLFW/glfw3.h>

#include <cstdio>
#include <unordered_set>

namespace zv
{

ImguiCanvas* g_currentCanvas = nullptr;

// Global instance for emscripten callbacks
#if PLATFORM_EMSCRIPTEN
int g_canvasWidth = 0;
int g_canvasHeight = 0;

// Function used by c++ to get the size of the html canvas
EM_JS(int, em_canvasGetWidth, (), {
    return Module.canvas.width;
});

// Function used by c++ to get the size of the html canvas
EM_JS(int, em_canvasGetHeight, (), {
    return Module.canvas.height;
});

// Function called by javascript
EM_JS(void, em_resizeCanvas, (), {
    js_resizeCanvasToFitWindow();
});

#endif

#if PLATFORM_EMSCRIPTEN
extern "C" {
    // Function to be called from JavaScript
    EMSCRIPTEN_KEEPALIVE void zv_resizeCanvas(int width, int height) {
        // Need to access a global ImguiCanvas instance
        // You'll need to implement a way to store and retrieve the current ImguiCanvas instance
        if (g_currentCanvas)
        {
            g_currentCanvas->handleResize(width, height);
        }
    }
}
#endif


struct ImguiCanvasWindow::Impl
{
    bool enabled = false;
    bool closeRequested = false;

    FrameInfo currentFrameInfo;
    
    zv::Point nextWindowPos;
    zv::Point nextContainerContentSize;

    zv::Point windowPos;
    zv::Point windowSize;
    
    std::string title;

    float contentDpiScale = 1.f;

    ImguiCanvasWindow::WindowSizeChangedCb windowSizeChangedCb;
};

ImguiCanvasWindow::ImguiCanvasWindow()
: impl (new Impl())
{}

ImguiCanvasWindow::~ImguiCanvasWindow()
{
    shutdown();
}

GLFWwindow* ImguiCanvasWindow::native_glfwWindow ()
{
    return nullptr;
}

bool ImguiCanvasWindow::isEnabled () const
{
    return impl->enabled;
}

void ImguiCanvasWindow::setEnabled (bool enabled)
{
    if (impl->enabled == enabled)
        return;
    
    impl->enabled = enabled;
}

bool ImguiCanvasWindow::closeRequested () const
{
    return impl->closeRequested;
}

void ImguiCanvasWindow::cancelCloseRequest ()
{
    impl->closeRequested = false;
}

void ImguiCanvasWindow::triggerCloseRequest ()
{
    impl->closeRequested = true;
}

void ImguiCanvasWindow::onContainerSizeChanged (int width, int height)
{
    if (!impl->windowSizeChangedCb)
        return;


    impl->windowSizeChangedCb (width, height, /*fromUser=*/ true);
}

void ImguiCanvasWindow::setContainerSizeChangedCallback (WindowSizeChangedCb&& callback)
{
    impl->windowSizeChangedCb = callback;
}

void ImguiCanvasWindow::setContainerTitle (const std::string& title)
{
    if (impl->title == title)
        return;

    impl->title = title;
}

void ImguiCanvasWindow::setContainerPos (int x, int y)
{
    zv_dbg("[CanvasContainer] setWindowPos %d %d", x, y);
    impl->nextWindowPos = zv::Point(x, y);
}

void ImguiCanvasWindow::setContainerSize (int width, int height)
{
    zv_dbg("[CanvasContainer] setWindowSize %d %d", width, height);
    impl->nextContainerContentSize = zv::Point(width, height);
}

zv::Rect ImguiCanvasWindow::containerGeometry() const
{
    zv::Rect geom;

    geom.origin.x = impl->windowPos.x;
    geom.origin.y = impl->windowPos.y;
    
    geom.size.x = impl->windowSize.x;
    geom.size.y = impl->windowSize.y;
    return geom;
}

void ImguiCanvasWindow::shutdown()
{
}

bool ImguiCanvasWindow::isInitialized () const
{
    return impl->windowPos.isValid();
}

bool ImguiCanvasWindow::initialize (const std::string& title,
                                       const zv::Rect& geometry)
{
    zv_dbg ("Initializing window %s with geometry %f x %f (%f, %f)", title.c_str(), geometry.size.x, geometry.size.y, geometry.origin.x, geometry.origin.y);

    impl->title = title;
    impl->contentDpiScale = ImGui_primaryMonitorContentDpiScale().x;
   
    impl->windowPos = geometry.origin;
    impl->windowSize = geometry.size;
    
    impl->nextWindowPos = geometry.origin;
    impl->nextContainerContentSize = geometry.size;

    return true;
}

zv::Padding ImguiCanvasWindow::canvasDecorationSize () const
{
    return zv::Padding{0,0,0,0};
}

void ImguiCanvasWindow::setSwapInterval (int interval)
{
}

zv::Rect ImguiCanvasWindow::canvasArea () const
{
    Rect r;
    r.origin = zv::Point(0, 0);
    r.size = canvasSize();
    return r;
}

zv::Point ImguiCanvasWindow::canvasSize () const
{
#if PLATFORM_EMSCRIPTEN
    return zv::Point(g_canvasWidth, g_canvasHeight);
#else
    return g_currentCanvas->canvasSize();
#endif
}

ImVec2 ImguiCanvasWindow::imguiWindowToDrawList (const FrameInfo& frameInfo, const ImVec2& p) const
{
    return p + imVec2(frameInfo.containerOrigin);
}

ImVec2 ImguiCanvasWindow::drawListToImguiWindow (const FrameInfo& frameInfo, const ImVec2& p) const
{
    return p - imVec2(frameInfo.containerOrigin);
}

ImVec2 ImguiCanvasWindow::viewportToImguiWindow (const FrameInfo& frameInfo, const ImVec2& p) const
{
    return p - imVec2(frameInfo.containerOrigin) - ImVec2(frameInfo.containerDecorationSize.left, frameInfo.containerDecorationSize.top);
}

void ImguiCanvasWindow::focus ()
{
}

void ImguiCanvasWindow::setResizable (bool resizable)
{
}

void ImguiCanvasWindow::bringToFront ()
{
}

void ImguiCanvasWindow::enableContexts ()
{
}

void ImguiCanvasWindow::disableContexts ()
{
}

FrameInfo ImguiCanvasWindow::beginFrame ()
{
        // Use ImGui style values for window decorations
    const ImGuiStyle& style = ImGui::GetStyle();
    impl->currentFrameInfo.containerDecorationSize.left = 0; // style.WindowPadding.x + style.WindowBorderSize;
    impl->currentFrameInfo.containerDecorationSize.right = 0; //style.WindowPadding.x + style.WindowBorderSize;
    impl->currentFrameInfo.containerDecorationSize.top = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
    impl->currentFrameInfo.containerDecorationSize.bottom = 0; //style.WindowPadding.y + style.WindowBorderSize;

    impl->currentFrameInfo.containerOrigin = impl->windowPos;
    impl->currentFrameInfo.containerWidth = impl->windowSize.x;
    impl->currentFrameInfo.containerHeight = impl->windowSize.y;
    impl->currentFrameInfo.frameBufferWidth = impl->windowSize.x;
    impl->currentFrameInfo.frameBufferHeight = impl->windowSize.y;
    impl->currentFrameInfo.contentDpiScale = impl->contentDpiScale;    
    return impl->currentFrameInfo;
}

void ImguiCanvasWindow::endFrame ()
{
}

bool ImguiCanvasWindow::ImGuiBegin (const FrameInfo& frameInfo, bool* p_open, ImGuiWindowFlags extraFlags)
{
    ImGuiWindowFlags flags = (
                            ImGuiWindowFlags_NoScrollbar
                            // ImGuiWindowFlags_NoTitleBar
                            // | ImGuiWindowFlags_NoResize
                            // | ImGuiWindowFlags_NoMove
                            // ImGuiWindowFlags_NoScrollWithMouse
                            // | ImGuiWindowFlags_NoCollapse
                            // | ImGuiWindowFlags_NoBackground
                            | ImGuiWindowFlags_NoSavedSettings
                            // | ImGuiWindowFlags_HorizontalScrollbar
                            // | ImGuiWindowFlags_NoDocking
                            | ImGuiWindowFlags_NoNav);

    flags |= extraFlags;

    if (impl->nextWindowPos.isValid())
    {
        ImGui::SetNextWindowPos(imVec2(impl->nextWindowPos), ImGuiCond_Always);
        impl->nextWindowPos = zv::Point();
    }

    if (impl->nextContainerContentSize.isValid())
    {
        ImGui::SetNextWindowSize(imVec2(impl->nextContainerContentSize) + imVec2(frameInfo.containerDecorationSize.topLeft()), ImGuiCond_Always);
        impl->nextContainerContentSize = zv::Point();
    }
     
    bool ok = ImGui::Begin(impl->title.c_str(), p_open, flags);
        
    impl->windowPos = toPoint(ImGui::GetWindowPos());

    return ok;
}

void ImguiCanvasWindow::ImGuiEnd ()
{
    impl->windowSize = toPoint(ImGui::GetWindowSize());
    ImGui::End();
}

struct ImguiCanvas::Impl
{
    ImGuiContext* imGuiContext = nullptr;
    bool enabled = false;
    int width = 0;
    int height = 0;
    float contentDpiScale = 1.f;
    GLFWwindow* window = nullptr;
};

ImguiCanvas::ImguiCanvas()
: impl(new Impl())
{}

ImguiCanvas::~ImguiCanvas()
{
    if (impl->imGuiContext)
    {
        ImGui::SetCurrentContext(impl->imGuiContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(impl->imGuiContext);
        impl->imGuiContext = nullptr;
    }
}

void ImguiCanvas::initialize()
{
    zv_assert(g_currentCanvas == nullptr, "g_currentCanvas is already set");
    g_currentCanvas = this;

#if PLATFORM_EMSCRIPTEN
    em_resizeCanvas();

    // Set the global canvas instance for emscripten callbacks
    g_canvasWidth = em_canvasGetWidth();
    g_canvasHeight = em_canvasGetWidth();
#endif

    // Create window with graphics context.
#ifndef PLATFORM_EMSCRIPTEN
    impl->window = glfwCreateWindow(1280, 1024, "ZV Viewer", nullptr, nullptr);
#else
    // For Emscripten, glfwCreateWindow uses the canvas element by default.
    // The size is controlled by the canvas element's dimensions.
    impl->window = glfwCreateWindow(g_canvasWidth, g_canvasHeight, "ZV Viewer", nullptr, nullptr); // Size doesn't matter for Emscripten
#endif

    glfwMakeContextCurrent(impl->window);

#if !PLATFORM_EMSCRIPTEN
    // Make sure that gl3w is initialized.
    bool err = gl3wInit() != 0;
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        zv_assert(false, "Failed to initialize OpenGL loader!");
    }
#endif

    glfwShowWindow(impl->window);

    checkGLError ();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    impl->imGuiContext = ImGui::CreateContext();
    impl->imGuiContext->IO.IniFilename = nullptr;
    ImGui::SetCurrentContext(impl->imGuiContext);

    ImGuiIO& io = ImGui::GetIO();
    impl->contentDpiScale = ImGui_primaryMonitorContentDpiScale().x;

    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Load the fonts with the proper dpi scale
    {
        const zv::Point dpiScale = ImGui_primaryMonitorContentDpiScale();
        const zv::Point retinaScaleFactor = ImGui_primaryMonitorRetinaFrameBufferScale();

        static const ImWchar ranges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin Supplement
            0x03BC, 0x03BC, // micro
            0x0394, 0x0394, // delta
            0,
        };

        {
            auto* font = io.Fonts->AddFontFromMemoryCompressedTTF(zv::Arimo_compressed_data, zv::Arimo_compressed_size, 15.0f * retinaScaleFactor.x * dpiScale.x, nullptr, ranges);

            ImFontConfig config;
            config.MergeMode = true;
            config.GlyphOffset.y = 3.0*dpiScale.x;
            config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
            static const ImWchar icon_ranges[] = { ICON_MIN, ICON_MAX, 0 };
            font = io.Fonts->AddFontFromMemoryCompressedTTF(zv::Icomoon_compressed_data, zv::Icomoon_compressed_size, 17.0f * retinaScaleFactor.x * dpiScale.x, &config, icon_ranges);
            font->Scale /= retinaScaleFactor.x;
        }

        {
            auto* font = io.Fonts->AddFontFromMemoryCompressedTTF(zv::ProggyVector_compressed_data, zv::ProggyVector_compressed_size, 16.0f * retinaScaleFactor.x * dpiScale.x);
            font->Scale /= retinaScaleFactor.x;
        }

        {
            auto* font = io.Fonts->AddFontFromMemoryCompressedTTF(zv::ProggyVector_compressed_data,
                                                                  zv::ProggyVector_compressed_size,
                                                                  15.0f * retinaScaleFactor.x * dpiScale.x,
                                                                  nullptr,
                                                                  ranges);
            font->Scale /= retinaScaleFactor.x;
        }

        if (!floatEquals(dpiScale.x, 1.f))
        {
            ImGui::GetStyle().ScaleAllSizes(dpiScale.x);
        }
    }

#if !PLATFORM_EMSCRIPTEN
    glfwSwapInterval(1); // Enable vsync
#endif

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(impl->window,
                                 true /* install callbacks, we only have one context */);

    // Setup Platform/Renderer bindings
    ImGui_ImplOpenGL3_Init(glslVersion());
}

void ImguiCanvas::onCanvasSizeChanged(int width, int height)
{
    impl->width = width;
    impl->height = height;
}

ImGuiContext* ImguiCanvas::imGuiContext()
{
    return impl->imGuiContext;
}

zv::Point ImguiCanvas::canvasSize () const
{
    return zv::Point(impl->width, impl->height);
}

// Non-static method implementation
void ImguiCanvas::handleResize(int width, int height)
{
    zv_dbg("handleResize %d %d", width, height);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    // Assuming a 1:1 mapping for framebuffer scale for simplicity,
    // but this might need adjustment based on DPI/retina scaling.
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // Update the window size using GLFW
    if (impl && impl->window)
    {
        glfwSetWindowSize(impl->window, width, height);
    }
    
    // Also update our internal size tracking
    onCanvasSizeChanged(width, height);
}

void ImguiCanvas::beginFrame()
{
#if PLATFORM_EMSCRIPTEN
    int width = em_canvasGetWidth();
    int height = em_canvasGetHeight();

    if (width != g_canvasWidth || height != g_canvasHeight)
    {
        g_canvasWidth = width;
        g_canvasHeight = height;
        handleResize(width, height);
    }
#endif

    ImGui::SetCurrentContext(impl->imGuiContext);
    glfwMakeContextCurrent(impl->window);
    glfwPollEvents();

    ImGui::SetCurrentContext(impl->imGuiContext);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImguiCanvas::endFrame()
{
    ImGui::Render();

    checkGLError ();
    
    int frameBufferWidth, frameBufferHeight;
    glfwGetFramebufferSize(impl->window, &frameBufferWidth, &frameBufferHeight);

    glViewport(0, 0, frameBufferWidth, frameBufferHeight);
    glClearColor(0.1, 0.1, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(impl->window);
}

} // zv
