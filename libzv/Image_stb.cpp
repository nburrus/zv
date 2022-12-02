//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Image.h"
#include "Utils.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <turbojpeg.h>

#include <fstream>
#include <vector>

namespace zv
{    


    inline bool ends_with(std::string const & value, std::string const & ending)
    {
        if (ending.size() > value.size()) return false;
        return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
    }

    inline bool fileHasJpegExtension (const std::string& fname)
    {
        std::string lowerFilename = fname;
        std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), [](unsigned char c){ return std::tolower(c); });
        return ends_with(lowerFilename, ".jpg") || ends_with(lowerFilename, ".jpeg");
    }

    bool readImageFile (const std::string& inputFileName, ImageSRGBA& outputImage)
    {
        if (fileHasJpegExtension(inputFileName))
        {
            return readJpegFile (inputFileName, outputImage);
        }

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

    bool readJpegFile (const std::string& inputFilename, ImageSRGBA& outputImage)
    {
        static thread_local tjhandle tjdecompressor = nullptr;
        if (!tjdecompressor)
        {
            tjdecompressor = tjInitDecompress();
        }
        zv_assert (tjdecompressor != nullptr, "Could not initialize a decompressor");

        std::ifstream file (inputFilename, std::ios::binary | std::ios::ate);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
        file.read(buffer.data(), size);
        if (!file.good())
            return false;

        int width = -1;
        int height = -1;
        int inSubsamp = -1;
        int inColorspace = -1;
        int ret = tjDecompressHeader3(tjdecompressor, (unsigned char*)buffer.data(), buffer.size(), &width, &height, &inSubsamp, &inColorspace);
        if (ret < 0)
        {
            zv_dbg ("Invalid JPEG header");
            return false;
        }

        outputImage.ensureAllocatedBufferForSize (width, height);
        ret = tjDecompress2 (tjdecompressor, (unsigned char*)buffer.data(), buffer.size(), outputImage.rawBytes(), width, outputImage.bytesPerRow(), height, TJPF_RGBA, /*flags=*/ 0);
        if (ret < 0)
        {
            zv_dbg ("Failed to decompress");
            return false;
        }

        return true;
    }

    bool writeJpegFile (const std::string& filePath, const ImageSRGBA& image)
    {
        // Save image with turbojpeg
        static thread_local tjhandle tjcompressor = nullptr;
        if (!tjcompressor)
        {
            tjcompressor = tjInitCompress();
        }
        zv_assert (tjcompressor != nullptr, "Could not initialize a compressor");

        unsigned char* jpegBuf = nullptr;
        unsigned long jpegSize = 0;
        int ret = tjCompress2 (tjcompressor,
                           image.rawBytes(), image.width(), image.bytesPerRow(), image.height(), TJPF_RGBA,
                           &jpegBuf, &jpegSize, TJSAMP_444, 90, 0);
        if (ret < 0)
        {
            zv_dbg ("Failed to compress");
            return false;
        }

        // Save to disk
        std::ofstream file (filePath, std::ios::binary | std::ios::trunc);
        if (!file.good())
        {
            zv_dbg ("Failed to open file for writing");
            return false;
        }
        file.write ((char*)jpegBuf, jpegSize);

        tjFree (jpegBuf);
        return true;
    }
    
    bool writeImageFile (const std::string& filePath, const ImageSRGBA& image)
    {
        if (fileHasJpegExtension(filePath))
        {
            return writeJpegFile (filePath, image);
        }
        
        return stbi_write_png(filePath.c_str(), image.width(), image.height(), 4, image.data(), image.bytesPerRow());
    }
    
} // zv

