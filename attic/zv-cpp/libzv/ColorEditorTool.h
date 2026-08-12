//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#pragma once

#include <libzv/InteractiveTool.h>
#include <libzv/ImageColorStats.h>
#include <libzv/Modifiers.h>
#include <libzv/OpenGL.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <memory>

namespace zv
{

class ColorEditorTool : public InteractiveTool
{
public:
    ColorEditorTool();

    // InteractiveTool interface
    void renderAsActiveTool (const InteractiveToolRenderingContext&) override {}
    void renderControls (const ImageSRGBA& firstIm) override;
    void addToImage (ModifiedImage& image) override;
    ImageRenderingOverride overrideImageRendering (const ImageRenderingContext& ctx) override;

    void onCancel();
    void onReset();
    bool hasPendingCommitRequest() const;
    void clearPendingCommitRequest();

private:
    enum class ActiveHandle { None, InputBlack, Gamma, InputWhite, OutputBlack, OutputWhite };

    void updateStats (const ImageSRGBA& image);
    void renderLevelsSection ();
    void renderLevelsHeader ();
    void renderLevelsHistogram ();
    void renderLevelsButtons ();
    void renderHueShiftSection ();
    void renderOneShotActionsSection ();

    LevelsParams& currentLevelsParams ();
    const LevelsParams& currentLevelsParams () const;
    const ChannelStats& currentChannelStats () const;
    const char* currentChannelName () const;
    void markLevelsChanged ();
    void resetCurrentChannel ();
    void resetAllLevels ();
    void resetHueShift ();
    bool allLevelsIdentity () const;
    void uploadPreviewLutIfNeeded ();

    ImageColorStats _stats;
    uint64_t _statsCacheKey = 0; // invalidate when image pointer/version changes

    LevelsAdjustmentParams _levelsParams;
    LevelsChannel _levelsChannel = LevelsChannel::Luma;
    bool _histLogScale = false;
    bool _levelsPreviewDirty = false;
    bool _lutDirty = false;
    ActiveHandle _activeHandle = ActiveHandle::None;
    bool _applyRequested = false;
    std::optional<OneShotColorParams> _pendingOneShotAction;
    int _labelColorizeSeed = 1;

    float _hueShiftDegrees = 0.f;
    bool _hueShiftPreviewDirty = false;
    bool _hueShiftApplyRequested = false;

    GLTexturePtr _previewLutTexture;
    std::unordered_map<uint32_t, std::unique_ptr<GLFrameBuffer>> _previewFrameBuffers;
    GLShader _previewShader;
    GLShader _hueShiftShader;
    GLImageRenderer _previewRenderer;
    bool _previewRendererInitialized = false;
    int32_t _previewLutUniformLocation = -1;
    int32_t _hueShiftUniformLocation = -1;
};

} // zv
