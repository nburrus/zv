//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "PlatformSpecific.h"

#include "GeneratedConfig.h"

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

namespace zv
{

void openURLInBrowser(const char* url)
{
    // [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:[NSString stringWithUTF8String:url]]];
    ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

void getVersionAndBuildNumber(std::string& version, std::string& build)
{
    // https://stackoverflow.com/questions/10015304/refer-to-build-number-or-version-number-in-code
    // NSDictionary *infoDict = [[NSBundle mainBundle] infoDictionary];
    // NSString *appVersion = [infoDict objectForKey:@"CFBundleShortVersionString"]; // example: 1.0.0
    // NSString *buildNumber = [infoDict objectForKey:@"CFBundleVersion"]; // example: 42
    // version = [appVersion UTF8String];
    // build = [buildNumber UTF8String];
    version = PROJECT_VERSION;
    build = PROJECT_VERSION_COMMIT;
}

} // zv
