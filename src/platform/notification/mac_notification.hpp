#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "platform/result.hpp"

namespace kiseki::platform::notification {

OperationResult notify_once_macos(const std::string& message);

namespace detail {

std::string_view macos_notification_script();
std::vector<std::string> macos_notification_process_arguments(const std::string& message);

}

}
