#include "core/config/config_validation.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace kiseki::core::config {

namespace {

bool one_of(std::string_view value, const auto& allowed) {
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

void add_issue(ValidationResult& result, std::string path, std::string message) {
    result.issues.push_back(ValidationIssue{
        .path = std::move(path),
        .message = std::move(message),
    });
}

}

bool ValidationResult::valid() const {
    return issues.empty();
}

bool ValidationResult::has_issue(std::string_view path) const {
    return std::any_of(issues.begin(), issues.end(), [path](const ValidationIssue& issue) {
        return issue.path == path;
    });
}

ValidationResult validate_config(const AppConfig& config) {
    ValidationResult result;

    if (config.schema_version != 1) {
        add_issue(result, "schemaVersion", "schemaVersion must be 1");
    }

    if (config.webui.host.empty()) {
        add_issue(result, "webui.host", "host must not be empty");
    }

    if (config.webui.port == 0) {
        add_issue(result, "webui.port", "port must be between 1 and 65535");
    }

    if (config.heartbeat.enabled && config.heartbeat.interval_seconds < 1) {
        add_issue(result, "heartbeat.intervalSeconds", "intervalSeconds must be at least 1 when heartbeat is enabled");
    }

    if (config.heartbeat.notification_enabled && config.heartbeat.message.empty()) {
        add_issue(result, "heartbeat.message", "message must not be empty when notifications are enabled");
    }

    constexpr std::array<std::string_view, 2> input_backends{
        "driver",
        "background-window",
    };
    if (!one_of(config.input.default_backend, input_backends)) {
        add_issue(result, "input.defaultBackend", "defaultBackend must be driver or background-window");
    }

    constexpr std::array<std::string_view, 7> windows_drivers{
        "AnyDriver",
        "SendInput",
        "Logitech",
        "LogitechGHubNew",
        "Razer",
        "DD",
        "MouClassInputInjection",
    };
    if (!one_of(config.input.windows_driver, windows_drivers)) {
        add_issue(
            result,
            "input.windowsDriver",
            "windowsDriver must be AnyDriver, SendInput, Logitech, LogitechGHubNew, Razer, DD, or MouClassInputInjection");
    }

    if (config.input.linux_driver != "uinput") {
        add_issue(result, "input.linuxDriver", "linuxDriver must be uinput");
    }

    if (config.screenshot.burst_fps < 1 || config.screenshot.burst_fps > 240) {
        add_issue(result, "screenshot.burstFps", "burstFps must be between 1 and 240");
    }

    if (config.screenshot.burst_frames < 1 || config.screenshot.burst_frames > 240) {
        add_issue(result, "screenshot.burstFrames", "burstFrames must be between 1 and 240");
    }

    constexpr std::array<std::string_view, 1> formats{"bmp"};
    if (!one_of(config.screenshot.format, formats)) {
        add_issue(result, "screenshot.format", "format must be bmp");
    }

    return result;
}

}
