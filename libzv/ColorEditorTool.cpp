//
// Copyright (c) 2017, Nicolas Burrus
// This software may be modified and distributed under the terms
// of the BSD license.  See the LICENSE file for details.
//

#include "ColorEditorTool.h"

#include <libzv/ImageColorStats.h>
#include <libzv/ImguiUtils.h>
#include <libzv/Modifiers.h>

#include "imgui.h"
#include "implot.h"

#include <GL/gl3w.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace zv
{

ColorEditorTool::ColorEditorTool()
    : InteractiveTool(Kind::Modifier)
{}

// Cache key: use the raw data pointer as a proxy for image identity/version.
// This is invalidated whenever ModifiedImage produces new output (new pointer).
static uint64_t cacheKeyForImage(const ImageSRGBA& image)
{
    return reinterpret_cast<uint64_t>(image.data());
}

static float gammaToMidpointX(const LevelsParams& p)
{
    const float t = std::pow(0.5f, p.gamma);
    return static_cast<float>(p.inputBlack) + t * static_cast<float>(p.inputWhite - p.inputBlack);
}

static float midpointXToGamma(int inputBlack, int inputWhite, float x)
{
    const float span = static_cast<float>(std::max(1, inputWhite - inputBlack));
    const float t = std::clamp((x - static_cast<float>(inputBlack)) / span, 0.01f, 0.99f);
    return std::clamp(std::log(t) / std::log(0.5f), 0.10f, 10.0f);
}

static float valueToX(float value, const ImVec2& min, const ImVec2& size)
{
    return min.x + (value / 255.0f) * size.x;
}

static int xToValue(float x, const ImVec2& min, const ImVec2& size)
{
    if (size.x <= 0.0f)
        return 0;
    const float t = std::clamp((x - min.x) / size.x, 0.0f, 1.0f);
    return std::clamp(static_cast<int>(std::round(t * 255.0f)), 0, 255);
}

static bool isMouseHoveringHandle(float x, float y, float radius)
{
    const ImVec2 mouse = ImGui::GetMousePos();
    return std::abs(mouse.x - x) <= radius && std::abs(mouse.y - y) <= radius;
}

static const char* levelsPreviewFragmentShader_glsl_130 = R"(
    uniform sampler2D Texture;
    uniform sampler2D LevelsLut;
    in vec2 Frag_UV;
    out vec4 Out_Color;
    void main()
    {
        vec4 srgb = texture(Texture, Frag_UV.st);
        float r = texture(LevelsLut, vec2(srgb.r, 0.5)).r;
        float g = texture(LevelsLut, vec2(srgb.g, 0.5)).g;
        float b = texture(LevelsLut, vec2(srgb.b, 0.5)).b;
        Out_Color = vec4(r, g, b, srgb.a);
    }
)";

static const char* hueShiftFragmentShader_glsl_130 = R"(
    uniform sampler2D Texture;
    uniform float HueShift;
    in vec2 Frag_UV;
    out vec4 Out_Color;

    vec3 rgb2hsv(vec3 c) {
        vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
        vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
        vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
        float d = q.x - min(q.w, q.y);
        float e = 1.0e-10;
        return vec3(abs(q.z + (q.w - q.y) / (6.0*d + e)), d / (q.x + e), q.x);
    }

    vec3 hsv2rgb(vec3 c) {
        vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
        vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
        return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
    }

    void main()
    {
        vec4 srgb = texture(Texture, Frag_UV.st);
        vec3 hsv = rgb2hsv(srgb.rgb);
        hsv.x = fract(hsv.x + HueShift);
        Out_Color = vec4(hsv2rgb(hsv), srgb.a);
    }
)";

void ColorEditorTool::updateStats(const ImageSRGBA& image)
{
    const uint64_t key = cacheKeyForImage(image);
    if (key == _statsCacheKey)
        return;
    _stats = computeImageColorStats(image);
    _statsCacheKey = key;
}

LevelsParams& ColorEditorTool::currentLevelsParams()
{
    switch (_levelsChannel)
    {
        case LevelsChannel::Luma:  return _levelsParams.lumaLevels;
        case LevelsChannel::Red:   return _levelsParams.redLevels;
        case LevelsChannel::Green: return _levelsParams.greenLevels;
        case LevelsChannel::Blue:  return _levelsParams.blueLevels;
    }
    return _levelsParams.lumaLevels;
}

