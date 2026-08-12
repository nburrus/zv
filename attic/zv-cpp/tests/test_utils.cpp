#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <libzv/Utils.h>
#include <libzv/Image.h>
#include <libzv/ImageList.h>
#include <libzv/ImageWindowState.h>
#include <libzv/MathUtils.h>
#include <libzv/ColorConversion.h>
#include <libzv/ImageColorStats.h>
#include <set>
#include <filesystem>

TEST_CASE("uniquePrettyNames with already unique names") {
    std::vector<std::string> inputPaths = {
        "test-2025-12-31-07-10-13/image1.png",
        "test-2025-12-31-07-10-13/image2.png",
        "test-2025-12-31-07-10-13/image3.png",
    };

    std::vector<std::string> expectedOutput = {
        "image1.png",
        "image2.png",
        "image3.png"
    };
    
    std::vector<std::string> outputNames = zv::uniquePrettyNames(inputPaths);

    // Check that output size matches input size
    CHECK(outputNames.size() == inputPaths.size());

    // Check that all output names are unique
    std::set<std::string> uniqueNames(outputNames.begin(), outputNames.end());
    CHECK(uniqueNames.size() == outputNames.size());
        
    for (size_t i = 0; i < expectedOutput.size(); ++i) {
        CHECK(outputNames[i] == expectedOutput[i]);
    }
}

TEST_CASE("uniquePrettyNames produces unique names") {
    std::vector<std::string> inputPaths = {
        "test-2025-12-31-07-10-13/subfolder/left/overlay.png",
        "test-2025-12-31-07-10-13/subfolder/right/overlay.png",
        "test-2025-12-31-14-03-50/subfolder/left/overlay2.png",
        "test-2025-12-31-14-03-50/subfolder/right/overlay.png",
        "test-2025-12-31-14-05-47/subfolder/left/overlay.png",
        "test-2025-12-31-14-05-47/subfolder/right/overlay.png",
    };

    // Check the actual expected output for each name
    std::vector<std::string> expectedOutput = {
        "test-2025-12-31-07-10-13/.../left/overlay.png",
        "test-2025-12-31-07-10-13/.../right/overlay.png",
        "...overlay2.png",
        "test-2025-12-31-14-03-50/.../right/overlay.png",
        "test-2025-12-31-14-05-47/.../left/overlay.png",
        "test-2025-12-31-14-05-47/.../right/overlay.png"
    };
    
    std::vector<std::string> outputNames = zv::uniquePrettyNames(inputPaths);

    // Check that output size matches input size
    CHECK(outputNames.size() == inputPaths.size());

    // Check that all output names are unique
    std::set<std::string> uniqueNames(outputNames.begin(), outputNames.end());
    CHECK(uniqueNames.size() == outputNames.size());
        
    for (size_t i = 0; i < expectedOutput.size(); ++i) {
        CHECK(outputNames[i] == expectedOutput[i]);
    }
}

TEST_CASE("uniquePrettyNames with single-letter prefixes") {
    std::vector<std::string> inputPaths = {
        "a/test-2025-12-31-07-10-13/subfolder/left/overlay.png",
        "b/test-2025-12-31-07-10-13/subfolder/right/overlay.png",
        "c/test-2025-12-31-14-03-50/subfolder/left/overlay.png",
        "d/test-2025-12-31-14-03-50/subfolder/right/overlay.png",
        "e/test-2025-12-31-14-05-47/subfolder/left/overlay.png",
        "f/test-2025-12-31-14-05-47/subfolder/right/overlay.png",
    };

    std::vector<std::string> expectedOutput = {
        "test-2025-12-31-07-10-13/.../left/overlay.png",
        "test-2025-12-31-07-10-13/.../right/overlay.png",
        "test-2025-12-31-14-03-50/.../left/overlay.png",
        "test-2025-12-31-14-03-50/.../right/overlay.png",
        "test-2025-12-31-14-05-47/.../left/overlay.png",
        "test-2025-12-31-14-05-47/.../right/overlay.png"
    };
    
    std::vector<std::string> outputNames = zv::uniquePrettyNames(inputPaths);

    // Check that output size matches input size
    CHECK(outputNames.size() == inputPaths.size());

    // Check that all output names are unique
    std::set<std::string> uniqueNames(outputNames.begin(), outputNames.end());
    CHECK(uniqueNames.size() == outputNames.size());
        
    for (size_t i = 0; i < expectedOutput.size(); ++i) {
        CHECK(outputNames[i] == expectedOutput[i]);
    }
}

