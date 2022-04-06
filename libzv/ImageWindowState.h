//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/Modifiers.h>

namespace zv
{

enum class ViewerMode {
    None = -2,
    Original = -1,

    NumModes,
};

std::string viewerModeName (ViewerMode mode);

struct LayoutConfig
{
    int numImages() const { return numRows*numCols; }
    int numRows = 1;
    int numCols = 1;
    
    bool operator==(const LayoutConfig& rhs) const
    {
        return memcmp(this, &rhs, sizeof(LayoutConfig)) == 0;
    }
    bool operator!=(const LayoutConfig& rhs) const { return !(*this == rhs); }
};

struct ActiveToolState
{
    enum class Kind {
        None,
        Crop,
    };
    
    Kind kind = Kind::None;
    
    CropImageModifier::Params cropParams;
};

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
    
    ActiveToolState activeToolState;
    
    LayoutConfig layoutConfig;

    bool infoOverlayEnabled = true;

    double timeOfLastCopyToClipboard = NAN;
};

} // zv
