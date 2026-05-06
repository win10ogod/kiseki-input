#include <catch2/catch_test_macros.hpp>

#include "core/config/config_model.hpp"
#include "core/config/config_validation.hpp"

using kiseki::core::config::default_config;
using kiseki::core::config::validate_config;

TEST_CASE("default config is valid") {
    const auto result = validate_config(default_config());
    REQUIRE(result.valid());
    REQUIRE(result.issues.empty());
}

TEST_CASE("invalid webui port is rejected") {
    auto config = default_config();
    config.webui.port = 0;

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.issues.at(0).path == "webui.port");
}

TEST_CASE("invalid backend and screenshot settings are rejected") {
    auto config = default_config();
    config.input.default_backend = "raw";
    config.input.windows_driver = "Unknown";
    config.screenshot.burst_fps = 0;
    config.screenshot.burst_frames = 0;
    config.screenshot.format = "gif";

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("input.defaultBackend"));
    REQUIRE(result.has_issue("input.windowsDriver"));
    REQUIRE(result.has_issue("screenshot.burstFps"));
    REQUIRE(result.has_issue("screenshot.burstFrames"));
    REQUIRE(result.has_issue("screenshot.format"));
}

TEST_CASE("empty heartbeat message is rejected when notifications are enabled") {
    auto config = default_config();
    config.heartbeat.notification_enabled = true;
    config.heartbeat.message = "";

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("heartbeat.message"));
}
