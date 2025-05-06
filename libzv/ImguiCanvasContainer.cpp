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

#include <cstdio>
#include <unordered_set>

namespace zv
{

struct ImguiCanvasContainer::Impl
{
    bool enabled = false;
    bool closeRequested = false;

    ImguiCanvasContainer::FrameInfo currentFrameInfo;
    
    zv::Point pos;
    zv::Point size;
    
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

GLFWwindow* ImguiCanvasContainer::glfwWindow ()
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



    // Leave two frames of delay before concluding that the size changed
    // indeed came from the user and not from our own call to setWindowSize.
    const int fc = ImGui::GetFrameCount();    
    bool fromUser = ((fc - impl->lastSizeRequest) > 2);
    impl->windowSizeChangedCb (width, height, fromUser);
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
    glfwSetWindowTitle(impl->window, title.c_str());
}

void ImguiCanvasContainer::setWindowPos (int x, int y)
{
    glfwRestoreWindow (impl->window);
    glfwSetWindowPos (impl->window, x, y);
}

void ImguiCanvasContainer::setWindowSize (int width, int height)
{
    impl->lastSizeRequest = ImGui::GetFrameCount();
    // Some window managers might maximize automatically
    glfwRestoreWindow (impl->window);
    glfwSetWindowSize (impl->window, width, height);
}

zv::Rect ImguiCanvasContainer::geometry() const
{
    zv::Rect geom;

    int platformWindowX, platformWindowY;
    glfwGetWindowPos(impl->window, &platformWindowX, &platformWindowY);
    
    int platformWindowWidth, platformWindowHeight;
    glfwGetWindowSize(impl->window, &platformWindowWidth, &platformWindowHeight);

    geom.origin.x = platformWindowX;
    geom.origin.y = platformWindowY;
    
    geom.size.x = platformWindowWidth;
    geom.size.y = platformWindowHeight;
    return geom;
}

void ImguiCanvasContainer::shutdown()
{
    if (impl->window)
    {
        enableContexts ();
        
        // Cleanup
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGuiContextTracker::instance()->removeContext(impl->imGuiContext);
        ImGui::DestroyContext(impl->imGuiContext);
        impl->imGuiContext = nullptr;

        glfwDestroyWindow (impl->window);
        impl->window = nullptr;

        disableContexts ();
    }
}

static void glfwErrorFunction (int code, const char* error)
{
    fprintf (stderr, "GLFW Error %d: %s", code, error);
}

static void windowPosCallback(GLFWwindow* w, int x, int y)
{
    zv_dbg ("Got a window pos callback (%p) %d %d", w, x, y);
}

bool ImguiCanvasContainer::isInitialized () const
{
    return impl->window != nullptr;
}

bool ImguiCanvasContainer::initialize (GLFWwindow* parentWindow,
                                  const std::string& title,
                                  const zv::Rect& geometry,
                                  bool enableImguiViewports)
{
    glfwSetErrorCallback (glfwErrorFunction);
    
    zv_dbg ("Initializing window %s with geometry %f %f", title.c_str(), geometry.size.x, geometry.size.y);

    impl->title = title;
    impl->contentDpiScale = ImGui_primaryMonitorContentDpiScale().x;

    // Always start invisible, we'll show it later when we need to.
    glfwWindowHint(GLFW_VISIBLE, false);
    impl->window = glfwCreateWindow(geometry.size.x, geometry.size.y, title.c_str(), NULL, parentWindow);
    glfwWindowHint(GLFW_VISIBLE, true);
    if (impl->window == NULL)
        return false;

    int frameLeft, frameRight, frameTop, frameBottom;
#if PLATFORM_EMSCRIPTEN
    frameLeft = 0;
    frameRight = geometry.size.x;
    frameTop = 0;
    frameBottom = geometry.size.y;
#else    
    glfwGetWindowFrameSize (impl->window, &frameLeft, &frameTop, &frameRight, &frameBottom);
#endif
    // No decorations reported on X11.
    impl->decorationSize.left = std::max(frameLeft, 8);
    impl->decorationSize.right = std::max(frameRight, 8);
    impl->decorationSize.top = std::max(frameTop, 32);
    impl->decorationSize.bottom = std::max(frameBottom, 8);
   
    // Won't do anything on macOS, we don't even load the file.
    GLFWimage glfwImage;
    glfwImage.pixels = const_cast<unsigned char*>(Icon::instance().rgba32x32());
    if (glfwImage.pixels)
    {
        glfwImage.width = 32;
        glfwImage.height = 32;
        glfwSetWindowIcon(impl->window, 1, &glfwImage);
    }
    
    glfwSetWindowPos(impl->window, geometry.origin.x, geometry.origin.y);

    glfwSetWindowUserPointer(impl->window, this);
    {
        glfwSetWindowFocusCallback(impl->window, zv_glfw_WindowFocusCallback);
        glfwSetCursorEnterCallback(impl->window, zv_glfw_CursorEnterCallback);
        glfwSetMouseButtonCallback(impl->window, zv_glfw_MouseButtonCallback);
        glfwSetCursorPosCallback(impl->window, zv_glfw_CursorPosCallback);
        glfwSetScrollCallback(impl->window,      zv_glfw_ScrollCallback);
        glfwSetKeyCallback(impl->window,         zv_glfw_KeyCallback);
        glfwSetCharCallback(impl->window,        zv_glfw_CharCallback);
        glfwSetMonitorCallback(zv_glfw_MonitorCallback);

        glfwSetWindowSizeCallback(impl->window, zv_glfw_WindowSizeCallback);
    }

    glfwMakeContextCurrent(impl->window);
    
#if !PLATFORM_EMSCRIPTEN
    // Make sure that gl3w is initialized.
    bool err = gl3wInit() != 0;
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return false;
    }
#endif

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    impl->imGuiContext = ImGui::CreateContext(); // FIXME: use a shared font atlas.
    impl->imGuiContext->IO.IniFilename = nullptr;
    ImGuiContextTracker::instance()->addContext(impl->imGuiContext);
    ImGui::SetCurrentContext(impl->imGuiContext);

