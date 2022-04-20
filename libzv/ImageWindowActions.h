//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

namespace zv
{

enum class ImageWindowAction
{
    Zoom_Normal,
    Zoom_RestoreAspectRatio,
    Zoom_x2,
    Zoom_div2,
    Zoom_Inc10p,
    Zoom_Dec10p,
    Zoom_Maxspect,

    File_OpenImage,

    Edit_CopyCursorInfoToClipboard,
    Edit_CopyImageToClipboard,
    Edit_PasteImageFromClipboard,
    Edit_Undo,

    View_ToggleOverlay,
    View_NextImage,
    View_PrevImage,
    View_NextPageOfImage,
    View_PrevPageOfImage,

    Modify_Rotate90,
    Modify_Rotate180,
    Modify_Rotate270,
    
    ApplyCurrentTool,
};

} // zv
