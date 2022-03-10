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

int main (int argc, char* argv[])
{
    zv::Profiler p("main");
    
    zv::Viewer viewer;
    viewer.initialize (argc, argv);
    p.lap ("init");
    
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
