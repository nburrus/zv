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
        return false;
    }

    zv::Viewer viewer;
    viewer.initialize ();

    try
    {
        auto images = parser.get<std::vector<std::string>>("images");
        zv_dbg("%d images provided", (int)images.size());

        viewer.addImageFromFile (images[0]);
    }
    catch (std::logic_error& e)
    {
        std::cerr << "No files provided" << std::endl;
    }
   
    zv::RateLimit rateLimit;
    while (!viewer.exitRequested())
    {
        viewer.renderFrame ();
        rateLimit.sleepIfNecessary (1 / 30.);
    }
    
    return 0;
}
