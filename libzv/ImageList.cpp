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

ImageItem::~ImageItem ()
{
    // fprintf (stderr, "ImageItem destructor, sourceImagePath=%s\n", sourceImagePath.c_str());
}

void ImageItem::fillFromFilePath (const std::string& imagePath)
{
    source = ImageItem::Source::FilePath;
    sourceImagePath = imagePath;
    prettyName = fs::path(imagePath).filename().string();
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
    entry->fillFromFilePath (imagePath);
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

    auto output = std::make_unique<ImageItemData>();
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

std::unique_ptr<ImageItemData> loadImageData(ImageItem& input)
{
    std::unique_ptr<ImageItemData> output;
    
    switch (input.source)
    {
        case ImageItem::Source::Data:
        {
            auto* staticData = new ImageItemData();
            staticData->status = ImageItemData::Status::Ready;
            staticData->cpuData = input.sourceData;
            output.reset (staticData);
            break;
        }

        case ImageItem::Source::FilePath:
        {
            auto* staticData = new ImageItemData();
            staticData->status = ImageItemData::Status::Ready;
            staticData->cpuData = std::make_shared<ImageSRGBA>();

            Profiler tc (formatted("Load %s", input.sourceImagePath.c_str()).c_str());
            bool couldLoad = readImageFile (input.sourceImagePath, *staticData->cpuData);
            if (!couldLoad)
            {
                zv_dbg("Could not load %s", input.sourceImagePath.c_str());
                staticData->status = ImageItemData::Status::FailedToLoad;
            }
            tc.stop ();

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

    if (output && output->cpuData && output->cpuData->hasData())
    {
        input.metadata.width = output->cpuData->width();
        input.metadata.height = output->cpuData->height();
    }

    return output;
}

class ImageItemCache
{
public:
    ImageItemCache (int maxCacheSize = 8) : _lruCache (maxCacheSize)
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

    ImageItemDataPtr getData (ImageItem* entry)
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
    Impl ()
    {
        fillSelectedIndices();
    }

    // Sorted set of images.
    std::vector<ImageItemPtr> entries;        
    std::vector<int> enabledEntries;

    std::function<bool(const std::string& name)> filter;

    SelectionRange selection;
    
    // These refer to the enabledEntries array.
    int selectionStart = 0;
    int selectionCount = 1;
    
    // This one refer to the global entries array.
    // It might be selected or not.
    int globalSelectionStart = 0;

    ImageItemCache cache;

    void fillSelectedIndices ();
    void selectClosestEnabledEntry (int globalIndex);
    void applyFilter ();
    void updateFilterAfterAddImage ();
    void dumpSelectionState(const char* label);
};

void ImageList::Impl::dumpSelectionState(const char* label)
{
    // zv_dbg ("(%s) NSEL=%d START=%d COUNT=%d GLOBAL_START=%d", label, (int)enabledEntries.size(), selectionStart, selectionCount, globalSelectionStart);
}

// Much faster version that only checks if something changed after the addition.
// Critical to have this when launching zv with tons of input images.
void ImageList::Impl::updateFilterAfterAddImage ()
{
    entries.back()->disabled = filter && !filter(entries.back()->prettyName);

    if (!entries.back()->disabled)
    {
        enabledEntries.push_back (entries.size() - 1);
        if (selection.indices.back() < 0)
            fillSelectedIndices ();
    }
}

void ImageList::Impl::applyFilter ()
{
    enabledEntries.clear ();
    enabledEntries.reserve (entries.size());
    for (int i = 0; i < entries.size(); ++i)
    {
        auto& e = entries[i];
        e->disabled = filter ? !filter(e->prettyName) : false;
        if (!e->disabled)
            enabledEntries.push_back (i);
    }

    selectClosestEnabledEntry (globalSelectionStart);
    fillSelectedIndices ();
}

void ImageList::Impl::selectClosestEnabledEntry (int globalIndex)
{
    auto it = std::lower_bound (enabledEntries.begin(), enabledEntries.end(), globalIndex);
    // Can't find something past it? Try before.
    if (it == enabledEntries.end())
    {
        it = std::upper_bound (enabledEntries.begin(), enabledEntries.end(), globalIndex);
    }

    if (it != enabledEntries.end())
    {
        selectionStart = it - enabledEntries.begin();
    }
    else
    {
        selectionStart = 0; // reset it entirely.
    }
}

void ImageList::Impl::fillSelectedIndices ()
{
    selection.indices.resize (selectionCount);
    for (int i = 0; i < selectionCount; ++i)
    {
        int idxInSelectedEntries = selectionStart + i;
        if (idxInSelectedEntries >= 0 && idxInSelectedEntries < enabledEntries.size())
        {
            selection.indices[i] = enabledEntries[idxInSelectedEntries];
        }
        else
        {
            selection.indices[i] = -1;
        }
    }
}

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

int ImageList::numEnabledImages () const
{
    return impl->enabledEntries.size();
}

const SelectionRange& ImageList::selectedRange() const
{
    return impl->selection;
}

void ImageList::setFilter (std::function<bool(const std::string& name)>&& filter)
{
    impl->filter = std::move(filter);
    impl->applyFilter ();
    impl->dumpSelectionState ("setFilter");
}

void ImageList::advanceCurrentSelection (int count)
{
    int index = impl->selectionStart + count;

    while (index >= (int)impl->enabledEntries.size())
    {
        index -= impl->selectionCount;
        impl->dumpSelectionState ("advanceCurrentSelection - early return");
    }

    while ((index + impl->selectionCount) <= 0)
    {
        index += impl->selectionCount;
    }

    impl->selectionStart = index;
    impl->fillSelectedIndices ();
    if (impl->selection.firstValidIndex() >= 0)
        impl->globalSelectionStart = impl->selection.indices[impl->selection.firstValidIndex()];
    impl->dumpSelectionState ("advanceCurrentSelection");
}

void ImageList::setSelectionStart (int globalIndex)
{
    impl->globalSelectionStart = globalIndex;
    impl->selectClosestEnabledEntry (globalIndex);
    impl->fillSelectedIndices();
    impl->dumpSelectionState ("setSelectionStart");
}

void ImageList::setSelectionCount (int count)
{
    impl->selectionCount = count;
    impl->fillSelectedIndices ();
    impl->dumpSelectionState ("setSelectionCount");
}

void ImageList::refreshPrettyFileNames ()
{
    std::unordered_map<std::string, std::vector<int>> groupedNames;
    for (int idx = 0; idx < impl->entries.size(); ++idx)
    {
        const auto& entry = impl->entries[idx];
        if (!entry->sourceImagePath.empty())
        {
            groupedNames[fs::path(entry->sourceImagePath).filename().string()].push_back(idx);
        }
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

int ImageList::firstSelectedAndEnabledIndex () const
{
    for (int idx = 0; idx < numImages(); ++idx)
    {
        const ImageItemPtr& itemPtr = imageItemFromIndex(idx);
        if (itemPtr->disabled) // from the filter.
            continue;

        bool selected = impl->selection.isSelected(idx);
        if (selected)
        {
            return idx;
        }
    }
    return -1;
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

    impl->updateFilterAfterAddImage ();
    impl->dumpSelectionState ("addImage");
    return imageId;
}

void ImageList::removeImage (int index)
{
    // Make sure that we remove it from the cache so we don't accidentally load the wrong data.
    const ImageItem* item = impl->entries[index].get();
    impl->cache.removeItem (item);
    impl->entries.erase (impl->entries.begin() + index);
    impl->applyFilter ();
    impl->dumpSelectionState ("removeImage");
}

ImageItemDataPtr ImageList::getData (ImageItem* entry)
{
    return impl->cache.getData (entry);
}

const ImageItemPtr& ImageList::imageItemFromIndex (int index) const
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

void ImageList::swapItems (int idx1, int idx2)
{
    std::swap (impl->entries[idx1], impl->entries[idx2]);
}

} // zv
