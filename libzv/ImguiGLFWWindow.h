//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include "ImguiWindowContainer.h"

#include <libzv/MathUtils.h>

#include <memory>
#include <functional>
#include <string>

struct GLFWwindow;

struct ImGuiIO;

typedef int ImGuiWindowFlags;

namespace zv
{

struct Rect;

/**
 * GLFW-backed window with its own ImGui and GL context.
 */
class ImguiGLFWWindow : public ImguiWindowContainer
{
public:
    ImguiGLFWWindow();
    ~ImguiGLFWWindow();
    
public:
    bool initialize (GLFWwindow* parentWindow,
                     const std::string& title,
                     const zv::Rect& geometry,
                     bool enableImguiViewports = false);
    bool isInitialized () const override;
    void shutdown () override;

    void setEnabled (bool enabled) override;
    bool isEnabled () const override;
    
    void setWindowTitle (const std::string& title) override;
    void setWindowPos (int x, int y) override;
    void setWindowSize (int width, int height) override;    
    
    void onWindowSizeChanged (int width, int height) override;
    using WindowSizeChangedCb = std::function<void(int,int,bool /* from user interaction */)>;
    void setWindowSizeChangedCallback (WindowSizeChangedCb&& callback) override;

    zv::Padding decorationSize () const override;

public:
    bool closeRequested () const override;
    void cancelCloseRequest () override;
    void triggerCloseRequest () override;

    zv::Rect geometry() const override;

public:
    FrameInfo beginFrame () override;
    void endFrame () override;

    bool ImGuiBegin (const FrameInfo& frameInfo, bool* p_open, ImGuiWindowFlags extraFlags) override;

    void enableContexts () override;
    void disableContexts () override;

    void setSwapInterval (int interval) override;

    GLFWwindow* native_glfwWindow () override;

    void focus () override;
    void setResizable (bool resizable) override;
    void bringToFront () override;

private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
    friend class ImGuiScopedContext;
};

} // zv
