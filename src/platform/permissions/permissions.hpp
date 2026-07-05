#pragma once

#include "platform/result.hpp"

namespace kiseki::platform::permissions {

OperationResult request_macos_screen_recording(bool prompt, bool open_settings);
OperationResult request_macos_accessibility(bool prompt, bool open_settings);

}
