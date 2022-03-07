//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include <libzv/Viewer.h>
#include <libzv/Utils.h>

int main ()
{
    zv::Viewer viewer;
    viewer.initialize ();
    
    zv::RateLimit rateLimit;
    bool shouldExit = false;
    while (!shouldExit)
    {
        viewer.runOnce ();
        rateLimit.sleepIfNecessary (1 / 30.);
    }
    
    return 0;
}
