//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImageColorStats.h"

#include <cstdlib>
#include <cstring>

namespace zv
{

namespace
{

// sRGB luma coefficients (BT.709)
inline uint8_t lumaFromPixel(const PixelSRGBA& p)
{
    return static_cast<uint8_t>(0.2126 * p.r + 0.7152 * p.g + 0.0722 * p.b + 0.5);
}

void accumulate(ChannelStats& s, uint8_t v, uint64_t& sum)
{
    s.histogram[v]++;
    if (v < s.min) s.min = v;
    if (v > s.max) s.max = v;
    sum += v;
}

} // anonymous

ImageColorStats computeImageColorStats(const ImageSRGBA& image)
{
    ImageColorStats stats;

    const int w = image.width();
    const int h = image.height();
    stats.pixelCount = static_cast<uint64_t>(w) * h;

    if (stats.pixelCount == 0)
        return stats;

    uint64_t sumR = 0, sumG = 0, sumB = 0, sumA = 0, sumL = 0;

    for (int row = 0; row < h; ++row)
    {
        const PixelSRGBA* rowPtr = image.atRowPtr(row);
        for (int col = 0; col < w; ++col)
        {
            const PixelSRGBA& p = rowPtr[col];
            accumulate(stats.r, p.r, sumR);
            accumulate(stats.g, p.g, sumG);
            accumulate(stats.b, p.b, sumB);
            accumulate(stats.a, p.a, sumA);
            accumulate(stats.luma, lumaFromPixel(p), sumL);
        }
    }

    const double n = static_cast<double>(stats.pixelCount);
    stats.r.mean    = sumR / n;
    stats.g.mean    = sumG / n;
    stats.b.mean    = sumB / n;
    stats.a.mean    = sumA / n;
    stats.luma.mean = sumL / n;

    stats.alphaAllOpaque    = (stats.a.min == 255);
    stats.hasAnyTransparency = (stats.a.min < 255);

    // Single-color check: all histogram bins empty except one per channel
    stats.singleColor = (stats.r.min == stats.r.max &&
                         stats.g.min == stats.g.max &&
                         stats.b.min == stats.b.max);

    // Grayscale-like detection: sample up to ~10k pixels, tolerance ±2.
    // Alpha is excluded from the predicate.
    //
    // Stride over a flat pixel index so we get ~10k samples regardless of shape.
    // A separate row/col stride would undersample by its square.
    {
        constexpr uint64_t targetSamples = 10000;
        const uint64_t stride = std::max<uint64_t>(1, stats.pixelCount / targetSamples);
        bool allGray = true;
        for (uint64_t i = 0; i < stats.pixelCount && allGray; i += stride)
        {
            const int row = static_cast<int>(i / static_cast<uint64_t>(w));
            const int col = static_cast<int>(i % static_cast<uint64_t>(w));
            const PixelSRGBA& p = image.atRowPtr(row)[col];
            if (std::abs(int(p.r) - int(p.g)) > 2 ||
                std::abs(int(p.r) - int(p.b)) > 2)
                allGray = false;
        }
        stats.rgbChannelsEqual = allGray;
    }

    return stats;
}

} // zv
