//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ImageWindowState.h"

namespace zv
{
   
InteractiveTool* ActiveToolState::activeTool ()
{
    switch (kind)
    {
        case Kind::None: return nullptr;
        case Kind::Annotate_Line: return &lineTool;
        case Kind::Transform_Crop: return &cropTool;
    }
    return nullptr;
}

} // zv