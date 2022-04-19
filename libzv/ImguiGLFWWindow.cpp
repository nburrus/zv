//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImguiGLFWWindow.h"

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

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

#include "GLFWUtils.h"

#include <cstdio>
#include <unordered_set>

namespace zv
{
struct ImguiGLFWWindow::Impl
{
    ImGuiContext* imGuiContext = nullptr;

    GLFWwindow* window = nullptr;
    bool enabled = false;

    ImguiGLFWWindow::FrameInfo currentFrameInfo;
    
    zv::Point posToSetForNextShow;
    
    std::string title;

    float contentDpiScale = 1.f;

    ImguiGLFWWindow::WindowSizeChangedCb windowSizeChangedCb;
    int lastSizeRequest;
};

class ImGuiScopedContext
{
public:
    ImGuiScopedContext (GLFWwindow* w)
    {
        void* ptr = glfwGetWindowUserPointer(w);
        _imguiGLFWWindow = reinterpret_cast<ImguiGLFWWindow*>(ptr);
        initialize (_imguiGLFWWindow->impl->imGuiContext);
    }
    
    ImGuiScopedContext (ImGuiContext* context)
    {
        initialize (context);
    }
    
    void initialize (ImGuiContext* context)
    {
        prevContext = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext (context);
    }
    
    ~ImGuiScopedContext ()
    {
        ImGui::SetCurrentContext(prevContext);
    }

    ImguiGLFWWindow* imguiGLFWWindow() const { return _imguiGLFWWindow; }
    
private:
    ImGuiContext* prevContext;
    ImguiGLFWWindow* _imguiGLFWWindow;
};

// Singleton class to keep track of all the contexts.
class ImGuiContextTracker
{
public:
    static ImGuiContextTracker* instance() { static ImGuiContextTracker v; return &v; }
    
public:
    void addContext (ImGuiContext* context) { _contexts.insert (context); }
    void removeContext (ImGuiContext* context) { _contexts.erase (context); }
    const std::unordered_set<ImGuiContext*>& contexts () const { return _contexts; }
    
private:
    ImGuiContextTracker () = default;
    
private:
    std::unordered_set<ImGuiContext*> _contexts;
};

namespace {
// Forward all the events to the imgui backend, but first making sure that the right ImGui context is set.
void zv_glfw_WindowFocusCallback(GLFWwindow* w, int focused) { ImGuiScopedContext _ (w); ImGui_ImplGlfw_WindowFocusCallback (w, focused); }
void zv_glfw_CursorEnterCallback(GLFWwindow* w, int entered) { ImGuiScopedContext _ (w); ImGui_ImplGlfw_CursorEnterCallback (w, entered); }
void zv_glfw_MouseButtonCallback(GLFWwindow* w, int button, int action, int mods) { ImGuiScopedContext _ (w); ImGui_ImplGlfw_MouseButtonCallback (w, button, action, mods); }
void zv_glfw_CursorPosCallback(GLFWwindow* w, double x, double y) { ImGuiScopedContext _ (w); ImGui_ImplGlfw_CursorPosCallback (w, x, y); }

void zv_glfw_ScrollCallback(GLFWwindow* w, double xoffset, double yoffset) { ImGuiScopedContext _ (w); ImGui_ImplGlfw_ScrollCallback (w, xoffset, yoffset); }
void zv_glfw_KeyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) { ImGuiScopedContext _ (w); ImGui_ImplGlfw_KeyCallback (w, key, scancode, action, mods); }
void zv_glfw_CharCallback(GLFWwindow* w, unsigned int c) { ImGuiScopedContext _ (w); ImGui_ImplGlfw_CharCallback (w, c); }

// We need to make sure that all the ImGui contexts will get the monitor update info
void zv_glfw_MonitorCallback(GLFWmonitor* m, int event)
{
    ImGuiContextTracker* tracker = ImGuiContextTracker::instance();
    ImGuiContext* prevContext = ImGui::GetCurrentContext();
    for (auto* context : tracker->contexts())
    {
        ImGui::SetCurrentContext(context);
        ImGui_ImplGlfw_MonitorCallback (m, event);
    }
    ImGui::SetCurrentContext(prevContext);
}

void zv_glfw_WindowSizeCallback(GLFWwindow* w, int width, int height) 
{ 
    ImGuiScopedContext ctx (w);
    ctx.imguiGLFWWindow()->onWindowSizeChanged (width, height);
}

} // anonymous

