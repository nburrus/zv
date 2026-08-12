//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Modifiers.h"

#include <libzv/Annotations.h>
#include <libzv/ImguiUtils.h>
#include <libzv/Utils.h>
#include <libzv/MathUtils.h>

#include <imgui.h>

#include <stb_image_resize.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>

namespace zv
{

ModifiedImage::ModifiedImage(const ImageItemPtr& item, const ImageItemDataPtr& originalData)
    : _item(item)
    , _originalData(originalData)
    , _annotations(std::make_unique<AnnotationDocument>())
{}

ModifiedImage::~ModifiedImage() = default;

const ImageItemDataPtr& ModifiedImage::preAnnotationData() const
{
    if (!_modifiers.empty())
        return _modifiers.back()->output();
    return _originalData;
}

const ImageItemDataPtr& ModifiedImage::finalData() const
{
    if (_annotatedData && _annotatedData->status == ImageItemData::Status::Ready)
        return _annotatedData;
    return preAnnotationData();
}

bool ModifiedImage::hasPendingChanges() const
{
    return !_modifiers.empty() || hasAnnotations();
}

AnnotationDocument& ModifiedImage::annotations()
{
    return *_annotations;
}

const AnnotationDocument& ModifiedImage::annotations() const
{
    return *_annotations;
}

bool ModifiedImage::hasAnnotations() const
{
    return _annotations && !_annotations->empty();
}

void ModifiedImage::markAnnotationsDirty()
{
    _annotationsDirty = true;
}

bool ModifiedImage::saveChanges (const std::string& outputPath)
{
    ImageItemDataPtr maybeModifiedData = finalData();

    if (!writeImageFile (outputPath, *(maybeModifiedData->cpuData)))
        return false;

    _item->fillFromFilePath (outputPath);
    _item->alreadyModifiedAndSaved = true;

    if (maybeModifiedData != _originalData)
    {
        *_originalData = *maybeModifiedData;
        _modifiers.clear ();
        _annotations->clear();
        _annotatedData.reset();
        _annotationsDirty = false;
    }

    return true;
}

void ModifiedImage::discardChanges ()
{
    if (_modifiers.empty() && !hasAnnotations())
        return;
    _modifiers.clear();
    _annotations->clear();
    _annotatedData.reset();
    _modifiersChangedSinceLastUpdate = true;
    _annotationsDirty = true;
}

void ModifiedImage::resetToNewData (const ImageItemDataPtr& newData)
{
    _originalData = newData;
    _modifiers.clear();
    _actions.clear();
    _annotations->clear();
    _annotatedData.reset();
    _modifiersChangedSinceLastUpdate = true;
    _annotationsDirty = true;
}

bool ModifiedImage::updateModifiers()
{
    if (!_originalData)
        return false;

    bool originalChanged = _originalData->update();

    if (!originalChanged && !_modifiersChangedSinceLastUpdate)
        return false;

    // Reapply the modification pipeline if the original was reloaded.
    if (originalChanged && _originalData->cpuData->hasData())
    {
        ImageItemDataPtr input = _originalData;
        for (auto& modifier : _modifiers)
        {
            modifier->apply (input);
            input = modifier->output ();
        }
        _annotationsDirty = true;
    }

    clearIntermediateModifiersData ();
    _modifiersChangedSinceLastUpdate = false;

    syncItemMetadataFromFinalData();

    return true;
}

bool ModifiedImage::updateAnnotations(AnnotationRenderer& renderer)
{
    if (!_originalData)
        return false;
    if (!_annotationsDirty)
        return false;

    const ImageItemDataPtr& base = preAnnotationData();
    if (hasAnnotations() && base && base->cpuData && base->cpuData->hasData())
    {
        compositeAnnotationLayer(renderer);
    }
    else
    {
        _annotatedData.reset();
    }
    _annotationsDirty = false;
    syncItemMetadataFromFinalData();
    return true;
}

void ModifiedImage::syncItemMetadataFromFinalData()
{
    const ImageItemDataPtr& currentData = finalData();
    if (currentData && currentData->cpuData && currentData->cpuData->hasData())
    {
        _item->metadata.width = currentData->cpuData->width();
        _item->metadata.height = currentData->cpuData->height();
    }
}

void ModifiedImage::addModifier (std::unique_ptr<ImageModifier> modifier)
{
    const ImageItemDataPtr& base = preAnnotationData();
    if (base && base->status == ImageItemData::Status::Ready)
    {
        modifier->apply (base);
    }
    _modifiers.push_back (std::move(modifier));
    _modifiersChangedSinceLastUpdate = true;
    _annotatedData.reset();
    _annotationsDirty = true;

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
    _annotatedData.reset();
    _annotationsDirty = true;
}

void ModifiedImage::undoLastChange ()
{
    if (_actions.empty())
        return;
    _actions.back().undo();
    _actions.pop_back();
}

void ModifiedImage::pushUndoAction (std::function<void(void)>&& undoFunc)
{
    _actions.push_back(ImageAction(std::move(undoFunc)));
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

void ModifiedImage::compositeAnnotationLayer(AnnotationRenderer& renderer)
{
    const ImageItemDataPtr& base = preAnnotationData();
    if (!base || !base->cpuData || !base->cpuData->hasData())
        return;

    if (!_annotatedData)
        _annotatedData = std::make_shared<ImageItemData>();

    const int w = base->cpuData->width();
    const int h = base->cpuData->height();

    renderer.beginRendering(*base);

    AnnotationRenderTransform transform;
    transform.imageWidth = w;
    transform.imageHeight = h;
    transform.imagePixelToScreenPixel = 1.0f;
    transform.textureToScreen = [w, h](const Point& p) {
        return ImVec2(float(p.x * w), float(p.y * h));
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (const auto& el : _annotations->elements())
    {
        renderAnnotationElement(drawList, el, transform);
    }

    renderer.endRendering(*_annotatedData);
}

} // zv

namespace zv
{

bool levelsParamsIdentity(const LevelsParams& p)
{
    return p.inputBlack == 0 && p.inputWhite == 255 && p.gamma == 1.0f &&
           p.outputBlack == 0 && p.outputWhite == 255;
}

LevelsParams sanitizedLevelsParams(LevelsParams p)
{
    p.inputBlack = std::clamp(p.inputBlack, 0, 254);
    p.inputWhite = std::clamp(p.inputWhite, 1, 255);
    if (p.inputBlack >= p.inputWhite)
        p.inputBlack = p.inputWhite - 1;
    p.gamma = std::clamp(p.gamma, 0.10f, 10.0f);
    p.outputBlack = std::clamp(p.outputBlack, 0, 255);
    p.outputWhite = std::clamp(p.outputWhite, 0, 255);
    if (p.outputBlack > p.outputWhite)
        std::swap(p.outputBlack, p.outputWhite);
    return p;
}

static std::array<uint8_t, 256> compileLevelsChannelLut(const LevelsParams& params)
{
    const LevelsParams p = sanitizedLevelsParams(params);
    std::array<uint8_t, 256> lut = {};
    const double inBlack = static_cast<double>(p.inputBlack);
    const double inWhite = static_cast<double>(p.inputWhite);
    const double outBlack = static_cast<double>(p.outputBlack);
    const double outWhite = static_cast<double>(p.outputWhite);
    const double gamma = static_cast<double>(p.gamma);
    const double invInputRange = 1.0 / std::max(1.0, inWhite - inBlack);

    for (int i = 0; i < 256; ++i)
    {
        double normalized = (static_cast<double>(i) - inBlack) * invInputRange;
        normalized = std::clamp(normalized, 0.0, 1.0);
        normalized = std::pow(normalized, gamma);
        const double out = outBlack + normalized * (outWhite - outBlack);
        lut[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(out)), 0, 255));
    }
    return lut;
}

static uint8_t lumaFromPixel(const PixelSRGBA& p)
{
    return uint8_t(std::round(0.2126f*p.r + 0.7152f*p.g + 0.0722f*p.b));
}

static std::array<uint8_t, 256> compileHistogramEqualizationLut(const ImageSRGBA& image)
{
    std::array<uint64_t, 256> histogram = {};
    for (int r = 0; r < image.height(); ++r)
    {
        const PixelSRGBA* row = image.atRowPtr(r);
        for (int c = 0; c < image.width(); ++c)
            histogram[lumaFromPixel(row[c])]++;
    }

    std::array<uint8_t, 256> lut = {};
    uint64_t cdfMin = 0;
    for (int i = 0; i < 256; ++i)
    {
        if (histogram[i] != 0)
        {
            cdfMin = histogram[i];
            break;
        }
    }

    const uint64_t pixelCount = static_cast<uint64_t>(image.width()) * image.height();
    if (pixelCount == 0 || cdfMin == 0 || cdfMin >= pixelCount)
    {
        for (int i = 0; i < 256; ++i)
            lut[i] = static_cast<uint8_t>(i);
        return lut;
    }

    uint64_t cdf = 0;
    const double denom = static_cast<double>(pixelCount - cdfMin);
    for (int i = 0; i < 256; ++i)
    {
        cdf += histogram[i];
        const uint64_t adjustedCdf = (cdf > cdfMin) ? (cdf - cdfMin) : 0;
        const double mapped = 255.0 * (static_cast<double>(adjustedCdf) / denom);
        lut[i] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(mapped)), 0, 255));
    }
    return lut;
}

