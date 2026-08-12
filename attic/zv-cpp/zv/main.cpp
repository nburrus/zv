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
    if (!app.initialize (argc, argv))
    {
        return 1;
    }
    p.lap ("init");
    
    app.run ();    
    return 0;
}
