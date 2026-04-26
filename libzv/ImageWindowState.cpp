//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImageWindowState.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace zv
{

LayoutConfig bestLayoutForImageCount(int numImages, int maxImages, float targetAspectRatio)
{
    LayoutConfig bestConfig;
    if (numImages <= 0 || maxImages <= 0)
        return bestConfig;

    const int cappedNumImages = std::min(numImages, maxImages);
    int bestWaste = std::numeric_limits<int>::max();
    float bestAspectError = std::numeric_limits<float>::infinity();

    for (int numRows = 1; numRows <= cappedNumImages; ++numRows)
    {
        for (int numCols = 1; numCols <= cappedNumImages; ++numCols)
        {
            const int layoutSize = numRows * numCols;
            if (layoutSize < cappedNumImages || layoutSize > maxImages)
                continue;

            const int waste = layoutSize - cappedNumImages;
            const float aspectRatio = static_cast<float>(numCols) / numRows;
            const float aspectError = std::abs(std::log(aspectRatio / targetAspectRatio));

            if (waste < bestWaste
                || (waste == bestWaste && aspectError < bestAspectError)
                || (waste == bestWaste && aspectError == bestAspectError
                    && layoutSize < bestConfig.numImages()))
            {
                bestWaste = waste;
                bestAspectError = aspectError;
                bestConfig.numRows = numRows;
                bestConfig.numCols = numCols;
            }
        }
    }

    return bestConfig;
}

LayoutConfig shortcutLayoutForImageCount(int numImages)
{
    switch (numImages)
    {
        case 1: return { 1, 1 };
        case 2: return { 1, 2 };
        case 3: return { 1, 3 };
        case 4: return { 2, 2 };
        case 5: return { 2, 3 };
        case 6: return { 2, 3 };
        case 7: return { 2, 4 };
        case 8: return { 2, 4 };
        case 9: return { 3, 3 };
        default: return bestLayoutForImageCount(numImages);
    }
}

std::string layoutLabel(const LayoutConfig& config)
{
    return std::to_string(config.numRows) + "x" + std::to_string(config.numCols);
}

const std::vector<LayoutMenuEntry>& layoutMenuEntries()
{
    static const std::vector<LayoutMenuEntry> entries = {
        { "Single image", "1", { 1, 1 } },
        { "2 columns", "2", { 1, 2 } },
        { "3 columns", "3", { 1, 3 } },
        { "2 rows", nullptr, { 2, 1 } },
        { "3 rows", nullptr, { 3, 1 } },
        { "2x2", "4", { 2, 2 } },
        { "2x3", "5/6", { 2, 3 } },
        { "3x2", nullptr, { 3, 2 } },
        { "2x4", "7/8", { 2, 4 } },
        { "4x2", nullptr, { 4, 2 } },
        { "3x3", "9", { 3, 3 } },
        { "3x4", nullptr, { 3, 4 } },
        { "4x3", nullptr, { 4, 3 } },
    };

    return entries;
}
   
InteractiveTool* ActiveToolState::activeTool ()
{
    switch (kind)
    {
        case Kind::None: return nullptr;
        case Kind::Annotate: return &annotationTool;
        case Kind::Transform_Crop: return &cropTool;
    }
    return nullptr;
}

} // zv
