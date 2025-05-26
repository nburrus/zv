//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/Platform.h>
#include <libzv/MathUtils.h>

#include <imgui.h>

#include <memory>
#include <functional>
#include <string>

// Temporarily use canvas everywhere.
#if true || PLATFORM_EMSCRIPTEN
#define ZV_IMGUI_WINDOW_CONTAINER_TYPE_CANVAS 1
#else
#define ZV_IMGUI_WINDOW_CONTAINER_TYPE_CANVAS 0
#endif

struct GLFWwindow;

struct ImGuiIO;

typedef int ImGuiWindowFlags;

namespace zv
{

struct Rect;

struct FrameInfo
{
    Point containerOrigin = Point(0,0);
    Padding containerDecorationSize = zv::Padding{0,0,0,0};
    int containerWidth = -1;
    int containerHeight = -1;
    int frameBufferWidth = -1;
    int frameBufferHeight = -1;
    float contentDpiScale = 1.f;
};

// ImguiWindowContainer provides an abstraction layer for managing ImGui windows across different platforms.
// It handles two main scenarios:
// 1. Native desktop: Each ImGui window can be embedded in its own GLFW window (viewport)
// 2. Web/Emscripten: All ImGui windows share a single canvas
//
// The container implementation varies based on the platform:
// - Desktop: Uses a maximized, title-less ImGui window within an OS window viewport
// - Web: Uses regular ImGui windows within a shared canvas viewport
//
// In addition to the content "container" abstracting how to get the ImGui window, the notion of "canvas"
// is used to abstract the notion of the "canvas" (web) or "monitor" (desktop) that the ImGui windows are
// rendered into. The canvas is the thing where containers can be created and displayed.
//
// The notion of "viewport" still exists for IO events. io.MousePos is expected to be in the viewport
// coordinates: the GLFW window on desktop and the canvas on the web.
class ImguiWindowContainer
{
public:
    ImguiWindowContainer() {}
    virtual ~ImguiWindowContainer() {}
    
public:
    virtual bool isInitialized () const = 0;
    virtual void shutdown () = 0;

    virtual void setEnabled (bool enabled) = 0;
    virtual bool isEnabled () const = 0;
    
    // Set the container properties etc. in the canvas
    virtual void setContainerTitle (const std::string& title) = 0;
    virtual void setContainerPos (int x, int y) = 0;
    virtual void setContainerSize (int width, int height) = 0;    
    
    virtual void onContainerSizeChanged (int width, int height) = 0;
    using ContainerSizeChangedCb = std::function<void(int,int,bool /* from user interaction */)>;
    virtual void setContainerSizeChangedCallback (ContainerSizeChangedCb&& callback) = 0;

    virtual zv::Rect canvasArea () const = 0;
    virtual zv::Point canvasSize () const = 0;
    virtual zv::Padding canvasDecorationSize () const = 0;

    virtual ImVec2 imguiWindowToDrawList (const FrameInfo& frameInfo, const ImVec2& p) const = 0;
    virtual ImVec2 drawListToImguiWindow (const FrameInfo& frameInfo, const ImVec2& p) const = 0;
    
    virtual ImVec2 viewportToImguiWindow (const FrameInfo& frameInfo, const ImVec2& p) const = 0;

public:
    virtual bool closeRequested () const = 0;
    virtual void cancelCloseRequest () = 0;
    virtual void triggerCloseRequest () = 0;

    virtual zv::Rect containerGeometry() const = 0;

public:
    virtual FrameInfo beginFrame () = 0;
    virtual void endFrame () = 0;

    virtual bool ImGuiBegin (const FrameInfo& frameInfo, bool* p_open, ImGuiWindowFlags extraFlags) = 0;
    virtual void ImGuiEnd () = 0;

    virtual void enableContexts () = 0;
    virtual void disableContexts () = 0;

    virtual void setSwapInterval (int interval) = 0;

    virtual void bringToFront () = 0;
    virtual void focus () = 0;
    virtual void setResizable (bool resizable) = 0;

    virtual GLFWwindow* native_glfwWindow () = 0;    
};

} // zv
