#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "platform/result.hpp"

namespace kiseki::platform::session {

struct MacCuaLaunchOptions {
    std::string bundle_id;
    std::string name;
    std::vector<std::string> urls;
    bool creates_new_instance = false;
    std::vector<std::string> additional_arguments;
};

struct MacCuaWindowListOptions {
    int pid = 0;
    bool has_pid = false;
    bool on_screen_only = false;
};

struct MacCuaWindowStateOptions {
    int pid = 0;
    unsigned int window_id = 0;
    std::filesystem::path output_path;
    std::string query;
};

struct MacCuaScreenshotOptions {
    unsigned int window_id = 0;
    std::filesystem::path output_path;
    std::string format = "png";
    int quality = 95;
};

struct MacCuaClickOptions {
    int pid = 0;
    unsigned int window_id = 0;
    bool has_window_id = false;
    int element_index = 0;
    bool has_element_index = false;
    double x = 0.0;
    double y = 0.0;
    bool has_xy = false;
    std::string button = "left";
    std::vector<std::string> modifiers;
};

struct MacCuaTextOptions {
    int pid = 0;
    std::string text;
    unsigned int window_id = 0;
    bool has_window_id = false;
    int element_index = 0;
    bool has_element_index = false;
    int delay_ms = 30;
};

struct MacCuaKeyOptions {
    int pid = 0;
    std::string key;
    unsigned int window_id = 0;
    bool has_window_id = false;
    int element_index = 0;
    bool has_element_index = false;
    std::vector<std::string> modifiers;
};

struct MacCuaHotkeyOptions {
    int pid = 0;
    std::vector<std::string> keys;
    unsigned int window_id = 0;
    bool has_window_id = false;
};

struct MacCuaDragOptions {
    int pid = 0;
    unsigned int window_id = 0;
    bool has_window_id = false;
    double from_x = 0.0;
    double from_y = 0.0;
    double to_x = 0.0;
    double to_y = 0.0;
    int duration_ms = 500;
    int steps = 20;
    std::string button = "left";
    std::vector<std::string> modifiers;
};

struct MacCuaPoint {
    double x = 0.0;
    double y = 0.0;
};

struct MacCuaDrawOptions {
    int pid = 0;
    unsigned int window_id = 0;
    std::vector<MacCuaPoint> points;
    int duration_ms = 120;
    int steps = 6;
    int stroke_gap_ms = 0;
    int max_segments = 96;
    std::string button = "left";
    std::vector<std::string> modifiers;
};

struct MacCuaFeedbackEnableOptions {
    bool enabled = true;
};

struct MacCuaFeedbackMotionOptions {
    bool has_start_handle = false;
    double start_handle = 0.0;
    bool has_end_handle = false;
    double end_handle = 0.0;
    bool has_arc_size = false;
    double arc_size = 0.0;
    bool has_arc_flow = false;
    double arc_flow = 0.0;
    bool has_spring = false;
    double spring = 0.0;
    bool has_glide_duration_ms = false;
    double glide_duration_ms = 0.0;
    bool has_dwell_after_click_ms = false;
    double dwell_after_click_ms = 0.0;
    bool has_idle_hide_ms = false;
    double idle_hide_ms = 0.0;
};

struct MacCuaFeedbackStyleOptions {
    bool has_gradient_colors = false;
    std::vector<std::string> gradient_colors;
    bool has_bloom_color = false;
    std::string bloom_color;
    bool has_image_path = false;
    std::filesystem::path image_path;
    bool reset = false;
};

struct MacCuaFeedbackPresetOptions {
    std::string name = "natural";
};

bool cua_background_available();
bool macos_cua_background_available();

OperationResult macos_cua_status(bool prompt);
OperationResult macos_cua_launch(const MacCuaLaunchOptions& options);
OperationResult macos_cua_list_windows(const MacCuaWindowListOptions& options);
OperationResult macos_cua_window_state(const MacCuaWindowStateOptions& options);
OperationResult macos_cua_screenshot(const MacCuaScreenshotOptions& options);
OperationResult macos_cua_click(const MacCuaClickOptions& options);
OperationResult macos_cua_type_text(const MacCuaTextOptions& options);
OperationResult macos_cua_press_key(const MacCuaKeyOptions& options);
OperationResult macos_cua_hotkey(const MacCuaHotkeyOptions& options);
OperationResult macos_cua_drag(const MacCuaDragOptions& options);
OperationResult macos_cua_draw(const MacCuaDrawOptions& options);
OperationResult macos_cua_feedback_state();
OperationResult macos_cua_feedback_enable(const MacCuaFeedbackEnableOptions& options);
OperationResult macos_cua_feedback_motion(const MacCuaFeedbackMotionOptions& options);
OperationResult macos_cua_feedback_style(const MacCuaFeedbackStyleOptions& options);
OperationResult macos_cua_feedback_preset(const MacCuaFeedbackPresetOptions& options);

}
