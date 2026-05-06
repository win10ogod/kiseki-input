#include <catch2/catch_test_macros.hpp>

#include "core/capabilities/capabilities_model.hpp"

using kiseki::core::capabilities::foundation_capabilities;
using kiseki::core::capabilities::to_json;

TEST_CASE("foundation capabilities include explicit limitations") {
    const auto capabilities = foundation_capabilities();
    const auto json = to_json(capabilities);

    REQUIRE(json["input"]["driver"].get<bool>() == false);
    REQUIRE(json["input"]["backgroundWindow"].get<bool>() == false);
    REQUIRE(json["capture"]["desktop"].get<bool>() == false);
    REQUIRE(json["capture"]["window"].get<bool>() == false);
    REQUIRE(json["capture"]["region"].get<bool>() == false);
    REQUIRE(json["capture"]["burst"].get<bool>() == false);
    REQUIRE_FALSE(json["limitations"].empty());
}
