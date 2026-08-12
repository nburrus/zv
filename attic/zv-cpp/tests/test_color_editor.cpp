#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <libzv/Image.h>
#include <libzv/Modifiers.h>

#include <cmath>

TEST_CASE("Levels LUT - identity and endpoints")
{
    zv::LevelsAdjustmentParams params;
    auto identity = zv::compileLevelsLut(params);
    CHECK(identity.r[0] == 0);
    CHECK(identity.r[127] == 127);
    CHECK(identity.r[255] == 255);

    params.lumaLevels.inputBlack = 64;
    params.lumaLevels.inputWhite = 192;
    auto stretched = zv::compileLevelsLut(params);
    CHECK(stretched.r[0] == 0);
    CHECK(stretched.r[64] == 0);
    CHECK(stretched.r[192] == 255);
    CHECK(stretched.r[255] == 255);
}

TEST_CASE("Levels LUT - output range and channel override")
{
    zv::LevelsAdjustmentParams params;
    params.lumaLevels.outputBlack = 10;
    params.lumaLevels.outputWhite = 200;
    params.redLevels.inputBlack = 128;
    params.redLevels.inputWhite = 255;

    auto lut = zv::compileLevelsLut(params);
    CHECK(lut.g[0] == 10);
    CHECK(lut.g[255] == 200);
    CHECK(lut.b[0] == 10);
    CHECK(lut.b[255] == 200);
    CHECK(lut.r[127] == 0);
    CHECK(lut.r[255] == 255);
}

TEST_CASE("LevelsModifier applies LUT and preserves alpha")
{
    auto inputData = std::make_shared<zv::ImageItemData>();
    inputData->status = zv::ImageItemData::Status::Ready;
    inputData->cpuData = std::make_shared<zv::ImageSRGBA>(2, 1);
    (*inputData->cpuData)(0, 0) = {0, 64, 128, 17};
    (*inputData->cpuData)(1, 0) = {255, 192, 64, 99};

    zv::LevelsAdjustmentParams params;
    params.lumaLevels.outputBlack = 10;
    params.lumaLevels.outputWhite = 200;
    params.redLevels.inputBlack = 128;
    params.redLevels.inputWhite = 255;

    zv::LevelsModifier modifier(params);
    zv::ImageItemData outputData;
    modifier.apply(*inputData, outputData);

    REQUIRE(outputData.status == zv::ImageItemData::Status::Ready);
    REQUIRE(outputData.cpuData);
    CHECK((*outputData.cpuData)(0, 0).r == 0);
    CHECK((*outputData.cpuData)(0, 0).g == 58);
    CHECK((*outputData.cpuData)(0, 0).b == 105);
    CHECK((*outputData.cpuData)(0, 0).a == 17);
    CHECK((*outputData.cpuData)(1, 0).r == 255);
    CHECK((*outputData.cpuData)(1, 0).a == 99);
}

static zv::ImageItemDataPtr makeInputData(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255,
                                          uint8_t r2 = 0, uint8_t g2 = 0, uint8_t b2 = 0, uint8_t a2 = 200)
{
    auto d = std::make_shared<zv::ImageItemData>();
    d->status = zv::ImageItemData::Status::Ready;
    d->cpuData = std::make_shared<zv::ImageSRGBA>(2, 1);
    (*d->cpuData)(0, 0) = {r, g, b, a};
    (*d->cpuData)(1, 0) = {r2, g2, b2, a2};
    return d;
}

static zv::ImageItemData applyOneShot(const zv::ImageItemDataPtr& input, zv::OneShotColorParams params)
{
    zv::OneShotColorModifier mod(params);
    zv::ImageItemData out;
    mod.apply(*input, out);
    return out;
}

TEST_CASE("OneShotColorModifier - Invert RGB maps 0<->255 and preserves alpha")
{
    auto input = makeInputData(0, 128, 255, 42);
    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::Invert;
    p.invertTarget = zv::OneShotColorParams::InvertTarget::RGB;
    auto out = applyOneShot(input, p);
    REQUIRE(out.status == zv::ImageItemData::Status::Ready);
    CHECK((*out.cpuData)(0, 0).r == 255);
    CHECK((*out.cpuData)(0, 0).g == 127);
    CHECK((*out.cpuData)(0, 0).b == 0);
    CHECK((*out.cpuData)(0, 0).a == 42);
}

TEST_CASE("OneShotColorModifier - Invert Red only changes red channel")
{
    auto input = makeInputData(100, 150, 200, 255);
    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::Invert;
    p.invertTarget = zv::OneShotColorParams::InvertTarget::Red;
    auto out = applyOneShot(input, p);
    CHECK((*out.cpuData)(0, 0).r == 155);
    CHECK((*out.cpuData)(0, 0).g == 150);
    CHECK((*out.cpuData)(0, 0).b == 200);
    CHECK((*out.cpuData)(0, 0).a == 255);
}

