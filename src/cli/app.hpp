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
int run(const std::vector<std::string>& args, std::filesystem::path config_path, Io io);

}