    ImGuiIO &io = ImGui::GetIO();

    if (enableImguiViewports)
    {
        // Enable Multi-Viewport / Platform Windows. Will be used by the highlight similar color companion window.
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }

    // Load the fonts with the proper dpi scale.
    {
        // Note: will still be 1 on macOS retina displays, they only change the framebuffer size.
        const zv::Point dpiScale = ImGui_primaryMonitorContentDpiScale();

        // The first default font is not a monospace anymore, a bit nicer to
        // read and it can scale properly with higher DPI.

        // Taken from Tracy https://github.com/davidwed/tracy
        static const ImWchar ranges[] = {
            0x0020,
            0x00FF, // Basic Latin + Latin Supplement
            0x03BC,
            0x03BC, // micro
            0x0394, // delta
            0x0394,
            0,
        };
        
        // On Windows and Linux the scale factor is handled by the dpi, but on macOS
        // it's handled via a bigger frameBuffer.
        const zv::Point retinaScaleFactor = ImGui_primaryMonitorRetinaFrameBufferScale();

        {
            auto* font = io.Fonts->AddFontFromMemoryCompressedTTF(zv::Arimo_compressed_data, zv::Arimo_compressed_size, 15.0f * retinaScaleFactor.x * dpiScale.x, nullptr, ranges);

            ImFontConfig config;
            config.MergeMode = true;
            config.GlyphOffset.y = 3.0*dpiScale.x; // so icons are centered in buttons.
            config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
            // config.GlyphMinAdvanceX = 15.0f; // Use if you want to make the icon monospaced
            static const ImWchar icon_ranges[] = { ICON_MIN, ICON_MAX, 0 };
            font = io.Fonts->AddFontFromMemoryCompressedTTF(zv::Icomoon_compressed_data, zv::Icomoon_compressed_size, 17.0f * retinaScaleFactor.x * dpiScale.x, &config, icon_ranges);
            // font = io.Fonts->AddFontFromMemoryCompressedTTF(zv::FontAwesome5_solid_compressed_data, zv::FontAwesome5_solid_compressed_size, 17.0f * retinaScaleFactor.x * dpiScale.x, &config, icon_ranges);
            // font = io.Fonts->AddFontFromMemoryCompressedTTF(zv::FontAwesome5_compressed_data, zv::FontAwesome5_compressed_size, 17.0f * retinaScaleFactor.x * dpiScale.x, &config, icon_ranges);
            
            font->Scale /= retinaScaleFactor.x;
        }

        // The second font is the monospace one.

        // Generated from https://github.com/bluescan/proggyfonts
        {
            auto* font = io.Fonts->AddFontFromMemoryCompressedTTF(zv::ProggyVector_compressed_data, zv::ProggyVector_compressed_size, 16.0f * retinaScaleFactor.x * dpiScale.x);
            font->Scale /= retinaScaleFactor.x;
        }
        
        // Third font, small monospace
        {
            auto* font = io.Fonts->AddFontFromMemoryCompressedTTF(zv::ProggyVector_compressed_data,
                                                                  zv::ProggyVector_compressed_size,
                                                                  15.0f * retinaScaleFactor.x * dpiScale.x,
                                                                  nullptr,
                                                                  ranges);
            font->Scale /= retinaScaleFactor.x;
        }

        // To scale the original font (poor quality)
        // ImFontConfig cfg;
        // cfg.SizePixels = roundf(13 * dpiScale.x);
        // cfg.GlyphOffset.y = dpiScale.x;
        // ImFont* font = ImGui::GetIO().Fonts->AddFontDefault(&cfg);
        
        // io.Fonts->AddFontFromFileTTF ("C:\\Windows\\Fonts\\segoeui.ttf", roundf(16.0f * dpiScale.x), nullptr, ranges);
        // io.Fonts->AddFontFromFileTTF ("C:\\Windows\\Fonts\\consola.ttf", 16.0f * dpiScale.x, nullptr, ranges);
        
        if (!floatEquals(dpiScale.x, 1.f))
        {
            ImGui::GetStyle().ScaleAllSizes(dpiScale.x);
        }
    }

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(impl->window,
                                 false /* do NOT install callbacks,
                                        we'll forward manually to properly handle multiple contexts
                                        */);
#ifdef __EMSCRIPTEN__
    // ImGui_ImplGlfw_InstallEmscriptenCallbacks(impl->window, "#canvas");
#endif
    ImGui_ImplOpenGL3_Init(glslVersion());
        
