#pragma once

#include <string>

namespace kiseki::platform::permissions {

struct PermissionStatus {
    // False when the check does not exist on this platform; `message` then explains why.
    bool supported = false;
    std::string permission; // "screen-recording" or "accessibility"
    bool granted = false;
    bool prompted = false;
    bool settings_opened = false;
    std::string message;
};

// Checks the current process's TCC grant; optionally asks macOS to prompt or opens System Settings.
PermissionStatus macos_screen_recording_status(bool prompt, bool open_settings);
PermissionStatus macos_accessibility_status(bool prompt, bool open_settings);

}
