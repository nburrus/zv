//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageList.h>

#include <deque>

namespace zv
{

class ImageModifier
{
public:
    virtual ~ImageModifier () {}

public:
    void apply (const ImageItemDataPtr& input)
    {
        _outputData = std::make_shared<ImageItemData>();
        apply (*input, *_outputData);
    }

    const ImageItemDataPtr& output () const
    {
        return _outputData;
    }

protected:
    virtual void apply (const ImageItemData& input, ImageItemData& output) = 0;

private:
    ImageItemDataPtr _outputData;
};

class RotateImageModifier : ImageModifier
{
public:
    enum Angle {
        Angle_90,
        Angle_180,
        Angle_270,
    };

    RotateImageModifier (Angle angle) : _angle (angle)
    {}

public:
    virtual void apply (const ImageItemData& input, ImageItemData& output) override;

private:
    Angle _angle = Angle::Angle_90;
};

// Image currently active in the viewer, maybe modified.
struct ModifiedImage
{
    ModifiedImage(const ImageItemPtr& item,
                  const ImageItemDataPtr& originalData)
                  : _item (item)
                  , _originalData (originalData)
    {}

    bool hasValidData() const { return data() && data()->status == ImageItemData::Status::Ready; }

    bool hasPendingChanges () const { return !_modifiers.empty(); }

    bool update ()
    {
        if (!_originalData)
            return false;
        
        if (_originalData->update())
        {
            if (_originalData->cpuData->hasData())
            {
                _item->metadata.width = _originalData->cpuData->width();
                _item->metadata.height = _originalData->cpuData->height();
            }
            return true;
        }

        return false;
    }

    const ImageItemDataPtr& data() const { return _modifiers.empty() ? _originalData : _modifiers.back().output(); }
    
    ImageItemPtr& item() { return _item; }
    const ImageItemPtr& item() const { return _item; }

private:
    ImageItemPtr _item;
    ImageItemDataPtr _originalData;
    std::deque<ImageModifier> _modifiers; // for undo.
};
using ModifiedImagePtr = std::shared_ptr<ModifiedImage>;

} // zv
