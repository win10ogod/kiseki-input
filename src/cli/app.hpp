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

struct InputKeyOptions {
    std::string key;
};

struct InputComboOptions {
    std::string keys;
};

struct InputTextOptions {
    std::string text;
};

struct InputMouseOptions {
    int dx;
    int dy;
    std::string click;
};

struct DaemonOptions {
    bool once;
};

struct Dependencies {
    std::function<int(const WebUiLaunchOptions&, const std::filesystem::path&, Io)> launch_config_ui;
    std::function<int(const ScreenshotDesktopOptions&, Io)> capture_desktop;
    std::function<int(const ScreenshotBurstOptions&, Io)> capture_burst;
    std::function<int(const InputKeyOptions&, Io)> input_key;
    std::function<int(const InputComboOptions&, Io)> input_combo;
    std::function<int(const InputTextOptions&, Io)> input_text;
    std::function<int(const InputMouseOptions&, Io)> input_mouse;
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
