//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "App.h"

#include <libzv/Viewer.h>
#include <libzv/Utils.h>
#include <libzv/Server.h>

#include "GeneratedConfig.h"

#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

#include <argparse.hpp>

#include <unordered_map>

namespace zv
{

struct App::Impl
{
    Impl (App& that) : that(that) {}
    App& that;

    Server server;

    RateLimit rateLimit;

    std::unique_ptr<argparse::ArgumentParser> argsParser;

    std::unordered_map<std::string, std::unique_ptr<Viewer>> viewers;
    
    void updateOnce ()
    {
        server.updateOnce ();

        std::vector<std::string> viewersToRemove;
        for (auto& nameAndViewer : viewers)
        {
            auto& viewer = *nameAndViewer.second;
            if (viewer.exitRequested())
            {
                viewer.shutdown();
                viewersToRemove.push_back (nameAndViewer.first);
            }
            else
            {
                viewer.renderFrame ();
            }
        }

        for (const auto& name: viewersToRemove)
        {
            viewers.erase (name);
        }
    }
};

App::App()
: impl (new Impl(*this))
{}
    
App::~App()
{
    shutdown();
}

// Call it once, calls glfwInit, etc.
bool App::initialize(int argc, const char *const argv[])
{
    std::vector<std::string> stl_argv(argc);
    for (int i = 0; i < argc; ++i)
    {
        stl_argv[i] = argv[i];
    }
    return initialize(stl_argv);
}

bool App::initialize (const std::vector<std::string>& args)
{
   argparse::ArgumentParser argsParser ("zv", PROJECT_VERSION);
   argsParser.add_argument("images")
       .help("Images to visualize")
       .remaining();

   try
   {
       argsParser.parse_args(args);
   }
   catch (const std::runtime_error &err)
   {
       std::cerr << "Wrong usage" << std::endl;
       std::cerr << err.what() << std::endl;
       std::cerr << argsParser;
       return false;
   }

   Viewer *defaultViewer = createViewer("default");
   defaultViewer->initialize();

   try
   {
       auto images = argsParser.get<std::vector<std::string>>("images");
       zv_dbg("%d images provided", (int)images.size());

       for (const auto &im : images)
           defaultViewer->addImageFromFile(im);
   }
   catch (const std::exception &err)
   {
       zv_dbg("No images provided, using default.");
   }

   impl->server.start ();
   impl->server.setImageReceivedCallback ([this](ImageItemUniquePtr imageItem, int flags) {
       bool replace = flags;
       auto viewerIt = impl->viewers.begin();
       zv_assert (viewerIt != impl->viewers.end(), "No viewer!");
       viewerIt->second->addImageItem (std::move(imageItem), -1, replace);
   });

   return true;
}

void App::shutdown()
{
    impl->server.stop ();

    for (auto& nameAndViewer : impl->viewers)
        nameAndViewer.second->shutdown();
    impl->viewers.clear ();
    glfwTerminate();
}

Viewer* App::getViewer (const std::string& name)
{
    Viewer* viewer = nullptr;
    auto nameAndViewerIt = impl->viewers.find (name);
    if (nameAndViewerIt != impl->viewers.end())
        viewer = nameAndViewerIt->second.get();
    return viewer;
}

int App::numViewers () const
{
    return impl->viewers.size();
}

std::vector<std::string> App::viewerNames() const
{
    std::vector<std::string> names;
    names.reserve (impl->viewers.size());
    for (const auto& it : impl->viewers)
        names.push_back (it.first);
    return names;
}

Viewer* App::createViewer(const std::string& name)
{
    // Noop if it does not exist.
    removeViewer (name);

    auto viewerPtr = std::make_unique<Viewer>(name, impl->viewers.size());
    viewerPtr->initialize ();
    auto* viewerRawPtr = viewerPtr.get(); // save it before the move.
    impl->viewers[name] = std::move(viewerPtr);
    return viewerRawPtr;
}

void App::removeViewer (const std::string& name)
{
    auto nameAndViewerIt = impl->viewers.find (name);
    if (nameAndViewerIt == impl->viewers.end())
        return;
    
    Viewer& viewer = *nameAndViewerIt->second;
    viewer.shutdown();
    impl->viewers.erase (nameAndViewerIt);
}

void App::updateOnce(double minDuration)
{    
    impl->updateOnce ();
    
    if (minDuration > 0.0)
    {
        impl->rateLimit.sleepIfNecessary (minDuration);
    }
}

} // zv
