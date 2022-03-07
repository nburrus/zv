//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImageViewerController.h>
#include <libzv/Image.h>

#include <memory>
#include <functional>

struct GLFWwindow;

namespace zv
{

// Manages a single ImGuiWindow
class ImageViewer : public ImageViewerController
{
public:
    ImageViewer();
    ~ImageViewer();
    
public:   
    bool initialize (GLFWwindow* parentWindow);
    
    void showImage (const ImageSRGBA& image, const std::string& imagePath);
    
    void shutdown ();
    void runOnce ();
    
    bool isEnabled () const;
    void setEnabled (bool enabled);
    
    bool helpWindowRequested () const;
    void notifyHelpWindowRequestHandled ();

public:
    // Controller methods.
    virtual void onDismissRequested () override;
    virtual void onHelpRequested () override;
    virtual void onControlsRequested () override;
    virtual ImageViewerWindow* activeViewerWindow() override;
    virtual ImageViewerControlsWindow* controlsWindow() override;
    
private:
    struct Impl;
    friend struct Impl;
    std::unique_ptr<Impl> impl;
};

} // zv
