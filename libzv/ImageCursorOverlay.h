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
#define IMGUI_DEFINE_MATH_OPERATORS 1
#include "imgui_internal.h"

namespace zv
{

struct CursorOverlayInfo
{
    bool valid() const { return itemAndData.data != nullptr; }
    
    void clear ()
    {
        itemAndData = {};
    }

    ImVec2 mousePosInImage() const
    {
        const auto& image = *itemAndData.data->cpuData;
        ImVec2 imageSize (image.width(), image.height());
        return mousePosInOriginalTexture() * imageSize;
    }

    ImVec2 mousePosInOriginalTexture() const
    {
        const auto& image = *itemAndData.data->cpuData;
        ImVec2 imageSize (image.width(), image.height());

        // This 0.5 offset is important since the mouse coordinate is an integer.
        // So when we are in the center of a pixel we'll return 0,0 instead of
        // 0.5,0.5.
        ImVec2 widgetPos = (mousePos + ImVec2(0.5f,0.5f)) - imageWidgetTopLeft;
        ImVec2 uv_window = widgetPos / imageWidgetSize;
        return (uvBottomRight-uvTopLeft)*uv_window + uvTopLeft;
    }

    ImageItemAndData itemAndData;
    bool showHelp = false;
    ImVec2 imageWidgetTopLeft;
    ImVec2 imageWidgetSize;
    ImVec2 uvTopLeft = ImVec2(0, 0);
    ImVec2 uvBottomRight = ImVec2(1, 1);
    ImVec2 roiWindowSize = ImVec2(15, 15);
    ImVec2 mousePos = ImVec2(0,0);
    // Might be zoomed in, not the same as mousePosInOriginalTexture()
    ImVec2 mousePosInTexture = ImVec2(0,0); // normalized to 0,1
    double timeOfLastCopyToClipboard = NAN;
};

class ImageCursorOverlay
{
public:
    void showTooltip(const CursorOverlayInfo& info, bool showAsTooltip = true);
};

} // zv
