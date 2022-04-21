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
inline void helpMarker(const char* desc, float wrapWidth, bool add_question_mark = true)
{
    if (add_question_mark)
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

struct ImageWidgetRoi
{
    ImVec2 uv0;
    ImVec2 uv1;
};

// Takes into account the zoom level.
struct WidgetToImageTransform
{
    WidgetToImageTransform () = default;

    WidgetToImageTransform (const ImageWidgetRoi& uvRoi, const Rect& widgetRect)
    : uvRoi (uvRoi), widgetRect(widgetRect)
    {}
    
    Rect textureToWidget(const Rect& textureRoi) const
    {
        Rect widgetRoi;
        widgetRoi.origin = textureToWidget(textureRoi.origin);
        Point bottomRight = textureToWidget(textureRoi.bottomRight());
        widgetRoi.size = bottomRight - widgetRoi.origin;
        return widgetRoi;
    }

    Line textureToWidget(const Line& textureLine) const
    {
        return Line(textureToWidget(textureLine.p1), textureToWidget(textureLine.p2));        
    }
    
    // texturePos means normalized image coordinates ([0,1])
    // The zoom level change uv0 (topLeft) and uv1 (bottomRight)
    // of the input image texture.
    Point textureToWidget (Point texturePos) const
    {
        Point uvRoiPos;
        // First go to the uvRoi coordinate space.
        {
            double sx = (uvRoi.uv1.x - uvRoi.uv0.x);
            double sy = (uvRoi.uv1.y - uvRoi.uv0.y);
            uvRoiPos.x = (texturePos.x - uvRoi.uv0.x)/sx;
            uvRoiPos.y = (texturePos.y - uvRoi.uv0.y)/sy;
        }
        
        // Now go to the widget space.
        Point widgetPos;
        {
            double sx = widgetRect.size.x;
            double sy = widgetRect.size.y;
            widgetPos.x = uvRoiPos.x*sx + widgetRect.origin.x;
            widgetPos.y = uvRoiPos.y*sy + widgetRect.origin.y;
        }
        return widgetPos;
    }
    
    // Inverse transform.
    Point widgetToTexture (Point widgetPos) const
    {
        // First go to the uvRoi coordinate space
        Point uvRoiPos;
        {
            double sx = widgetRect.size.x;
            double sy = widgetRect.size.y;
            uvRoiPos.x = (widgetPos.x - widgetRect.origin.x) / sx;
            uvRoiPos.y = (widgetPos.y - widgetRect.origin.y) / sy;
        }
        
        // Now to the texture space.
        Point texturePos;
        {
            double sx = (uvRoi.uv1.x - uvRoi.uv0.x);
            double sy = (uvRoi.uv1.y - uvRoi.uv0.y);
            texturePos.x = uvRoiPos.x*sx + uvRoi.uv0.x;
            texturePos.y = uvRoiPos.y*sy + uvRoi.uv0.y;
        }
        
        return texturePos;
    }
    
    ImageWidgetRoi uvRoi;
    Rect widgetRect;
};

} // zv
