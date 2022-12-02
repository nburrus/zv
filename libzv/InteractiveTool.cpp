//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "InteractiveTool.h"

namespace zv
{
    
void CropTool::renderAsActiveTool (const InteractiveToolRenderingContext& context)
{
    auto *drawList = ImGui::GetWindowDrawList();
    Rect textureRoi = _params.imageAlignedTextureRect(context.imageWidth, context.imageHeight);
    Rect widgetRoi = context.widgetToImageTransform.textureToWidget(textureRoi);

    ImGui::GetWindowDrawList()->AddRect(imVec2(widgetRoi.topLeft()),
                                        imVec2(widgetRoi.bottomRight()),
                                        IM_COL32(255, 215, 0, 255),
                                        0.0f /* rounding */,
                                        0 /* ImDrawFlags */,
                                        2.0f /* thickness */);

    if (context.firstValidImageIndex)
    {
        if (_controlPoints.empty())
        {
            for (int i = 0; i < _params.numControlPoints(); ++i)
            {
                _controlPoints.push_back(ControlPoint(_params.controlPointPos(i, textureRoi)));
            }
        }

        for (int i = 0; i < _params.numControlPoints(); ++i)
        {
            const auto widgetPos = context.widgetToImageTransform.textureToWidget(_params.controlPointPos(i, textureRoi));
            _controlPoints[i].update(widgetPos, [&](Point updatedWidgetPos) {
                Point updatedTexturePos = context.widgetToImageTransform.widgetToTexture(updatedWidgetPos);
                _params.updateControlPoint(i, updatedTexturePos, context.imageWidth, context.imageHeight); 
            });
        }

        for (const auto &cp : _controlPoints)
            cp.render();
    }
}

void CropTool::renderControls (const ImageSRGBA& firstIm)
{
    ImGui::Text("Cropping Tool");
    
    auto& textureRect = _params.textureRect;
    int leftInPixels = textureRect.origin.x * firstIm.width() + 0.5f;
    if (ImGui::SliderInt("Left", &leftInPixels, 0, firstIm.width()))
    {
        textureRect.origin.x = leftInPixels / float(firstIm.width());
    }
    
    int topInPixels = textureRect.origin.y * firstIm.height() + 0.5f;
    if (ImGui::SliderInt("Top", &topInPixels, 0, firstIm.height()))
    {
        textureRect.origin.y = topInPixels / float(firstIm.height());
    }
    
    int widthInPixels = textureRect.size.x * firstIm.width() + 0.5f;
    if (ImGui::SliderInt("Width", &widthInPixels, 0, firstIm.width()))
    {
        textureRect.size.x = widthInPixels / float(firstIm.width());
    }
    
    int heightInPixels = textureRect.size.y * firstIm.height() + 0.5f;
    if (ImGui::SliderInt("Height", &heightInPixels, 0, firstIm.height()))
    {
        textureRect.size.y = heightInPixels / float(firstIm.height());
    }
}

void LineTool::renderAsActiveTool (const InteractiveToolRenderingContext& context)
{
    auto *drawList = ImGui::GetWindowDrawList();
    Line textureLine = _params.imageAlignedTextureLine(context.imageWidth, context.imageHeight);
    Line widgetLine = context.widgetToImageTransform.textureToWidget(textureLine);

    // Need to take into account the current rendering size so the preview line
    // looks like the line applied at the image resolution.
    ImVec2 pixelScale = context.widgetToImageTransform.pixelScale (context.imageWidth, context.imageHeight);

    ImGui::GetWindowDrawList()->AddLine(imVec2(widgetLine.p1),
                                        imVec2(widgetLine.p2),
                                        _params.color,
                                        _params.lineWidth * pixelScale.x);

    if (context.firstValidImageIndex)
    {
        if (_controlPoints.empty())
        {
            for (int i = 0; i < _params.numControlPoints(); ++i)
            {
                _controlPoints.push_back(ControlPoint(_params.controlPointPos(i, textureLine)));
            }
        }

        for (int i = 0; i < _params.numControlPoints(); ++i)
        {
            const auto widgetPos = context.widgetToImageTransform.textureToWidget(_params.controlPointPos(i, textureLine));
            _controlPoints[i].update(widgetPos, [&](Point updatedWidgetPos) {
                Point updatedTexturePos = context.widgetToImageTransform.widgetToTexture(updatedWidgetPos);
                _params.updateControlPoint(i, updatedTexturePos, context.imageWidth, context.imageHeight); 
            });
        }

        for (const auto &cp : _controlPoints)
            cp.render();
    }
}

void LineTool::renderControls (const ImageSRGBA& firstIm)
{
    ImGui::Text("Add Line");

    auto& textureLine = _params.textureLine;

    ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoAlpha;
    ImGui::ColorEdit4("LineColor", (float*)&_params.color.Value, flags);
    
    ImGui::SliderInt("Line Width", &_params.lineWidth, 1, 10);

    
    int p1x = textureLine.p1.x * firstIm.width() + 0.5f;
    if (ImGui::SliderInt("Point 1 [x]", &p1x, 0, firstIm.width()))
    {
        textureLine.p1.x = p1x / float(firstIm.width());
    }

    int p1y = textureLine.p1.y * firstIm.height() + 0.5f;
    if (ImGui::SliderInt("Point 1 [y]", &p1y, 0, firstIm.height()))
    {
        textureLine.p1.y = p1y / float(firstIm.height());
    }

    int p2x = textureLine.p2.x * firstIm.width() + 0.5f;
    if (ImGui::SliderInt("Point 2 [x]", &p2x, 0, firstIm.width()))
    {
        textureLine.p2.x = p2x / float(firstIm.width());
    }

    int p2y = textureLine.p2.y * firstIm.height() + 0.5f;
    if (ImGui::SliderInt("Point 2 [y]", &p2y, 0, firstIm.height()))
    {
        textureLine.p2.y = p2y / float(firstIm.height());
    }
}

} // zv