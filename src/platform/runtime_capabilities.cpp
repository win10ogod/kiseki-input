#include "platform/runtime_capabilities.hpp"

#include "platform/capture/screenshot.hpp"
#include "platform/input/input.hpp"
#include "platform/session/background_desktop.hpp"

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
    capabilities.limitations = {
        "WebUI is configuration-only; operational input, screenshot, notification, and daemon actions are CLI-only",
        "background-window input depends on whether the target accepts system window messages or public automation events",
        "Linux true background desktop requires Xvfb and runs applications inside an isolated X11 DISPLAY",
        "Windows background screenshot uses selected-window capture and does not require VM or Docker",
        "Windows selected-window input commands are compatibility helpers for targets that accept system window messages",
        "some Raw Input, DirectInput, protected fullscreen, and hardware-overlay targets may ignore background input or window capture",
    };
    if (!capabilities.input.driver) {
#ifdef _WIN32
        capabilities.limitations.push_back("Windows driver input requires IbInputSimulator.dll next to the executable; otherwise system input is used when available");
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
