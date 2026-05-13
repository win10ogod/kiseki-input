#include <catch2/catch_test_macros.hpp>

#include <string>

#include "core/capabilities/capabilities_model.hpp"

using kiseki::core::capabilities::foundation_capabilities;
using kiseki::core::capabilities::CaptureCapabilities;
using kiseki::core::capabilities::InputCapabilities;
using kiseki::core::capabilities::SessionCapabilities;
using kiseki::core::capabilities::to_json;

namespace {

consteval bool default_input_capabilities_fail_closed() {
    InputCapabilities capabilities;
    return !capabilities.driver && !capabilities.background_window;
}

consteval bool default_capture_capabilities_fail_closed() {
    CaptureCapabilities capabilities;
    return !capabilities.desktop && !capabilities.window && !capabilities.region && !capabilities.burst;
}

consteval bool default_session_capabilities_fail_closed() {
    SessionCapabilities capabilities;
    return !capabilities.background_desktop;
}

}

TEST_CASE("default capability structs fail closed") {
    STATIC_REQUIRE(default_input_capabilities_fail_closed());
    STATIC_REQUIRE(default_capture_capabilities_fail_closed());
    STATIC_REQUIRE(default_session_capabilities_fail_closed());
}

TEST_CASE("foundation capabilities include explicit limitations") {
    const auto capabilities = foundation_capabilities();
    const auto json = to_json(capabilities);

    REQUIRE(json["input"]["driver"].get<bool>() == false);
    REQUIRE(json["input"]["backgroundWindow"].get<bool>() == false);
    REQUIRE(json["capture"]["desktop"].get<bool>() == false);
    REQUIRE(json["capture"]["window"].get<bool>() == false);
    REQUIRE(json["capture"]["region"].get<bool>() == false);
    REQUIRE(json["capture"]["burst"].get<bool>() == false);
    REQUIRE(json["session"]["backgroundDesktop"].get<bool>() == false);
    REQUIRE_FALSE(json["limitations"].empty());

    const auto limitations = json["limitations"].dump();
    REQUIRE(limitations.find("configuration and WebUI only") != std::string::npos);
    REQUIRE(limitations.find("separate implementation slices") != std::string::npos);
    REQUIRE(limitations.find("background-window input depends") != std::string::npos);
}
