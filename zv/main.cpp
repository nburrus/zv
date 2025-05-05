//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include <libzv/Platform.h>
#include <libzv/App.h>
#include <libzv/Viewer.h>
#include <libzv/Utils.h>
#include <libzv/Image.h>
#include <libzv/Viewer.h>

#include "GeneratedConfig.h"

#if PLATFORM_EMSCRIPTEN
#include <emscripten.h>

std::function<void()> mainLoopForEmscripten;
void mainLoop() { mainLoopForEmscripten(); }
#endif

int main (int argc, char* argv[])
{    
    zv::Profiler p("main");
    
    zv::App app;
    if (!app.initialize (argc, argv))
    {
        return 1;
    }
    p.lap ("init");

#if PLATFORM_EMSCRIPTEN
    const int fps = 0; // means requestAnimationFrame
    const bool simulateInfiniteLoop = true;
    mainLoopForEmscripten = [&app]() { 
        app.updateOnce(); 
    };
    emscripten_set_main_loop(mainLoop, fps, simulateInfiniteLoop);
#else
    app.run ();    
#endif


    return 0;
}
