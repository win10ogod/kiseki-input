#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace kiseki::core::config {

struct WebUiConfig {
    std::string host;
    std::uint16_t port;
};

struct HeartbeatConfig {
    bool enabled;
    std::uint32_t interval_seconds;
    bool notification_enabled;
    std::string message;
};

struct InputConfig {
    std::string default_backend;
    std::string windows_driver;
    std::string linux_driver;
    bool background_input_enabled;
};

struct ScreenshotConfig {
    std::string default_output_directory;
    std::uint32_t burst_fps;
    std::uint32_t burst_frames;
    std::string format;
};

struct SafetyConfig {
    bool allow_driver_input_without_target;
    bool allow_background_input_for_games;
};

struct AppConfig {
    std::uint32_t schema_version;
    WebUiConfig webui;
    HeartbeatConfig heartbeat;
    InputConfig input;
    ScreenshotConfig screenshot;
    SafetyConfig safety;
};

AppConfig default_config();
nlohmann::json to_json(const AppConfig& config);
AppConfig config_from_json(const nlohmann::json& json);

}
