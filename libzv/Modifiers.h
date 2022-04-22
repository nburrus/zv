//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageList.h>
#include <libzv/MathUtils.h>

#include <deque>

namespace zv
{

class AnnotationRenderer;

class ImageModifier
{
public:
    virtual ~ImageModifier () {}

public:
    void apply (const ImageItemDataPtr& input, AnnotationRenderer& annotationRenderer)
    {
        _outputData = std::make_shared<ImageItemData>();
        apply (*input, *_outputData, annotationRenderer);
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
    virtual void apply (const ImageItemData& input, ImageItemData& output, AnnotationRenderer& annotationRenderer) = 0;

private:
    ImageItemDataPtr _outputData;
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
    
    bool hasPendingChanges () const { return !_modifiers.empty(); }

    bool canUndo () const { return !_actions.empty(); }

    const ImageItemDataPtr& data() const 
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

    bool saveChanges (const std::string& outputPath);
    void discardChanges ();
    void undoLastChange ();

private:
    void clearIntermediateModifiersData ();

private:
    ImageItemPtr _item;
    ImageItemDataPtr _originalData;
    AnnotationRenderer& _annotationRenderer;
    std::deque<std::unique_ptr<ImageModifier>> _modifiers;
    std::deque<ImageAction> _actions;
    bool _modifiersChangedSinceLastUpdate = false;
};
using ModifiedImagePtr = std::shared_ptr<ModifiedImage>;

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
    virtual void apply (const ImageItemData& input, ImageItemData& output, AnnotationRenderer&) override;

private:
    Angle _angle = Angle::Angle_90;
};

class CropImageModifier : public ImageModifier
{
public:
    struct Params
    {
        // All these values are ratio.
        // This makes it easy to apply to multiples images
        // with different original sizes.
        Rect textureRect = Rect::from_x_y_w_h(0.1, 0.1, 0.8, 0.8);

        Rect imageAlignedTextureRect (int width, int height) const;
        Rect validImageRectForSize(int width, int height) const;    

        int numControlPoints () const { return 4; }
        void updateControlPoint (int idx, const Point& p, int imageWidth, int imageHeight);

        static Point controlPointPos (int idx, const Rect& imageAlignedTextureRect);
    };

    CropImageModifier (const Params& params) : _params (params)
    {}

    const Params& params () const { return _params; }

public:
    virtual void apply (const ImageItemData& input, ImageItemData& output, AnnotationRenderer&) override;

private:
    Params _params;
};
using CropImageModifierPtr = std::shared_ptr<CropImageModifier>;

} // zv
