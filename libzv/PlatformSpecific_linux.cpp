//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "PlatformSpecific.h"
#include "GeneratedConfig.h"

#include <libzv/Utils.h>

// getpid
#include <sys/types.h>
#include <unistd.h>

namespace zv
{

void openURLInBrowser(const char* url)
{
    // Can't call that super safe, but well, we only call it with our own fixed strings.
    std::string op = std::string("xdg-open \"") + url + "\"";
    int failed = system(op.c_str());
    zv_assert (!failed, "Could not open the URL in browser.");
}

void getVersionAndBuildNumber(std::string& version, std::string& build)
{
    version = PROJECT_VERSION;
    build = PROJECT_VERSION_COMMIT;
}

} // zv
