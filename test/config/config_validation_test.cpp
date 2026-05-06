#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "core/config/config_model.hpp"
#include "core/config/config_validation.hpp"

using kiseki::core::config::default_config;
using kiseki::core::config::validate_config;

namespace {

std::string message_for_issue(const kiseki::core::config::ValidationResult& result, std::string_view path) {
    for (const auto& issue : result.issues) {
        if (issue.path == path) {
            return issue.message;
        }
    }

    return "";
}

}

TEST_CASE("default config is valid") {
    const auto result = validate_config(default_config());
    REQUIRE(result.valid());
    REQUIRE(result.issues.empty());
}

TEST_CASE("invalid schema version is rejected") {
    auto config = default_config();
    config.schema_version = 2;

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("schemaVersion"));
}

TEST_CASE("empty webui host is rejected") {
    auto config = default_config();
    config.webui.host = "";

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("webui.host"));
}

TEST_CASE("invalid webui port is rejected") {
    auto config = default_config();
    config.webui.port = 0;

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.issues.at(0).path == "webui.port");
}

TEST_CASE("invalid heartbeat interval is rejected when heartbeat is enabled") {
    auto config = default_config();
    config.heartbeat.enabled = true;
    config.heartbeat.interval_seconds = 0;

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("heartbeat.intervalSeconds"));
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

TEST_CASE("invalid linux input driver is rejected") {
    auto config = default_config();
    config.input.linux_driver = "evdev";

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("input.linuxDriver"));
}

TEST_CASE("windows input driver validation message lists accepted values") {
    auto config = default_config();
    config.input.windows_driver = "Unknown";

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("input.windowsDriver"));
    REQUIRE(
        message_for_issue(result, "input.windowsDriver") ==
        "windowsDriver must be AnyDriver, SendInput, Logitech, LogitechGHubNew, Razer, DD, or MouClassInputInjection");
}

TEST_CASE("screenshot burst fps above maximum is rejected") {
    auto config = default_config();
    config.screenshot.burst_fps = 241;

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("screenshot.burstFps"));
}

TEST_CASE("screenshot burst frames above maximum is rejected") {
    auto config = default_config();
    config.screenshot.burst_frames = 241;

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("screenshot.burstFrames"));
}

TEST_CASE("empty heartbeat message is rejected when notifications are enabled") {
    auto config = default_config();
    config.heartbeat.notification_enabled = true;
    config.heartbeat.message = "";

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("heartbeat.message"));
}
