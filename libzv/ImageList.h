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

using ImageId = int64_t;

struct UniqueId
{
    static int64_t newId();
};

struct ImageItemData
{
    virtual ~ImageItemData () {}

    enum class Status
    {
        FailedToLoad = -2,
        Unknown = -1,
        Ready = 0,
        StillLoading = 1,
    };

    Status status = Status::Unknown;

    // Update is the only operation that can actually change the content.
    // Returns true if the content changed.
    virtual bool update () = 0;

    // In a context compatible with ImageWindowContext
    std::shared_ptr<ImageSRGBA> cpuData;
    std::unique_ptr<GLTexture> textureData;
};
using ImageItemDataPtr = std::shared_ptr<ImageItemData>;
using ImageItemDataUniquePtr = std::unique_ptr<ImageItemData>;

struct StaticImageItemData : public ImageItemData
{
    virtual bool update () override { return false; }
};

struct ImageItem
{
    enum class Source
    {
        Invalid,
        FilePath,
        Data,
        Callback,
    } source;
   
    struct Metadata
    {
        int width = -1;
        int height = -1;
    };

    ImageId uniqueId = -1;
    std::string errorString;
    std::string sourceImagePath; // also used for the pretty name of other sources.
    std::string prettyName;
    std::string viewerName = "default";
    std::shared_ptr<ImageSRGBA> sourceData;
    std::function<ImageItemDataUniquePtr()> loadDataCallback;    

    using EventCallbackType = std::function<void(ImageId, float, float, void* userData)>;
    EventCallbackType eventCallback = nullptr;
    void* eventCallbackData = nullptr;

    Metadata metadata;

    // Could add thumbnail, etc.
    // Everything should be lazy though.
};
using ImageItemPtr = std::shared_ptr<ImageItem>;
using ImageItemUniquePtr = std::unique_ptr<ImageItem>;

std::unique_ptr<ImageItem> imageItemFromPath (const std::string& imagePath);
std::unique_ptr<ImageItem> imageItemFromData (const ImageSRGBA& im, const std::string& name);

struct ImageItemAndData
{
    bool hasValidData() const { return data && data->status == ImageItemData::Status::Ready; }

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
    ImageItemPtr imageItemFromId (ImageId imageId);

    // Takes ownership.
    ImageId addImage (std::unique_ptr<ImageItem> image, int position, bool replaceExisting);
    void removeImage (int index);

    void refreshPrettyFileNames ();

    // Important to call this with a GL context set as it may release some GL textures.
    ImageItemDataPtr getData (ImageItem* entry);
    
    // Important to call this with a GL context set as it may release some textures.
    void releaseGL ();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
