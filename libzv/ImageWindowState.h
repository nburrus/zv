//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

namespace zv
{

enum class ViewerMode {
    None = -2,
    Original = -1,

    NumModes,
};

std::string viewerModeName (ViewerMode mode);

struct ImageWindowState
{
    ViewerMode activeMode = ViewerMode::None;
    
    // modeForCurrentFrame can be different from activeMode
    // if the user presses the SHIFT key.
    ViewerMode modeForCurrentFrame = ViewerMode::None;    

    struct InputState
    {
        bool shiftIsPressed = false;
    };

    InputState controlsInputState;
    InputState inputState;
};

} // zv