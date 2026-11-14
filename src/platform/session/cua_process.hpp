#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace kiseki::platform::session::detail {

struct ProcessResult {
    int code = 2;
    std::string output;
    std::string error;
};

// Variables set in the child in addition to (or replacing) the inherited environment.
using EnvironmentOverrides = std::vector<std::pair<std::string, std::string>>;

// Arguments and stdin never pass through a shell. Output is not truncated.
ProcessResult run_process(const std::vector<std::string>& arguments, const std::string& input = "",
    const EnvironmentOverrides& environment = {});
ProcessResult start_process(const std::vector<std::string>& arguments, const std::filesystem::path& log);
std::string path_text(const std::filesystem::path& path);
std::string environment_text(const char* name);

}
