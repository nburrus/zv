//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImguiGLFWWindow.h>
#include <libzv/ImageViewerController.h>

#include <memory>
#include <functional>

namespace zv
{

class ImageViewerWindow;

struct ControlsWindowInputState
{
    bool shiftIsPressed = false;
};

class ImageViewerControlsWindow
{    
public:
    ImageViewerControlsWindow();
    ~ImageViewerControlsWindow();

public:
    const ControlsWindowInputState& inputState () const;

public:
    bool initialize (GLFWwindow* parentWindow, ImageViewerController* controller);
    void runOnce ();
    void repositionAfterNextRendering (const zv::Rect& viewerWindowGeometry, bool showRequested);

public:
    void shutdown ();
    void setEnabled (bool enabled);
    bool isEnabled () const;    
    void bringToFront ();
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};


} // zv