TEST_CASE("OneShotColorModifier - Grayscale LumaSRGB produces equal RGB and preserves alpha")
{
    auto input = makeInputData(255, 0, 0, 77);
    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::Grayscale;
    p.grayscaleMode = zv::OneShotColorParams::GrayscaleMode::LumaSRGB;
    auto out = applyOneShot(input, p);
    const auto& px = (*out.cpuData)(0, 0);
    CHECK(px.r == px.g);
    CHECK(px.g == px.b);
    CHECK(px.a == 77);
    CHECK(px.r == 54); // round(0.2126*255) = 54
}

TEST_CASE("OneShotColorModifier - Grayscale channel extraction")
{
    auto input = makeInputData(10, 20, 30, 255);
    {
        zv::OneShotColorParams p;
        p.kind = zv::OneShotColorParams::Kind::Grayscale;
        p.grayscaleMode = zv::OneShotColorParams::GrayscaleMode::Green;
        auto out = applyOneShot(input, p);
        const auto& px = (*out.cpuData)(0, 0);
        CHECK(px.r == 20); CHECK(px.g == 20); CHECK(px.b == 20);
    }
}

TEST_CASE("OneShotColorModifier - SwapRB swaps red and blue")
{
    auto input = makeInputData(10, 20, 30, 255);
    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::SwapRB;
    auto out = applyOneShot(input, p);
    const auto& px = (*out.cpuData)(0, 0);
    CHECK(px.r == 30); CHECK(px.g == 20); CHECK(px.b == 10); CHECK(px.a == 255);
}

TEST_CASE("OneShotColorModifier - SwapRG swaps red and green")
{
    auto input = makeInputData(10, 20, 30, 255);
    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::SwapRG;
    auto out = applyOneShot(input, p);
    const auto& px = (*out.cpuData)(0, 0);
    CHECK(px.r == 20); CHECK(px.g == 10); CHECK(px.b == 30); CHECK(px.a == 255);
}

TEST_CASE("OneShotColorModifier - SwapGB swaps green and blue")
{
    auto input = makeInputData(10, 20, 30, 255);
    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::SwapGB;
    auto out = applyOneShot(input, p);
    const auto& px = (*out.cpuData)(0, 0);
    CHECK(px.r == 10); CHECK(px.g == 30); CHECK(px.b == 20); CHECK(px.a == 255);
}

TEST_CASE("OneShotColorModifier - HistEq equalizes luma and preserves alpha")
{
    auto input = std::make_shared<zv::ImageItemData>();
    input->status = zv::ImageItemData::Status::Ready;
    input->cpuData = std::make_shared<zv::ImageSRGBA>(4, 1);
    (*input->cpuData)(0, 0) = {0, 0, 0, 11};
    (*input->cpuData)(1, 0) = {64, 64, 64, 22};
    (*input->cpuData)(2, 0) = {128, 128, 128, 33};
    (*input->cpuData)(3, 0) = {255, 255, 255, 44};

    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::HistEq;
    auto out = applyOneShot(input, p);

    REQUIRE(out.status == zv::ImageItemData::Status::Ready);
    CHECK((*out.cpuData)(0, 0) == zv::PixelSRGBA(0, 0, 0, 11));
    CHECK((*out.cpuData)(1, 0) == zv::PixelSRGBA(85, 85, 85, 22));
    CHECK((*out.cpuData)(2, 0) == zv::PixelSRGBA(170, 170, 170, 33));
    CHECK((*out.cpuData)(3, 0) == zv::PixelSRGBA(255, 255, 255, 44));
}

TEST_CASE("OneShotColorModifier - HistEq keeps flat images unchanged")
{
    auto input = makeInputData(42, 42, 42, 77, 42, 42, 42, 88);
    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::HistEq;
    auto out = applyOneShot(input, p);
    CHECK((*out.cpuData)(0, 0) == zv::PixelSRGBA(42, 42, 42, 77));
    CHECK((*out.cpuData)(1, 0) == zv::PixelSRGBA(42, 42, 42, 88));
}

TEST_CASE("OneShotColorModifier - LabelColorize is deterministic and preserves alpha")
{
    auto input = makeInputData(5, 5, 5, 77, 42, 42, 42, 88);
    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::LabelColorize;
    p.labelColorize.seed = 1234;
    p.labelColorize.backgroundValue = 0;

    auto outA = applyOneShot(input, p);
    auto outB = applyOneShot(input, p);

    REQUIRE(outA.status == zv::ImageItemData::Status::Ready);
    CHECK((*outA.cpuData)(0, 0) == (*outB.cpuData)(0, 0));
    CHECK((*outA.cpuData)(1, 0) == (*outB.cpuData)(1, 0));
    CHECK((*outA.cpuData)(0, 0).a == 77);
    CHECK((*outA.cpuData)(1, 0).a == 88);
    CHECK((*outA.cpuData)(0, 0).r != 5);
    CHECK((*outA.cpuData)(1, 0).r != 42);
}

