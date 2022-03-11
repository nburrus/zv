//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImguiUtils.h"

#include "imgui_internal.h"

namespace zv
{

bool IsItemHovered(ImGuiHoveredFlags flags, float delaySeconds)
{
    return ImGui::IsItemHovered(flags) && ImGui::GetCurrentContext()->HoveredIdTimer > delaySeconds;
}

} // zv