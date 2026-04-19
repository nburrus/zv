//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/Image.h>

#include <array>
#include <cstdint>

namespace zv
{

struct ChannelStats
{
    uint8_t min = 255;
    uint8_t max = 0;
    double mean = 0.0;
    std::array<uint64_t, 256> histogram = {};
};

struct ImageColorStats
{
    ChannelStats r, g, b, a, luma;

    bool rgbChannelsEqual = false; // R==G==B within tolerance on a sample → grayscale-like
    bool alphaAllOpaque = false;
    bool hasAnyTransparency = false;
    bool singleColor = false;
    uint64_t pixelCount = 0;
};

ImageColorStats computeImageColorStats(const ImageSRGBA& image);

} // zv
