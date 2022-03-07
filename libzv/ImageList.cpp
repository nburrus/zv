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

std::unique_ptr<ImageEntry> imageEntryFromPath (const std::string& imagePath)
{
    auto entry = std::make_unique<ImageEntry>();
    entry->source = ImageEntry::Source::FilePath;
    entry->sourceImagePath = imagePath;
    return entry;
}

std::unique_ptr<ImageEntryData> loadImageData(const ImageEntry& input)
{
    auto output = std::make_unique<ImageEntryData>();
    output->entry = &input;
    
    switch (input.source)
    {
        case ImageEntry::Source::Data:
        {
            output->cpuData = input.sourceData;
            break;
        }

        case ImageEntry::Source::FilePath:
        {
            output->cpuData = std::make_shared<ImageSRGBA>();
            bool couldLoad = readPngImage (input.sourceImagePath, *output->cpuData);
            if (!couldLoad)
            {
                zv_dbg("Could not load %s", input.sourceImagePath.c_str());
            }
            break;
        }
    }

    return output;
}

class ImageEntryCache
{
public:
    ImageEntryCache (int maxCacheSize = 5) : _lruCache (maxCacheSize)
    {

    }

    std::shared_ptr<ImageEntryData> getData (const ImageEntry* entry)
    {
        const std::shared_ptr<ImageEntryData>* cacheEntry = _lruCache.get (entry);
        if (cacheEntry)
        {
            return *cacheEntry;
        }
        else
        {
            std::shared_ptr<ImageEntryData> imageData = loadImageData(*entry);
            _lruCache.put (entry, imageData);
            return imageData;
        }
    }
    
    // For later.
    void asyncPreload (ImageEntry* entry) {}

private:
    lru_cache<const ImageEntry*, std::shared_ptr<ImageEntryData>> _lruCache;
};

} // zv

namespace zv
{

struct ImageList::Impl
{
    // Sorted set of images.
    std::vector<std::unique_ptr<ImageEntry>> entries;

    int selectedIndex = 0;

    ImageEntryCache cache;
};

ImageList::ImageList()
: impl (new Impl())
{}

ImageList::~ImageList() = default;

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
void ImageList::appendImage (std::unique_ptr<ImageEntry> image)
{
    impl->entries.push_back (std::move(image));
}

std::shared_ptr<ImageEntryData> ImageList::getData (const ImageEntry* entry)
{
    return impl->cache.getData (entry);
}

const ImageEntry* ImageList::imageEntryFromIndex (int index)
{
    zv_assert (index < impl->entries.size(), "Image index out of bounds");
    return impl->entries[index].get();
}

} // zv
