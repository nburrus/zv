//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Modifiers.h"

namespace zv
{

void RotateImageModifier::apply (const ImageItemData& input, ImageItemData& output)
{
    const auto& inIm = (*input.cpuData);
    const int inW = inIm.width();
    const int inH = inIm.height();

    if (_angle == Angle::Angle_90) // Rotate Right
    {
        output.cpuData = std::make_shared<ImageSRGBA>(inH, inW);
        auto& outIm = *output.cpuData;
        const int outW = outIm.width();
        const int outH = outIm.height();
        for (int r = 0; r < outH; ++r)
        {
            PixelSRGBA* rowPtr = outIm.atRowPtr(r);
            for (int c = 0; c < outW; ++c)
            {
                const int rowInIn = inH-c-1;
                const int colInIn = r;
                rowPtr[c] = inIm(colInIn, rowInIn);
            }
        }
    }
    else if (_angle == Angle::Angle_270) // Rotate Left
    {
        output.cpuData = std::make_shared<ImageSRGBA>(inH, inW);
        auto& outIm = *output.cpuData;
        const int outW = outIm.width();
        const int outH = outIm.height();
        for (int r = 0; r < outH; ++r)
        {
            PixelSRGBA* rowPtr = outIm.atRowPtr(r);
            for (int c = 0; c < outW; ++c)
            {
                const int rowInIn = c;
                const int colInIn = inW-r-1;
                rowPtr[c] = inIm(colInIn, rowInIn);
            }
        }
    }
    else if (_angle == Angle::Angle_180) // Upside down
    {
        output.cpuData = std::make_shared<ImageSRGBA>(inW, inH);
        auto& outIm = *output.cpuData;
        for (int r = 0; r < inH; ++r)
        {
            PixelSRGBA* outRowPtr = outIm.atRowPtr(r);
            const PixelSRGBA* inRowPtr = inIm.atRowPtr(inH-r-1);
            for (int c = 0; c < inW; ++c)
            {
                outRowPtr[c] = inRowPtr[inW-c-1];
            }
        }
    }

    output.status = ImageItemData::Status::Ready;
}

} // zv