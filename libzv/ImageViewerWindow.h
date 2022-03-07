//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageViewerController.h>

#include <libzv/MathUtils.h>
#include <libzv/Image.h>

#include <memory>
#include <functional>

struct GLFWwindow;

namespace zv
{

struct ImageViewerWindowState;
struct CursorOverlayInfo;

// Manages a single ImGuiWindow
class ImageViewerWindow
{
public:
    ImageViewerWindow();
    ~ImageViewerWindow();
    
public:   
    bool initialize (GLFWwindow* parentWindow, ImageViewerController* controller);
    
    void showImage (const ImageSRGBA& image, const std::string& imagePath, zv::Rect& updatedWindowGeometry);
    
    const CursorOverlayInfo& cursorOverlayInfo() const;
    
    void shutdown ();
    void runOnce ();
    
    bool isEnabled () const;
    void setEnabled (bool enabled);

    zv::Rect geometry () const;

    void processKeyEvent (int keycode);
    void checkImguiGlobalImageKeyEvents ();
    void checkImguiGlobalImageMouseEvents ();
    void saveCurrentImage ();

public:
    // State that one can modify directly between frames.
    ImageViewerWindowState& mutableState ();
    
private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