TEST_CASE("readImageFile loads content even when extension is misleading") {
    zv::ImageSRGBA image;
    std::string errorMessage;
    const auto imagePath = (std::filesystem::path(__FILE__).parent_path() / "rgbgrid_wrong_ext.jpg").string();

    const bool couldLoad = zv::readImageFile(imagePath, image, &errorMessage);

    CHECK(couldLoad);
    CHECK(image.hasData());
    CHECK(image.width() == 216);
    CHECK(image.height() == 216);
    CHECK(errorMessage.empty());
}

TEST_CASE("ImageList keeps remote images with identical basenames but different paths") {
    zv::ImageList imageList;

    auto makeRemoteImage = [](const std::string& path) {
        auto entry = std::make_unique<zv::ImageItem>();
        entry->uniqueId = zv::UniqueId::newId();
        entry->source = zv::ImageItem::Source::Callback;
        entry->sourceImagePath = path;
        entry->prettyName = std::filesystem::path(path).filename().string();
        entry->loadDataCallback = []() {
            return std::make_unique<zv::ImageItemData>();
        };
        return entry;
    };

    imageList.addImage(makeRemoteImage("/tmp/folder_a/shared.png"), -1, true);
    imageList.addImage(makeRemoteImage("/tmp/folder_b/shared.png"), -1, true);

    CHECK(imageList.numImages() == 2);
}

TEST_CASE("ImageList still replaces remote images that point to the same path") {
    zv::ImageList imageList;

    auto makeRemoteImage = [](const std::string& path) {
        auto entry = std::make_unique<zv::ImageItem>();
        entry->uniqueId = zv::UniqueId::newId();
        entry->source = zv::ImageItem::Source::Callback;
        entry->sourceImagePath = path;
        entry->prettyName = std::filesystem::path(path).filename().string();
        entry->loadDataCallback = []() {
            return std::make_unique<zv::ImageItemData>();
        };
        return entry;
    };

    imageList.addImage(makeRemoteImage("/tmp/folder/shared.png"), -1, true);
    imageList.addImage(makeRemoteImage("/tmp/folder/shared.png"), -1, true);

    CHECK(imageList.numImages() == 1);
}

TEST_CASE("bestLayoutForImageCount minimizes blank cells before aspect ratio") {
    const auto layout = zv::bestLayoutForImageCount(6);

    CHECK(layout.numRows == 2);
    CHECK(layout.numCols == 3);
    CHECK(layout.numImages() == 6);
}

TEST_CASE("sRGB transfer functions - boundary values")
{
    // Clamp at 0 and 1
    CHECK(zv::srgbToLinear(0.0) == doctest::Approx(0.0));
    CHECK(zv::srgbToLinear(1.0) == doctest::Approx(1.0));
    CHECK(zv::srgbToLinear(-1.0) == doctest::Approx(0.0));
    CHECK(zv::srgbToLinear(2.0) == doctest::Approx(1.0));

    CHECK(zv::linearToSrgb(0.0) == doctest::Approx(0.0));
    CHECK(zv::linearToSrgb(1.0) == doctest::Approx(1.0));
    CHECK(zv::linearToSrgb(-1.0) == doctest::Approx(0.0));
    CHECK(zv::linearToSrgb(2.0) == doctest::Approx(1.0));
}

TEST_CASE("sRGB transfer functions - known segment values")
{
    // Linear segment (x < 0.04045): value / 12.92
    CHECK(zv::srgbToLinear(0.001) == doctest::Approx(0.001 / 12.92).epsilon(1e-12));
    CHECK(zv::srgbToLinear(0.02)  == doctest::Approx(0.02  / 12.92).epsilon(1e-12));

    // Power segment (x >= 0.04045): ((x + 0.055) / 1.055)^2.4
    CHECK(zv::srgbToLinear(0.5) == doctest::Approx(std::pow((0.5 + 0.055) / 1.055, 2.4)).epsilon(1e-12));

    // Linear segment (x < 0.0031308): x * 12.92
    CHECK(zv::linearToSrgb(0.001) == doctest::Approx(0.001 * 12.92).epsilon(1e-12));
    CHECK(zv::linearToSrgb(0.002) == doctest::Approx(0.002 * 12.92).epsilon(1e-12));

    // Power segment (x >= 0.0031308): x^(1/2.4) * 1.055 - 0.055
    CHECK(zv::linearToSrgb(0.5) == doctest::Approx(std::pow(0.5, 1.0/2.4) * 1.055 - 0.055).epsilon(1e-12));
}

