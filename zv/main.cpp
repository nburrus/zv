//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include <libzv/App.h>
#include <libzv/Viewer.h>
#include <libzv/Utils.h>
#include <libzv/Image.h>
#include <libzv/Viewer.h>

#include "GeneratedConfig.h"

int main (int argc, char* argv[])
{    
    zv::Profiler p("main");
    
    zv::App app;
    app.initialize (argc, argv);
    p.lap ("init");
    
    app.createViewer ("secondViewer");

    zv::RateLimit rateLimit;
    bool firstFrame = true;
    while (app.numViewers() > 0)
    {
        app.updateOnce ();
        if (firstFrame)
        {
            p.lap ("firstUpdate");
            p.stop ();
            firstFrame = false;
        }
        rateLimit.sleepIfNecessary (1 / 30.);
    }
    
    return 0;
}
