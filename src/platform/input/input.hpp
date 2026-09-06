#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <set>

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
    int click_count = 1;
    int click_interval_ms = 100;
    int hold_ms = 0;
    int wheel = 0;  // Positive up; Windows/X11: 120 per detent, macOS: pixels.
    int hwheel = 0; // Positive is right.
    bool cleanup_only = false;
};

struct MousePoint {
    int x;
    int y;
    std::int64_t time_ms = -1; // Optional monotonic offset from the first drag point.
};

// A sequence keeps the recipient acquired by a down even if its title changes.
// Only buttons acquired through this binding belong to its cleanup.
struct BackgroundMouseBinding {
    std::string window_id;
    std::string receiver_window_id;
    std::uint32_t process_id = 0;
    std::set<std::string> buttons;
};

struct BackgroundMouseOptions {
    kiseki::platform::target::TargetQuery target;
    int x;
    int y;
    std::string click;
    std::string held_buttons;
    std::string receiver_window_id;
    int click_count = 1;
    int click_interval_ms = 100;
    int hold_ms = 0;
    bool cleanup_only = false;
    std::shared_ptr<BackgroundMouseBinding> binding;
};

struct BackgroundDragOptions {
    kiseki::platform::target::TargetQuery target;
    std::vector<MousePoint> points;
    std::string button = "left";
    int step_delay_ms = 2;
    int start_hold_ms = 0;
    int end_hold_ms = 0;
};

bool system_input_available();
bool driver_input_available();
bool background_window_input_available();

bool key_supported(const std::string &key);
OperationResult key_action(const std::string &key, bool down, const std::string &backend = "auto",
                           bool cleanup_only = false);
OperationResult tap_key(const std::string &key, const std::string &backend = "auto", int hold_ms = 0);
OperationResult key_combo(const std::string &keys, const std::string &backend = "auto", int hold_ms = 0);
OperationResult type_text(const std::string& text);
OperationResult mouse_action(const MouseOptions& options);
OperationResult mouse_drag_absolute(const std::vector<MousePoint> &points, const std::string &backend = "auto",
                                    int step_delay_ms = 2, int start_hold_ms = 0, int end_hold_ms = 0,
                                    const std::string &button = "left", const std::string &modifiers = "");
OperationResult background_type_text(const kiseki::platform::target::TargetQuery& target, const std::string& text);
OperationResult background_tap_key(const kiseki::platform::target::TargetQuery& target, const std::string& key);
OperationResult background_mouse_action(const BackgroundMouseOptions& options);
OperationResult background_mouse_drag(const BackgroundDragOptions& options);
// Wait for this thread's queued native events before ending an invocation.
OperationResult synchronize_input();

}
