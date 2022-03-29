//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Image.h"
#include "Utils.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <fstream>
#include <vector>

namespace zv
{
    
    bool readPngImage (const std::string& inputFileName, ImageSRGBA& outputImage)
    {
        int width = -1, height = -1, channels = -1;
        uint8_t* data = stbi_load(inputFileName.c_str(), &width, &height, &channels, 4);
        if (!data)
        {
            return false;
        }
        // channels can be anything (corresponding to the input image), but we requested 4
        // so the output data will always have 4.
        outputImage.ensureAllocatedBufferForSize (width, height);
        outputImage.copyDataFrom (data, width*4, width, height);
        return true;
    }
    
    bool writePngImage (const std::string& filePath, const ImageSRGBA& image)
    {
        return stbi_write_png(filePath.c_str(), image.width(), image.height(), 4, image.data(), image.bytesPerRow());
    }
    
} // zv