const LevelsParams& ColorEditorTool::currentLevelsParams() const
{
    switch (_levelsChannel)
    {
        case LevelsChannel::Luma:  return _levelsParams.lumaLevels;
        case LevelsChannel::Red:   return _levelsParams.redLevels;
        case LevelsChannel::Green: return _levelsParams.greenLevels;
        case LevelsChannel::Blue:  return _levelsParams.blueLevels;
    }
    return _levelsParams.lumaLevels;
}

const ChannelStats& ColorEditorTool::currentChannelStats() const
{
    switch (_levelsChannel)
    {
        case LevelsChannel::Luma:  return _stats.luma;
        case LevelsChannel::Red:   return _stats.r;
        case LevelsChannel::Green: return _stats.g;
        case LevelsChannel::Blue:  return _stats.b;
    }
    return _stats.luma;
}

const char* ColorEditorTool::currentChannelName() const
{
    switch (_levelsChannel)
    {
        case LevelsChannel::Luma:  return "Luma";
        case LevelsChannel::Red:   return "Red";
        case LevelsChannel::Green: return "Green";
        case LevelsChannel::Blue:  return "Blue";
    }
    return "Luma";
}

void ColorEditorTool::markLevelsChanged()
{
    _levelsPreviewDirty = !allLevelsIdentity();
    _lutDirty = true;
}

void ColorEditorTool::resetCurrentChannel()
{
    currentLevelsParams() = {};
    markLevelsChanged();
}

void ColorEditorTool::resetAllLevels()
{
    _levelsParams = {};
    _levelsPreviewDirty = false;
    _lutDirty = true;
    _activeHandle = ActiveHandle::None;
    _previewFrameBuffers.clear();
}

bool ColorEditorTool::allLevelsIdentity() const
{
    return levelsParamsIdentity(_levelsParams.lumaLevels) &&
           levelsParamsIdentity(_levelsParams.redLevels) &&
           levelsParamsIdentity(_levelsParams.greenLevels) &&
           levelsParamsIdentity(_levelsParams.blueLevels);
}

bool ColorEditorTool::hasPendingCommitRequest() const
{
    return _applyRequested || _pendingOneShotAction.has_value() || _hueShiftApplyRequested;
}

void ColorEditorTool::clearPendingCommitRequest()
{
    if (_applyRequested)
    {
        resetAllLevels();
        resetHueShift();
    }

    if (_hueShiftApplyRequested)
        resetHueShift();

    _applyRequested = false;
    _pendingOneShotAction.reset();
}

void ColorEditorTool::renderLevelsHeader()
{
    ImGui::Spacing();
    // Segmented channel control
    struct { const char* label; LevelsChannel ch; } channels[] = {
        {"Luma", LevelsChannel::Luma}, {"R", LevelsChannel::Red},
        {"G", LevelsChannel::Green},   {"B", LevelsChannel::Blue},
    };
    for (auto& [label, ch] : channels)
    {
        bool active = (_levelsChannel == ch);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::Button(label))
        {
            _levelsChannel = ch;
            _levelsParams.target = ch;
        }
        if (active)
            ImGui::PopStyleColor();
        ImGui::SameLine(0, 2);
    }
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);

    // Stats inline
    const ChannelStats& cs = currentChannelStats();
    ImGui::TextDisabled("min:%d  max:%d  mean:%.1f", cs.min, cs.max, cs.mean);

    // Log checkbox right-aligned
    const float logWidth = ImGui::CalcTextSize("Log").x + ImGui::GetFrameHeight()
                           + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - logWidth);
    ImGui::Checkbox("Log", &_histLogScale);
}

