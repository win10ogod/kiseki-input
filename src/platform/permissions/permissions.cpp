#include "platform/permissions/permissions.hpp"

#include <cstdlib>
#include <string>
#include <utility>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace kiseki::platform::permissions {

namespace {

#ifdef __APPLE__
void open_settings_url(const char* url) {
    std::string command = "open '";
    command += url;
    command += "' >/dev/null 2>&1";
    static_cast<void>(std::system(command.c_str()));
}

std::string permission_message(const char* name, bool granted, bool prompt, bool open_settings) {
    std::string message = "macOS ";
    message += name;
    message += " permission: ";
    message += granted ? "granted" : "not granted";
    if (!granted && prompt) {
        message += "; prompt requested";
    }
    if (!granted && open_settings) {
        message += "; System Settings opened";
    }
    if (!granted) {
        message += "; rerun from the same GUI app/terminal after approving";
    }
    return message;
}
#endif

PermissionStatus unsupported(const char* permission, const char* name) {
    return PermissionStatus{
        .supported = false,
        .permission = permission,
        .granted = false,
        .prompted = false,
        .settings_opened = false,
        .message = std::string{"macOS "} + name + " permission helper is only available on macOS",
    };
}

} // namespace

PermissionStatus macos_screen_recording_status(bool prompt, bool open_settings) {
#ifdef __APPLE__
    bool granted = CGPreflightScreenCaptureAccess();
    const bool prompted = !granted && prompt;
    if (prompted) {
        granted = CGRequestScreenCaptureAccess();
    }
    const bool opened = !granted && open_settings;
    if (opened) {
        open_settings_url("x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture");
    }
    return PermissionStatus{
        .supported = true,
        .permission = "screen-recording",
        .granted = granted,
        .prompted = prompted,
        .settings_opened = opened,
        .message = permission_message("Screen Recording", granted, prompt, open_settings),
    };
#else
    (void)prompt;
    (void)open_settings;
    return unsupported("screen-recording", "Screen Recording");
#endif
}

PermissionStatus macos_accessibility_status(bool prompt, bool open_settings) {
#ifdef __APPLE__
    bool granted = AXIsProcessTrusted();
    const bool prompted = !granted && prompt;
    if (prompted) {
        const void* keys[] = {kAXTrustedCheckOptionPrompt};
        const void* values[] = {kCFBooleanTrue};
        CFDictionaryRef options = CFDictionaryCreate(
            kCFAllocatorDefault,
            keys,
            values,
            1,
            &kCFCopyStringDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        granted = AXIsProcessTrustedWithOptions(options);
        if (options != nullptr) {
            CFRelease(options);
        }
    }
    const bool opened = !granted && open_settings;
    if (opened) {
        open_settings_url("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility");
    }
    return PermissionStatus{
        .supported = true,
        .permission = "accessibility",
        .granted = granted,
        .prompted = prompted,
        .settings_opened = opened,
        .message = permission_message("Accessibility", granted, prompt, open_settings),
    };
#else
    (void)prompt;
    (void)open_settings;
    return unsupported("accessibility", "Accessibility");
#endif
}

}
