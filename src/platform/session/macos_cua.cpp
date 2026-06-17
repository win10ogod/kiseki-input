#include "platform/session/macos_cua.hpp"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef __APPLE__
#include <array>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace kiseki::platform::session {

namespace {

OperationResult ok(std::string message) {
    return OperationResult{
        .ok = true,
        .code = 0,
        .message = std::move(message),
        .error = "",
    };
}

OperationResult fail(std::string error, int code = 2) {
    return OperationResult{
        .ok = false,
        .code = code,
        .message = "",
        .error = std::move(error),
    };
}

#ifdef __APPLE__

bool executable_file(const std::filesystem::path& path) {
    return !path.empty() && access(path.c_str(), X_OK) == 0;
}

std::filesystem::path find_on_path(const std::string& name) {
    const char* raw_path = std::getenv("PATH");
    if (raw_path == nullptr) {
        return {};
    }

    std::istringstream stream{raw_path};
    std::string segment;
    while (std::getline(stream, segment, ':')) {
        if (segment.empty()) {
            segment = ".";
        }
        const auto candidate = std::filesystem::path{segment} / name;
        if (executable_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::filesystem::path cua_driver_binary() {
    if (const char* override_path = std::getenv("KISEKI_CUA_DRIVER");
        override_path != nullptr && executable_file(override_path)) {
        return override_path;
    }
    if (const auto path = find_on_path("cua-driver"); !path.empty()) {
        return path;
    }
    const auto app_binary = std::filesystem::path{"/Applications/CuaDriver.app/Contents/MacOS/cua-driver"};
    if (executable_file(app_binary)) {
        return app_binary;
    }
    return {};
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (const char c : value) {
        if (c == '\'') {
            quoted += "'\"'\"'";
        } else {
            quoted.push_back(c);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

struct CommandResult {
    int code = 2;
    std::string output;
};

CommandResult run_shell_command(const std::string& command) {
    std::array<char, 4096> buffer{};
    std::string output;

    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        return CommandResult{.code = 2, .output = "failed to launch cua-driver"};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (status == -1) {
        return CommandResult{.code = 2, .output = output.empty() ? "failed to read cua-driver exit status" : output};
    }
    if (WIFEXITED(status)) {
        return CommandResult{.code = WEXITSTATUS(status), .output = output};
    }
    return CommandResult{.code = 2, .output = output.empty() ? "cua-driver did not exit normally" : output};
}

OperationResult run_cua_tool(
    const std::string& tool,
    const nlohmann::json& arguments,
    const std::filesystem::path& screenshot_output = {}) {
    const auto binary = cua_driver_binary();
    if (binary.empty()) {
        return fail("cua-driver was not found. Install Cua Driver or set KISEKI_CUA_DRIVER to the cua-driver binary");
    }

    std::string command = shell_quote(binary.string()) + " call " + shell_quote(tool) + " " + shell_quote(arguments.dump());
    if (!screenshot_output.empty()) {
        command += " --screenshot-out-file " + shell_quote(std::filesystem::absolute(screenshot_output).string());
    }

    const auto result = run_shell_command(command);
    if (result.code == 0) {
        return ok(result.output.empty() ? ("cua-driver " + tool + " completed") : result.output);
    }
    return fail(result.output.empty() ? ("cua-driver " + tool + " failed") : result.output, result.code);
}

void add_window_id(nlohmann::json& json, unsigned int window_id, bool has_window_id) {
    if (has_window_id) {
        json["window_id"] = window_id;
    }
}

void add_element_index(nlohmann::json& json, int element_index, bool has_element_index) {
    if (has_element_index) {
        json["element_index"] = element_index;
    }
}

void add_modifiers(nlohmann::json& json, const std::vector<std::string>& modifiers) {
    if (!modifiers.empty()) {
        json["modifier"] = modifiers;
    }
}

std::string configured_cua_session_id() {
    const char* session = std::getenv("KISEKI_CUA_SESSION");
    if (session != nullptr && std::string{session}.size() > 0) {
        return session;
    }
    return {};
}

void add_session_if_configured(nlohmann::json& json) {
    const auto session = configured_cua_session_id();
    if (!session.empty()) {
        json["session"] = session;
    }
}

void add_cursor_id(nlohmann::json& json) {
    const auto session = configured_cua_session_id();
    json["cursor_id"] = session.empty() ? "default" : session;
}

int resolve_pid_for_window_id(unsigned int window_id, std::string& error) {
    const auto windows_result = run_cua_tool("list_windows", nlohmann::json::object());
    if (!windows_result.ok) {
        error = windows_result.error;
        return 0;
    }

    try {
        const auto parsed = nlohmann::json::parse(windows_result.message);
        const nlohmann::json* windows = nullptr;
        if (parsed.is_array()) {
            windows = &parsed;
        } else if (parsed.contains("windows") && parsed.at("windows").is_array()) {
            windows = &parsed.at("windows");
        }

        if (windows == nullptr) {
            error = "Cua Driver list_windows output did not contain a windows array";
            return 0;
        }

        for (const auto& window : *windows) {
            if (window.value("window_id", 0ULL) == static_cast<unsigned long long>(window_id)) {
                return window.value("pid", 0);
            }
        }
        error = "Cua Driver list_windows did not contain window_id " + std::to_string(window_id);
        return 0;
    } catch (const std::exception& parse_error) {
        error = std::string{"failed to parse Cua Driver list_windows output: "} + parse_error.what();
        return 0;
    }
}

#endif

OperationResult unsupported() {
#ifdef __APPLE__
    return fail("cua-driver was not found. Install Cua Driver or set KISEKI_CUA_DRIVER to the cua-driver binary");
#else
    return fail("macOS CUA background operation requires macOS with cua-driver installed");
#endif
}

}

bool macos_cua_background_available() {
#ifdef __APPLE__
    return !cua_driver_binary().empty();
#else
    return false;
#endif
}

OperationResult macos_cua_status(bool prompt) {
#ifdef __APPLE__
    return run_cua_tool("check_permissions", nlohmann::json{{"prompt", prompt}});
#else
    (void)prompt;
    return unsupported();
#endif
}

OperationResult macos_cua_launch(const MacCuaLaunchOptions& options) {
#ifdef __APPLE__
    if (options.bundle_id.empty() && options.name.empty()) {
        return fail("mac-background launch requires --bundle-id or --name");
    }
    nlohmann::json arguments = nlohmann::json::object();
    if (!options.bundle_id.empty()) {
        arguments["bundle_id"] = options.bundle_id;
    }
    if (!options.name.empty()) {
        arguments["name"] = options.name;
    }
    if (!options.urls.empty()) {
        arguments["urls"] = options.urls;
    }
    if (options.creates_new_instance) {
        arguments["creates_new_application_instance"] = true;
    }
    if (!options.additional_arguments.empty()) {
        arguments["additional_arguments"] = options.additional_arguments;
    }
    return run_cua_tool("launch_app", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_list_windows(const MacCuaWindowListOptions& options) {
#ifdef __APPLE__
    nlohmann::json arguments = nlohmann::json::object();
    if (options.has_pid) {
        arguments["pid"] = options.pid;
    }
    if (options.on_screen_only) {
        arguments["on_screen_only"] = true;
    }
    return run_cua_tool("list_windows", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_window_state(const MacCuaWindowStateOptions& options) {
#ifdef __APPLE__
    if (options.pid <= 0 || options.window_id == 0) {
        return fail("mac-background state requires positive --pid and --window-id");
    }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"window_id", options.window_id},
    };
    if (!options.query.empty()) {
        arguments["query"] = options.query;
    }
    if (!options.output_path.empty()) {
        arguments["screenshot_out_file"] = std::filesystem::absolute(options.output_path).string();
    }
    return run_cua_tool("get_window_state", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_screenshot(const MacCuaScreenshotOptions& options) {
#ifdef __APPLE__
    if (options.window_id == 0) {
        return fail("mac-background screenshot requires --window-id");
    }
    if (options.output_path.empty()) {
        return fail("mac-background screenshot requires --output");
    }
    nlohmann::json arguments = {
        {"window_id", options.window_id},
        {"format", options.format.empty() ? "png" : options.format},
        {"quality", options.quality},
    };
    const auto direct = run_cua_tool("screenshot", arguments, options.output_path);
    if (direct.ok) {
        return direct;
    }

    std::string resolve_error;
    const int pid = resolve_pid_for_window_id(options.window_id, resolve_error);
    if (pid <= 0) {
        return fail(
            "mac-background screenshot failed through Cua Driver screenshot tool and fallback pid resolution failed. "
            "screenshot error: " +
                direct.error + "; fallback error: " + resolve_error,
            direct.code);
    }

    nlohmann::json fallback_arguments = {
        {"pid", pid},
        {"window_id", options.window_id},
        {"screenshot_out_file", std::filesystem::absolute(options.output_path).string()},
    };
    const auto fallback = run_cua_tool("get_window_state", fallback_arguments);
    if (!fallback.ok) {
        return fail(
            "mac-background screenshot failed through Cua Driver screenshot tool and get_window_state fallback. "
            "screenshot error: " +
                direct.error + "; fallback error: " + fallback.error,
            fallback.code);
    }
    std::error_code file_error;
    if (!std::filesystem::exists(options.output_path, file_error) ||
        std::filesystem::file_size(options.output_path, file_error) == 0) {
        return fail(
            "mac-background screenshot fallback completed but did not create a non-empty output file: " +
            std::filesystem::absolute(options.output_path).string());
    }
    return ok(
        "mac-background screenshot captured through get_window_state fallback: " +
        std::filesystem::absolute(options.output_path).string());
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_click(const MacCuaClickOptions& options) {
#ifdef __APPLE__
    if (options.pid <= 0) {
        return fail("mac-background click requires positive --pid");
    }
    if (options.has_element_index == options.has_xy) {
        return fail("mac-background click requires either --element-index or both --x and --y");
    }
    if (options.has_element_index && !options.has_window_id) {
        return fail("mac-background click with --element-index requires --window-id");
    }

    nlohmann::json arguments = {{"pid", options.pid}};
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    add_element_index(arguments, options.element_index, options.has_element_index);
    if (options.has_xy) {
        arguments["x"] = options.x;
        arguments["y"] = options.y;
    }
    add_modifiers(arguments, options.modifiers);

    const std::string button = options.button.empty() ? "left" : options.button;
    if (button == "left") {
        return run_cua_tool("click", arguments);
    }
    if (button == "double") {
        return run_cua_tool("double_click", arguments);
    }
    if (button == "right") {
        return run_cua_tool("right_click", arguments);
    }
    return fail("mac-background click --button must be left, right, or double");
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_type_text(const MacCuaTextOptions& options) {
#ifdef __APPLE__
    if (options.pid <= 0 || options.text.empty()) {
        return fail("mac-background text requires positive --pid and non-empty --text or --file");
    }
    if (options.has_element_index && !options.has_window_id) {
        return fail("mac-background text with --element-index requires --window-id");
    }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"text", options.text},
        {"delay_ms", options.delay_ms},
    };
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    add_element_index(arguments, options.element_index, options.has_element_index);
    return run_cua_tool("type_text", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_press_key(const MacCuaKeyOptions& options) {
#ifdef __APPLE__
    if (options.pid <= 0 || options.key.empty()) {
        return fail("mac-background key requires positive --pid and --key");
    }
    if (options.has_element_index && !options.has_window_id) {
        return fail("mac-background key with --element-index requires --window-id");
    }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"key", options.key},
    };
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    add_element_index(arguments, options.element_index, options.has_element_index);
    if (!options.modifiers.empty()) {
        arguments["modifiers"] = options.modifiers;
    }
    return run_cua_tool("press_key", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_hotkey(const MacCuaHotkeyOptions& options) {
#ifdef __APPLE__
    if (options.pid <= 0 || options.keys.size() < 2) {
        return fail("mac-background hotkey requires positive --pid and at least two --keys entries");
    }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"keys", options.keys},
    };
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    return run_cua_tool("hotkey", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_drag(const MacCuaDragOptions& options) {
#ifdef __APPLE__
    if (options.pid <= 0) {
        return fail("mac-background drag requires positive --pid");
    }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"from_x", options.from_x},
        {"from_y", options.from_y},
        {"to_x", options.to_x},
        {"to_y", options.to_y},
        {"duration_ms", options.duration_ms},
        {"steps", options.steps},
        {"button", options.button.empty() ? "left" : options.button},
    };
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    add_modifiers(arguments, options.modifiers);
    return run_cua_tool("drag", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_draw(const MacCuaDrawOptions& options) {
#ifdef __APPLE__
    if (options.pid <= 0) {
        return fail("mac-background draw requires positive --pid");
    }
    if (options.window_id == 0) {
        return fail("mac-background draw requires --window-id");
    }
    if (options.points.size() < 2) {
        return fail("mac-background draw requires at least two points");
    }
    if (options.duration_ms < 0) {
        return fail("mac-background draw --duration-ms must be non-negative");
    }
    if (options.steps < 1) {
        return fail("mac-background draw --steps must be at least 1");
    }
    if (options.stroke_gap_ms < 0) {
        return fail("mac-background draw --stroke-gap-ms must be non-negative");
    }
    if (options.max_segments < 1) {
        return fail("mac-background draw --max-segments must be at least 1");
    }
    const auto segment_count = options.points.size() - 1;
    if (segment_count > static_cast<std::size_t>(options.max_segments)) {
        return fail(
            "mac-background draw path has " + std::to_string(segment_count) +
            " segments; reduce the point file, raise --max-segments intentionally, or use foreground input drag for dense drawing");
    }

    for (std::size_t index = 1; index < options.points.size(); ++index) {
        nlohmann::json arguments = {
            {"pid", options.pid},
            {"window_id", options.window_id},
            {"from_x", options.points[index - 1].x},
            {"from_y", options.points[index - 1].y},
            {"to_x", options.points[index].x},
            {"to_y", options.points[index].y},
            {"duration_ms", options.duration_ms},
            {"steps", options.steps},
            {"button", options.button.empty() ? "left" : options.button},
        };
        add_session_if_configured(arguments);
        add_modifiers(arguments, options.modifiers);

        const auto result = run_cua_tool("drag", arguments);
        if (!result.ok) {
            return fail(
                "mac-background draw segment " + std::to_string(index) + " failed: " + result.error,
                result.code);
        }
        if (options.stroke_gap_ms > 0 && index + 1 < options.points.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.stroke_gap_ms));
        }
    }

    return ok(
        "mac-background draw sent " + std::to_string(options.points.size() - 1) +
        " drag segment(s)");
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_feedback_state() {
#ifdef __APPLE__
    nlohmann::json arguments = nlohmann::json::object();
    add_cursor_id(arguments);
    return run_cua_tool("get_agent_cursor_state", arguments);
#else
    return unsupported();
#endif
}

OperationResult macos_cua_feedback_enable(const MacCuaFeedbackEnableOptions& options) {
#ifdef __APPLE__
    nlohmann::json arguments = {{"enabled", options.enabled}};
    add_cursor_id(arguments);
    return run_cua_tool("set_agent_cursor_enabled", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_feedback_motion(const MacCuaFeedbackMotionOptions& options) {
#ifdef __APPLE__
    nlohmann::json arguments = nlohmann::json::object();
    add_cursor_id(arguments);
    if (options.has_start_handle) {
        arguments["start_handle"] = options.start_handle;
    }
    if (options.has_end_handle) {
        arguments["end_handle"] = options.end_handle;
    }
    if (options.has_arc_size) {
        arguments["arc_size"] = options.arc_size;
    }
    if (options.has_arc_flow) {
        arguments["arc_flow"] = options.arc_flow;
    }
    if (options.has_spring) {
        arguments["spring"] = options.spring;
    }
    if (options.has_glide_duration_ms) {
        arguments["glide_duration_ms"] = options.glide_duration_ms;
    }
    if (options.has_dwell_after_click_ms) {
        arguments["dwell_after_click_ms"] = options.dwell_after_click_ms;
    }
    if (options.has_idle_hide_ms) {
        arguments["idle_hide_ms"] = options.idle_hide_ms;
    }
    if (arguments.size() == 1) {
        return fail("mac-background feedback motion requires at least one motion option");
    }
    return run_cua_tool("set_agent_cursor_motion", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_feedback_style(const MacCuaFeedbackStyleOptions& options) {
#ifdef __APPLE__
    nlohmann::json arguments = nlohmann::json::object();
    add_cursor_id(arguments);
    if (options.reset) {
        arguments["gradient_colors"] = nlohmann::json::array();
        arguments["bloom_color"] = "";
        arguments["image_path"] = "";
    }
    if (options.has_gradient_colors) {
        arguments["gradient_colors"] = options.gradient_colors;
    }
    if (options.has_bloom_color) {
        arguments["bloom_color"] = options.bloom_color;
    }
    if (options.has_image_path) {
        arguments["image_path"] = options.image_path.empty() ? "" : options.image_path.string();
    }
    if (arguments.size() == 1) {
        return fail("mac-background feedback style requires --reset or at least one style option");
    }
    return run_cua_tool("set_agent_cursor_style", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

OperationResult macos_cua_feedback_preset(const MacCuaFeedbackPresetOptions& options) {
#ifdef __APPLE__
    const std::string name = options.name.empty() ? "natural" : options.name;
    if (name == "quiet") {
        nlohmann::json enabled_arguments = {{"enabled", false}};
        add_cursor_id(enabled_arguments);
        const auto enabled = run_cua_tool("set_agent_cursor_enabled", enabled_arguments);
        if (!enabled.ok) {
            return enabled;
        }
        return ok("mac-background feedback preset quiet applied");
    }

    nlohmann::json motion = nlohmann::json::object();
    nlohmann::json style = nlohmann::json::object();
    if (name == "natural") {
        motion = {
            {"start_handle", 0.30},
            {"end_handle", 0.30},
            {"arc_size", 0.25},
            {"arc_flow", 0.0},
            {"spring", 0.72},
            {"glide_duration_ms", 550},
            {"dwell_after_click_ms", 160},
            {"idle_hide_ms", 3500},
        };
        style = {
            {"gradient_colors", nlohmann::json::array({"#00C2FF", "#22C55E"})},
            {"bloom_color", "#38BDF8"},
        };
    } else if (name == "fast") {
        motion = {
            {"start_handle", 0.22},
            {"end_handle", 0.24},
            {"arc_size", 0.16},
            {"arc_flow", 0.0},
            {"spring", 0.86},
            {"glide_duration_ms", 220},
            {"dwell_after_click_ms", 60},
            {"idle_hide_ms", 1800},
        };
        style = {
            {"gradient_colors", nlohmann::json::array({"#14B8A6", "#84CC16"})},
            {"bloom_color", "#14B8A6"},
        };
    } else if (name == "recording") {
        motion = {
            {"start_handle", 0.34},
            {"end_handle", 0.34},
            {"arc_size", 0.28},
            {"arc_flow", 0.08},
            {"spring", 0.72},
            {"glide_duration_ms", 850},
            {"dwell_after_click_ms", 320},
            {"idle_hide_ms", 6000},
        };
        style = {
            {"gradient_colors", nlohmann::json::array({"#FF6B6B", "#F59E0B"})},
            {"bloom_color", "#F59E0B"},
        };
    } else {
        return fail("mac-background feedback preset must be natural, fast, recording, or quiet");
    }

    nlohmann::json enabled_arguments = {{"enabled", true}};
    add_cursor_id(enabled_arguments);
    add_cursor_id(motion);
    add_cursor_id(style);
    const auto enabled = run_cua_tool("set_agent_cursor_enabled", enabled_arguments);
    if (!enabled.ok) {
        return enabled;
    }
    const auto motion_result = run_cua_tool("set_agent_cursor_motion", motion);
    if (!motion_result.ok) {
        return motion_result;
    }
    const auto style_result = run_cua_tool("set_agent_cursor_style", style);
    if (!style_result.ok) {
        return style_result;
    }
    return ok("mac-background feedback preset " + name + " applied");
#else
    (void)options;
    return unsupported();
#endif
}

}