void ColorEditorTool::renderLevelsHistogram()
{
    LevelsParams& params = currentLevelsParams();
    const ChannelStats& channel = currentChannelStats();
    const char* channelName = currentChannelName();

    ImVec4 color(1, 1, 1, 0.9f);
    switch (_levelsChannel)
    {
        case LevelsChannel::Luma:  color = {1,1,1,0.9f}; break;
        case LevelsChannel::Red:   color = {1,.35f,.35f,.9f}; break;
        case LevelsChannel::Green: color = {.35f,1,.35f,.9f}; break;
        case LevelsChannel::Blue:  color = {.45f,.55f,1,.9f}; break;
    }

    uint64_t peak = 1;
    for (int b = 0; b < 256; ++b)
        peak = std::max(peak, channel.histogram[b]);

    std::array<double, 256> bins;
    std::array<double, 256> values;
    for (int b = 0; b < 256; ++b)
    {
        bins[b] = b;
        values[b] = _histLogScale
            ? std::log10(1.0 + static_cast<double>(channel.histogram[b]))
            : static_cast<double>(channel.histogram[b]);
    }

    const double yMax = _histLogScale ? std::log10(1.0 + static_cast<double>(peak)) : static_cast<double>(peak);
    const ImPlotFlags plotFlags = ImPlotFlags_CanvasOnly;
    const ImPlotAxisFlags axisFlags = ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock;
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.03f, 0.035f, 0.04f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_PlotBorder, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.0f);
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
    if (ImPlot::BeginPlot("##hist", ImVec2(-1, 72), plotFlags))
    {
        ImPlot::SetupAxes(nullptr, nullptr, axisFlags, axisFlags);
        ImPlot::SetupAxesLimits(-0.5, 255.5, 0, yMax, ImPlotCond_Always);

        ImPlot::SetNextFillStyle(color);
        ImPlot::PlotBars(channelName, bins.data(), values.data(), 256, 1.0);

        ImDrawList* drawList = ImPlot::GetPlotDrawList();
        const ImVec2 plotPos = ImPlot::GetPlotPos();
        const ImVec2 plotSize = ImPlot::GetPlotSize();
        const ImVec2 plotMax(plotPos.x + plotSize.x, plotPos.y + plotSize.y);

        const ImU32 gridColor = IM_COL32(255, 255, 255, 45);
        const int gridValues[] = {0, 64, 128, 192, 255};
        for (int gridValue : gridValues)
        {
            const float x = valueToX(static_cast<float>(gridValue), plotPos, plotSize);
            drawList->AddLine(ImVec2(x, plotPos.y), ImVec2(x, plotMax.y), gridColor, 1.0f);
        }

        const float blackX = valueToX(static_cast<float>(params.inputBlack), plotPos, plotSize);
        const float whiteX = valueToX(static_cast<float>(params.inputWhite), plotPos, plotSize);
        const float gammaX = valueToX(gammaToMidpointX(params), plotPos, plotSize);
        drawList->AddRectFilled(plotPos, ImVec2(blackX, plotMax.y), IM_COL32(0, 0, 0, 90));
        drawList->AddRectFilled(ImVec2(whiteX, plotPos.y), plotMax, IM_COL32(0, 0, 0, 90));
        const float handleY = plotMax.y;
        const float r = 8.0f;

        drawList->AddLine(ImVec2(blackX, plotPos.y), ImVec2(blackX, handleY - r), IM_COL32(220, 220, 220, 190), 1.5f);
        drawList->AddLine(ImVec2(whiteX, plotPos.y), ImVec2(whiteX, handleY - r), IM_COL32(220, 220, 220, 190), 1.5f);

        auto dimmed = [](ImVec4 c) { c.w *= 0.35f; return ImGui::ColorConvertFloat4ToU32(c); };
        const ImU32 handleColor = _hueShiftPreviewDirty ? dimmed(color)
                                                        : ImGui::ColorConvertFloat4ToU32(color);
        const ImU32 gammaColor  = _hueShiftPreviewDirty ? IM_COL32(140, 140, 140, 255)
                                                        : IM_COL32(235, 235, 235, 255);
        const ImU32 outlineColor = _hueShiftPreviewDirty ? IM_COL32(0, 0, 0, 0) : IM_COL32(20, 20, 20, 255);
        auto drawTriangle = [&](float x, ImU32 fill) {
            drawList->AddTriangleFilled(ImVec2(x, handleY - r), ImVec2(x - r, handleY), ImVec2(x + r, handleY), fill);
            drawList->AddTriangle(ImVec2(x, handleY - r), ImVec2(x - r, handleY), ImVec2(x + r, handleY), outlineColor, 1.0f);
        };
        auto drawDiamond = [&](float x, ImU32 fill) {
            drawList->AddQuadFilled(ImVec2(x, handleY - r), ImVec2(x + r, handleY - r * 0.5f),
                                    ImVec2(x, handleY), ImVec2(x - r, handleY - r * 0.5f), fill);
            drawList->AddQuad(ImVec2(x, handleY - r), ImVec2(x + r, handleY - r * 0.5f),
                              ImVec2(x, handleY), ImVec2(x - r, handleY - r * 0.5f), outlineColor, 1.0f);
        };
        drawTriangle(blackX, handleColor);
        drawDiamond(gammaX, gammaColor);
        drawTriangle(whiteX, handleColor);

        auto hoverHandle = ActiveHandle::None;
        if (!_hueShiftPreviewDirty)
        {
            if (isMouseHoveringHandle(blackX, handleY - r * 0.5f, 9.0f))
                hoverHandle = ActiveHandle::InputBlack;
            else if (isMouseHoveringHandle(gammaX, handleY - r * 0.5f, 9.0f))
                hoverHandle = ActiveHandle::Gamma;
            else if (isMouseHoveringHandle(whiteX, handleY - r * 0.5f, 9.0f))
                hoverHandle = ActiveHandle::InputWhite;
        }

        if (hoverHandle != ActiveHandle::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            _activeHandle = hoverHandle;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            (_activeHandle == ActiveHandle::InputBlack || _activeHandle == ActiveHandle::Gamma || _activeHandle == ActiveHandle::InputWhite))
        {
            _activeHandle = ActiveHandle::None;
        }

        if (!_hueShiftPreviewDirty && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const int mouseValue = xToValue(ImGui::GetMousePos().x, plotPos, plotSize);
            bool changed = false;
            if (_activeHandle == ActiveHandle::InputBlack)
            {
                const int v = std::min(mouseValue, params.inputWhite - 1);
                changed = params.inputBlack != v;
                params.inputBlack = v;
            }
            else if (_activeHandle == ActiveHandle::InputWhite)
            {
                const int v = std::max(mouseValue, params.inputBlack + 1);
                changed = params.inputWhite != v;
                params.inputWhite = v;
            }
            else if (_activeHandle == ActiveHandle::Gamma)
            {
                const float g = midpointXToGamma(params.inputBlack, params.inputWhite, static_cast<float>(mouseValue));
                changed = std::abs(params.gamma - g) > 0.001f;
                params.gamma = g;
            }
            if (changed)
                markLevelsChanged();
        }

        if (_activeHandle == ActiveHandle::InputBlack ||
            _activeHandle == ActiveHandle::Gamma ||
            _activeHandle == ActiveHandle::InputWhite)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ImGui::BeginTooltip();
            switch (_activeHandle)
            {
                case ActiveHandle::InputBlack: ImGui::Text("min: %d", params.inputBlack); break;
                case ActiveHandle::Gamma:      ImGui::Text("gamma: %.2f", params.gamma); break;
                case ActiveHandle::InputWhite: ImGui::Text("max: %d", params.inputWhite); break;
                default: break;
            }
            ImGui::EndTooltip();
        }

        if (hoverHandle == ActiveHandle::None && _activeHandle == ActiveHandle::None &&
            ImPlot::IsPlotHovered() && _stats.pixelCount > 0)
        {
            const int bin = std::clamp(static_cast<int>(std::round(ImPlot::GetPlotMousePos().x)), 0, 255);
            ImGui::BeginTooltip();
            ImGui::Text("Bin %d  [%d-%d]", bin, bin, bin);
            ImGui::Separator();
            const uint64_t count = channel.histogram[bin];
            const double pct = 100.0 * count / _stats.pixelCount;

            uint64_t cumul = 0;
            for (int b = 0; b <= bin; ++b) cumul += channel.histogram[b];
            const double cpct = 100.0 * cumul / _stats.pixelCount;

            ImGui::TextColored(color, "%s  count:%" PRIu64 "  %.2f%%  cum %.2f%%",
                channelName, count, pct, cpct);
            ImGui::EndTooltip();
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar(2);
    ImPlot::PopStyleColor(3);
    ImGui::Spacing();
}

