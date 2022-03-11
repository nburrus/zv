//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/Image.h>

#include <memory>
#include <functional>
#include <vector>
#include <string>

namespace zv
{

class ImageWindow;
class ControlsWindow;
class ImageList;

struct ViewerState
{
    bool helpRequested = false;
    bool controlsRequested = false;
    bool dismissRequested = false;
    bool openImageRequested = false;
};

class Viewer
{
public:
    Viewer(const std::string& name, int index);
    ~Viewer();
    
    // Call it once, creates the context, etc.
    bool initialize ();
    
    void shutdown ();
    
    bool exitRequested () const;

    void renderFrame ();

public:
    void addImageFromFile (const std::string& imagePath);
    void addImageData (const ImageSRGBA& image, const std::string& imageName, int insertPos = -1, bool replaceExisting = false);
    void addPastedImage ();
        
protected:
    // Controller-like global methods that member windows can call.
    void onDismissRequested ();
    void onHelpRequested ();
    void onControlsRequested ();
    void onImageWindowGeometryUpdated (const Rect& geometry);
    void onOpenImage ();

    ImageWindow* imageWindow();
    ControlsWindow* controlsWindow();
    ImageList& imageList();

    int globalIndex () const;
    const std::string& name() const;

    friend class ImageWindow;
    friend class ControlsWindow;

private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
