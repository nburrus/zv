//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/Image.h>

#include <functional>
#include <memory>
#include <string>

namespace zv
{

class ImageWriter
{
public:
    virtual void write (const ImageSRGBA& image) = 0;
};

class AppClient
{
public:
    AppClient ();
    ~AppClient ();

    bool isConnected () const;
    bool connect (const std::string& hostname = "127.0.0.1", int port = 4207);
    void waitUntilDisconnected ();

    using GetDataCallback = std::function<bool(ImageWriter&)>;
    void addImage (uint64_t imageId, const std::string& imageName, const ImageSRGBA& imageBuffer, bool replaceExisting = true);
    void addImage (uint64_t imageId, const std::string& imageName, GetDataCallback&& getDataCallback, bool replaceExisting = true);
    void addImageFromFile (const std::string& imPath);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
