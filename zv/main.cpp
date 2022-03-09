//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include <libzv/Viewer.h>
#include <libzv/Utils.h>
#include <libzv/Image.h>
#include <libzv/Viewer.h>

#include "GeneratedConfig.h"

#include <argparse.hpp>

int main (int argc, char* argv[])
{
    zv::Profiler p("main");

    argparse::ArgumentParser parser("zv", PROJECT_VERSION);
    parser.add_argument("images")
          .help("Images to visualize")
          .remaining();

    try
    {
        parser.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err)
    {
        std::cerr << "Wrong usage" << std::endl;
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    zv::Viewer viewer;
    viewer.initialize ();
    p.lap ("init");
    
    try
    {
        auto images = parser.get<std::vector<std::string>>("images");
        zv_dbg("%d images provided", (int)images.size());

        for (const auto& im : images)
            viewer.addImageFromFile (im);
    }
    catch (std::logic_error& e)
    {
        std::cerr << "No files provided" << std::endl;
    }
   
    p.lap ("args");

    bool firstFrame = true;
    zv::RateLimit rateLimit;
    while (!viewer.exitRequested())
    {
        // Max 30 FPS
        viewer.renderFrame ();
        if (firstFrame)
        {
            p.lap ("firstRendered");
            p.stop ();
            firstFrame = false;
        }
        rateLimit.sleepIfNecessary (1 / 30.);
    }
    
    return 0;
}
