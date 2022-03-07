//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImguiGLFWWindow.h>

#include <libzv/ImageList.h>
#include <libzv/OpenGL.h>
#include <libzv/Image.h>
#include <libzv/MathUtils.h>

#include "imgui.h"

namespace zv
{

struct CursorOverlayInfo
{
    bool valid() const { return imageData != nullptr; }
    
    const ImageItemData* imageData = nullptr;
    bool showHelp = false;
    ImVec2 imageWidgetTopLeft;
    ImVec2 imageWidgetSize;
    ImVec2 uvTopLeft = ImVec2(0, 0);
    ImVec2 uvBottomRight = ImVec2(1, 1);
    ImVec2 roiWindowSize = ImVec2(15, 15);
    ImVec2 mousePos = ImVec2(0,0);
};

class ImageCursorOverlay
{
public:
    void showTooltip(const CursorOverlayInfo& info, bool showAsTooltip = true);

private:
    double _timeOfLastCopyToClipboard = NAN;
};

} // zv
