#include "ztrip/version.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("core library links and reports a version", "[smoke]") {
    REQUIRE_FALSE(ztrip::version().empty());
}
