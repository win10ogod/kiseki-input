#include "core/config/config_model.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

[[noreturn]] void throw_invalid_unsigned_integer_field(const char* path, std::uint64_t max_value) {
    throw std::invalid_argument(
        "Invalid config field '" + std::string(path) + "': expected integer in range 0.." +
        std::to_string(max_value));
}

template <typename UInt>
UInt read_unsigned_integer_field(
    const nlohmann::json& object,
    const char* key,
    const char* path,
    UInt default_value) {
    if (!object.contains(key)) {
        return default_value;
    }

    const auto& value = object.at(key);
    constexpr auto max_value = static_cast<std::uint64_t>(std::numeric_limits<UInt>::max());
    std::uint64_t parsed_value = 0;

    if (value.is_number_unsigned()) {
        parsed_value = value.get<std::uint64_t>();
    } else if (value.is_number_integer()) {
        const auto signed_value = value.get<std::int64_t>();
        if (signed_value < 0) {
            throw_invalid_unsigned_integer_field(path, max_value);
        }
        parsed_value = static_cast<std::uint64_t>(signed_value);
    } else {
        throw_invalid_unsigned_integer_field(path, max_value);
    }

    if (parsed_value > max_value) {
        throw_invalid_unsigned_integer_field(path, max_value);
    }

    return static_cast<UInt>(parsed_value);
}

}

namespace kiseki::core::config {

AppConfig default_config() {
    return AppConfig{
        .schema_version = 1,
        .webui = WebUiConfig{
            .host = "127.0.0.1",
            .port = 8787,
        },
        .heartbeat = HeartbeatConfig{
            .enabled = true,
            .interval_seconds = 300,
            .notification_enabled = true,
            .message = "Kiseki Input is running",
        },
        .input = InputConfig{
            .default_backend = "background-window",
            .windows_driver = "AnyDriver",
            .linux_driver = "uinput",
            .background_input_enabled = true,
        },
        .screenshot = ScreenshotConfig{
            .default_output_directory = "",
            .burst_fps = 60,
            .burst_frames = 8,
            .format = "bmp",
        },
        .safety = SafetyConfig{
            .allow_driver_input_without_target = true,
            .allow_background_input_for_games = true,
        },
    };
}

nlohmann::json to_json(const AppConfig& config) {
    return nlohmann::json{
        {"schemaVersion", config.schema_version},
        {"webui", {
            {"host", config.webui.host},
            {"port", config.webui.port},
        }},
        {"heartbeat", {
            {"enabled", config.heartbeat.enabled},
            {"intervalSeconds", config.heartbeat.interval_seconds},
            {"notificationEnabled", config.heartbeat.notification_enabled},
            {"message", config.heartbeat.message},
        }},
        {"input", {
            {"defaultBackend", config.input.default_backend},
            {"windowsDriver", config.input.windows_driver},
            {"linuxDriver", config.input.linux_driver},
            {"backgroundInputEnabled", config.input.background_input_enabled},
        }},
        {"screenshot", {
            {"defaultOutputDirectory", config.screenshot.default_output_directory},
            {"burstFps", config.screenshot.burst_fps},
            {"burstFrames", config.screenshot.burst_frames},
            {"format", config.screenshot.format},
        }},
        {"safety", {
            {"allowDriverInputWithoutTarget", config.safety.allow_driver_input_without_target},
            {"allowBackgroundInputForGames", config.safety.allow_background_input_for_games},
        }},
    };
}

AppConfig config_from_json(const nlohmann::json& json) {
    AppConfig config = default_config();

    config.schema_version = read_unsigned_integer_field(json, "schemaVersion", "schemaVersion", config.schema_version);

    const auto webui = json.value("webui", nlohmann::json::object());
    config.webui.host = webui.value("host", config.webui.host);
    config.webui.port = read_unsigned_integer_field(webui, "port", "webui.port", config.webui.port);

    const auto heartbeat = json.value("heartbeat", nlohmann::json::object());
    config.heartbeat.enabled = heartbeat.value("enabled", config.heartbeat.enabled);
    config.heartbeat.interval_seconds = read_unsigned_integer_field(
        heartbeat,
        "intervalSeconds",
        "heartbeat.intervalSeconds",
        config.heartbeat.interval_seconds);
    config.heartbeat.notification_enabled = heartbeat.value("notificationEnabled", config.heartbeat.notification_enabled);
    config.heartbeat.message = heartbeat.value("message", config.heartbeat.message);

    const auto input = json.value("input", nlohmann::json::object());
    config.input.default_backend = input.value("defaultBackend", config.input.default_backend);
    config.input.windows_driver = input.value("windowsDriver", config.input.windows_driver);
    config.input.linux_driver = input.value("linuxDriver", config.input.linux_driver);
    config.input.background_input_enabled = input.value("backgroundInputEnabled", config.input.background_input_enabled);

    const auto screenshot = json.value("screenshot", nlohmann::json::object());
    config.screenshot.default_output_directory = screenshot.value("defaultOutputDirectory", config.screenshot.default_output_directory);
    config.screenshot.burst_fps =
        read_unsigned_integer_field(screenshot, "burstFps", "screenshot.burstFps", config.screenshot.burst_fps);
    config.screenshot.burst_frames = read_unsigned_integer_field(
        screenshot,
        "burstFrames",
        "screenshot.burstFrames",
        config.screenshot.burst_frames);
    config.screenshot.format = screenshot.value("format", config.screenshot.format);

    const auto safety = json.value("safety", nlohmann::json::object());
    config.safety.allow_driver_input_without_target =
        safety.value("allowDriverInputWithoutTarget", config.safety.allow_driver_input_without_target);
    config.safety.allow_background_input_for_games =
        safety.value("allowBackgroundInputForGames", config.safety.allow_background_input_for_games);

    return config;
}

}
