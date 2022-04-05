//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Modifiers.h"

#include <libzv/ImguiUtils.h>
#include <libzv/Utils.h>

namespace zv
{

void ModifiedImage::discardChanges ()
{
    if (_modifiers.empty ())
        return;
    _modifiers.clear ();
    _modifiersOrAnnotationsChangedSinceLastUpdate = true;
}

bool ModifiedImage::update ()
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

        renderAnnotations ();
    }
    
    clearIntermediateModifiersData ();
    _modifiersOrAnnotationsChangedSinceLastUpdate = false;

    const ImageItemDataPtr& currentData = data();
    if (currentData->cpuData->hasData())
    {
        _item->metadata.width = currentData->cpuData->width();
        _item->metadata.height = currentData->cpuData->height();
    }

    return true;
}

void ModifiedImage::addModifier (std::unique_ptr<ImageModifier> modifier)
{
    bool applyNow = hasValidData();
    if (applyNow)
    {
        modifier->apply (dataWithoutAnnotations());
    }
    _modifiers.push_back (std::move(modifier));
    _modifiersOrAnnotationsChangedSinceLastUpdate = true;
    
    _actions.push_back(ImageAction([this]() {
        removeLastModifier();
    }));
    
    if (applyNow)
    {
        renderAnnotations();
    }
}

void ModifiedImage::addAnnotation (std::unique_ptr<ImageAnnotation> annotation)
{
    _annotations.push_back (std::move(annotation));
    renderAnnotations ();
    _modifiersOrAnnotationsChangedSinceLastUpdate = true;
    _actions.push_back(ImageAction([this]() {
        removeLastAnnotation();
    }));
}

void ModifiedImage::removeLastModifier()
{
    if (_modifiers.empty())
        return;
    _modifiers.pop_back();
    _modifiersOrAnnotationsChangedSinceLastUpdate = true;
}

void ModifiedImage::removeLastAnnotation()
{
    if (_annotations.empty())
        return;
    _annotations.pop_back();
    _modifiersOrAnnotationsChangedSinceLastUpdate = true;
}

void ModifiedImage::undoLastChange ()
{
    if (_actions.empty())
        return;
    _actions.back().undo();
    _actions.pop_back();
}

void ModifiedImage::clearIntermediateModifiersData ()
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

void ModifiedImage::renderAnnotations ()
{
    if (_annotations.empty())
    {
        _annotatedData = nullptr;
        return;
    }

    if (!_annotatedData)
        _annotatedData = std::make_shared<ImageItemData>();
    
    ImageItemDataPtr inputData = dataWithoutAnnotations();
    if (inputData->status != ImageItemData::Status::Ready)
        return;

    _annotationRenderer.beginRendering (*inputData);
    for (auto& it: _annotations)
        it->render ();
    _annotationRenderer.endRendering (*_annotatedData);
    _modifiersOrAnnotationsChangedSinceLastUpdate = true;
}

} // zv

namespace zv
{

void RotateImageModifier::apply (const ImageItemData& input, ImageItemData& output)
{
    const auto& inIm = (*input.cpuData);
    const int inW = inIm.width();
    const int inH = inIm.height();

    if (_angle == Angle::Angle_90) // Rotate Right
    {
        output.cpuData = std::make_shared<ImageSRGBA>(inH, inW);
        auto& outIm = *output.cpuData;
        const int outW = outIm.width();
        const int outH = outIm.height();
        for (int r = 0; r < outH; ++r)
        {
            PixelSRGBA* rowPtr = outIm.atRowPtr(r);
            for (int c = 0; c < outW; ++c)
            {
                const int rowInIn = inH-c-1;
                const int colInIn = r;
                rowPtr[c] = inIm(colInIn, rowInIn);
            }
        }
    }
    else if (_angle == Angle::Angle_270) // Rotate Left
    {
        output.cpuData = std::make_shared<ImageSRGBA>(inH, inW);
        auto& outIm = *output.cpuData;
        const int outW = outIm.width();
        const int outH = outIm.height();
        for (int r = 0; r < outH; ++r)
        {
            PixelSRGBA* rowPtr = outIm.atRowPtr(r);
            for (int c = 0; c < outW; ++c)
            {
                const int rowInIn = c;
                const int colInIn = inW-r-1;
                rowPtr[c] = inIm(colInIn, rowInIn);
            }
        }
    }
    else if (_angle == Angle::Angle_180) // Upside down
    {
        output.cpuData = std::make_shared<ImageSRGBA>(inW, inH);
        auto& outIm = *output.cpuData;
        for (int r = 0; r < inH; ++r)
        {
            PixelSRGBA* outRowPtr = outIm.atRowPtr(r);
            const PixelSRGBA* inRowPtr = inIm.atRowPtr(inH-r-1);
            for (int c = 0; c < inW; ++c)
            {
                outRowPtr[c] = inRowPtr[inW-c-1];
            }
        }
    }

    output.textureData = {};
    output.status = ImageItemData::Status::Ready;
}

} // zv
