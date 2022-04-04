//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/MathUtils.h>
#include <libzv/Image.h>
#include <libzv/ImageWindowActions.h>
#include <libzv/Modifiers.h>

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
    struct Command
    {
        using ExecFunc = std::function<void(ImageWindow &)>;

        Command(const ExecFunc &f) : execFunc(f) {}
        Command() {}

        // Prevent copies as it should not be required.
        Command(Command&&) = default;

        ExecFunc execFunc = nullptr;
        // Could add undo later on.
    };

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
    
    // Force a move semantic to avoid copies of the embedded std::function.
    void addCommand (Command&& command);

    zv::Rect geometry () const;

    void processKeyEvent (int keycode);
    void checkImguiGlobalImageKeyEvents ();
    void checkImguiGlobalImageMouseEvents ();
    void saveCurrentImage ();
    void discardAllChanges ();

    ModifiedImagePtr getFirstModifiedImage();

public:
    static Command actionCommand (ImageWindowAction action);
    static Command layoutCommand(int nrows, int ncols);

public:
    // State that one can modify directly between frames.
    ImageWindowState& mutableState ();
    
private:
    // Public accessors can use actionCommand
    void runAction (ImageWindowAction action);

private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