void ColorEditorTool::renderLevelsButtons()
{
    ImGui::TextUnformatted("Level Mapping:");
    ImGui::SameLine();
    const bool disabled = !_levelsPreviewDirty || _hueShiftPreviewDirty;
    if (disabled)
        ImGui::BeginDisabled();
    if (ImGui::Button("Apply"))
        _applyRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
        resetAllLevels();
    if (disabled)
        ImGui::EndDisabled();
    ImGui::SameLine();
    const bool autoDisabled = _levelsPreviewDirty || _hueShiftPreviewDirty;
    if (autoDisabled)
        ImGui::BeginDisabled();
    if (ImGui::Button("Auto"))
    {
        OneShotColorParams p;
        p.kind = OneShotColorParams::Kind::AutoLevels;
        _pendingOneShotAction = p;
    }
    if (autoDisabled)
        ImGui::EndDisabled();
    ImGui::SameLine();
    helpMarker("Drag the triangles at the bottom of the histogram to adjust the input level mapping.\n"
               "Left/right triangles set the black/white input points. Middle diamond adjusts gamma.",
               300.f);
}

void ColorEditorTool::resetHueShift()
{
    _hueShiftDegrees = 0.f;
    _hueShiftPreviewDirty = false;
    _hueShiftApplyRequested = false;
}

