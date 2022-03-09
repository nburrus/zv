//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include "Image.h"
#include "MathUtils.h"

#include <array>
#include <cstdint>

namespace zv
{
    
    class RGBAToLMSConverter
    {
    public:
        RGBAToLMSConverter ();
        
        void convertToLms (const ImageLinearRGB& rgbImage, ImageLMS& lmsImage);
        void convertToLinearRGB (const ImageLMS& lmsImage, ImageLinearRGB& rgbImage);
        
    private:
        ColMajorMatrix3f _linearRgbToLmsMatrix;
        ColMajorMatrix3f _lmsToLinearRgbMatrix;
    };

    ImageSRGBA convertToSRGBA(const ImageLinearRGB& rgb);
    ImageLinearRGB convertToLinearRGB(const ImageSRGBA& srgb);
    
    PixelYCbCr convertToYCbCr(const PixelSRGBA& p);
    
    // LinearRGB
    PixelSRGBA convertToSRGBA(const PixelLinearRGB& rgb);
    PixelLinearRGB convertToLinearRGB(const PixelSRGBA& srgb);

    // HSV
    PixelHSV convertToHSV(const PixelSRGBA& p);
    PixelSRGBA convertToSRGBA(const PixelHSV& p);

    // XYZ
    PixelXYZ convertToXYZ(const PixelSRGBA& srgb);
    PixelSRGBA convertToSRGBA(const PixelXYZ& xyz);

    // CIE Lab
    PixelLab convertToLab(const PixelSRGBA& p);
    PixelSRGBA convertToSRGBA(const PixelLab& p);

    ImageSRGBA srgbaFromSrgb (uint8_t* rgb_buffer, int width, int height, int bytesPerRow);
    ImageSRGBA srgbaFromGray (uint8_t* rgb_buffer, int width, int height, int bytesPerRow);
    
    ImageSRGBA srgbaFromFloatSrgb (uint8_t* srgba_buffer, int width, int height, int bytesPerRow);
    ImageSRGBA srgbaFromFloatSrgba (uint8_t* srgba_buffer, int width, int height, int bytesPerRow);
    ImageSRGBA srgbaFromFloatGray (uint8_t* rgb_buffer, int width, int height, int bytesPerRow);

    struct ColorEntry
    {
        const char* className;
        const char* colorName;
        uint8_t r, g, b;
    };

    struct ColorMatchingResult
    {
        int indexInTable = -1;
        const ColorEntry* entry = nullptr;
        double distance = NAN;
    };

    enum class ColorDistance
    {
        RGB_L1,
        CIE2000, // Lab distance. Pretty expensive.
    };

    double colorDistance_CIE2000(const PixelLab& p1, const PixelLab& p2);

    inline double colorDistance_CIE2000(const PixelSRGBA& p1, const PixelSRGBA& p2)
    {
        return colorDistance_CIE2000(convertToLab(p1), convertToLab(p2));
    }

    double colorDistance_RGBL1(const PixelSRGBA& p1, const PixelSRGBA& p2);

    // Index 0 will have the closest one. Index 1 the second closest.
    std::array<ColorMatchingResult,2> closestColorEntries (const PixelSRGBA& rgba, ColorDistance distance);
    
} // zv
