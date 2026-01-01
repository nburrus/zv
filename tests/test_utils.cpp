#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <libzv/Utils.h>
#include <libzv/MathUtils.h>
#include <set>

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