static bool isGrayLikeLabelMap(const ImageSRGBA& image)
{
    const uint64_t pixelCount = static_cast<uint64_t>(image.width()) * image.height();
    if (pixelCount == 0)
        return false;

    constexpr uint64_t targetSamples = 10000;
    const uint64_t stride = std::max<uint64_t>(1, pixelCount / targetSamples);
    for (uint64_t i = 0; i < pixelCount; i += stride)
    {
        const int row = static_cast<int>(i / static_cast<uint64_t>(image.width()));
        const int col = static_cast<int>(i % static_cast<uint64_t>(image.width()));
        const PixelSRGBA& p = image.atRowPtr(row)[col];
        if (std::abs(int(p.r) - int(p.g)) > 2 ||
            std::abs(int(p.r) - int(p.b)) > 2)
            return false;
    }
    return true;
}

static std::array<PixelSRGBA, 256> compileLabelColorizePalette(uint32_t seed)
{
    std::array<PixelSRGBA, 256> palette = {};
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> hueDist(0, 359);
    std::uniform_int_distribution<int> satDist(58, 92);
    std::uniform_int_distribution<int> valDist(62, 96);

    for (int i = 0; i < 256; ++i)
    {
        const float h = hueDist(rng) / 60.f;
        const float s = satDist(rng) / 100.f;
        const float v = valDist(rng) / 100.f;
        const int hi = static_cast<int>(std::floor(h)) % 6;
        const float f = h - std::floor(h);
        const float p = v * (1.f - s);
        const float q = v * (1.f - s * f);
        const float t = v * (1.f - s * (1.f - f));

        float r = 0.f, g = 0.f, b = 0.f;
        switch (hi)
        {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
        palette[i] = {uint8_t(std::round(r * 255.f)),
                      uint8_t(std::round(g * 255.f)),
                      uint8_t(std::round(b * 255.f)),
                      255};
    }
    return palette;
}

CompiledLevelsLut compileLevelsLut(const LevelsAdjustmentParams& params)
{
    CompiledLevelsLut lut;
    const auto luma = compileLevelsChannelLut(params.lumaLevels);
    lut.r = luma;
    lut.g = luma;
    lut.b = luma;

    if (!levelsParamsIdentity(params.redLevels))
        lut.r = compileLevelsChannelLut(params.redLevels);
    if (!levelsParamsIdentity(params.greenLevels))
        lut.g = compileLevelsChannelLut(params.greenLevels);
    if (!levelsParamsIdentity(params.blueLevels))
        lut.b = compileLevelsChannelLut(params.blueLevels);

    return lut;
}

void LevelsModifier::apply (const ImageItemData& input, ImageItemData& output)
{
    const auto& inIm = *input.cpuData;
    output.cpuData = std::make_shared<ImageSRGBA>(inIm.width(), inIm.height());
    output.textureData = {};
    output.status = ImageItemData::Status::Ready;

    auto& outIm = *output.cpuData;
    const auto lut = compileLevelsLut(_params);
    for (int r = 0; r < inIm.height(); ++r)
    {
        const PixelSRGBA* inRow = inIm.atRowPtr(r);
        PixelSRGBA* outRow = outIm.atRowPtr(r);
        for (int c = 0; c < inIm.width(); ++c)
        {
            const PixelSRGBA& in = inRow[c];
            outRow[c] = PixelSRGBA(lut.r[in.r], lut.g[in.g], lut.b[in.b], in.a);
        }
    }
}

void OneShotColorModifier::apply (const ImageItemData& input, ImageItemData& output)
{
    const auto& inIm = *input.cpuData;
    output.cpuData = std::make_shared<ImageSRGBA>(inIm.width(), inIm.height());
    output.textureData = {};
    output.status = ImageItemData::Status::Ready;
    auto& outIm = *output.cpuData;
    const bool labelColorizeCanApply = (_params.kind != OneShotColorParams::Kind::LabelColorize) ||
                                       isGrayLikeLabelMap(inIm);
    const auto labelPalette = (_params.kind == OneShotColorParams::Kind::LabelColorize && labelColorizeCanApply)
        ? compileLabelColorizePalette(_params.labelColorize.seed)
        : std::array<PixelSRGBA, 256>{};
    const auto histEqLut = (_params.kind == OneShotColorParams::Kind::HistEq)
        ? compileHistogramEqualizationLut(inIm)
        : std::array<uint8_t, 256>{};

    for (int r = 0; r < inIm.height(); ++r)
    {
        const PixelSRGBA* inRow = inIm.atRowPtr(r);
        PixelSRGBA* outRow = outIm.atRowPtr(r);
        for (int c = 0; c < inIm.width(); ++c)
        {
            const PixelSRGBA& in = inRow[c];
            PixelSRGBA out = in;
            switch (_params.kind)
            {
                case OneShotColorParams::Kind::Invert:
                    switch (_params.invertTarget)
                    {
                        case OneShotColorParams::InvertTarget::RGB:
                            out = {uint8_t(255-in.r), uint8_t(255-in.g), uint8_t(255-in.b), in.a};
                            break;
                        case OneShotColorParams::InvertTarget::Red:
                            out = {uint8_t(255-in.r), in.g, in.b, in.a};
                            break;
                        case OneShotColorParams::InvertTarget::Green:
                            out = {in.r, uint8_t(255-in.g), in.b, in.a};
                            break;
                        case OneShotColorParams::InvertTarget::Blue:
                            out = {in.r, in.g, uint8_t(255-in.b), in.a};
                            break;
                    }
                    break;
                case OneShotColorParams::Kind::Grayscale:
                {
                    uint8_t gray = 0;
                    switch (_params.grayscaleMode)
                    {
                        case OneShotColorParams::GrayscaleMode::LumaSRGB:
                            gray = uint8_t(std::round(0.2126f*in.r + 0.7152f*in.g + 0.0722f*in.b));
                            break;
                        case OneShotColorParams::GrayscaleMode::Red:   gray = in.r; break;
                        case OneShotColorParams::GrayscaleMode::Green:  gray = in.g; break;
                        case OneShotColorParams::GrayscaleMode::Blue:   gray = in.b; break;
                    }
                    out = {gray, gray, gray, in.a};
                    break;
                }
                case OneShotColorParams::Kind::SwapRB:
                    out = {in.b, in.g, in.r, in.a};
                    break;
                case OneShotColorParams::Kind::SwapRG:
                    out = {in.g, in.r, in.b, in.a};
                    break;
                case OneShotColorParams::Kind::SwapGB:
                    out = {in.r, in.b, in.g, in.a};
                    break;
                case OneShotColorParams::Kind::HistEq:
                {
                    const uint8_t eq = histEqLut[lumaFromPixel(in)];
                    out = {eq, eq, eq, in.a};
                    break;
                }
                case OneShotColorParams::Kind::LabelColorize:
                {
                    if (!labelColorizeCanApply)
                        break;

                    const uint8_t label = in.r;
                    if (label == _params.labelColorize.backgroundValue)
                    {
                        switch (_params.labelColorize.backgroundMode)
                        {
                            case OneShotColorParams::LabelColorize::BackgroundMode::Preserve:
                                out = in;
                                break;
                            case OneShotColorParams::LabelColorize::BackgroundMode::Black:
                                out = {0, 0, 0, in.a};
                                break;
                            case OneShotColorParams::LabelColorize::BackgroundMode::Transparent:
                                out = {0, 0, 0, 0};
                                break;
                        }
                    }
                    else
                    {
                        const PixelSRGBA color = labelPalette[label];
                        out = {color.r, color.g, color.b, in.a};
                    }
                    break;
                }
            }
            outRow[c] = out;
        }
    }
}

void HueShiftModifier::apply (const ImageItemData& input, ImageItemData& output)
{
    const auto& inIm = *input.cpuData;
    output.cpuData = std::make_shared<ImageSRGBA>(inIm.width(), inIm.height());
    output.textureData = {};
    output.status = ImageItemData::Status::Ready;
    auto& outIm = *output.cpuData;

    const float shift = _hueDegrees / 360.f;
    for (int r = 0; r < inIm.height(); ++r)
    {
        const PixelSRGBA* inRow = inIm.atRowPtr(r);
        PixelSRGBA* outRow = outIm.atRowPtr(r);
        for (int c = 0; c < inIm.width(); ++c)
        {
            const PixelSRGBA& in = inRow[c];
            const float rf = in.r / 255.f, gf = in.g / 255.f, bf = in.b / 255.f;

            // RGB -> HSV
            const float maxC = std::max({rf, gf, bf});
            const float minC = std::min({rf, gf, bf});
            const float delta = maxC - minC;
            float h = 0.f;
            if (delta > 1e-6f)
            {
                if (maxC == rf)      h = std::fmod((gf - bf) / delta, 6.f) / 6.f;
                else if (maxC == gf) h = ((bf - rf) / delta + 2.f) / 6.f;
                else                 h = ((rf - gf) / delta + 4.f) / 6.f;
                if (h < 0.f) h += 1.f;
            }
            const float s = (maxC > 1e-6f) ? delta / maxC : 0.f;
            const float v = maxC;

            // Shift hue
            h = std::fmod(h + shift + 1.f, 1.f);

            // HSV -> RGB
            const float hh = h * 6.f;
            const int   hi = static_cast<int>(hh);
            const float ff = hh - hi;
            const float p  = v * (1.f - s);
            const float q2 = v * (1.f - s * ff);
            const float t  = v * (1.f - s * (1.f - ff));
            float ro, go, bo;
            switch (hi % 6)
            {
                case 0: ro=v; go=t; bo=p; break;
                case 1: ro=q2;go=v; bo=p; break;
                case 2: ro=p; go=v; bo=t; break;
                case 3: ro=p; go=q2;bo=v; break;
                case 4: ro=t; go=p; bo=v; break;
                default:ro=v; go=p; bo=q2;break;
            }
            outRow[c] = {uint8_t(std::round(ro*255.f)),
                         uint8_t(std::round(go*255.f)),
                         uint8_t(std::round(bo*255.f)),
                         in.a};
        }
    }
}

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

void CropImageModifier::apply (const ImageItemData& input, ImageItemData& output)
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
    // Rect bottom-right is exclusive (origin + size), so max extent is width/height.
    br.x = keepInRange(br.x, alignedRect.origin.x + 1., double(width));
    br.y = keepInRange(br.y, alignedRect.origin.y + 1., double(height));
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

void ResizeImageModifier::apply (const ImageItemData& input, ImageItemData& output)
{
    const auto& inIm = (*input.cpuData);
    const int inW = inIm.width();
    const int inH = inIm.height();

    output.cpuData = std::make_shared<ImageSRGBA>(_params.targetWidth, _params.targetHeight);
    auto& outIm = *output.cpuData;
    const int outW = outIm.width();
    const int outH = outIm.height();

    // Resize the image using stb
    stbir_resize_uint8_srgb((const unsigned char*)inIm.rawBytes(), inW, inH, inIm.bytesPerRow(),
                            (unsigned char*)outIm.data(), outW, outH, outIm.bytesPerRow(),
                            4, 3, 0);

    output.textureData = {};
    output.status = ImageItemData::Status::Ready;
}

} // zv
