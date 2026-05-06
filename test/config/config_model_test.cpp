#include <catch2/catch_test_macros.hpp>

#include "core/config/config_model.hpp"

using kiseki::core::config::AppConfig;
using kiseki::core::config::config_from_json;
using kiseki::core::config::default_config;
using kiseki::core::config::to_json;

TEST_CASE("default config matches schema version one") {
    const AppConfig config = default_config();

    REQUIRE(config.schema_version == 1);
    REQUIRE(config.webui.host == "127.0.0.1");
    REQUIRE(config.webui.port == 8787);
    REQUIRE(config.heartbeat.enabled);
    REQUIRE(config.heartbeat.interval_seconds == 300);
    REQUIRE(config.heartbeat.notification_enabled);
    REQUIRE(config.heartbeat.message == "Kiseki Input is running");
    REQUIRE(config.input.default_backend == "background-window");
    REQUIRE(config.input.windows_driver == "AnyDriver");
    REQUIRE(config.input.linux_driver == "uinput");
    REQUIRE(config.input.background_input_enabled);
    REQUIRE(config.screenshot.default_output_directory == "");
    REQUIRE(config.screenshot.burst_fps == 60);
    REQUIRE(config.screenshot.burst_frames == 8);
    REQUIRE(config.screenshot.format == "png");
    REQUIRE(config.safety.allow_driver_input_without_target);
    REQUIRE(config.safety.allow_background_input_for_games);
}

TEST_CASE("config converts to and from json") {
    AppConfig config = default_config();
    config.webui.port = 9000;
    config.heartbeat.message = "running";
    config.screenshot.default_output_directory = "frames";

    const auto json = to_json(config);
    const AppConfig parsed = config_from_json(json);

    REQUIRE(parsed.webui.port == 9000);
    REQUIRE(parsed.heartbeat.message == "running");
    REQUIRE(parsed.screenshot.default_output_directory == "frames");
    REQUIRE(to_json(parsed) == json);
}
