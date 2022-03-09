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

    const std::string& prettyName() { return sourceImagePath; }
    
    int64_t uniqueId = -1;
    std::string errorString;
    std::string sourceImagePath; // also used for the pretty name of other sources.
    std::shared_ptr<ImageSRGBA> sourceData;
    std::function<ImageSRGBAPtr(void)> loadDataCallback;    

    // Could add thumbnail, etc.
    // Everything should be lazy though.
};
using ImageItemPtr = std::shared_ptr<ImageItem>;

std::unique_ptr<ImageItem> imageItemFromPath (const std::string& imagePath);
std::unique_ptr<ImageItem> imageItemFromData (const ImageSRGBA& im, const std::string& name);

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
    void addImage (std::unique_ptr<ImageItem> image, int position, bool replaceExisting);
    void removeImage (int index);

    // Important to call this with a GL context set as it may release some GL textures.
    ImageItemDataPtr getData (const ImageItem* entry);
    
    // Important to call this with a GL context set as it may release some textures.
    void releaseGL ();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
