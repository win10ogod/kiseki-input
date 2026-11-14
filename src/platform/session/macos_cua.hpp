#pragma once

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "platform/result.hpp"

namespace kiseki::platform::session {

struct MacCuaLaunchOptions {
    std::string bundle_id;
    std::string name;
    std::vector<std::string> urls;
    bool creates_new_instance = false;
    std::vector<std::string> additional_arguments;
    std::string launch_path;
    std::string aumid; // Windows packaged-app id (launch_app `aumid`)
    std::string path;  // Windows executable path (launch_app `path`)
};

// Driver 0.39+ addresses an element only by element_token, whose value is `<snapshot_id>:<index>` (Driver 0.49.9 schema). An index is
// therefore sent as the token of its own snapshot; an index without its snapshot, or an index that disagrees with an explicit token, is
// rejected before dispatch (std::invalid_argument). Returns "" when no element was named.
std::string cua_element_token(bool has_element_index, int element_index, const std::string& snapshot_id, const std::string& element_token);

// Notes for stderr about a get_window_state answer that an agent must not miss: a truncated tree, and a screenshot whose pixel frame
// is reduced relative to the window (the provider's max_image_dimension cap). Empty when there is nothing to say.
std::string cua_observation_notes(const nlohmann::json& response);

// The arguments for the provider's launch_app. Driver 0.38 accepts an absolute .app path in `name` on macOS (list_apps reports it as
// `launch_path`), so a --launch-path that names a .app bundle goes there when MACOS_APP_PATHS is set and no --name was given; any other path
// stays `launch_path` (native Windows/Linux launch paths). The function is pure so both targets are tested on any host.
nlohmann::json cua_launch_arguments(const MacCuaLaunchOptions& options, bool macos_app_paths);

struct MacCuaWindowListOptions {
    int pid = 0;
    bool has_pid = false;
    bool on_screen_only = false;
    std::string app;
};

// get_window_state controls (Driver 0.39+). Unset fields are omitted so the provider's defaults apply.
struct MacCuaWindowStateOptions {
    int pid = 0;
    std::uint64_t window_id = 0;
    std::filesystem::path output_path;
    std::string query;
    std::string provider_json = "{}";
    std::optional<int> max_elements;
    std::optional<int> max_depth;
    std::optional<int> timeout_ms;
    std::optional<int> max_image_dimension;
    std::string tree_format;
    std::string since;
    bool full_output = false;
    bool no_tree = false;
    bool verbose = false;
};

struct MacCuaScreenshotOptions {
    std::uint64_t window_id = 0;
    int pid = 0; // optional; avoids an unfiltered list_windows lookup
    std::filesystem::path output_path;
    std::string format; // png or jpeg; empty follows the output extension (png otherwise)
    std::optional<int> quality;
    std::optional<int> max_image_dimension;
};

struct MacCuaClickOptions {
    int pid = 0;
    std::uint64_t window_id = 0;
    bool has_window_id = false;
    int element_index = 0;
    bool has_element_index = false;
    std::string snapshot_id;
    std::string element_token;
    double x = 0.0;
    double y = 0.0;
    bool has_xy = false;
    std::string button = "left";
    std::vector<std::string> modifiers;
    std::string provider_json = "{}";
};

struct MacCuaTextOptions {
    int pid = 0;
    std::string text;
    std::uint64_t window_id = 0;
    bool has_window_id = false;
    int element_index = 0;
    bool has_element_index = false;
    std::string snapshot_id;
    std::string element_token;
    std::optional<int> delay_ms; // omitted unless requested: the provider's own pacing applies
    std::string provider_json = "{}";
};

struct MacCuaKeyOptions {
    int pid = 0;
    std::string key;
    std::uint64_t window_id = 0;
    bool has_window_id = false;
    int element_index = 0;
    bool has_element_index = false;
    std::string snapshot_id;
    std::string element_token;
    std::vector<std::string> modifiers;
    std::string provider_json = "{}";
};

struct MacCuaHotkeyOptions {
    int pid = 0;
    std::vector<std::string> keys;
    std::uint64_t window_id = 0;
    bool has_window_id = false;
    int element_index = 0;
    bool has_element_index = false;
    std::string snapshot_id;
    std::string element_token;
    std::string provider_json = "{}";
};

struct MacCuaDragOptions {
    int pid = 0;
    std::uint64_t window_id = 0;
    bool has_window_id = false;
    double from_x = 0.0;
    double from_y = 0.0;
    double to_x = 0.0;
    double to_y = 0.0;
    int duration_ms = 500;
    int steps = 20;
    std::string button = "left";
    std::vector<std::string> modifiers;
    std::string provider_json = "{}";
};

struct MacCuaPoint {
    double x = 0.0;
    double y = 0.0;
};

struct MacCuaDrawOptions {
    int pid = 0;
    std::uint64_t window_id = 0;
    std::vector<MacCuaPoint> points;
    // One duration per segment from the point file's time_ms offsets; empty uses duration_ms for every segment.
    std::vector<int> segment_durations_ms;
    int duration_ms = 120;
    int steps = 6;
    int stroke_gap_ms = 0;
    int max_segments = 96;
    std::string button = "left";
    std::vector<std::string> modifiers;
    std::string provider_json = "{}";
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
    std::string style;  // Driver 0.34+, e.g. signature_arc, spring_settle, magnetic, comet_swoop, adaptive, classic
    std::string timing; // Driver 0.34+, e.g. native, fitts, fixed, relaxed, steady, calm, deliberate
    bool has_turn_radius = false;
    double turn_radius = 0.0;
    std::vector<std::pair<std::string, std::string>> effects; // effect name -> on, off or default
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
