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

}
