#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <libzv/Utils.h>
#include <libzv/MathUtils.h>

TEST_CASE("dummy test") {
    CHECK(1 + 1 == 2);
}

TEST_CASE("test libzv utils") {
    // Dummy test to verify we can link against libzv
    // You can add actual tests here later
    CHECK(true);
}
