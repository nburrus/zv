//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageList.h>
#include <libzv/MathUtils.h>

#include <array>
#include <deque>

namespace zv
{

class AnnotationDocument;
class AnnotationRenderer;

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
    ModifiedImage(const ImageItemPtr& item, const ImageItemDataPtr& originalData);
    ~ModifiedImage();

    bool hasValidData() const { return finalData() && finalData()->status == ImageItemData::Status::Ready; }

    bool hasPendingChanges () const;

    bool canUndo () const { return !_actions.empty(); }

    // The final image at the end of the pipeline: the annotation-composited
    // output when annotations exist, otherwise the result of the normal
    // modifier chain. This is what gets rendered and what gets saved.
    const ImageItemDataPtr& finalData() const;

    // The result of the normal modifier chain, before the annotation layer
    // is composited on top. Use this when something needs to feed into the
    // annotation pipeline (like adding a new modifier).
    const ImageItemDataPtr& preAnnotationData() const;

    ImageItemPtr& item() { return _item; }
    const ImageItemPtr& item() const { return _item; }

    bool updateModifiers();
    bool updateAnnotations(AnnotationRenderer& renderer);

    void addModifier (std::unique_ptr<ImageModifier> modifier);
    void removeLastModifier();

    bool saveChanges (const std::string& outputPath);
    void discardChanges ();
    // Replace the base image data with a freshly loaded version, discarding
    // any modifiers and undo history. Used by refresh-from-disk so the slot
    // is never transiently null while the new data loads.
    void resetToNewData (const ImageItemDataPtr& newData);
    void undoLastChange ();

    // Push an arbitrary undo callback. Used by tools that mutate state
    // outside the modifier chain (e.g. annotation edits).
    void pushUndoAction (std::function<void(void)>&& undoFunc);

    // Annotation access. Tools should call markAnnotationsDirty() after
    // mutating the document so the final-layer compositor reruns.
    AnnotationDocument& annotations();
    const AnnotationDocument& annotations() const;
    bool hasAnnotations() const;
    void markAnnotationsDirty();

private:
    void clearIntermediateModifiersData ();
    void syncItemMetadataFromFinalData();
    void compositeAnnotationLayer(AnnotationRenderer& renderer);

private:
    ImageItemPtr _item;
    ImageItemDataPtr _originalData;
    std::deque<std::unique_ptr<ImageModifier>> _modifiers;
    std::deque<ImageAction> _actions;

    std::unique_ptr<AnnotationDocument> _annotations;
    ImageItemDataPtr _annotatedData;

    bool _modifiersChangedSinceLastUpdate = false;
    bool _annotationsDirty = false;
};
using ModifiedImagePtr = std::shared_ptr<ModifiedImage>;

enum class LevelsChannel
{
    Luma,
    Red,
    Green,
    Blue,
};

struct LevelsParams
{
    int inputBlack = 0;
    int inputWhite = 255;
    float gamma = 1.0f;
    int outputBlack = 0;
    int outputWhite = 255;
};

struct LevelsAdjustmentParams
{
    LevelsChannel target = LevelsChannel::Luma;
    LevelsParams lumaLevels;
    LevelsParams redLevels;
    LevelsParams greenLevels;
    LevelsParams blueLevels;
};

struct CompiledLevelsLut
{
    std::array<uint8_t, 256> r = {};
    std::array<uint8_t, 256> g = {};
    std::array<uint8_t, 256> b = {};
};

CompiledLevelsLut compileLevelsLut(const LevelsAdjustmentParams& params);

bool levelsParamsIdentity(const LevelsParams& p);
LevelsParams sanitizedLevelsParams(LevelsParams p);

class LevelsModifier : public ImageModifier
{
public:
    LevelsModifier(const LevelsAdjustmentParams& params)
        : _params(params)
    {}

public:
    virtual void apply (const ImageItemData& input, ImageItemData& output) override;

private:
    LevelsAdjustmentParams _params;
};

struct OneShotColorParams
{
    enum class Kind { Invert, Grayscale, SwapRB, SwapRG, SwapGB, HistEq, LabelColorize };
    Kind kind = Kind::Invert;

    enum class InvertTarget { RGB, Red, Green, Blue };
    InvertTarget invertTarget = InvertTarget::RGB;

    enum class GrayscaleMode { LumaSRGB, Red, Green, Blue };
    GrayscaleMode grayscaleMode = GrayscaleMode::LumaSRGB;

    struct LabelColorize
    {
        enum class BackgroundMode { Preserve, Black, Transparent };

        uint32_t seed = 1;
        uint8_t backgroundValue = 0;
        BackgroundMode backgroundMode = BackgroundMode::Preserve;
    };
    LabelColorize labelColorize;
};

class OneShotColorModifier : public ImageModifier
{
public:
    OneShotColorModifier(const OneShotColorParams& params) : _params(params) {}

public:
    virtual void apply (const ImageItemData& input, ImageItemData& output) override;

private:
    OneShotColorParams _params;
};

class HueShiftModifier : public ImageModifier
{
public:
    HueShiftModifier(float hueDegrees) : _hueDegrees(hueDegrees) {}

public:
    virtual void apply (const ImageItemData& input, ImageItemData& output) override;

private:
    float _hueDegrees;
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
    virtual void apply (const ImageItemData& input, ImageItemData& output) override;

private:
    Params _params;
};
using CropImageModifierPtr = std::shared_ptr<CropImageModifier>;

class ResizeImageModifier : public ImageModifier
{
public:
    struct Params
    {
        int targetWidth = -1;
        int targetHeight = -1;
    };

    ResizeImageModifier (int targetWidth, int targetHeight) : _params ({targetWidth, targetHeight})
    {}

    const Params& params () const { return _params; }

public:
    virtual void apply (const ImageItemData& input, ImageItemData& output) override;

private:
    Params _params;
};
using ResizeImageModifierPtr = std::shared_ptr<ResizeImageModifier>;

} // zv
