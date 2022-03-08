//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/Image.h>
#include <libzv/OpenGL.h>

#include <memory>
#include <vector>

namespace zv
{

struct ImageItem
{
    enum class Source
    {
        Invalid,
        FilePath,
        Data,
        Callback,
    } source;

    std::string errorString;
    std::string sourceImagePath;
    std::shared_ptr<ImageSRGBA> sourceData;
    std::function<ImageSRGBA(void)> callback;    

    // Could add thumbnail, etc.
    // Everything should be lazy though.
};
using ImageItemPtr = std::shared_ptr<ImageItem>;

std::unique_ptr<ImageItem> imageItemFromPath (const std::string& imagePath);

struct ImageItemData
{
    std::shared_ptr<ImageSRGBA> cpuData;
    
    // In a context compatible with ImageWindowContext
    std::unique_ptr<GLTexture> textureData;
};
using ImageItemDataPtr = std::shared_ptr<ImageItemData>;

struct ImageItemAndData
{
    ImageItemPtr item;
    ImageItemDataPtr data;
};

class ImageList
{
public:
    ImageList ();
    ~ImageList ();

public:
    int numImages () const;
    int selectedIndex () const;
    void selectImage (int index);
    const ImageItemPtr& imageItemFromIndex (int index);

    // Takes ownership.
    void appendImage (std::unique_ptr<ImageItem> image);

    // Important to call this with a GL context set as it may release some GL textures.
    ImageItemDataPtr getData (const ImageItem* entry);
    
    // Important to call this with a GL context set as it may release some textures.
    void releaseGL ();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
