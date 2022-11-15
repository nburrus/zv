//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImguiGLFWWindow.h>
#include <libzv/ImageWindowActions.h>

#include <memory>
#include <functional>
#include <string>

namespace zv
{

class ImageWindow;
class Viewer;

struct ControlsWindowInputState
{
    bool shiftIsPressed = false;
};

struct ActionToConfirm
{
    std::string title;
    std::function<bool(Confirmation&)>  renderDialog;
    std::function<void(void)> onOk;
    std::function<void(void)> onCancelled;
    std::function<void(void)> onDiscard;

    bool isActive() const { return !title.empty(); }
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
    void saveAllChanges (bool forcePathSelectionOnSave);
    void confirmPendingChanges ();
    void setCurrentActionToConfirm (const ActionToConfirm& actionToConfirm);
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};


} // zv
