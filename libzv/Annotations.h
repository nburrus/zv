//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageList.h>
#include <libzv/ImguiUtils.h>
#include <libzv/Modifiers.h>


namespace zv
{

class AnnotationRenderer
{
public:
    AnnotationRenderer ();
    ~AnnotationRenderer ();
    
public:
    void initializeFromCurrentContext ();
    void shutdown ();

    void beginRendering (const ImageItemData& inputData);
    void endRendering (ImageItemData& outputData);

private:
    void enableContext ();
    void disableContext ();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class AnnotationModifier : public ImageModifier
{
public:
    virtual void apply (const ImageItemData& input, ImageItemData& output, AnnotationRenderer& annotationRenderer) override;

protected:
    virtual void renderAnnotation (int imageWidth, int imageHeight) = 0;
};

class LineAnnotation : public AnnotationModifier
{
public:
    struct Params
    {
        // The coordinates are in the uv texture ([0,1] range).
        Line textureLine = Line(Point(0.1,0.1), Point(0.5,0.5));
        int lineWidth = 2;
        ImColor color = ImColor (ImVec4(1,1,0,1));

        Line imageAlignedTextureLine (int width, int height) const;
        Line validImageLineForSize(int width, int height) const;

        int numControlPoints () const { return 2; }
        void updateControlPoint (int idx, const Point& p, int imageWidth, int imageHeight);

        static Point controlPointPos (int idx, const Line& imageAlignedTextureLine);
    };
    
public:
    LineAnnotation (const Params& params) : _params (params) {}
    virtual void renderAnnotation (int imageWidth, int imageHeight) override;

private:
    Params _params;
};

} // namespace zv
