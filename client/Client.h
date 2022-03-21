//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <functional>
#include <memory>
#include <string>

namespace zv
{

// View of an existing buffer, no ownership will be taken and no reference
// will get stored.
struct ImageView
{
    ImageView() {}
    ImageView(uint8_t *pixels_RGBA32, int width, int height, int bytesPerRow = 0)
        : pixels_RGBA32(pixels_RGBA32), width(width), height(height), bytesPerRow(bytesPerRow)
    {
        if (this->bytesPerRow == 0)
            this->bytesPerRow = width * 4;
    }

    inline size_t numBytes() const { return height * bytesPerRow; }

    uint8_t *pixels_RGBA32 = nullptr;
    int width = 0;
    int height = 0;
    int bytesPerRow = 0;
};

class ImageWriter
{
public:
    virtual void write (const ImageView& imageView) = 0;
};

class Client
{
public:
    Client ();
    ~Client ();

    bool isConnected () const;
    bool connect (const std::string& hostname = "127.0.0.1", int port = 4207);

    using GetDataCallback = std::function<bool(ImageWriter&)>;
    void addImage (uint64_t imageId, const std::string& imageName, const ImageView& imageBuffer, bool replaceExisting = true);
    void addImage (uint64_t imageId, const std::string& imageName, const GetDataCallback& getDataCallback, bool replaceExisting = true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
