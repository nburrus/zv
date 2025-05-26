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

struct ImGuiContext;

namespace zv
{

struct Rect;

// Represent a global canvas for all the windows, typically used by emscripten.
class ImguiCanvas
{
public:
    ImguiCanvas();
    ~ImguiCanvas();

    void initialize ();
    void onCanvasSizeChanged (int width, int height);
    ImGuiContext* imGuiContext();
    void beginFrame ();
    void endFrame ();

    // Method to handle canvas resize from JavaScript
    void handleResize  (int width, int height);
    zv::Point canvasSize () const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/**
 * Virtual container that will just create an ImGui window with its title bar, etc. 
 */
class ImguiCanvasWindow : public ImguiWindowContainer
{
public:
    ImguiCanvasWindow();
    ~ImguiCanvasWindow();
    
public:
    bool initialize (const std::string& title,
                     const zv::Rect& geometry);
    bool isInitialized () const override;
    void shutdown () override;

    void setEnabled (bool enabled) override;
    bool isEnabled () const override;
    
    void setContainerTitle (const std::string& title) override;
    void setContainerPos (int x, int y) override;
    void setContainerContentSize (int width, int height) override;    
    
    void onContainerSizeChanged (int width, int height) override;
    using WindowSizeChangedCb = std::function<void(int,int,bool /* from user interaction */)>;
    void setContainerSizeChangedCallback (WindowSizeChangedCb&& callback) override;

    zv::Rect canvasArea () const override;
    zv::Point canvasSize () const override;
    zv::Padding canvasDecorationSize () const override;

    ImVec2 imguiWindowToDrawList (const FrameInfo& frameInfo, const ImVec2& p) const override;
    ImVec2 drawListToImguiWindow (const FrameInfo& frameInfo, const ImVec2& p) const override;
    ImVec2 viewportToImguiWindow (const FrameInfo& frameInfo, const ImVec2& p) const override;

public:
    bool closeRequested () const override;
    void cancelCloseRequest () override;
    void triggerCloseRequest () override;

    zv::Rect containerGeometry() const override;

public:
    FrameInfo beginFrame () override;
    void endFrame () override;

    bool ImGuiBegin (const FrameInfo& frameInfo, bool* p_open, ImGuiWindowFlags extraFlags) override;
    void ImGuiEnd () override;

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
};

} // zv