TEST_CASE("OneShotColorModifier - LabelColorize maps equal labels to equal colors")
{
    auto input = makeInputData(12, 12, 12, 17, 12, 12, 12, 99);
    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::LabelColorize;
    p.labelColorize.seed = 7;
    p.labelColorize.backgroundValue = 0;

    auto out = applyOneShot(input, p);
    const auto& a = (*out.cpuData)(0, 0);
    const auto& b = (*out.cpuData)(1, 0);
    CHECK(a.r == b.r);
    CHECK(a.g == b.g);
    CHECK(a.b == b.b);
    CHECK(a.a == 17);
    CHECK(b.a == 99);
}

TEST_CASE("OneShotColorModifier - LabelColorize background modes")
{
    auto input = makeInputData(0, 0, 0, 77, 3, 3, 3, 88);

    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::LabelColorize;
    p.labelColorize.seed = 9;
    p.labelColorize.backgroundValue = 0;
    p.labelColorize.backgroundMode = zv::OneShotColorParams::LabelColorize::BackgroundMode::Preserve;
    auto preserve = applyOneShot(input, p);
    CHECK((*preserve.cpuData)(0, 0) == zv::PixelSRGBA(0, 0, 0, 77));
    CHECK((*preserve.cpuData)(1, 0).a == 88);

    p.labelColorize.backgroundMode = zv::OneShotColorParams::LabelColorize::BackgroundMode::Black;
    auto black = applyOneShot(input, p);
    CHECK((*black.cpuData)(0, 0) == zv::PixelSRGBA(0, 0, 0, 77));

    p.labelColorize.backgroundMode = zv::OneShotColorParams::LabelColorize::BackgroundMode::Transparent;
    auto transparent = applyOneShot(input, p);
    CHECK((*transparent.cpuData)(0, 0) == zv::PixelSRGBA(0, 0, 0, 0));
}

TEST_CASE("OneShotColorModifier - LabelColorize skips non-gray-like images")
{
    auto input = makeInputData(10, 20, 30, 77, 40, 40, 40, 88);
    zv::OneShotColorParams p;
    p.kind = zv::OneShotColorParams::Kind::LabelColorize;
    p.labelColorize.seed = 1;

    auto out = applyOneShot(input, p);
    CHECK((*out.cpuData)(0, 0) == zv::PixelSRGBA(10, 20, 30, 77));
    CHECK((*out.cpuData)(1, 0) == zv::PixelSRGBA(40, 40, 40, 88));
}

static zv::ImageItemData applyHueShift(const zv::ImageItemDataPtr& input, float degrees)
{
    zv::HueShiftModifier mod(degrees);
    zv::ImageItemData out;
    mod.apply(*input, out);
    return out;
}

TEST_CASE("HueShiftModifier - zero shift is identity")
{
    auto input = makeInputData(100, 150, 200, 77);
    auto out = applyHueShift(input, 0.f);
    REQUIRE(out.status == zv::ImageItemData::Status::Ready);
    const auto& px = (*out.cpuData)(0, 0);
    CHECK(px.r == 100); CHECK(px.g == 150); CHECK(px.b == 200); CHECK(px.a == 77);
}

TEST_CASE("HueShiftModifier - 360 degree shift is identity")
{
    auto input = makeInputData(80, 160, 40, 255);
    auto out0 = applyHueShift(input, 0.f);
    auto out360 = applyHueShift(input, 360.f);
    const auto& p0 = (*out0.cpuData)(0, 0);
    const auto& p360 = (*out360.cpuData)(0, 0);
    CHECK(std::abs(p0.r - p360.r) <= 1);
    CHECK(std::abs(p0.g - p360.g) <= 1);
    CHECK(std::abs(p0.b - p360.b) <= 1);
}

TEST_CASE("HueShiftModifier - preserves alpha")
{
    auto input = makeInputData(200, 100, 50, 42);
    auto out = applyHueShift(input, 90.f);
    CHECK((*out.cpuData)(0, 0).a == 42);
}

TEST_CASE("HueShiftModifier - gray pixels are unchanged")
{
    auto input = makeInputData(128, 128, 128, 255);
    auto out = applyHueShift(input, 137.f);
    const auto& px = (*out.cpuData)(0, 0);
    CHECK(px.r == 128); CHECK(px.g == 128); CHECK(px.b == 128);
}

TEST_CASE("HueShiftModifier - 180 degree shift on pure red gives cyan")
{
    auto input = makeInputData(255, 0, 0, 255);
    auto out = applyHueShift(input, 180.f);
    const auto& px = (*out.cpuData)(0, 0);
    // Hue of red=0 degrees, shift 180 degrees gives cyan with full saturation/value.
    CHECK(px.r <= 1);
    CHECK(px.g >= 254);
    CHECK(px.b >= 254);
}

TEST_CASE("HueShiftModifier - 120 degree shift on pure red gives pure green")
{
    auto input = makeInputData(255, 0, 0, 255);
    auto out = applyHueShift(input, 120.f);
    const auto& px = (*out.cpuData)(0, 0);
    CHECK(px.r <= 1);
    CHECK(px.g >= 254);
    CHECK(px.b <= 1);
}
