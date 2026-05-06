#pragma once

#include <string>

#include "platform/result.hpp"

namespace kiseki::platform::input {

struct MouseOptions {
    int dx;
    int dy;
    std::string click;
};

bool system_input_available();
bool driver_input_available();

OperationResult tap_key(const std::string& key);
OperationResult key_combo(const std::string& keys);
OperationResult type_text(const std::string& text);
OperationResult mouse_action(const MouseOptions& options);

}
