#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "platform/result.hpp"

namespace kiseki::platform::notification {

OperationResult notify_once(const std::string& message);
int run_heartbeat_daemon(const std::filesystem::path& config_path, bool once, std::ostream& out, std::ostream& err);

}
