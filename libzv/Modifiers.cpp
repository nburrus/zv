//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Modifiers.h"

#include <libzv/ImguiUtils.h>
#include <libzv/Utils.h>
#include <libzv/MathUtils.h>

namespace zv
{

bool ModifiedImage::saveChanges (const std::string& outputPath)
{
    ImageItemDataPtr maybeModifiedData = data();
    
    if (!writeImageFile (outputPath, *(maybeModifiedData->cpuData)))
        return false;

    _item->fillFromFilePath (outputPath);
    _item->alreadyModifiedAndSaved = true;

    if (maybeModifiedData != _originalData)
    {
        *_originalData = *maybeModifiedData;
        _modifiers.clear ();
    }

    return true;
}

void ModifiedImage::discardChanges ()
{
    if (_modifiers.empty ())
        return;
    _modifiers.clear ();
    _modifiersChangedSinceLastUpdate = true;
}

bool ModifiedImage::update ()
{
    if (!_originalData)
        return false;
    
    bool originalChanged = _originalData->update();

    if (!originalChanged && !_modifiersChangedSinceLastUpdate)
    {
        return false;
    }

    // Reapply the modification pipeline if needed.
    if (originalChanged && _originalData->cpuData->hasData())
    {
        ImageItemDataPtr input = _originalData;
        for (auto& modifier : _modifiers)
        {
            modifier->apply (input, _annotationRenderer);
            input = modifier->output ();
        }
    }
    
    clearIntermediateModifiersData ();
    _modifiersChangedSinceLastUpdate = false;

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
    if (hasValidData())
    {
        modifier->apply (data(), _annotationRenderer);
    }
    _modifiers.push_back (std::move(modifier));
    _modifiersChangedSinceLastUpdate = true;
    
    _actions.push_back(ImageAction([this]() {
        removeLastModifier();
    }));
}

void ModifiedImage::removeLastModifier()
{
    if (_modifiers.empty())
        return;
    _modifiers.pop_back();
    _modifiersChangedSinceLastUpdate = true;
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

} // zv

namespace zv
{

void RotateImageModifier::apply (const ImageItemData& input, ImageItemData& output, AnnotationRenderer&)
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

void CropImageModifier::apply (const ImageItemData& input, ImageItemData& output, AnnotationRenderer&)
{
    const auto& inIm = (*input.cpuData);
    const int inW = inIm.width();
    const int inH = inIm.height();
    
    Rect rect = _params.validImageRectForSize (inW, inH);
    
    output.cpuData = std::make_shared<ImageSRGBA>();
    *output.cpuData = crop (inIm, rect);
    output.textureData = {};
    output.status = ImageItemData::Status::Ready;
}

Rect CropImageModifier::Params::imageAlignedTextureRect (int width, int height) const
{
    Rect rounded;
    auto tl = textureRect.topLeft();
    auto br = textureRect.bottomRight();
    tl.x = int(tl.x*width + 0.5f) / double(width);
    tl.y = int(tl.y*height + 0.5f) / double(height);
    br.x = int(br.x*width + 0.5f) / double(width);
    br.y = int(br.y*height + 0.5f) / double(height);
    
    rounded.origin.x = tl.x;
    rounded.origin.y = tl.y;
    rounded.size.x = br.x - tl.x;
    rounded.size.y = br.y - tl.y;
    return rounded;
}

Rect CropImageModifier::Params::validImageRectForSize(int width, int height) const
{
    Rect alignedRect = imageAlignedTextureRect(width, height);
    alignedRect.scale (width, height);
    alignedRect.origin.x = keepInRange(alignedRect.origin.x, 0., width-2.);
    alignedRect.origin.y = keepInRange(alignedRect.origin.y, 0., height-2.);
    Point br = alignedRect.bottomRight();
    br.x = keepInRange(br.x, alignedRect.origin.x + 1., width - 1.);
    br.y = keepInRange(br.y, alignedRect.origin.y + 1., height - 1.);
    alignedRect.size.x = br.x - alignedRect.origin.x;
    alignedRect.size.y = br.y - alignedRect.origin.y;
    return alignedRect;
}

Point CropImageModifier::Params::controlPointPos (int idx, const Rect& imageAlignedTextureRect)
{
    switch (idx)
    {
        case 0: return imageAlignedTextureRect.topLeft();
        case 1: return imageAlignedTextureRect.topRight();
        case 2: return imageAlignedTextureRect.bottomLeft();
        case 3: return imageAlignedTextureRect.bottomRight();
    }
    return Point(-1,-1);
}

void CropImageModifier::Params::updateControlPoint (int idx, const Point& p, int imageWidth, int imageHeight)
{
    switch (idx)
    {
        case 0: textureRect.moveTopLeft(p); break;
        case 1: textureRect.moveTopRight(p); break;
        case 2: textureRect.moveBottomLeft(p); break;
        case 3: textureRect.moveBottomRight(p); break;
    }

    // makeValid (imageWidth, imageHeight);
}

} // zv
