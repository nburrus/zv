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
    // Default is a static item data.
    virtual bool update () { return false; };

    void ensureUploadedToGPU () const
    {
        if (textureData)
            return;
        
        textureData = std::make_unique<GLTexture>();
        textureData->initialize();
        textureData->upload(*cpuData);
    }
    
    // In a context compatible with ImageWindowContext
    std::shared_ptr<ImageSRGBA> cpuData;
    mutable GLTexturePtr textureData;
};
using ImageItemDataPtr = std::shared_ptr<ImageItemData>;
using ImageItemDataUniquePtr = std::unique_ptr<ImageItemData>;

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

    // Disabled by the name filter?
    bool disabled = false;

    // Whether it was already modified and saved at least once.
    // In that case we won't ask for confirmation to save it again.
    bool alreadyModifiedAndSaved = false;

    // Could add thumbnail, etc.
    // Everything should be lazy though.

    void fillFromFilePath (const std::string& path);

    ~ImageItem ();
};
using ImageItemPtr = std::shared_ptr<ImageItem>;
using ImageItemUniquePtr = std::unique_ptr<ImageItem>;

std::unique_ptr<ImageItem> imageItemFromPath (const std::string& imagePath);
std::unique_ptr<ImageItem> imageItemFromData (const ImageSRGBA& im, const std::string& name);

std::unique_ptr<ImageItem> defaultImageItem ();

struct SelectionRange
{
    bool isSelected (int idx) const
    {
        for (const auto& k : indices)
            if (k == idx)
                return true;
        return false;
    }

    int firstValidIndex () const
    {
        for (int i = 0; i < indices.size(); ++i)
            if (indices[i] >= 0)
                return i;
        return -1;
    }

    std::vector<int> indices;
};

class ImageList
{
public:
    ImageList ();
    ~ImageList ();

public:
    int numImages () const;
    int numEnabledImages () const;
    
    void setFilter (std::function<bool(const std::string& name)>&& filter);

    const SelectionRange& selectedRange() const;
    void advanceCurrentSelection (int count);
    void setSelectionStart (int startIndex);
    void setSelectionCount (int count);

    int firstSelectedAndEnabledIndex () const;

    const ImageItemPtr& imageItemFromIndex (int index) const;
    ImageItemPtr imageItemFromId (ImageId imageId);

    void swapItems (int idx1, int idx2);

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