ImguiGLFWWindow::ImguiGLFWWindow()
: impl (new Impl())
{}

ImguiGLFWWindow::~ImguiGLFWWindow()
{
    shutdown();
}

GLFWwindow* ImguiGLFWWindow::glfwWindow ()
{
    return impl->window;
}

bool ImguiGLFWWindow::isEnabled () const
{
    return impl->enabled;
}

void ImguiGLFWWindow::setEnabled (bool enabled)
{
    if (impl->enabled == enabled)
        return;
    
    impl->enabled = enabled;
    if (impl->enabled)
    {
        glfwSetWindowShouldClose(impl->window, false);
        glfwShowWindow(impl->window);
        
        // This seems necessary on Linux to avoid random issues with the window not getting focus.
        glfw_reliableBringToFront(impl->window);

        // Save the window position as the next show will put it anywhere on Linux :(
        if (impl->posToSetForNextShow.isValid())
        {
            glfwSetWindowPos (impl->window, impl->posToSetForNextShow.x, impl->posToSetForNextShow.y);
            impl->posToSetForNextShow = {};
        }
    }
    else
    {
        // Save the position before the hide.
        int x, y;
        glfwGetWindowPos(impl->window, &x, &y);
        impl->posToSetForNextShow.x = x;
        impl->posToSetForNextShow.y = y;
        glfwSetWindowShouldClose(impl->window, false);
        glfwHideWindow(impl->window);
    }
}

bool ImguiGLFWWindow::closeRequested () const
{
    return glfwWindowShouldClose (impl->window);
}

void ImguiGLFWWindow::cancelCloseRequest ()
{
    glfwSetWindowShouldClose (impl->window, false);
}

void ImguiGLFWWindow::triggerCloseRequest ()
{
    glfwSetWindowShouldClose (impl->window, true);
}

void ImguiGLFWWindow::onWindowSizeChanged (int width, int height)
{
    if (!impl->windowSizeChangedCb)
        return;

    // Leave two frames of delay before concluding that the size changed
    // indeed came from the user and not from our own call to setWindowSize.
    const int fc = ImGui::GetFrameCount();    
    bool fromUser = ((fc - impl->lastSizeRequest) > 2);
    impl->windowSizeChangedCb (width, height, fromUser);
}

void ImguiGLFWWindow::setWindowSizeChangedCallback (WindowSizeChangedCb&& callback)
{
    impl->windowSizeChangedCb = callback;
}

void ImguiGLFWWindow::setWindowPos (int x, int y)
{
    glfwSetWindowPos (impl->window, x, y);
}

void ImguiGLFWWindow::setWindowSize (int width, int height)
{
    impl->lastSizeRequest = ImGui::GetFrameCount();
    glfwSetWindowSize (impl->window, width, height);
}

zv::Rect ImguiGLFWWindow::geometry() const
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

zv::Point ImguiGLFWWindow::primaryMonitorContentDpiScale ()
{
    float dpiScale_x = 1.f, dpiScale_y = 1.f;

    // On macOS, content scaling will be done automatically. Instead the
    // framebuffers will get resized.
#if !PLATFORM_MACOS
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    glfwGetMonitorContentScale(monitor, &dpiScale_x, &dpiScale_y);
#endif

    return zv::Point(dpiScale_x, dpiScale_y);
}

