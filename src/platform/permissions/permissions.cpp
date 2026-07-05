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

OperationResult ok(std::string message) {
    return OperationResult{
        .ok = true,
        .code = 0,
        .message = std::move(message),
        .error = "",
    };
}

OperationResult fail(std::string error) {
    return OperationResult{
        .ok = false,
        .code = 2,
        .message = "",
        .error = std::move(error),
    };
}

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

} // namespace

OperationResult request_macos_screen_recording(bool prompt, bool open_settings) {
#ifdef __APPLE__
    bool granted = CGPreflightScreenCaptureAccess();
    if (!granted && prompt) {
        granted = CGRequestScreenCaptureAccess();
    }
    if (!granted && open_settings) {
        open_settings_url("x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture");
    }
    return ok(permission_message("Screen Recording", granted, prompt, open_settings));
#else
    (void)prompt;
    (void)open_settings;
    return fail("macOS Screen Recording permission helper is only available on macOS");
#endif
}

OperationResult request_macos_accessibility(bool prompt, bool open_settings) {
#ifdef __APPLE__
    bool granted = AXIsProcessTrusted();
    if (!granted && prompt) {
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
    if (!granted && open_settings) {
        open_settings_url("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility");
    }
    return ok(permission_message("Accessibility", granted, prompt, open_settings));
#else
    (void)prompt;
    (void)open_settings;
    return fail("macOS Accessibility permission helper is only available on macOS");
#endif
}

}
