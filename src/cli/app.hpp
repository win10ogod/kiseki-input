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

struct Dependencies {
    std::function<int(const WebUiLaunchOptions&, const std::filesystem::path&, Io)> launch_config_ui;
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
