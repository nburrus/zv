//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageList.h>

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

    void beginRendering (ImageItemData& inputData);
    void endRendering (ImageItemData& outputData);

private:
    void enableContext ();
    void disableContext ();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
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
