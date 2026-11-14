#pragma once

#include <filesystem>
#include <string>

#include "platform/result.hpp"

namespace kiseki::platform::session {

struct BackgroundDesktopStartOptions {
    std::string display;
    std::filesystem::path state_directory;
    int width = 1280;
    int height = 720;
    int depth = 24;
};

struct BackgroundDesktopStopOptions {
    std::string display;
    std::filesystem::path state_directory;
};

struct BackgroundDesktopLaunchOptions {
    std::string display;
    std::string command;
};

struct BackgroundDesktopScreenshotOptions {
    std::string display;
    std::filesystem::path output_path;
};

struct BackgroundDesktopTextOptions {
    std::string display;
    std::string text;
};

struct BackgroundDesktopKeyOptions {
    std::string display;
    std::string key;
};

struct BackgroundDesktopMouseOptions {
    std::string display;
    int x = 0;
    int y = 0;
    std::string click = "none";
};

struct BackgroundDesktopStatusOptions {
    std::string display;
    std::filesystem::path state_directory;
};

struct BackgroundDesktopStatus {
    bool supported = false;
    // "running" (Kiseki-started Xvfb answers), "unresponsive" (its process lives but
    // the display does not answer), "not-running", "stale-pid-file", "foreign-server"
    // (an X server not started by Kiseki answers on the display), "invalid-display",
    // or "unsupported".
    std::string state;
    std::string display;
    std::filesystem::path pid_file;
    long long pid = 0;
    bool process_alive = false;
    std::string process_name;
    bool display_reachable = false;
    int width = 0;
    int height = 0;
    int depth = 0;
    std::string message;
};

bool background_desktop_available();
std::filesystem::path default_state_directory();

// Read-only: inspects the pid file, process, and display; starts or stops nothing.
BackgroundDesktopStatus background_desktop_status(const BackgroundDesktopStatusOptions& options);

OperationResult start_background_desktop(const BackgroundDesktopStartOptions& options);
OperationResult stop_background_desktop(const BackgroundDesktopStopOptions& options);
OperationResult launch_in_background_desktop(const BackgroundDesktopLaunchOptions& options);
CaptureResult screenshot_background_desktop(const BackgroundDesktopScreenshotOptions& options);
OperationResult text_background_desktop(const BackgroundDesktopTextOptions& options);
OperationResult key_background_desktop(const BackgroundDesktopKeyOptions& options);
OperationResult mouse_background_desktop(const BackgroundDesktopMouseOptions& options);

}
