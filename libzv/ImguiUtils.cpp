//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImguiUtils.h"

#include <libzv/Utils.h>

#include "imgui_internal.h"

namespace zv
{

bool IsItemHovered(ImGuiHoveredFlags flags, float delaySeconds)
{
    return ImGui::IsItemHovered(flags) && ImGui::GetCurrentContext()->HoveredIdTimer > delaySeconds;
}

void ControlPoint::update (Point pos, const std::function<void(Point)>& onDragUpdate)
{
    _pos = pos;

    auto& io = ImGui::GetIO();
    const bool isLeftButtonDragged = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (_dragged)
    {
        if (isLeftButtonDragged)
        {
            onDragUpdate(toPoint(io.MousePos));
            return;
        }
        else
        {
            _dragged = false;
            return;
        }
    }

    if (isLeftButtonDragged && (toPoint(io.MouseClickedPos[0]) - _pos).length() < _radius*1.5)
    {
        _dragged = true;
        onDragUpdate (toPoint(io.MousePos));
    }
}

void ControlPoint::render () const
{
    ImGui::GetWindowDrawList()->AddCircleFilled(imVec2(_pos), _radius, IM_COL32(255,215,0,255));
}

} // zv
