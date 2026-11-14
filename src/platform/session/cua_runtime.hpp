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
OperationResult cua_session_call(const std::string& tool, nlohmann::json arguments,
    const std::filesystem::path& output = {});
OperationResult cua_driver_command(const std::vector<std::string>& arguments);
// Workflow boundary: install if missing, apply an available update when the automatic check is due (at most once per
// 24 hours per installed version), then make sure the daemon runs. With ENSURE_DAEMON false the daemon is only
// checked when the binary was installed or replaced; callers start it on demand when a tool call finds none.
OperationResult cua_setup(bool update = true, int startup_wait_ms = 30000, bool ensure_daemon = true);
// Explicit check of the selected channel, bypassing the Driver's cache; APPLY installs an available update.
OperationResult cua_update(bool apply, bool force_check = true);

}
