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
        activateControls |= state.pendingChangesConfirmationRequested;

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

            if (state.pendingChangesConfirmationRequested)
            {
                controlsWindow.confirmPendingChanges ();
                state.pendingChangesConfirmationRequested = false;
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

void Viewer::onSavePendingChangesConfirmed(Confirmation result, bool forcePathSelectionOnSave)
{
    if (result == Confirmation::Ok)
    {
        impl->controlsWindow.saveAllChanges (forcePathSelectionOnSave);
    }
    else if (result == Confirmation::Discard)
    {
        impl->imageWindow.discardAllChanges ();
        auto func = std::move(impl->state.funcIfChangesConfirmed);
        impl->state.funcIfChangesConfirmed = nullptr;
        func();
    }
    else if (result == Confirmation::Cancel)
    {
        // Discard the callback.
        impl->state.funcIfChangesConfirmed = {};
    }
}

void Viewer::onAllChangesSaved (bool cancelled)
{
    if (!impl->state.funcIfChangesConfirmed)
        return;
        
    auto func = std::move(impl->state.funcIfChangesConfirmed);
    impl->state.funcIfChangesConfirmed = nullptr;
    
    if (!cancelled)
        func();
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
    const SelectionRange& range = impl->imageList.selectedRange();
    const int firstValidIndex = range.firstValidIndex();
    if (firstValidIndex < 0)
        return -1;
    return impl->imageList.imageItemFromIndex(range.indices[firstValidIndex])->uniqueId;
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

ImageItemPtr Viewer::getImageItem (ImageId imageId) const
{
    return impl->imageList.imageItemFromId (imageId);
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

        const bool hasAlpha = spec.alpha_mask;
        for (int r = 0; r < im.height(); ++r)
        {
            const uint8_t* inRowPtr_bytes = reinterpret_cast<const uint8_t*>(clipImg.data()) + r*spec.bytes_per_row;
            const uint32_t* inRowPtr = reinterpret_cast<const uint32_t*>(inRowPtr_bytes);
            PixelSRGBA* outRowPtr = im.atRowPtr(r);
            for (int c = 0; c < im.width(); ++c)
            {
                uint32_t v = inRowPtr[c];
                outRowPtr[c].v[0] = (v&spec.red_mask) >> spec.red_shift;
                outRowPtr[c].v[1] = (v&spec.green_mask) >> spec.green_shift;
                outRowPtr[c].v[2] = (v&spec.blue_mask) >> spec.blue_shift;
                outRowPtr[c].v[3] = hasAlpha ? ((v&spec.alpha_mask) >> spec.alpha_shift) : 255;
            }
        }

        addImageData (im, "(pasted)", 0, false /* don't replace */);
        selectImageIndex (0);
        break;
    }

    case 24:
    {
        ImageSRGBA im;
        im.ensureAllocatedBufferForSize((int)spec.width, (int)spec.height);

        for (int r = 0; r < im.height(); ++r)
        {
            const uint8_t *inRowPtr_bytes = reinterpret_cast<const uint8_t *>(clipImg.data()) + r * spec.bytes_per_row;
            PixelSRGBA *outRowPtr = im.atRowPtr(r);
            for (int c = 0; c < im.width(); ++c)
            {
                uint32_t v = inRowPtr_bytes[c*3] << 0 | inRowPtr_bytes[c*3+1] << 8 | inRowPtr_bytes[c*3+2] << 16;
                outRowPtr[c].v[0] = (v & spec.red_mask) >> spec.red_shift;
                outRowPtr[c].v[1] = (v & spec.green_mask) >> spec.green_shift;
                outRowPtr[c].v[2] = (v & spec.blue_mask) >> spec.blue_shift;
                outRowPtr[c].v[3] = 255;
            }
        }

        addImageData(im, "(pasted)", 0, false /* don't replace */);
        selectImageIndex(0);
        break;
    }

    case 16:
    case 64:
    default:
    {
        std::cerr << "Only 32bpp clipboard supported right now." << std::endl;
        return false;
    }
    }

        return -1;
}

void Viewer::setGlobalEventCallback (const GlobalEventCallbackType& callback, void* userData)
{
    impl->imageWindow.setGlobalEventCallback (callback, userData);
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

void Viewer::runAfterConfirmingPendingChanges (std::function<void(void)>&& func)
{
    // Already a pending confirmation, skip.
    if (impl->state.funcIfChangesConfirmed)
        return;

    impl->state.funcIfChangesConfirmed = std::move(func);
    impl->state.pendingChangesConfirmationRequested = true;
}

} // zv
