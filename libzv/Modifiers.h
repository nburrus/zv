//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageList.h>
#include <libzv/MathUtils.h>
#include <libzv/Annotations.h>

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

    void clearTextureData ()
    {
        _outputData->textureData = {};
    }

protected:
    virtual void apply (const ImageItemData& input, ImageItemData& output) = 0;

private:
    ImageItemDataPtr _outputData;
};

class RotateImageModifier : public ImageModifier
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

class ImageAction
{
public:
    using UndoFunc = std::function<void(void)>;

public:
    ImageAction (std::function<void(void)>&& undoFunc) 
        : _undoFunc(std::move(undoFunc))
    {}

    // Only keep the move operators.
    ImageAction (ImageAction&& other) = default;
    ImageAction& operator= (ImageAction&& other) = default;

    void undo ()
    {
        if (_undoFunc)
        {
            auto f = std::move(_undoFunc);
            _undoFunc = nullptr;
            f ();
        }        
    }

private:
    UndoFunc _undoFunc;
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

    void discardChanges ()
    {
        if (_modifiers.empty ())
            return;
        _modifiers.clear ();
        _modifiersOrAnnotationsChangedSinceLastUpdate = true;
    }

    const ImageItemDataPtr& data() const { return _modifiers.empty() ? _originalData : _modifiers.back()->output(); }
    
    ImageItemPtr& item() { return _item; }
    const ImageItemPtr& item() const { return _item; }

    bool update (AnnotationRenderer& renderer)
    {
        if (!_originalData)
            return false;
        
        bool originalChanged = _originalData->update();

        if (!originalChanged && !_modifiersOrAnnotationsChangedSinceLastUpdate)
        {
            return false;
        }

        // Reapply the modification pipeline if needed.
        if (originalChanged && _originalData->cpuData->hasData())
        {
            ImageItemDataPtr input = _originalData;
            for (auto& modifier : _modifiers)
            {
                modifier->apply (input);
                input = modifier->output ();
            }
        }
        
        clearIntermediateModifiersData ();
        _modifiersOrAnnotationsChangedSinceLastUpdate = false;

        if (data()->cpuData->hasData())
        {
            _item->metadata.width = data()->cpuData->width();
            _item->metadata.height = data()->cpuData->height();
        }

        return true;
    }

    void addModifier (std::unique_ptr<ImageModifier> modifier)
    {
        if (hasValidData())
        {
            modifier->apply (data());
        }
        _modifiers.push_back (std::move(modifier));
        _modifiersOrAnnotationsChangedSinceLastUpdate = true;

        _actions.push_back(ImageAction([this]() {
            removeLastModifier();
        }));
    }

    void addAnnotation (std::unique_ptr<ImageAnnotation> annotation)
    {
        _annotations.push_back (std::move(annotation));
        _modifiersOrAnnotationsChangedSinceLastUpdate = true;
        _actions.push_back(ImageAction([this]() {
            removeLastAnnotation();
        }));
    }

    void removeLastModifier()
    {
        if (_modifiers.empty())
            return;
        _modifiers.pop_back();
        _modifiersOrAnnotationsChangedSinceLastUpdate = true;
    }

    void removeLastAnnnotation()
    {
        if (_annotations.empty())
            return;
        _annotations.pop_back();
        _modifiersOrAnnotationsChangedSinceLastUpdate = true;
    }

    void undoLastChange ()
    {
        if (_actions.empty())
            return;
        _actions.back().undo();
        _actions.pop_back();
    }

private:
    void clearIntermediateModifiersData ()
    {
        if (_modifiers.size() < 2)
            return;
        auto it = _modifiers.rbegin();
        ++it;
        while (it != _modifiers.rend())
        {
            (*it)->clearTextureData ();
            ++it;
        }
    }

private:
    ImageItemPtr _item;
    ImageItemDataPtr _originalData;
    std::deque<std::unique_ptr<ImageModifier>> _modifiers;
    std::deque<std::unique_ptr<ImageAnnotation>> _annotations;
    std::deque<ImageAction> _actions;
    bool _modifiersOrAnnotationsChangedSinceLastUpdate = false;    
};
using ModifiedImagePtr = std::shared_ptr<ModifiedImage>;

} // zv
