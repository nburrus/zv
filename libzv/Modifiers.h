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
    ModifiedImage(AnnotationRenderer& renderer,
                  const ImageItemPtr& item,
                  const ImageItemDataPtr& originalData)
                  : _annotationRenderer (renderer)
                  , _item (item)
                  , _originalData (originalData)
    {}

    bool hasValidData() const { return data() && data()->status == ImageItemData::Status::Ready; }
    
    bool hasPendingChanges () const { return _annotatedData || !_modifiers.empty(); }

    bool canUndo () const { return !_actions.empty(); }

    const ImageItemDataPtr& data() const 
    { 
        if (_annotatedData) 
            return _annotatedData;
        return dataWithoutAnnotations();
    }

    const ImageItemDataPtr& dataWithoutAnnotations() const 
    {
        if (!_modifiers.empty()) 
            return _modifiers.back()->output();
        return _originalData;
    }
    
    ImageItemPtr& item() { return _item; }
    const ImageItemPtr& item() const { return _item; }

    bool update ();

    void addModifier (std::unique_ptr<ImageModifier> modifier);
    void removeLastModifier();

    void addAnnotation (std::unique_ptr<ImageAnnotation> annotation);
    void removeLastAnnotation();

    void discardChanges ();
    void undoLastChange ();

private:
    void renderAnnotations ();
    void clearIntermediateModifiersData ();

private:
    ImageItemPtr _item;
    ImageItemDataPtr _originalData;
    AnnotationRenderer& _annotationRenderer;
    ImageItemDataPtr _annotatedData;
    std::deque<std::unique_ptr<ImageModifier>> _modifiers;
    std::deque<std::unique_ptr<ImageAnnotation>> _annotations;
    std::deque<ImageAction> _actions;
    bool _modifiersOrAnnotationsChangedSinceLastUpdate = false;    
};
using ModifiedImagePtr = std::shared_ptr<ModifiedImage>;

} // zv
