//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImguiCanvasContainer.h"

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

struct ImguiCanvasContainer::Impl
{
    bool enabled = false;
    bool closeRequested = false;

    ImguiCanvasContainer::FrameInfo currentFrameInfo;
    
    zv::Point nextWindowPos;
    zv::Point nextWindowSize;

    zv::Point windowPos;
    zv::Point windowSize;
    
    std::string title;

    float contentDpiScale = 1.f;

    ImguiCanvasContainer::WindowSizeChangedCb windowSizeChangedCb;
    zv::Padding decorationSize;
};

ImguiCanvasContainer::ImguiCanvasContainer()
: impl (new Impl())
{}

ImguiCanvasContainer::~ImguiCanvasContainer()
{
    shutdown();
}

GLFWwindow* ImguiCanvasContainer::native_glfwWindow ()
{
    return nullptr;
}

bool ImguiCanvasContainer::isEnabled () const
{
    return impl->enabled;
}

void ImguiCanvasContainer::setEnabled (bool enabled)
{
    if (impl->enabled == enabled)
        return;
    
    impl->enabled = enabled;
}

bool ImguiCanvasContainer::closeRequested () const
{
    return impl->closeRequested;
}

void ImguiCanvasContainer::cancelCloseRequest ()
{
    impl->closeRequested = false;
}

void ImguiCanvasContainer::triggerCloseRequest ()
{
    impl->closeRequested = true;
}

void ImguiCanvasContainer::onWindowSizeChanged (int width, int height)
{
    if (!impl->windowSizeChangedCb)
        return;


    impl->windowSizeChangedCb (width, height, /*fromUser=*/ true);
}

void ImguiCanvasContainer::setWindowSizeChangedCallback (WindowSizeChangedCb&& callback)
{
    impl->windowSizeChangedCb = callback;
}

void ImguiCanvasContainer::setWindowTitle (const std::string& title)
{
    if (impl->title == title)
        return;

    impl->title = title;
}

void ImguiCanvasContainer::setWindowPos (int x, int y)
{
    impl->nextWindowPos = zv::Point(x, y);
}

void ImguiCanvasContainer::setWindowSize (int width, int height)
{
    impl->nextWindowSize = zv::Point(width, height);
}

zv::Rect ImguiCanvasContainer::geometry() const
{
    zv::Rect geom;

    geom.origin.x = impl->windowPos.x;
    geom.origin.y = impl->windowPos.y;
    
    geom.size.x = impl->windowSize.x;
    geom.size.y = impl->windowSize.y;
    return geom;
}

void ImguiCanvasContainer::shutdown()
{
}

bool ImguiCanvasContainer::isInitialized () const
{
    return impl->windowPos.isValid();
}

bool ImguiCanvasContainer::initialize (const std::string& title,
                                       const zv::Rect& geometry)
{
    zv_dbg ("Initializing window %s with geometry %f %f", title.c_str(), geometry.size.x, geometry.size.y);

    impl->title = title;
    impl->contentDpiScale = ImGui_primaryMonitorContentDpiScale().x;

    // Use ImGui style values for window decorations
    const ImGuiStyle& style = ImGui::GetStyle();
    impl->decorationSize.left = style.WindowPadding.x + style.WindowBorderSize;
    impl->decorationSize.right = style.WindowPadding.x + style.WindowBorderSize;
    impl->decorationSize.top = style.WindowPadding.y + style.WindowBorderSize + ImGui::GetFrameHeight(); // Add title bar height
    impl->decorationSize.bottom = style.WindowPadding.y + style.WindowBorderSize;
   
    impl->windowPos = geometry.origin;
    impl->windowSize = geometry.size;
    
    impl->nextWindowPos = geometry.origin;
    impl->nextWindowSize = geometry.size;

    return true;
}

zv::Padding ImguiCanvasContainer::decorationSize () const
{
    return impl->decorationSize;
}

void ImguiCanvasContainer::setSwapInterval (int interval)
{
}

void ImguiCanvasContainer::focus ()
{
}

void ImguiCanvasContainer::setResizable (bool resizable)
{
}

void ImguiCanvasContainer::bringToFront ()
{
}

void ImguiCanvasContainer::enableContexts ()
{
}

void ImguiCanvasContainer::disableContexts ()
{
}

ImguiCanvasContainer::FrameInfo ImguiCanvasContainer::beginFrame ()
{
    impl->currentFrameInfo.windowContentWidth = impl->windowSize.x;
    impl->currentFrameInfo.windowContentHeight = impl->windowSize.y;
    impl->currentFrameInfo.frameBufferWidth = impl->windowSize.x;
    impl->currentFrameInfo.frameBufferHeight = impl->windowSize.y;
    impl->currentFrameInfo.contentDpiScale = impl->contentDpiScale;    
    return impl->currentFrameInfo;
}

void ImguiCanvasContainer::endFrame ()
{
}

bool ImguiCanvasContainer::ImGuiBegin (const FrameInfo& frameInfo, ImGuiWindowFlags extraFlags)
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

    if (impl->nextWindowSize.isValid())
    {
        ImGui::SetNextWindowSize(imVec2(impl->nextWindowSize), ImGuiCond_Always);
        impl->nextWindowSize = zv::Point();
    }
     
    bool ok = ImGui::Begin(impl->title.c_str(), nullptr, flags);
    
    impl->windowSize = toPoint(ImGui::GetWindowSize());
    impl->windowPos = toPoint(ImGui::GetWindowPos());

    return ok;
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
    // Create window with graphics context.
    impl->window = glfwCreateWindow(1280, 1024, "ZV Viewer", nullptr, nullptr);    

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

    glfwSwapInterval(1); // Enable vsync

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

void ImguiCanvas::beginFrame()
{
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
