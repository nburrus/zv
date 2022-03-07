//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "Viewer.h"

#include <libzv/ImageViewer.h>
#include <libzv/HelpWindow.h>
#include <libzv/PlatformSpecific.h>
#include <libzv/Prefs.h>
#include <libzv/Utils.h>

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
    GLFWwindow* mainContextWindow = nullptr;
    
    ImageViewer imageViewer;
    HelpWindow helpWindow;
    
    bool helpRequested = false;
    
    void onDisplayLinkRefresh ()
    {
        if (helpRequested)
        {
            helpWindow.setEnabled(true);
            helpRequested = false;
        }
        
        if (imageViewer.isEnabled())
        {
            imageViewer.runOnce();
            if (imageViewer.helpWindowRequested())
            {
                helpWindow.setEnabled(true);
                imageViewer.notifyHelpWindowRequestHandled();
            }
        }

        if (helpWindow.isEnabled())
        {
            helpWindow.runOnce();
        }
    }
};

Viewer::Viewer()
: impl (new Impl())
{
    
}

Viewer::~Viewer()
{
    impl->imageViewer.shutdown();
    impl->helpWindow.shutdown();
    
    if (impl->mainContextWindow)
    {
        glfwDestroyWindow(impl->mainContextWindow);
        glfwTerminate();
    }
}

static void glfw_error_callback(int error, const char* description)
{
    zv_assert (false, "GLFW error %d: %s\n", error, description);
}

bool Viewer::initialize ()
{
    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return false;
        
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
    
    glfwWindowHint(GLFW_DECORATED, false);
    glfwWindowHint(GLFW_VISIBLE, false);
    impl->mainContextWindow = glfwCreateWindow(1, 1, "zv Hidden Parent Content", NULL, NULL);
    if (impl->mainContextWindow == NULL)
        return false;
    glfwWindowHint(GLFW_DECORATED, true); // restore the default.
    glfwWindowHint(GLFW_VISIBLE, true);
    
    glfwMakeContextCurrent(impl->mainContextWindow);
    
    // Initialize OpenGL loader
    // bool err = glewInit() != GLEW_OK;
    bool err = gl3wInit() != 0;
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return false;
    }
    
    glfwSwapInterval(1); // no vsync on that dummy window to avoid delaying other windows.
    
    glfwSetWindowPos(impl->mainContextWindow, 0, 0);    
        
    impl->imageViewer.initialize(impl->mainContextWindow);
    impl->helpWindow.initialize(impl->mainContextWindow);
    
    if (Prefs::showHelpOnStartup())
    {
        impl->helpWindow.setEnabled (true);
    }
    return true;
}

void Viewer::runOnce ()
{
    impl->onDisplayLinkRefresh();
}

void Viewer::helpRequested ()
{
    impl->helpRequested = true;
}

void Viewer::shutdown ()
{
    
}

} // zv
