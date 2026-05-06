#include <catch2/catch_test_macros.hpp>

#include "core/version.hpp"

TEST_CASE("version is available") {
    REQUIRE(kiseki::core::version() == "0.1.0");
}