void ColorEditorTool::renderHueShiftSection()
{
    ImGui::Separator();
    ImGui::Spacing();

    const bool blocked = _levelsPreviewDirty;
    if (blocked)
        ImGui::BeginDisabled();

    const float btnWidth = (ImGui::CalcTextSize("Apply").x + ImGui::CalcTextSize("Reset").x)
                           + ImGui::GetStyle().FramePadding.x * 4.f
                           + ImGui::GetStyle().ItemSpacing.x * 2.f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnWidth);
    if (ImGui::SliderFloat("##hueShift", &_hueShiftDegrees, 0.f, 360.f, "Hue: %.0f°"))
        _hueShiftPreviewDirty = (_hueShiftDegrees != 0.f);
    ImGui::SameLine();
    const bool applyDisabled = !_hueShiftPreviewDirty;
    if (applyDisabled)
        ImGui::BeginDisabled();
    if (ImGui::Button("Apply##hue"))
        _hueShiftApplyRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Reset##hue"))
        resetHueShift();
    if (applyDisabled)
        ImGui::EndDisabled();

    if (blocked)
        ImGui::EndDisabled();
}

void ColorEditorTool::renderOneShotActionsSection()
{
    ImGui::Separator();

    const bool blocked = _levelsPreviewDirty || _hueShiftPreviewDirty;
    if (blocked)
        ImGui::BeginDisabled();

    struct { const char* label; OneShotColorParams::Kind kind; } buttons[] = {
        {"Swap R/B",  OneShotColorParams::Kind::SwapRB},
        {"Swap R/G",  OneShotColorParams::Kind::SwapRG},
        {"Swap G/B",  OneShotColorParams::Kind::SwapGB},
        {"Invert",    OneShotColorParams::Kind::Invert},
        {"Grayscale", OneShotColorParams::Kind::Grayscale},
        {"HistEq",    OneShotColorParams::Kind::HistEq},
    };
    if (ImGui::BeginTable("##oneShotActions", 3, ImGuiTableFlags_SizingStretchSame))
    {
        for (auto& btn : buttons)
        {
            ImGui::TableNextColumn();
            if (ImGui::Button(btn.label, ImVec2(-1, 0)))
            {
                OneShotColorParams p; p.kind = btn.kind;
                _pendingOneShotAction = p;
            }
        }

        ImGui::TableNextColumn();
        const float cellWidth = ImGui::GetContentRegionAvail().x;
        const float seedLabelWidth = ImGui::CalcTextSize("Seed").x;
        const float inputWidth = 34.f;
        const float plusButtonWidth = ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 2.f;
        const float spacing = std::min(ImGui::GetStyle().ItemSpacing.x, 4.f);
        const float buttonWidth = std::max(1.f, cellWidth - inputWidth - plusButtonWidth - seedLabelWidth - spacing * 3.f);

        const bool labelDisabled = !_stats.rgbChannelsEqual;
        if (labelDisabled)
            ImGui::BeginDisabled();
        if (ImGui::Button("Label Colorize", ImVec2(buttonWidth, 0)))
        {
            OneShotColorParams p;
            p.kind = OneShotColorParams::Kind::LabelColorize;
            p.labelColorize.seed = static_cast<uint32_t>(_labelColorizeSeed);
            _pendingOneShotAction = p;
        }
        if (labelDisabled)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Label Colorize applies to grayscale-like label maps.");
            ImGui::EndDisabled();
        }
        ImGui::SameLine(0, spacing);
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::InputInt("##labelColorizeSeed", &_labelColorizeSeed, 0, 0);
        if (_labelColorizeSeed < 0)
            _labelColorizeSeed = 0;
        ImGui::SameLine(0, spacing);
        if (ImGui::Button("+##labelColorizeSeed", ImVec2(plusButtonWidth, 0)))
            ++_labelColorizeSeed;
        ImGui::SameLine(0, spacing);
        ImGui::TextUnformatted("Seed");

        ImGui::EndTable();
    }

    if (blocked)
    {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Apply or Reset the active preview first.");
    }
}

void ColorEditorTool::renderLevelsSection()
{
    renderLevelsHeader();
    renderLevelsHistogram();
    renderLevelsButtons();
}

