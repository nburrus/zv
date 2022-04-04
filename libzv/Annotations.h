//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageList.h>

struct ImGuiContext;

namespace zv
{

class AnnotationRenderer
{
public:
    void initializeFromCurrentContext ();
    void shutdown ();

    void beginRendering (const ImageItemDataPtr& inputData);
    void endRendering (ImageItemDataPtr& outputData);

private:
    void enableContext ();
    void disableContext ();

private:
    ImGuiContext* _sharedImguiContext = nullptr;
    ImGuiContext* _prevContext = nullptr;
};

class ImageAnnotation
{
public:
    virtual ~ImageAnnotation () = default;

public:
    virtual void render () = 0;
};

class LineAnnotation : public ImageAnnotation
{
public:
    LineAnnotation (const Point& p1, const Point& p2) : _p1 (p1), _p2 (p2) {}
    virtual void render () override;

private:
    Point _p1, _p2;
};

} // namespace zv
