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

bool background_desktop_available();
std::filesystem::path default_state_directory();

OperationResult start_background_desktop(const BackgroundDesktopStartOptions& options);
OperationResult stop_background_desktop(const BackgroundDesktopStopOptions& options);
OperationResult launch_in_background_desktop(const BackgroundDesktopLaunchOptions& options);
CaptureResult screenshot_background_desktop(const BackgroundDesktopScreenshotOptions& options);
OperationResult text_background_desktop(const BackgroundDesktopTextOptions& options);
OperationResult key_background_desktop(const BackgroundDesktopKeyOptions& options);
OperationResult mouse_background_desktop(const BackgroundDesktopMouseOptions& options);

}
