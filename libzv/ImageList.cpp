//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImageList.h"

#include <libzv/Utils.h>
#include <libzv/lrucache.hpp>

#include <unordered_map>

#include <filesystem>
namespace fs = std::filesystem;

namespace zv
{

int64_t UniqueId::newId()
{
    static uint64_t lastId = 0;
    return lastId++;
}

std::unique_ptr<ImageItem> imageItemFromData (const ImageSRGBA& im, const std::string& name)
{
    auto entry = std::make_unique<ImageItem>();
    entry->uniqueId = UniqueId::newId();
    entry->source = ImageItem::Source::Data;
    entry->sourceData = std::make_shared<ImageSRGBA>(im);
    entry->prettyName = name;
    return entry;
}

std::unique_ptr<ImageItem> imageItemFromPath (const std::string& imagePath)
{
    auto entry = std::make_unique<ImageItem>();
    entry->uniqueId = UniqueId::newId();
    entry->source = ImageItem::Source::FilePath;
    entry->sourceImagePath = imagePath;
    entry->prettyName = fs::path(entry->sourceImagePath).filename();
    return entry;
}

std::unique_ptr<ImageItemData> getDefaultImage ()
{
    static ImageSRGBAPtr image;
    
    if (!image)
    {
        int width = 256;
        int height = 256;
        image = std::make_shared<ImageSRGBA>(width, height);
        for (int r = 0; r < height; ++r)
        {
            auto *rowPtr = image->atRowPtr(r);
            for (int c = 0; c < width; ++c)
            {
                rowPtr[c] = PixelSRGBA(r % 256, c % 256, (r + c) % 256, 255);
            }
        }
    }

    auto output = std::make_unique<StaticImageItemData>();
    output->cpuData = image;
    output->status = ImageItemData::Status::Ready;
    return output;
}

std::unique_ptr<ImageItem> defaultImageItem ()
{
    auto entry = std::make_unique<ImageItem>();
    entry->uniqueId = UniqueId::newId();
    entry->source = ImageItem::Source::Callback;
    entry->prettyName = "<<default>>";
    entry->loadDataCallback = getDefaultImage;
    return entry;
}

std::unique_ptr<ImageItemData> loadImageData(const ImageItem& input)
{
    std::unique_ptr<ImageItemData> output;
    
    switch (input.source)
    {
        case ImageItem::Source::Data:
        {
            auto* staticData = new StaticImageItemData();
            staticData->status = ImageItemData::Status::Ready;
            staticData->cpuData = input.sourceData;
            output.reset (staticData);
            break;
        }

        case ImageItem::Source::FilePath:
        {
            auto* staticData = new StaticImageItemData();
            staticData->status = ImageItemData::Status::Ready;
            staticData->cpuData = std::make_shared<ImageSRGBA>();

            bool couldLoad = readPngImage (input.sourceImagePath, *staticData->cpuData);
            if (!couldLoad)
            {
                zv_dbg("Could not load %s", input.sourceImagePath.c_str());
                staticData->status = ImageItemData::Status::FailedToLoad;
            }

            output.reset (staticData);
            break;
        }

        case ImageItem::Source::Callback:
        {
            output = input.loadDataCallback();
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
    addImage(defaultImageItem(), 0, false);
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

void ImageList::refreshPrettyFileNames ()
{
    std::unordered_map<std::string, std::vector<int>> groupedNames;
    for (int idx = 0; idx < impl->entries.size(); ++idx)
    {
        const auto& entry = impl->entries[idx];
        groupedNames[entry->prettyName].push_back(idx);
    }
    
    for (const auto& it : groupedNames)
    {
        const auto& pathIndices = it.second;

        if (pathIndices.size() < 2)
            continue;
    
        std::vector<std::string> uniqueNames;
        std::vector<std::string> pathNames (pathIndices.size());
        for (int i = 0; i < pathIndices.size(); ++i)
        {
            pathNames[i] = impl->entries[pathIndices[i]]->sourceImagePath;
        }

        uniqueNames = uniquePrettyNames (pathNames);
        for (int i = 0; i < pathIndices.size(); ++i)
        {
            impl->entries[pathIndices[i]]->prettyName = uniqueNames[i];
        }
    }
}

// Takes ownership.
ImageId ImageList::addImage (std::unique_ptr<ImageItem> image, int insertPosition, bool replaceExisting)
{
    ImageId imageId = image->uniqueId;

    if (impl->entries.size() == 1 && impl->entries[0]->prettyName == "<<default>>")
    {
        removeImage (0);
    }

    if (insertPosition < 0)
        insertPosition = numImages();

    if (replaceExisting)
    {
        auto existing_element = std::find_if(impl->entries.begin(), impl->entries.end(), [&](const ImageItemPtr& e) {
            if (image->source == ImageItem::Source::FilePath && e->source == ImageItem::Source::FilePath)
                return e->sourceImagePath == image->sourceImagePath;
            return e->prettyName == image->prettyName;
        });
    
        if (existing_element != impl->entries.end())
        {
            const int position = existing_element - impl->entries.begin();
            removeImage (position);
            insertPosition = position;
        }
    }

    // FIXME: using a vector with front insertion is not great. Could use a list for once, I guess.
    impl->entries.insert (impl->entries.begin() + insertPosition, std::move(image));
    return imageId;
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

ImageItemPtr ImageList::imageItemFromId (ImageId imageId)
{
    for (const auto& entry: impl->entries)
        if (entry->uniqueId == imageId)
            return entry;
    return ImageItemPtr();
}

} // zv