zv::Point ImguiGLFWWindow::primaryMonitorRetinaFrameBufferScale ()
{
    float dpiScale_x = 1.f, dpiScale_y = 1.f;

    // This framebuffer scaling only happens on macOS.
#if PLATFORM_MACOS
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    glfwGetMonitorContentScale(monitor, &dpiScale_x, &dpiScale_y);
#endif

    return zv::Point(dpiScale_x, dpiScale_y);
}

void ImguiGLFWWindow::shutdown()
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

void ImguiGLFWWindow::PushMonoSpaceFont (const ImGuiIO& io, bool small)
{
    ImGui::PushFont(io.Fonts->Fonts[small ? 2 : 1]); 
}

float ImguiGLFWWindow::monoFontSize (const ImGuiIO& io)
{
    return io.Fonts->Fonts[1]->FontSize * io.Fonts->Fonts[1]->Scale;
}

bool ImguiGLFWWindow::isInitialized () const
{
    return impl->window != nullptr;
}

bool ImguiGLFWWindow::initialize (GLFWwindow* parentWindow,
                                  const std::string& title,
                                  const zv::Rect& geometry,
                                  bool enableImguiViewports)
{
    glfwSetErrorCallback (glfwErrorFunction);
    
    impl->title = title;
    impl->contentDpiScale = primaryMonitorContentDpiScale().x;

    // Always start invisible, we'll show it later when we need to.
    glfwWindowHint(GLFW_VISIBLE, false);
    impl->window = glfwCreateWindow(geometry.size.x, geometry.size.y, title.c_str(), NULL, parentWindow);
    glfwWindowHint(GLFW_VISIBLE, true);
    if (impl->window == NULL)
        return false;

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
    
    // Make sure that gl3w is initialized.
    bool err = gl3wInit() != 0;
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return false;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    impl->imGuiContext = ImGui::CreateContext(); // FIXME: use a shared font atlas.
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
        const zv::Point dpiScale = ImguiGLFWWindow::primaryMonitorContentDpiScale();

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
        const zv::Point retinaScaleFactor = primaryMonitorRetinaFrameBufferScale();

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
    ImGui_ImplOpenGL3_Init(glslVersion());
    
    
    // Important: do this only after creating the ImGuiContext. Otherwise we might
    // get some callbacks right away and get in trouble.
    // Start hidden. setEnabled will show it as needed.
    glfwSwapInterval(1); // Enable vsync

    return true;
}

static void AddUnderLine( ImColor col_ )
{
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    min.y = max.y;
    ImGui::GetWindowDrawList()->AddLine( min, max, col_, 1.0f );
}

// From https://gist.github.com/dougbinks/ef0962ef6ebe2cadae76c4e9f0586c69#file-imguiutils-h-L228-L262
static void TextURL( const char* name_, const char* URL_, bool SameLineBefore_, bool SameLineAfter_ )
{
    if( SameLineBefore_ ){ ImGui::SameLine( 0.0f, ImGui::GetStyle().ItemInnerSpacing.x ); }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
    ImGui::Text("%s", name_);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
    {
        if( ImGui::IsMouseClicked(0) )
        {
            zv::openURLInBrowser( URL_ );
        }
        AddUnderLine( ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered] );
        // ImGui::SetTooltip( ICON_FA_LINK "  Open in browser\n%s", URL_ );
    }
    else
    {
        AddUnderLine( ImGui::GetStyle().Colors[ImGuiCol_Button] );
    }
    if( SameLineAfter_ ){ ImGui::SameLine( 0.0f, ImGui::GetStyle().ItemInnerSpacing.x ); }
}

void ImguiGLFWWindow::enableContexts ()
{
    ImGui::SetCurrentContext(impl->imGuiContext);
    glfwMakeContextCurrent(impl->window);
}

void ImguiGLFWWindow::disableContexts ()
{
    ImGui::SetCurrentContext(nullptr);
}

ImguiGLFWWindow::FrameInfo ImguiGLFWWindow::beginFrame ()
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

void ImguiGLFWWindow::endFrame ()
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

} // zv
