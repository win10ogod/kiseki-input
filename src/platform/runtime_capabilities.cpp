#include "platform/runtime_capabilities.hpp"

#include "platform/capture/screenshot.hpp"
#include "platform/input/input.hpp"
#include "platform/session/background_desktop.hpp"
#include "platform/session/macos_cua.hpp"
#include "platform/target/target.hpp"

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace kiseki::platform {

kiseki::core::capabilities::CapabilityMatrix runtime_capabilities() {
    auto capabilities = kiseki::core::capabilities::foundation_capabilities();
    capabilities.input.driver = input::driver_input_available();
    capabilities.input.system = input::system_input_available();
    capabilities.input.background_window = input::background_window_input_available();
    capabilities.capture.desktop = capture::desktop_capture_available();
    capabilities.capture.window = capture::window_capture_available();
    capabilities.capture.burst = capabilities.capture.desktop || capabilities.capture.window;
    capabilities.session.background_desktop = session::background_desktop_available();
    capabilities.session.cua_background = session::cua_background_available();
    capabilities.session.macos_cua_background = session::macos_cua_background_available();
    capabilities.observation.window_tree = target::target_window_available();
#ifdef _WIN32
    capabilities.observation.windows_uia = true;
    capabilities.observation.macos_ax = false;
#elif defined(__APPLE__)
    capabilities.observation.windows_uia = false;
    capabilities.observation.macos_ax = AXIsProcessTrusted();
#else
    capabilities.observation.windows_uia = false;
    capabilities.observation.macos_ax = false;
#endif
    capabilities.limitations = {
        "WebUI is configuration-only; operational input, screenshot, notification, and daemon actions are CLI-only",
#if defined(__APPLE__)
        "macOS desktop and selected-window screenshots require Screen Recording permission in the active GUI session",
        "macOS global keyboard and mouse input uses Quartz CGEvent and requires Accessibility permission",
        "macOS input commands are global/current-session operations; CUA background operations are exposed separately under background cua",
        "CUA background operation is available only when cua-driver is installed and authorized for the active GUI session",
        "macOS CUA drag may use visible HID-style movement when the target is frontmost; backgrounded pid-routed drags can be rejected by some canvas/OpenGL-style targets",
        "macOS CUA agent-cursor feedback is an overlay and is not the real system pointer; set KISEKI_CUA_SESSION to a fresh per-run id when actions should declare a visible CUA session",
        "macOS screenshots use ScreenCaptureKit in the current user GUI session; target listing uses Window Services",
        "macOS observe ui can use Accessibility AX data when the active CLI host has Accessibility permission",
#else
        "background window input depends on whether the target accepts system window messages or public automation events",
#if defined(_WIN32)
        "Windows background screenshot uses selected-window capture and does not require VM or Docker",
        "Windows selected-window input commands are message/API helpers for targets that accept system window messages",
        "Windows CUA background operation uses optional cua-driver when installed in the interactive desktop session",
        "Windows observe ui can use UI Automation for structured non-visual UI trees when the target exposes UIA data",
#else
        "Linux true background desktop requires Xvfb and runs applications inside an isolated X11 DISPLAY",
        "Linux CUA background operation uses optional cua-driver; upstream currently marks Linux as pre-release while platform testing continues",
#endif
        "some Raw Input, DirectInput, protected fullscreen, and hardware-overlay targets may ignore background input or window capture",
#endif
    };
    if (!capabilities.input.driver) {
#ifdef _WIN32
        capabilities.limitations.push_back("Windows driver input requires IbInputSimulator.dll next to the executable; otherwise system input is used when available");
#elif defined(__APPLE__)
        capabilities.limitations.push_back("macOS driver-level input backend is not enabled; Quartz CGEvent system input is used when available");
#else
        capabilities.limitations.push_back("Linux driver-level input is not enabled; X11/XTest system input is used when available");
#endif
    }
    if (!capabilities.input.system) {
        capabilities.limitations.push_back("system input backend is unavailable on this platform/session");
    }
    if (!capabilities.capture.desktop) {
        capabilities.limitations.push_back("desktop screenshot backend is unavailable on this platform/session");
    }
    if (!capabilities.capture.window) {
        capabilities.limitations.push_back("window screenshot backend is unavailable on this platform/session");
    }
    return capabilities;
}

}
