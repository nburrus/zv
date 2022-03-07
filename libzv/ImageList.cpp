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

std::unique_ptr<ImageItem> imageItemFromPath (const std::string& imagePath)
{
    auto entry = std::make_unique<ImageItem>();
    entry->source = ImageItem::Source::FilePath;
    entry->sourceImagePath = imagePath;
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

    ImageItemDataPtr getData (const ImageItem* entry)
    {
        const ImageItemDataPtr* cacheEntry = _lruCache.get (entry);
        if (cacheEntry)
        {
            return *cacheEntry;
        }
        else
        {
            ImageItemDataPtr imageData = loadImageData(*entry);
            _lruCache.put (entry, imageData);
            return imageData;
        }
    }
    
    // For later.
    void asyncPreload (ImageItem* entry) {}

private:
    lru_cache<const ImageItem*, ImageItemDataPtr> _lruCache;
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
{}

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
        return;

    if (index < 0)
        return;

    impl->selectedIndex = index;
}

// Takes ownership.
void ImageList::appendImage (std::unique_ptr<ImageItem> image)
{
    impl->entries.push_back (std::move(image));
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
