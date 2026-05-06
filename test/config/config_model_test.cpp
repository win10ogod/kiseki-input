#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include "core/config/config_model.hpp"

using kiseki::core::config::AppConfig;
using kiseki::core::config::config_from_json;
using kiseki::core::config::default_config;
using kiseki::core::config::to_json;

namespace {

void require_invalid_config_message(const nlohmann::json& json, const std::string& expected_message) {
    try {
        (void)config_from_json(json);
        FAIL("expected invalid config value to throw");
    } catch (const std::invalid_argument& exception) {
        REQUIRE(exception.what() == expected_message);
    }
}

}

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

    REQUIRE(json.contains("schemaVersion"));
    REQUIRE(json.at("heartbeat").contains("intervalSeconds"));
    REQUIRE(json.at("input").contains("defaultBackend"));
    REQUIRE(json.at("screenshot").contains("defaultOutputDirectory"));
    REQUIRE(parsed.webui.port == 9000);
    REQUIRE(parsed.heartbeat.message == "running");
    REQUIRE(parsed.screenshot.default_output_directory == "frames");
    REQUIRE(to_json(parsed) == json);
}

TEST_CASE("missing config values use defaults") {
    const AppConfig empty = config_from_json(nlohmann::json::object());
    const AppConfig defaults = default_config();

    REQUIRE(empty.schema_version == defaults.schema_version);
    REQUIRE(empty.webui.host == defaults.webui.host);
    REQUIRE(empty.webui.port == defaults.webui.port);
    REQUIRE(empty.heartbeat.interval_seconds == defaults.heartbeat.interval_seconds);
    REQUIRE(empty.input.default_backend == defaults.input.default_backend);
    REQUIRE(empty.screenshot.default_output_directory == defaults.screenshot.default_output_directory);
    REQUIRE(empty.screenshot.burst_fps == defaults.screenshot.burst_fps);
    REQUIRE(empty.safety.allow_background_input_for_games == defaults.safety.allow_background_input_for_games);

    const AppConfig partial = config_from_json(nlohmann::json{
        {"webui", {
            {"port", 9000},
        }},
    });

    REQUIRE(partial.webui.port == 9000);
    REQUIRE(partial.webui.host == defaults.webui.host);
    REQUIRE(partial.heartbeat.message == defaults.heartbeat.message);
    REQUIRE(partial.input.windows_driver == defaults.input.windows_driver);
    REQUIRE(partial.screenshot.burst_frames == defaults.screenshot.burst_frames);
    REQUIRE(partial.safety.allow_driver_input_without_target == defaults.safety.allow_driver_input_without_target);
}

TEST_CASE("invalid unsigned integer config values throw") {
    require_invalid_config_message(
        nlohmann::json{
            {"webui", {
                {"port", 70000},
            }},
        },
        "Invalid config field 'webui.port': expected integer in range 0..65535");

    require_invalid_config_message(
        nlohmann::json{
            {"webui", {
                {"port", -1},
            }},
        },
        "Invalid config field 'webui.port': expected integer in range 0..65535");

    require_invalid_config_message(
        nlohmann::json{
            {"heartbeat", {
                {"intervalSeconds", 4294967296},
            }},
        },
        "Invalid config field 'heartbeat.intervalSeconds': expected integer in range 0..4294967295");

    require_invalid_config_message(
        nlohmann::json{
            {"screenshot", {
                {"burstFps", -1},
            }},
        },
        "Invalid config field 'screenshot.burstFps': expected integer in range 0..4294967295");

    require_invalid_config_message(
        nlohmann::json{
            {"screenshot", {
                {"burstFrames", -1},
            }},
        },
        "Invalid config field 'screenshot.burstFrames': expected integer in range 0..4294967295");

    require_invalid_config_message(
        nlohmann::json{
            {"screenshot", {
                {"burstFrames", 8.5},
            }},
        },
        "Invalid config field 'screenshot.burstFrames': expected integer in range 0..4294967295");
}
