//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/Modifiers.h>
#include <libzv/Annotations.h>
#include <libzv/ImguiUtils.h>
#include <libzv/Image.h>

namespace zv
{

struct InteractiveToolRenderingContext
{
    WidgetToImageTransform widgetToImageTransform;
    int imageWidth = -1;
    int imageHeight = -1;
    bool firstValidImageIndex = false;
};

struct ImageRenderingContext
{
    uint32_t textureId = 0;
    int width = 0;
    int height = 0;
};

struct ImageRenderingOverride
{
    uint32_t overrideTextureId = 0; // 0 = use original texture
};

class InteractiveTool
{
public:
    enum class Kind
    {
        Modifier,
        Annotation
    };

public:
    InteractiveTool(Kind kind) : _kind(kind) {}
    virtual ~InteractiveTool() = default;

    Kind kind() const { return _kind; }

    virtual void renderAsActiveTool (const InteractiveToolRenderingContext& context) = 0;
    virtual void renderControls (const ImageSRGBA& firstIm) = 0;
    virtual void addToImage (ModifiedImage& image) = 0;

    // Called before rendering each image. Override to substitute a post-processed texture.
    virtual ImageRenderingOverride overrideImageRendering (const ImageRenderingContext&)
    { return {}; }

private:
    const Kind _kind;
};

using InteractiveToolUniquePtr = std::unique_ptr<InteractiveTool>;

class CropTool : public InteractiveTool
{
public:    
    CropTool() : InteractiveTool (Kind::Modifier) {}

    virtual void renderAsActiveTool(const InteractiveToolRenderingContext &context) override;

    virtual void renderControls(const ImageSRGBA& firstIm) override;

    virtual void addToImage(ModifiedImage& image) override
    {
        image.addModifier(std::make_unique<CropImageModifier>(_params));
    }

private:
    CropImageModifier::Params _params;
    std::vector<ControlPoint> _controlPoints;
};

class LineTool : public InteractiveTool
{
public:
    LineTool() : InteractiveTool (Kind::Annotation) {}

    virtual void renderAsActiveTool(const InteractiveToolRenderingContext &context) override;

    virtual void renderControls(const ImageSRGBA& firstIm) override;

    virtual void addToImage(ModifiedImage& image) override
    {
        image.addModifier(std::make_unique<LineAnnotation>(_params));
    }

private:
    LineAnnotation::Params _params;
    std::vector<ControlPoint> _controlPoints;
};

} // zv
