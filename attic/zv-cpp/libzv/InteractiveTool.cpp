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

} // zv