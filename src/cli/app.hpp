#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace kiseki::cli {

struct Io {
    std::ostream& out;
    std::ostream& err;
};

std::filesystem::path resolve_config_path(std::filesystem::path override_path);

// Runs the CLI with argv-tail arguments only. The executable name / argv[0] is
// excluded; main.cpp strips argv[0] before calling this function.
int run(const std::vector<std::string>& args, std::filesystem::path config_path, Io io);

}
