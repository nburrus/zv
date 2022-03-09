//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImageList.h"

#include <libzv/Utils.h>
#include <libzv/lrucache.hpp>

#include <unordered_map>

namespace zv
{

struct UniqueId
{
    static int64_t newId()
    {
        static uint64_t lastId = 0;
        return lastId++;
    }
};

std::unique_ptr<ImageItem> imageItemFromData (const ImageSRGBA& im, const std::string& name)
{
    auto entry = std::make_unique<ImageItem>();
    entry->uniqueId = UniqueId::newId();
    entry->source = ImageItem::Source::Data;
    entry->sourceData = std::make_shared<ImageSRGBA>(im);
    entry->sourceImagePath = name;
    return entry;
}

std::unique_ptr<ImageItem> imageItemFromPath (const std::string& imagePath)
{
    auto entry = std::make_unique<ImageItem>();
    entry->uniqueId = UniqueId::newId();
    entry->source = ImageItem::Source::FilePath;
    entry->sourceImagePath = imagePath;
    return entry;
}

ImageSRGBAPtr getDefaultImage ()
{
    static ImageSRGBAPtr image;
    if (image)
        return image;

    int width = 256;
    int height = 256;
    image = std::make_shared<ImageSRGBA>(width,height);
    for (int r = 0; r < height; ++r)
    {
        auto* rowPtr = image->atRowPtr(r);
        for (int c = 0; c < width; ++c)
        {
            rowPtr[c] = PixelSRGBA(r%256, c%256, (r+c)%256, 255);
        }
    }

    return image;
}

std::unique_ptr<ImageItem> defaultImageItem ()
{
    auto entry = std::make_unique<ImageItem>();
    entry->uniqueId = UniqueId::newId();
    entry->source = ImageItem::Source::Callback;
    entry->sourceImagePath = "<<default>>";
    entry->loadDataCallback = getDefaultImage;
    return entry;
}

std::unique_ptr<ImageItemData> loadImageData(const ImageItem& input)
{
    auto output = std::make_unique<ImageItemData>();
    
    switch (input.source)
    {
        case ImageItem::Source::Data:
        {
            output->cpuData = input.sourceData;
            break;
        }

        case ImageItem::Source::FilePath:
        {
            output->cpuData = std::make_shared<ImageSRGBA>();
            bool couldLoad = readPngImage (input.sourceImagePath, *output->cpuData);
            if (!couldLoad)
            {
                zv_dbg("Could not load %s", input.sourceImagePath.c_str());
            }
            break;
        }

        case ImageItem::Source::Callback:
        {
            output->cpuData = input.loadDataCallback();
            break;
        }

        default:
            zv_assert (false, "Invalid source.");
            break;
    }

    return output;
}

class ImageItemCache
{
public:
    ImageItemCache (int maxCacheSize = 5) : _lruCache (maxCacheSize)
    {

    }

    void clear ()
    {
        _lruCache.clear();
    }

    void removeItem (const ImageItem* entry)
    {
        _lruCache.remove (entry->uniqueId);
    }

    ImageItemDataPtr getData (const ImageItem* entry)
    {
        const ImageItemDataPtr* cacheEntry = _lruCache.get (entry->uniqueId);
        if (cacheEntry)
        {
            return *cacheEntry;
        }
        else
        {
            ImageItemDataPtr imageData = loadImageData(*entry);
            _lruCache.put (entry->uniqueId, imageData);
            return imageData;
        }
    }
    
    // For later.
    void asyncPreload (ImageItem* entry) {}

private:
    lru_cache<uint64_t, ImageItemDataPtr> _lruCache;
};

} // zv

namespace zv
{

struct ImageList::Impl
{
    // Sorted set of images.
    std::vector<ImageItemPtr> entries;

    int selectedIndex = 0;

    ImageItemCache cache;
};

ImageList::ImageList()
: impl (new Impl())
{
    // Always add the default image.
    addImage(defaultImageItem());
}

ImageList::~ImageList() = default;

void ImageList::releaseGL ()
{
    impl->cache.clear();
}

int ImageList::numImages () const 
{ 
    return impl->entries.size();
}

int ImageList::selectedIndex () const
{
    return impl->selectedIndex;
}

void ImageList::selectImage (int index)
{
    if (index >= impl->entries.size())
    {
        return;
    }

    if (index < 0)
    {
        impl->selectedIndex = 0;
        return;
    }

    impl->selectedIndex = index;
}

// Takes ownership.
void ImageList::addImage (std::unique_ptr<ImageItem> image)
{
    if (impl->entries.size() == 1 && impl->entries[0]->sourceImagePath == "<<default>>")
    {
        removeImage (0);
    }

    // FIXME: using a vector with front insertion is not great. Could use a list for once, I guess.
    impl->entries.insert (impl->entries.begin(), std::move(image));
}

void ImageList::removeImage (int index)
{
    // Make sure that we remove it from the cache so we don't accidentally load the wrong data.
    const ImageItem* item = impl->entries[index].get();
    impl->cache.removeItem (item);
    impl->entries.erase (impl->entries.begin() + index);
}

ImageItemDataPtr ImageList::getData (const ImageItem* entry)
{
    return impl->cache.getData (entry);
}

const ImageItemPtr& ImageList::imageItemFromIndex (int index)
{
    zv_assert (index < impl->entries.size(), "Image index out of bounds");
    return impl->entries[index];
}

} // zv