void ColorEditorTool::renderControls(const ImageSRGBA& firstIm)
{
    updateStats(firstIm);

    renderLevelsSection();
    renderHueShiftSection();

    ImGui::Spacing();
    renderOneShotActionsSection();
}

void ColorEditorTool::addToImage(ModifiedImage& image)
{
    if (_applyRequested && _levelsPreviewDirty)
    {
        image.addModifier(std::make_unique<LevelsModifier>(_levelsParams));
        return;
    }
    if (_pendingOneShotAction)
    {
        image.addModifier(std::make_unique<OneShotColorModifier>(*_pendingOneShotAction));
        return;
    }
    if (_hueShiftApplyRequested && _hueShiftPreviewDirty)
        image.addModifier(std::make_unique<HueShiftModifier>(_hueShiftDegrees));
}

void ColorEditorTool::uploadPreviewLutIfNeeded()
{
    if (!_lutDirty && _previewLutTexture)
        return;

    if (!_previewLutTexture)
    {
        _previewLutTexture = std::make_shared<GLTexture>();
        _previewLutTexture->initialize();
    }

    const CompiledLevelsLut lut = compileLevelsLut(_levelsParams);
    std::array<uint8_t, 256 * 4> rgba = {};
    for (int i = 0; i < 256; ++i)
    {
        rgba[i * 4 + 0] = lut.r[i];
        rgba[i * 4 + 1] = lut.g[i];
        rgba[i * 4 + 2] = lut.b[i];
        rgba[i * 4 + 3] = 255;
    }
    _previewLutTexture->uploadRgba(rgba.data(), 256, 1);
    _lutDirty = false;
}

ImageRenderingOverride ColorEditorTool::overrideImageRendering(const ImageRenderingContext& ctx)
{
    if (!_levelsPreviewDirty && !_hueShiftPreviewDirty)
        return {};

    if (!_previewRendererInitialized)
    {
        _previewRenderer.initializeGL();
        _previewRendererInitialized = true;
    }

    // Evict when many distinct textures have been seen (e.g. scrolling through a long list).
    if (_previewFrameBuffers.size() > 16)
        _previewFrameBuffers.clear();

    auto& fbPtr = _previewFrameBuffers[ctx.textureId];
    if (!fbPtr)
        fbPtr = std::make_unique<GLFrameBuffer>();

    GLint prevViewport[4] = {};
    GLint prevActiveTexture = 0;
    GLint prevTexture0 = 0;
    GLint prevTexture1 = 0;
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture1);

    fbPtr->enable(ctx.width, ctx.height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    if (_levelsPreviewDirty)
    {
        // Levels preview (takes priority)
        if (_previewShader.glHandles().shaderHandle == 0)
        {
            _previewShader.initialize(glslVersion(), nullptr, levelsPreviewFragmentShader_glsl_130);
            _previewLutUniformLocation = glGetUniformLocation(_previewShader.glHandles().shaderHandle, "LevelsLut");
        }
        uploadPreviewLutIfNeeded();
        _previewShader.enable(0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.textureId);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, _previewLutTexture->textureId());
        glUniform1i(_previewLutUniformLocation, 1);
        _previewRenderer.render();
        _previewShader.disable();
    }
    else
    {
        // Hue shift preview
        if (_hueShiftShader.glHandles().shaderHandle == 0)
        {
            _hueShiftShader.initialize(glslVersion(), nullptr, hueShiftFragmentShader_glsl_130);
            _hueShiftUniformLocation = glGetUniformLocation(_hueShiftShader.glHandles().shaderHandle, "HueShift");
        }
        _hueShiftShader.enable(0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.textureId);
        glUniform1f(_hueShiftUniformLocation, _hueShiftDegrees / 360.f);
        _previewRenderer.render();
        _hueShiftShader.disable();
    }

    fbPtr->disable();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, prevTexture0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, prevTexture1);
    glActiveTexture(prevActiveTexture);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    // Orientation note: the offscreen preview is intentionally returned with the
    // same UVs as regular image textures. Rendering to the FBO writes memory row 0
    // from the fragment at NDC Y=-1, which samples source UV (0,0). ImGui then
    // samples preview UV (0,0) from that same memory row, so the two coordinate
    // conventions cancel and the preview is not vertically flipped.
    return { fbPtr->outputColorTexture().textureId() };
}

void ColorEditorTool::onCancel()
{
    resetAllLevels();
    resetHueShift();
}

void ColorEditorTool::onReset()
{
    resetCurrentChannel();
}

} // zv
