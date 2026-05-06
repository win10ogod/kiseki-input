#pragma once

#include <string>
#include <vector>

#include "platform/result.hpp"

namespace kiseki::platform::input {

struct MouseOptions {
    int dx;
    int dy;
    int x;
    int y;
    bool absolute;
    std::string backend;
    std::string click;
};

struct MousePoint {
    int x;
    int y;
};

bool system_input_available();
bool driver_input_available();

OperationResult tap_key(const std::string& key, const std::string& backend = "auto");
OperationResult key_combo(const std::string& keys, const std::string& backend = "auto");
OperationResult type_text(const std::string& text);
OperationResult mouse_action(const MouseOptions& options);
OperationResult mouse_drag_absolute(const std::vector<MousePoint>& points, const std::string& backend = "auto");

}