    // Important: do this only after creating the ImGuiContext. Otherwise we might
    // get some callbacks right away and get in trouble.
    // Start hidden. setEnabled will show it as needed.
    glfwSwapInterval(1); // Enable vsync

#if PLATFORM_EMSCRIPTEN
    // Prevent context menu on Ctrl+Click
    EM_ASM({
        const canvas = document.getElementById('canvas') || document.getElementsByTagName('canvas')[0];
        if (canvas) {
            canvas.addEventListener('contextmenu', function(e) {
                e.preventDefault();
            });
        }
    }, 0);
#endif

    return true;
}

zv::Padding ImguiCanvasContainer::decorationSize () const
{
    return impl->decorationSize;
}

void ImguiCanvasContainer::setSwapInterval (int interval)
{
#if PLATFORM_EMSCRIPTEN
#else
    glfwSwapInterval(interval);
#endif
}

void ImguiCanvasContainer::enableContexts ()
{
    ImGui::SetCurrentContext(impl->imGuiContext);
    glfwMakeContextCurrent(impl->window);
}

void ImguiCanvasContainer::disableContexts ()
{
    ImGui::SetCurrentContext(nullptr);
}

ImguiCanvasContainer::FrameInfo ImguiCanvasContainer::beginFrame ()
{
    enableContexts ();

    glfwGetFramebufferSize(impl->window, &(impl->currentFrameInfo.frameBufferWidth), &(impl->currentFrameInfo.frameBufferHeight));
    glfwGetWindowSize(impl->window, &(impl->currentFrameInfo.windowContentWidth), &(impl->currentFrameInfo.windowContentHeight));
    impl->currentFrameInfo.contentDpiScale = impl->contentDpiScale;

    glfwPollEvents();
    
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return impl->currentFrameInfo;
}

void ImguiCanvasContainer::endFrame ()
{
    // Rendering
    ImGui::Render();

    checkGLError ();
    
    glViewport(0, 0, impl->currentFrameInfo.frameBufferWidth, impl->currentFrameInfo.frameBufferHeight);
    glClearColor(0.1, 0.1, 0.1, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());   
    glfwSwapBuffers(impl->window);

    checkGLError ();

    // would be safer to call disableContexts now?
}

bool ImguiCanvasContainer::ImGuiBegin (const FrameInfo& frameInfo, ImGuiWindowFlags extraFlags)
{
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

    flags |= extraFlags;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(frameInfo.windowContentWidth, frameInfo.windowContentHeight), ImGuiCond_Always);
    return ImGui::Begin(impl->title.c_str(), nullptr, flags);
}

void ImguiCanvasContainer::endWindow ()
{
    ImGui::End();
}
} // zv
