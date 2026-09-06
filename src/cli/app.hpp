#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

#include "platform/input/input.hpp"

namespace kiseki::cli {

struct Io {
    std::ostream& out;
    std::ostream& err;
};

struct WebUiLaunchOptions {
    std::string host;
    std::uint16_t port;
};

struct ScreenshotDesktopOptions {
    std::filesystem::path output_path;
    bool json = false;
};

struct ScreenshotBurstOptions {
    std::filesystem::path output_directory;
    std::string prefix;
    std::uint32_t frames;
    std::uint32_t fps;
};

struct TargetOptions {
    std::string title;
    std::uint32_t pid;
    std::string window_id;
};

struct TargetListOptions {
    TargetOptions filter;
};

struct TargetInspectOptions {
    TargetOptions target;
};

struct ObserveUiOptions {
    TargetOptions target;
    std::string provider;
    int max_depth;
    int max_elements;
};

struct ScreenshotWindowOptions {
    TargetOptions target;
    std::filesystem::path output_path;
    bool json = false;
};

struct ScreenshotBackgroundWindowOptions {
    TargetOptions target;
    std::filesystem::path output_path;
    bool json = false;
};

struct ScreenshotWindowBurstOptions {
    TargetOptions target;
    std::filesystem::path output_directory;
    std::string prefix;
    std::uint32_t frames;
    std::uint32_t fps;
};

struct InputKeyOptions {
    std::string key;
    std::string backend;
    std::string action = "tap";
    int hold_ms = 0;
    bool cleanup_only = false;
};

struct InputComboOptions {
    std::string keys;
    std::string backend;
    int hold_ms = 0;
};

struct InputTextOptions {
    std::string text;
    std::filesystem::path text_file;
};

struct InputMouseOptions {
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
    int wheel = 0;
    int hwheel = 0;
    bool cleanup_only = false;
};

struct InputDragOptions {
    std::filesystem::path path;
    std::string backend;
    int step_delay_ms;
    int start_hold_ms;
    int end_hold_ms;
    std::string button = "left";
    std::string modifiers;
    std::vector<kiseki::platform::input::MousePoint> points;
};

struct BackgroundTextOptions {
    TargetOptions target;
    std::string text;
    std::filesystem::path text_file;
};

struct BackgroundKeyOptions {
    TargetOptions target;
    std::string key;
};

struct BackgroundMouseOptions {
    TargetOptions target;
    int x;
    int y;
    std::string click;
    std::string held_buttons;
    std::string receiver_window_id;
    int click_count = 1;
    int click_interval_ms = 100;
    int hold_ms = 0;
    bool cleanup_only = false;
};

struct BackgroundDragOptions {
    TargetOptions target;
    std::filesystem::path path;
    std::string button = "left";
    int step_delay_ms = 2;
    int start_hold_ms = 0;
    int end_hold_ms = 0;
    std::vector<kiseki::platform::input::MousePoint> points;
};

struct BackgroundDesktopStartOptions {
    std::string display;
    std::filesystem::path state_directory;
    int width;
    int height;
    int depth;
};

struct BackgroundDesktopStopOptions {
    std::string display;
    std::filesystem::path state_directory;
};

struct BackgroundDesktopLaunchOptions {
    std::string display;
    std::string command;
};

struct BackgroundDesktopScreenshotOptions {
    std::string display;
    std::filesystem::path output_path;
};

struct BackgroundDesktopTextOptions {
    std::string display;
    std::string text;
    std::filesystem::path text_file;
};

struct BackgroundDesktopKeyOptions {
    std::string display;
    std::string key;
};

struct BackgroundDesktopMouseOptions {
    std::string display;
    int x;
    int y;
    std::string click;
};

struct MacBackgroundStatusOptions {
    bool prompt;
};

struct MacBackgroundLaunchOptions {
    std::string bundle_id;
    std::string name;
    std::vector<std::string> urls;
    bool new_instance;
    std::vector<std::string> arguments;
};

struct MacBackgroundWindowsOptions {
    int pid;
    bool has_pid;
    bool on_screen_only;
};

struct MacBackgroundStateOptions {
    int pid;
    unsigned int window_id;
    std::filesystem::path output_path;
    std::string query;
};

struct MacBackgroundScreenshotOptions {
    unsigned int window_id;
    std::filesystem::path output_path;
    std::string format;
    int quality;
};

struct MacBackgroundClickOptions {
    int pid;
    unsigned int window_id;
    bool has_window_id;
    int element_index;
    bool has_element_index;
    double x;
    double y;
    bool has_xy;
    std::string button;
    std::vector<std::string> modifiers;
};

struct MacBackgroundTextOptions {
    int pid;
    std::string text;
    std::filesystem::path text_file;
    unsigned int window_id;
    bool has_window_id;
    int element_index;
    bool has_element_index;
    int delay_ms;
};

struct MacBackgroundKeyOptions {
    int pid;
    std::string key;
    unsigned int window_id;
    bool has_window_id;
    int element_index;
    bool has_element_index;
    std::vector<std::string> modifiers;
};

struct MacBackgroundHotkeyOptions {
    int pid;
    std::vector<std::string> keys;
    unsigned int window_id;
    bool has_window_id;
};

struct MacBackgroundDragOptions {
    int pid;
    unsigned int window_id;
    bool has_window_id;
    double from_x;
    double from_y;
    double to_x;
    double to_y;
    int duration_ms;
    int steps;
    std::string button;
    std::vector<std::string> modifiers;
};

struct MacBackgroundDrawOptions {
    int pid;
    unsigned int window_id;
    std::filesystem::path path;
    int duration_ms;
    int steps;
    int stroke_gap_ms;
    int max_segments;
    std::string button;
    std::vector<std::string> modifiers;
};

struct MacBackgroundFeedbackStatusOptions {};

struct MacBackgroundFeedbackEnableOptions {
    bool enabled;
};

struct MacBackgroundFeedbackMotionOptions {
    bool has_start_handle;
    double start_handle;
    bool has_end_handle;
    double end_handle;
    bool has_arc_size;
    double arc_size;
    bool has_arc_flow;
    double arc_flow;
    bool has_spring;
    double spring;
    bool has_glide_duration_ms;
    double glide_duration_ms;
    bool has_dwell_after_click_ms;
    double dwell_after_click_ms;
    bool has_idle_hide_ms;
    double idle_hide_ms;
};

struct MacBackgroundFeedbackStyleOptions {
    bool reset;
    bool has_gradient_colors;
    std::vector<std::string> gradient_colors;
    bool has_bloom_color;
    std::string bloom_color;
    bool has_image_path;
    std::filesystem::path image_path;
};

struct MacBackgroundFeedbackPresetOptions {
    std::string name;
};

struct MacPermissionOptions {
    bool prompt;
    bool open_settings;
};

struct DaemonOptions {
    bool once;
};

struct MacroOptions {
    std::filesystem::path path;
};

struct TeachRecordOptions {
    std::filesystem::path output_directory;
    std::filesystem::path text_file;
    std::filesystem::path video_file;
    std::filesystem::path audio_file;
    std::filesystem::path transcript_file;
    std::filesystem::path state_file;
    std::filesystem::path stop_file;
    std::uint32_t duration_ms;
    std::uint32_t frame_interval_ms;
    std::uint32_t event_poll_ms;
    std::uint32_t stop_timeout_ms;
    std::uint32_t video_keyframe_interval_ms;
    std::uint32_t video_keyframe_max;
    bool worker;
    bool no_video_keyframes;
    std::string title;
    std::string text;
};

struct TeachAnnotateOptions {
    std::filesystem::path session_directory;
    std::filesystem::path text_file;
    int frame_index;
    int event_index;
    bool has_frame_index;
    bool has_event_index;
    std::string text;
};

struct TeachTranscribeOptions {
    std::filesystem::path audio_file;
    std::filesystem::path output_path;
    std::filesystem::path model_path;
    std::filesystem::path script_path;
    std::string model_id;
    std::string language;
    std::string device;
    std::string compute_type;
};

struct Dependencies {
    std::function<int(const WebUiLaunchOptions&, const std::filesystem::path&, Io)> launch_config_ui;
    std::function<int(const TargetListOptions&, Io)> list_targets;
    std::function<int(const TargetInspectOptions&, Io)> inspect_target;
    std::function<int(const ObserveUiOptions&, Io)> observe_ui;
    std::function<int(const ScreenshotDesktopOptions&, Io)> capture_desktop;
    std::function<int(const ScreenshotBurstOptions&, Io)> capture_burst;
    std::function<int(const ScreenshotWindowOptions&, Io)> capture_window;
    std::function<int(const ScreenshotBackgroundWindowOptions&, Io)> capture_background_window;
    std::function<int(const ScreenshotWindowBurstOptions&, Io)> capture_window_burst;
    std::function<int(const InputKeyOptions&, Io)> input_key;
    std::function<int(const InputComboOptions&, Io)> input_combo;
    std::function<int(const InputTextOptions&, Io)> input_text;
    std::function<int(const InputMouseOptions&, Io)> input_mouse;
    std::function<int(const InputDragOptions&, Io)> input_drag;
    std::function<int(const BackgroundTextOptions&, Io)> input_background_text;
    std::function<int(const BackgroundKeyOptions&, Io)> input_background_key;
    std::function<int(const BackgroundMouseOptions&, Io)> input_background_mouse;
    std::function<int(const BackgroundDragOptions&, Io)> input_background_drag;
    std::function<int(const BackgroundDesktopStartOptions&, Io)> background_desktop_start;
    std::function<int(const BackgroundDesktopStopOptions&, Io)> background_desktop_stop;
    std::function<int(const BackgroundDesktopLaunchOptions&, Io)> background_desktop_launch;
    std::function<int(const BackgroundDesktopScreenshotOptions&, Io)> background_desktop_screenshot;
    std::function<int(const BackgroundDesktopTextOptions&, Io)> background_desktop_text;
    std::function<int(const BackgroundDesktopKeyOptions&, Io)> background_desktop_key;
    std::function<int(const BackgroundDesktopMouseOptions&, Io)> background_desktop_mouse;
    std::function<int(const MacBackgroundStatusOptions&, Io)> mac_background_status;
    std::function<int(const MacBackgroundLaunchOptions&, Io)> mac_background_launch;
    std::function<int(const MacBackgroundWindowsOptions&, Io)> mac_background_windows;
    std::function<int(const MacBackgroundStateOptions&, Io)> mac_background_state;
    std::function<int(const MacBackgroundScreenshotOptions&, Io)> mac_background_screenshot;
    std::function<int(const MacBackgroundClickOptions&, Io)> mac_background_click;
    std::function<int(const MacBackgroundTextOptions&, Io)> mac_background_text;
    std::function<int(const MacBackgroundKeyOptions&, Io)> mac_background_key;
    std::function<int(const MacBackgroundHotkeyOptions&, Io)> mac_background_hotkey;
    std::function<int(const MacBackgroundDragOptions&, Io)> mac_background_drag;
    std::function<int(const MacBackgroundDrawOptions&, Io)> mac_background_draw;
    std::function<int(const MacBackgroundFeedbackStatusOptions&, Io)> mac_background_feedback_status;
    std::function<int(const MacBackgroundFeedbackEnableOptions&, Io)> mac_background_feedback_enable;
    std::function<int(const MacBackgroundFeedbackMotionOptions&, Io)> mac_background_feedback_motion;
    std::function<int(const MacBackgroundFeedbackStyleOptions&, Io)> mac_background_feedback_style;
    std::function<int(const MacBackgroundFeedbackPresetOptions&, Io)> mac_background_feedback_preset;
    std::function<int(const MacPermissionOptions&, Io)> macos_screen_recording_permission;
    std::function<int(const MacPermissionOptions&, Io)> macos_accessibility_permission;
    std::function<int(const DaemonOptions&, const std::filesystem::path&, Io)> run_daemon;
    std::function<int(const TeachRecordOptions&, Io)> teach_record;
    std::function<int(const TeachAnnotateOptions&, Io)> teach_annotate;
    std::function<int(const TeachTranscribeOptions&, Io)> teach_transcribe;
};

std::filesystem::path resolve_config_path(std::filesystem::path override_path);
Dependencies default_dependencies();

// Runs the CLI with argv-tail arguments only. The executable name / argv[0] is
// excluded; main.cpp strips argv[0] before calling this function.
int run(
    const std::vector<std::string>& args,
    std::filesystem::path config_path,
    Io io,
    Dependencies dependencies = default_dependencies());

}