TEST_CASE("sRGB transfer functions - round-trip")
{
    const double eps = 1e-9;
    for (double v : {0.0, 0.01, 0.1, 0.18, 0.5, 0.9, 1.0})
    {
        CHECK(zv::linearToSrgb(zv::srgbToLinear(v)) == doctest::Approx(v).epsilon(eps));
        CHECK(zv::srgbToLinear(zv::linearToSrgb(v)) == doctest::Approx(v).epsilon(eps));
    }
}

TEST_CASE("sRGB transfer functions - float overloads consistent with double")
{
    const float eps = 1e-6f;
    for (float v : {0.0f, 0.04f, 0.18f, 0.5f, 1.0f})
    {
        CHECK(zv::srgbToLinear(v) == doctest::Approx(zv::srgbToLinear((double)v)).epsilon(eps));
        CHECK(zv::linearToSrgb(v) == doctest::Approx(zv::linearToSrgb((double)v)).epsilon(eps));
    }
}

// Helper: make a solid 4x4 image
static zv::ImageSRGBA makeSolid(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    zv::ImageSRGBA img(4, 4);
    img.apply([&](int, int, zv::PixelSRGBA& p){ p = {r, g, b, a}; });
    return img;
}

TEST_CASE("ImageColorStats - grayscale-like detection is independent of alpha")
{
    // Gray pixels (R==G==B) with varying alpha
    zv::ImageSRGBA img(4, 4);
    img.apply([](int c, int r, zv::PixelSRGBA& p){
        uint8_t v = static_cast<uint8_t>(c * 20 + r * 5);
        p = {v, v, v, static_cast<uint8_t>(c * 60)};
    });
    auto stats = zv::computeImageColorStats(img);
    CHECK(stats.rgbChannelsEqual == true);
    CHECK(stats.alphaAllOpaque   == false);
    CHECK(stats.hasAnyTransparency == true);
}

TEST_CASE("ImageColorStats - color image is not grayscale-like")
{
    auto stats = zv::computeImageColorStats(makeSolid(200, 100, 50));
    CHECK(stats.rgbChannelsEqual == false);
}

TEST_CASE("ImageColorStats - large mostly-gray image with off-grid colored region is not grayscale-like")
{
    // Regression: the old sampler used a row/col stride of pixelCount/10000
    // applied to both axes, giving ~stride² sparsity and landing exclusively
    // on the (stride*n, stride*m) lattice. A colored region that avoided those
    // lattice points was missed.
    zv::ImageSRGBA img(999, 999);
    img.apply([](int, int, zv::PixelSRGBA& p){ p = {128, 128, 128, 255}; });
    for (int r = 40; r < 80; ++r)
        for (int c = 40; c < 80; ++c)
            img(c, r) = {255, 0, 0, 255};
    auto stats = zv::computeImageColorStats(img);
    CHECK(stats.rgbChannelsEqual == false);
}

TEST_CASE("ImageColorStats - single-color detection")
{
    auto stats = zv::computeImageColorStats(makeSolid(128, 64, 32));
    CHECK(stats.singleColor == true);
    CHECK(stats.r.min == 128); CHECK(stats.r.max == 128);
    CHECK(stats.g.min == 64);  CHECK(stats.g.max == 64);
    CHECK(stats.b.min == 32);  CHECK(stats.b.max == 32);
}

TEST_CASE("ImageColorStats - per-channel min/max/mean on known image")
{
    // 2x1 image: pixel(0,0)={0,0,0,255}, pixel(1,0)={100,200,50,255}
    zv::ImageSRGBA img(2, 1);
    img(0, 0) = {0,   0,   0,  255};
    img(1, 0) = {100, 200, 50, 255};
    auto stats = zv::computeImageColorStats(img);

    CHECK(stats.r.min == 0);   CHECK(stats.r.max == 100);
    CHECK(stats.g.min == 0);   CHECK(stats.g.max == 200);
    CHECK(stats.b.min == 0);   CHECK(stats.b.max == 50);
    CHECK(stats.r.mean == doctest::Approx(50.0));
    CHECK(stats.g.mean == doctest::Approx(100.0));
    CHECK(stats.b.mean == doctest::Approx(25.0));

    CHECK(stats.r.histogram[0]   == 1);
    CHECK(stats.r.histogram[100] == 1);
    CHECK(stats.alphaAllOpaque   == true);
}

TEST_CASE("ImageColorStats - alpha transparency flags")
{
    auto opaque = zv::computeImageColorStats(makeSolid(128, 128, 128, 255));
    CHECK(opaque.alphaAllOpaque    == true);
    CHECK(opaque.hasAnyTransparency == false);

    auto transp = zv::computeImageColorStats(makeSolid(128, 128, 128, 0));
    CHECK(transp.alphaAllOpaque    == false);
    CHECK(transp.hasAnyTransparency == true);
}
