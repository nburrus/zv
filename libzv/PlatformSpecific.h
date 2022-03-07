//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/ImguiUtils.h>
#include <libzv/Image.h>
#include <libzv/OpenGL.h>

struct GLFWwindow;

namespace zv
{

void openURLInBrowser(const char* url);

void getVersionAndBuildNumber(std::string& version, std::string& build);

} // zv
