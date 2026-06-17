#pragma once

#include <string>
#include <vector>

#include "platform/result.hpp"
#include "platform/target/target.hpp"

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

struct BackgroundMouseOptions {
    kiseki::platform::target::TargetQuery target;
    int x;
    int y;
    std::string click;
};

struct BackgroundDragOptions {
    kiseki::platform::target::TargetQuery target;
    std::vector<MousePoint> points;
};

bool system_input_available();
bool driver_input_available();
bool background_window_input_available();

OperationResult tap_key(const std::string& key, const std::string& backend = "auto");
OperationResult key_combo(const std::string& keys, const std::string& backend = "auto");
OperationResult type_text(const std::string& text);
OperationResult mouse_action(const MouseOptions& options);
OperationResult mouse_drag_absolute(
    const std::vector<MousePoint>& points,
    const std::string& backend = "auto",
    int step_delay_ms = 2,
    int start_hold_ms = 0,
    int end_hold_ms = 0);
OperationResult background_type_text(const kiseki::platform::target::TargetQuery& target, const std::string& text);
OperationResult background_tap_key(const kiseki::platform::target::TargetQuery& target, const std::string& key);
OperationResult background_mouse_action(const BackgroundMouseOptions& options);
OperationResult background_mouse_drag(const BackgroundDragOptions& options);

}
