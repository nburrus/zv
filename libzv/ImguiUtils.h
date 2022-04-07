//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/MathUtils.h>
#include <FontIcomoon.h>

#include <imgui.h>

namespace zv
{

inline ImVec2 imVec2 (zv::Point p) { return ImVec2(p.x, p.y); }
inline ImVec2 imPos (zv::Rect& r) { return imVec2(r.origin); }
inline ImVec2 imSize (zv::Rect& r) { return imVec2(r.size); }
inline Point toPoint (ImVec2 v) { return Point(v.x, v.y); }

// Helper to display a little (?) mark which shows a tooltip when hovered.
// In your own code you may want to display an actual icon if you are using a merged icon fonts (see docs/FONTS.md)
inline void helpMarker(const char* desc, float wrapWidth)
{
    ImGui::Text(ICON_QUESTION); // FIXME: add question circle
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(wrapWidth);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

inline ImGuiWindowFlags windowFlagsWithoutAnything()
{
    return (ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoCollapse
            // | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_HorizontalScrollbar
            // | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoNav);
}

bool IsItemHovered(ImGuiHoveredFlags flags, float delaySeconds);

struct ControlPoint
{
public:
    ControlPoint (Point pos)
        : _pos (pos)
    {}

    void update (Point pos, const std::function<void(Point)>& onDragUpdate);
    void render () const;

private:
    // Widget pos.
    bool _dragged = false;
    Point _pos;
    const float _radius = 5.f;
};

} // zv
