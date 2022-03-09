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

#define IMGUI_DEFINE_MATH_OPERATORS 1
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

namespace zv
{

struct Viewer::Impl
{
    Impl (Viewer& that) : that(that) {}
    Viewer& that;
    RateLimit rateLimit;
    
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
        
        if (state.controlsRequested || state.openImageRequested)
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
            
            state.controlsRequested = false;
        }

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

Viewer::Viewer()
: impl (new Impl(*this))
{
    
}

Viewer::~Viewer()
{
    shutdown();
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

void Viewer::renderFrame (double minDuration)
{
    impl->renderFrame();
    if (!isnan(minDuration))
    {
        impl->rateLimit.sleepIfNecessary (minDuration);
    }
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
    
    glfwTerminate();
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

void Viewer::onControlsRequested()
{
    impl->state.controlsRequested = true;
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

void Viewer::addImageFromFile (const std::string& imagePath)
{
    impl->imageList.addImage (imageItemFromPath(imagePath));
}

void Viewer::addImageData (const ImageSRGBA& image, const std::string& imageName, bool replaceExisting)
{    
    impl->imageList.addImage (imageItemFromData (image, imageName), replaceExisting);
}

void Viewer::addPastedImage ()
{
    // Keep that old code around for now.
#if 0
    if (parser.get<bool>("--paste"))
    {
        impl->imagePath = "Pasted from clipboard";

        if (!clip::has(clip::image_format()))
        {
            std::cerr << "Clipboard doesn't contain an image" << std::endl;
            return false;
        }

        clip::image clipImg;
        if (!clip::get_image(clipImg))
        {
            std::cout << "Error getting image from clipboard\n";
            return false;
        }

        clip::image_spec spec = clipImg.spec();

        std::cerr << "Image in clipboard "
            << spec.width << "x" << spec.height
            << " (" << spec.bits_per_pixel << "bpp)\n"
            << "Format:" << "\n"
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
            impl->im.ensureAllocatedBufferForSize((int)spec.width, (int)spec.height);
            impl->im.copyDataFrom((uint8_t*)clipImg.data(), (int)spec.bytes_per_row, (int)spec.width, (int)spec.height);
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
    }
#endif
}

} // zv
