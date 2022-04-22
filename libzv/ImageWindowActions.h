//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

namespace zv
{

struct ImageWindowAction
{
    struct Params
    {
        virtual ~Params () {}

        // Allow a few parameters for convenience to avoid
        // always re-creating new Params type.
        int intParams[4];
        float floatParams[4];
    };
    using ParamsPtr = std::shared_ptr<Params>;

    enum class Kind
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
        View_SelectImage,

        Modify_Rotate90,
        Modify_Rotate180,
        Modify_Rotate270,
        
        ApplyCurrentTool,
        CancelCurrentTool,
    } kind;

    ImageWindowAction (Kind kind, const ParamsPtr& params = nullptr)
        : kind(kind), paramsPtr(params)
    {}

    ParamsPtr paramsPtr = nullptr;
};

} // zv
