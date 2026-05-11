#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace kiseki::cli {

struct Io {
    std::ostream& out;
    std::ostream& err;
};

struct WebUiLaunchOptions {
    std::string host;
    std::uint16_t port;
};

struct ScreenshotDesktopOptions {
    std::filesystem::path output_path;
};

struct ScreenshotBurstOptions {
    std::filesystem::path output_directory;
    std::string prefix;
    std::uint32_t frames;
    std::uint32_t fps;
};

struct TargetOptions {
    std::string title;
    std::uint32_t pid;
    std::string window_id;
};

struct ScreenshotWindowOptions {
    TargetOptions target;
    std::filesystem::path output_path;
};

struct ScreenshotWindowBurstOptions {
    TargetOptions target;
    std::filesystem::path output_directory;
    std::string prefix;
    std::uint32_t frames;
    std::uint32_t fps;
};

struct InputKeyOptions {
    std::string key;
    std::string backend;
};

struct InputComboOptions {
    std::string keys;
    std::string backend;
};

struct InputTextOptions {
    std::string text;
    std::filesystem::path text_file;
};

struct InputMouseOptions {
    int dx;
    int dy;
    int x;
    int y;
    bool absolute;
    std::string backend;
    std::string click;
};

struct InputDragOptions {
    std::filesystem::path path;
    std::string backend;
};

struct BackgroundTextOptions {
    TargetOptions target;
    std::string text;
    std::filesystem::path text_file;
};

struct BackgroundKeyOptions {
    TargetOptions target;
    std::string key;
};

struct BackgroundMouseOptions {
    TargetOptions target;
    int x;
    int y;
    std::string click;
};

struct DaemonOptions {
    bool once;
};

struct MacroOptions {
    std::filesystem::path path;
};

struct Dependencies {
    std::function<int(const WebUiLaunchOptions&, const std::filesystem::path&, Io)> launch_config_ui;
    std::function<int(const ScreenshotDesktopOptions&, Io)> capture_desktop;
    std::function<int(const ScreenshotBurstOptions&, Io)> capture_burst;
    std::function<int(const ScreenshotWindowOptions&, Io)> capture_window;
    std::function<int(const ScreenshotWindowBurstOptions&, Io)> capture_window_burst;
    std::function<int(const InputKeyOptions&, Io)> input_key;
    std::function<int(const InputComboOptions&, Io)> input_combo;
    std::function<int(const InputTextOptions&, Io)> input_text;
    std::function<int(const InputMouseOptions&, Io)> input_mouse;
    std::function<int(const InputDragOptions&, Io)> input_drag;
    std::function<int(const BackgroundTextOptions&, Io)> input_background_text;
    std::function<int(const BackgroundKeyOptions&, Io)> input_background_key;
    std::function<int(const BackgroundMouseOptions&, Io)> input_background_mouse;
    std::function<int(const DaemonOptions&, const std::filesystem::path&, Io)> run_daemon;
};

std::filesystem::path resolve_config_path(std::filesystem::path override_path);
Dependencies default_dependencies();

// Runs the CLI with argv-tail arguments only. The executable name / argv[0] is
// excluded; main.cpp strips argv[0] before calling this function.
int run(
    const std::vector<std::string>& args,
    std::filesystem::path config_path,
    Io io,
    Dependencies dependencies = default_dependencies());

}
