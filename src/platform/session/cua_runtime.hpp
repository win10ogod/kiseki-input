#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "platform/result.hpp"

namespace kiseki::platform::session {

std::filesystem::path cua_driver_path();
OperationResult cua_call(const std::string& tool, const nlohmann::json& arguments,
    const std::filesystem::path& output = {});
OperationResult cua_driver_command(const std::vector<std::string>& arguments);
// Workflow boundary: install if missing, periodically update, then start the daemon.
OperationResult cua_setup(bool update = true, int startup_wait_ms = 30000);
OperationResult cua_update(bool apply, bool force_check = true);

}
