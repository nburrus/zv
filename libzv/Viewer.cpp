//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Viewer.h"

#include <libzv/PlatformSpecific.h>
#include <libzv/Prefs.h>
#include <libzv/Utils.h>

#include <libzv/ImageList.h>
#include <libzv/ImageWindow.h>
#include <libzv/ControlsWindow.h>
#include <libzv/HelpWindow.h>

#include "GeneratedConfig.h"

#include <clip/clip.h>

#define IMGUI_DEFINE_MATH_OPERATORS 1
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

#include <iostream>

namespace zv
{

struct Viewer::Impl
{
    Impl (Viewer& that, const std::string& name, int index) 
        : that(that),
        name (name),
        globalIndex (index) 
    {}

    Viewer& that;
    const std::string name;
    const int globalIndex;
    
    GLFWwindow* mainContextWindow() { return imageWindow.glfwWindow(); }

    ImageList imageList;    
    ImageWindow imageWindow;
    ControlsWindow controlsWindow;
    HelpWindow helpWindow;

    ViewerState state;
    
    void renderFrame ()
    {
        if (state.helpRequested)
        {
            if (!helpWindow.isInitialized())
            {
                helpWindow.initialize(nullptr); // no need to share a context.
            }
            
            helpWindow.setEnabled(true);
            state.helpRequested = false;
        }
        
        if (helpWindow.isEnabled())
        {
            helpWindow.renderFrame();
        }

        if (state.toggleControlsRequested && controlsWindow.isEnabled())
        {
            controlsWindow.setEnabled(false);
            state.toggleControlsRequested = false;
        }

        bool activateControls = state.toggleControlsRequested && !controlsWindow.isEnabled();
        activateControls |= state.openImageRequested;

        if (activateControls)
        {
            if (!controlsWindow.isInitialized())
            {
                // Need to share the GL context for the cursor overlay.
                controlsWindow.initialize (mainContextWindow(), &that);
                controlsWindow.repositionAfterNextRendering (imageWindow.geometry(), true);
            }

            if (!controlsWindow.isEnabled())
            {
                controlsWindow.setEnabled(true);
            }
            else
            {
                controlsWindow.bringToFront();
            }
        }
        state.toggleControlsRequested = false;

        imageWindow.renderFrame();

        if (controlsWindow.isEnabled())
        {
            if (state.openImageRequested)
            {
                controlsWindow.openImage ();
                state.openImageRequested = false;
            }
            controlsWindow.renderFrame();
        }
    }
};

Viewer::Viewer(const std::string& name, int index)
: impl (new Impl(*this, name, index))
{
}

Viewer::~Viewer()
{
    shutdown();
}

int Viewer::globalIndex () const
{
    return impl->globalIndex;
}

const std::string& Viewer::name() const
{
    return impl->name;    
}

bool Viewer::exitRequested () const
{
    return impl->state.dismissRequested;
}

static void glfw_error_callback(int error, const char* description)
{
    zv_assert (false, "GLFW error %d: %s\n", error, description);
}

bool Viewer::initialize ()
{
    Profiler p ("Viewer::init");

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return false;
    
    p.lap ("glfwInit");

    // Decide GL+GLSL versions
#if __APPLE__
    // GL 3.2 + GLSL 150
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif
        
    impl->imageWindow.initialize (nullptr, this);
    p.lap ("imageWindow");
    
    if (Prefs::showHelpOnStartup())
    {
        impl->state.helpRequested = true;
    }

    return true;
}

void Viewer::renderFrame ()
{
    impl->renderFrame();
}

void Viewer::shutdown ()
{
    if (!impl->mainContextWindow())
        return;

    // Make sure a context is set for the textures.
    glfwMakeContextCurrent(impl->mainContextWindow());
    impl->imageList.releaseGL();
    glfwMakeContextCurrent(nullptr);
    
    impl->imageWindow.shutdown ();
    impl->controlsWindow.shutdown ();
    impl->helpWindow.shutdown();
}

void Viewer::onOpenImage ()
{
    impl->state.openImageRequested = true;
}

void Viewer::onDismissRequested ()
{
    impl->state.dismissRequested = true;
}

void Viewer::onHelpRequested()
{
    impl->state.helpRequested = true;
}

void Viewer::onToggleControls()
{
    impl->state.toggleControlsRequested = true;
}

void Viewer::onImageWindowGeometryUpdated (const Rect& geometry)
{
    impl->controlsWindow.repositionAfterNextRendering (geometry, true /* show by default */);
}

ImageWindow* Viewer::imageWindow()
{
    return &impl->imageWindow;
}

ControlsWindow* Viewer::controlsWindow()
{
    return &impl->controlsWindow;
}

ImageList& Viewer::imageList()
{
    return impl->imageList;
}

ImageId Viewer::selectedImage () const
{
    int firstValidSelection = std::max(0, impl->imageList.selectedRange().firstValidIndex());
    return impl->imageList.imageItemFromIndex(firstValidSelection)->uniqueId;
}

void Viewer::selectImageIndex (int index)
{    
    impl->imageList.setSelectionStart (index);
}

ImageId Viewer::addImageFromFile (const std::string& imagePath, bool replaceExisting)
{
    return impl->imageList.addImage (imageItemFromPath(imagePath), -1, replaceExisting);
}

ImageId Viewer::addImageData (const ImageSRGBA& image, const std::string& imageName, int insertPos, bool replaceExisting)
{    
    return impl->imageList.addImage (imageItemFromData (image, imageName), insertPos, replaceExisting);
}

ImageId Viewer::addImageItem (ImageItemUniquePtr imageItem, int insertPos, bool replaceExisting)
{
    return impl->imageList.addImage (std::move(imageItem), insertPos, replaceExisting);
}

void Viewer::refreshPrettyFileNames ()
{
    impl->imageList.refreshPrettyFileNames();    
}

ImageId Viewer::addPastedImage ()
{
    // Keep that old code around for now.
    if (!clip::has(clip::image_format()))
    {
        zv_dbg ("Clipboard doesn't contain an image");
        return -1;
    }

    clip::image clipImg;
    if (!clip::get_image(clipImg))
    {
        std::cerr << "Error getting image from clipboard\n";
        return false;
    }

    clip::image_spec spec = clipImg.spec();

    zv_dbg("Image in clipboard (%d %d) bpp=%d",
           spec.width,
           spec.height,
           spec.bits_per_pixel);
        
        std::cerr 
        << "Format:"
        << "\n"
        << std::hex
        << "  Red   mask: " << spec.red_mask << "\n"
        << "  Green mask: " << spec.green_mask << "\n"
        << "  Blue  mask: " << spec.blue_mask << "\n"
        << "  Alpha mask: " << spec.alpha_mask << "\n"
        << std::dec
        << "  Red   shift: " << spec.red_shift << "\n"
        << "  Green shift: " << spec.green_shift << "\n"
        << "  Blue  shift: " << spec.blue_shift << "\n"
        << "  Alpha shift: " << spec.alpha_shift << "\n";

    switch (spec.bits_per_pixel)
    {
    case 32:
    {
        ImageSRGBA im;
        im.ensureAllocatedBufferForSize((int)spec.width, (int)spec.height);
        im.copyDataFrom((uint8_t *)clipImg.data(), (int)spec.bytes_per_row, (int)spec.width, (int)spec.height);
        
        // Fill alpha to be sure.
        // Need to be careful about alpha! Need to replace it in some cases.
        // Need to check the alpha mask and shift.
        if (spec.alpha_mask == 0)
        {
            for (int r = 0; r < im.height(); ++r)
            {
                PixelSRGBA *rowPtr = im.atRowPtr(r);
                for (int c = 0; c < im.width(); ++c)
                {
                    rowPtr[c].a = 255;
                }
            }
        }

        addImageData (im, "(pasted)", 0, false /* don't replace */);
        selectImageIndex (0);
        break;
    }

    case 16:
    case 24:
    case 64:
    default:
    {
        std::cerr << "Only 32bpp clipboard supported right now." << std::endl;
        return false;
    }
    }

    return -1;
}

void Viewer::setEventCallback (ImageId imageId, EventCallbackType callback, void* userData)
{
    ImageItemPtr itemPtr = impl->imageList.imageItemFromId(imageId);
    zv_assert (itemPtr, "Could not find a matching image Id");
    if (!itemPtr)
        return;

    itemPtr->eventCallback = callback;
    itemPtr->eventCallbackData = userData;
}

void Viewer::setLayout (int nrows, int ncols)
{
    impl->imageWindow.addCommand (ImageWindow::layoutCommand(nrows, ncols));
}

void Viewer::runAction (ImageWindowAction action)
{
    impl->imageWindow.addCommand (ImageWindow::actionCommand(action));
}

} // zv
