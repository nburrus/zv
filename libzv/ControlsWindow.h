//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImguiGLFWWindow.h>

#include <memory>
#include <functional>

namespace zv
{

class ImageWindow;
class Viewer;

struct ControlsWindowInputState
{
    bool shiftIsPressed = false;
};

class ControlsWindow
{    
public:
    ControlsWindow();
    ~ControlsWindow();

public:
    const ControlsWindowInputState& inputState () const;

public:
    bool initialize (GLFWwindow* parentWindow, Viewer* viewer);
    bool isInitialized () const;
    void renderFrame ();
    void repositionAfterNextRendering (const zv::Rect& viewerWindowGeometry, bool showRequested);

public:
    void shutdown ();
    void setEnabled (bool enabled);
    bool isEnabled () const;    
    void bringToFront ();
    
    void openImage ();
    void saveAllChanges ();
    void confirmPendingChanges ();
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};


} // zv
