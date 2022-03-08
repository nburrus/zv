//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/MathUtils.h>
#include <libzv/Image.h>

#include <memory>
#include <functional>

struct GLFWwindow;

namespace zv
{

class Viewer;
struct ImageWindowState;
struct CursorOverlayInfo;

// Manages a single ImGuiWindow
class ImageWindow
{
public:
    ImageWindow();
    ~ImageWindow();
    
public:   
    bool initialize (GLFWwindow* parentWindow, Viewer* controller);
    GLFWwindow* glfwWindow ();
        
    const CursorOverlayInfo& cursorOverlayInfo() const;
    
    void shutdown ();
    void renderFrame ();
    
    bool isEnabled () const;
    void setEnabled (bool enabled);
    
    void setLayout (int numImages, int numRows, int numCols);

    zv::Rect geometry () const;

    void processKeyEvent (int keycode);
    void checkImguiGlobalImageKeyEvents ();
    void checkImguiGlobalImageMouseEvents ();
    void saveCurrentImage ();

public:
    // State that one can modify directly between frames.
    ImageWindowState& mutableState ();
    
private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
