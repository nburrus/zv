//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/Platform.h>
#include <libzv/MathUtils.h>

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

// Each ImGui window can be embedded into its own glfwWindow
// or all in the same canvas with emscripten. This class abstracts
// away this notion of ImGui window container.
class ImguiWindowContainer
{
public:
    struct FrameInfo
    {
        int windowContentWidth = -1;
        int windowContentHeight = -1;
        int frameBufferWidth = -1;
        int frameBufferHeight = -1;
        float contentDpiScale = 1.f;
    };

public:
    ImguiWindowContainer() {}
    virtual ~ImguiWindowContainer() {}
    
public:
    virtual bool isInitialized () const = 0;
    virtual void shutdown () = 0;

    virtual void setEnabled (bool enabled) = 0;
    virtual bool isEnabled () const = 0;
    
    virtual void setWindowTitle (const std::string& title) = 0;
    virtual void setWindowPos (int x, int y) = 0;
    virtual void setWindowSize (int width, int height) = 0;    
    
    virtual void onWindowSizeChanged (int width, int height) = 0;
    using WindowSizeChangedCb = std::function<void(int,int,bool /* from user interaction */)>;
    virtual void setWindowSizeChangedCallback (WindowSizeChangedCb&& callback) = 0;

    virtual zv::Padding decorationSize () const = 0;
    

public:
    virtual bool closeRequested () const = 0;
    virtual void cancelCloseRequest () = 0;
    virtual void triggerCloseRequest () = 0;

    virtual zv::Rect geometry() const = 0;

public:
    virtual FrameInfo beginFrame () = 0;
    virtual void endFrame () = 0;

    virtual bool ImGuiBegin (const FrameInfo& frameInfo, bool* p_open, ImGuiWindowFlags extraFlags) = 0;

    virtual void enableContexts () = 0;
    virtual void disableContexts () = 0;

    virtual void setSwapInterval (int interval) = 0;

    virtual void bringToFront () = 0;
    virtual void focus () = 0;
    virtual void setResizable (bool resizable) = 0;

    virtual GLFWwindow* native_glfwWindow () = 0;    
};

} // zv
