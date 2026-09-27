#include "platform/session/macos_cua.hpp"
#include "platform/session/cua_process.hpp"
#include "platform/session/cua_runtime.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <io.h>
#else
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

bool executable_file(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return false;
#ifdef _WIN32
    return !path.empty() && _waccess(path.c_str(), 0) == 0;
#else
    return !path.empty() && access(path.c_str(), X_OK) == 0;
#endif
}

char path_separator() {
#ifdef _WIN32
    return ';';
#else
    return ':';
#endif
}

std::filesystem::path find_on_path(const std::string& name) {
    const auto raw_path = detail::environment_text("PATH");
    if (raw_path.empty()) {
        return {};
    }

    std::istringstream stream{raw_path};
    std::string segment;
    while (std::getline(stream, segment, path_separator())) {
        if (segment.empty()) {
            segment = ".";
        }
        const auto candidate = std::filesystem::u8path(segment) / name;
        if (executable_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::filesystem::path cua_driver_binary() {
    if (const auto override_path = detail::environment_text("KISEKI_CUA_DRIVER"); !override_path.empty()) {
        return executable_file(std::filesystem::u8path(override_path)) ? std::filesystem::u8path(override_path) : std::filesystem::path{};
    }
#ifdef _WIN32
    if (const auto path = find_on_path("cua-driver.exe"); !path.empty()) {
        return path;
    }
    if (const auto path = find_on_path("cua-driver"); !path.empty()) {
        return path;
    }
    if (const auto local_app_data = detail::environment_text("LOCALAPPDATA"); !local_app_data.empty()) {
        const auto visible_binary =
            std::filesystem::u8path(local_app_data) / "Programs" / "Cua" / "cua-driver" / "bin" / "cua-driver.exe";
        if (executable_file(visible_binary)) {
            return visible_binary;
        }
    }
#else
    if (const auto path = find_on_path("cua-driver"); !path.empty()) {
        return path;
    }
#ifdef __APPLE__
    const auto app_binary = std::filesystem::path{"/Applications/CuaDriver.app/Contents/MacOS/cua-driver"};
    if (executable_file(app_binary)) {
        return app_binary;
    }
#else
    if (const auto home = detail::environment_text("HOME"); !home.empty()) {
        const auto local_binary = std::filesystem::u8path(home) / ".local" / "bin" / "cua-driver";
        if (executable_file(local_binary)) {
            return local_binary;
        }
    }
#endif
#endif
    return {};
}

struct CaptureOutput {
    std::filesystem::path target, directory, staged;
    explicit CaptureOutput(const std::filesystem::path& output) : target(std::filesystem::weakly_canonical(std::filesystem::absolute(output))) {
        std::filesystem::create_directories(target.parent_path());
        static std::atomic<unsigned long long> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        do {
            directory = target.parent_path() / (".kiseki-cua-capture-" + std::to_string(stamp) + "-" + std::to_string(counter++));
        } while (!std::filesystem::create_directory(directory));
        staged = directory / target.filename();
    }
    ~CaptureOutput() { std::error_code error; std::filesystem::remove_all(directory, error); }
    void validate() const {
        if (!std::filesystem::is_regular_file(staged) || std::filesystem::file_size(staged) == 0)
            throw std::runtime_error("CUA returned without writing a new screenshot: " + detail::path_text(target));
    }
    void finish() {
        std::filesystem::rename(staged, target);
    }
    static std::string normalized_path(const std::filesystem::path& path) {
        std::error_code error;
        auto resolved = std::filesystem::weakly_canonical(path, error);
        const auto utf8 = (error ? path.lexically_normal() : resolved).generic_u8string();
        std::string text(utf8.begin(), utf8.end());
#ifdef _WIN32
        if (text.starts_with("//?/")) text.erase(0, 4);
        for (auto& character : text) if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
#endif
        return text;
    }
    void fix_paths(nlohmann::json& value) const {
        if (value.is_string() && value.get_ref<const std::string&>().ends_with(detail::path_text(staged.filename())) && normalized_path(std::filesystem::u8path(value.get<std::string>())) == normalized_path(staged)) value = detail::path_text(target);
        else if (value.is_structured()) for (auto& child : value) fix_paths(child);
    }
};

bool refused_result(const nlohmann::json& value) {
    if (!value.is_object()) return false;
    for (const auto* field : {"status", "effect"}) {
        if (value.contains(field) && value[field].is_string()) {
            const auto result = value[field].get<std::string>();
            if (result == "refused" || result == "failed" || result == "error") return true;
        }
    }
    if ((value.contains("isError") && value["isError"] == true) ||
        (value.contains("is_error") && value["is_error"] == true) ||
        (value.contains("ok") && value["ok"] == false) ||
        (value.contains("refusal") && !value["refusal"].is_null())) return true;
    if (value.contains("code") && value["code"].is_string() && value.contains("detail") && !value.contains("effect")) return true;
    // Driver transports may wrap the same ActionResult in structuredContent.
    for (const auto* field : {"structuredContent", "structured_content"})
        if (value.contains(field) && refused_result(value[field])) return true;
    return false;
}

OperationResult run_cua_tool(
    const std::string& tool,
    const nlohmann::json& arguments,
    const std::filesystem::path& screenshot_output = {}) {
    const auto binary = cua_driver_binary();
    if (binary.empty()) {
        return fail("cua-driver was not found. Run kiseki background cua setup; an explicit KISEKI_CUA_DRIVER must point to an executable");
    }
    try {
        auto tool_arguments = arguments;
        std::vector<std::unique_ptr<CaptureOutput>> captures;
        const bool argument_capture = arguments.contains("screenshot_out_file") && arguments["screenshot_out_file"].is_string();
        if (argument_capture) {
            captures.push_back(std::make_unique<CaptureOutput>(std::filesystem::u8path(arguments["screenshot_out_file"].get<std::string>())));
            tool_arguments["screenshot_out_file"] = detail::path_text(captures.back()->staged);
        }
        std::vector<std::string> command{detail::path_text(binary), "call", tool};
        if (!screenshot_output.empty()) {
            if (captures.empty() || CaptureOutput::normalized_path(screenshot_output) != CaptureOutput::normalized_path(captures.front()->target))
                captures.push_back(std::make_unique<CaptureOutput>(screenshot_output));
            command.insert(command.end(), {"--screenshot-out-file", detail::path_text(captures.back()->staged)});
        }
        if (const auto socket = detail::environment_text("KISEKI_CUA_SOCKET"); !socket.empty()) command.insert(command.end(), {"--socket", socket});
        auto result = detail::run_process(command, tool_arguments.dump());
        if (result.code != 0) return fail(result.error + result.output, result.code);
        const auto payload = nlohmann::json::parse(result.output, nullptr, false);
        if (refused_result(payload)) return fail(result.error + result.output);
        if (!captures.empty()) {
            // Validate every requested output before replacing any prior file.
            for (const auto& capture : captures) capture->validate();
            for (const auto& capture : captures) capture->finish();
            auto parsed = nlohmann::json::parse(result.output, nullptr, false);
            if (!parsed.is_discarded()) {
                for (const auto& capture : captures) capture->fix_paths(parsed);
                result.output = parsed.dump(2);
            }
        }
        return OperationResult{.ok = true, .code = 0, .message = result.output, .error = result.error};
    } catch (const std::exception& error) { return fail(error.what()); }
}

OperationResult run_cua_with_options(const std::string& tool, nlohmann::json arguments, const std::string& provider_json) {
    try {
        const auto extra = nlohmann::json::parse(provider_json);
        if (!extra.is_object()) return fail("--provider-json must be a JSON object");
        for (const auto& [key, value] : extra.items()) {
            if (arguments.contains(key)) return fail("--provider-json conflicts with explicit field: " + key);
            arguments[key] = value;
        }
        return run_cua_tool(tool, arguments);
    } catch (const std::exception& error) { return fail(error.what()); }
}

OperationResult begin_workflow_and_call(const std::string& tool, const nlohmann::json& arguments) {
    const auto prepared = cua_setup();
    if (!prepared.ok) return prepared;
    auto called = run_cua_tool(tool, arguments);
    auto report = nlohmann::json::parse(prepared.message, nullptr, false);
    if (report.is_object() && (report.value("installed", false) || report.value("updated", false) ||
        (report.contains("warnings") && !report["warnings"].empty()))) {
        called.error += "CUA setup: " + report.dump() + "\n";
    }
    return called;
}

void add_window_id(nlohmann::json& json, std::uint64_t window_id, bool has_window_id) {
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
    const auto session = detail::environment_text("KISEKI_CUA_SESSION");
    if (!session.empty()) {
        return session;
    }
    return "kiseki";
}

void add_session_if_configured(nlohmann::json& json) {
    const auto session = configured_cua_session_id();
    if (!session.empty()) {
        json["session"] = session;
    }
}

void add_cursor_id(nlohmann::json& json) {
    const auto session = configured_cua_session_id();
    json["session"] = session.empty() ? "default" : session;
}

int resolve_pid_for_window_id(std::uint64_t window_id, std::string& error) {
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

}

std::filesystem::path cua_driver_path() { return cua_driver_binary(); }

OperationResult cua_call(const std::string& tool, const nlohmann::json& arguments, const std::filesystem::path& output) {
    if (!arguments.is_object()) return fail("CUA arguments must be a JSON object");
    return run_cua_tool(tool, arguments, output);
}

OperationResult cua_session_call(const std::string& tool, nlohmann::json arguments, const std::filesystem::path& output) {
    if (!arguments.is_object()) return fail("CUA arguments must be a JSON object");
    if (!arguments.contains("session")) add_session_if_configured(arguments);
    return run_cua_tool(tool, arguments, output);
}

bool cua_background_available() {
    return !cua_driver_binary().empty();
}

bool macos_cua_background_available() {
#ifdef __APPLE__
    return cua_background_available();
#else
    return false;
#endif
}

OperationResult macos_cua_status(bool prompt) {
    nlohmann::json arguments = nlohmann::json::object();
#ifdef __APPLE__
    arguments["prompt"] = prompt;
#else
    if (prompt) return fail("--prompt requests macOS permissions; use background cua status for native Windows/Linux readiness");
#endif
    return run_cua_tool("check_permissions", arguments);
}

OperationResult macos_cua_launch(const MacCuaLaunchOptions& options) {
    if (options.bundle_id.empty() && options.name.empty() && options.launch_path.empty()) {
        return fail("background cua launch requires --bundle-id, --name, or --launch-path");
    }
    nlohmann::json arguments = nlohmann::json::object();
    if (!options.bundle_id.empty()) {
        arguments["bundle_id"] = options.bundle_id;
    }
    if (!options.name.empty()) {
        arguments["name"] = options.name;
    }
    if (!options.launch_path.empty()) arguments["launch_path"] = options.launch_path;
    if (!options.urls.empty()) {
        arguments["urls"] = options.urls;
    }
    if (options.creates_new_instance) {
        arguments["creates_new_application_instance"] = true;
    }
    if (!options.additional_arguments.empty()) {
        arguments["additional_arguments"] = options.additional_arguments;
    }
    return begin_workflow_and_call("launch_app", arguments);
}

OperationResult macos_cua_list_windows(const MacCuaWindowListOptions& options) {
    nlohmann::json arguments = nlohmann::json::object();
    if (options.has_pid) {
        arguments["pid"] = options.pid;
    }
    if (options.on_screen_only) {
        arguments["on_screen_only"] = true;
    }
    return begin_workflow_and_call("list_windows", arguments);
}

OperationResult macos_cua_window_state(const MacCuaWindowStateOptions& options) {
    if (options.pid <= 0 || options.window_id == 0) {
        return fail("background cua state requires positive --pid and --window-id");
    }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"window_id", options.window_id},
    };
    add_session_if_configured(arguments);
    if (!options.query.empty()) {
        arguments["query"] = options.query;
    }
    if (!options.output_path.empty()) {
        arguments["screenshot_out_file"] = detail::path_text(std::filesystem::absolute(options.output_path));
    }
    return run_cua_with_options("get_window_state", arguments, options.provider_json);
}

OperationResult macos_cua_screenshot(const MacCuaScreenshotOptions& options) {
    if (options.window_id == 0) {
        return fail("background cua screenshot requires --window-id");
    }
    if (options.output_path.empty()) {
        return fail("background cua screenshot requires --output");
    }
    const auto format = options.format.empty() ? "png" : options.format;
    if (format != "png") {
        // Older providers offer JPEG through screenshot. Preserve the request
        // and its error; never label a PNG as JPEG on the modern state route.
        return run_cua_tool("screenshot", {{"window_id", options.window_id}, {"format", format}, {"quality", options.quality}}, options.output_path);
    }
    std::string error;
    const int pid = resolve_pid_for_window_id(options.window_id, error);
    if (pid <= 0) return fail(error);
    nlohmann::json arguments{{"pid", pid}, {"window_id", options.window_id},
        {"include_accessibility_tree", false}, {"include_screenshot", true},
        {"screenshot_out_file", detail::path_text(std::filesystem::absolute(options.output_path))}};
    add_session_if_configured(arguments);
    const auto captured = run_cua_tool("get_window_state", arguments);
    if (!captured.ok) return captured;
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(options.output_path, file_error) ||
        std::filesystem::file_size(options.output_path, file_error) == 0) {
        return fail("CUA returned without creating the requested screenshot: " + detail::path_text(options.output_path));
    }
    return captured;
}

OperationResult macos_cua_click(const MacCuaClickOptions& options) {
    if (options.pid <= 0) {
        return fail("background cua click requires positive --pid");
    }
    if ((options.has_element_index || !options.element_token.empty()) == options.has_xy) {
        return fail("background cua click requires either --element-index or both --x and --y");
    }
    if (options.has_element_index && options.element_token.empty() && !options.has_window_id) {
        return fail("background cua click with --element-index requires --window-id");
    }

    nlohmann::json arguments = {{"pid", options.pid}};
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    add_element_index(arguments, options.element_index, options.has_element_index);
    if (!options.snapshot_id.empty()) arguments["snapshot_id"] = options.snapshot_id;
    if (!options.element_token.empty()) arguments["element_token"] = options.element_token;
    if (options.has_xy) {
        arguments["x"] = options.x;
        arguments["y"] = options.y;
    }
    add_modifiers(arguments, options.modifiers);

    const std::string button = options.button.empty() ? "left" : options.button;
    if (button == "left") {
        return run_cua_with_options("click", arguments, options.provider_json);
    }
    if (button == "double") {
        return run_cua_with_options("double_click", arguments, options.provider_json);
    }
    if (button == "right") {
        return run_cua_with_options("right_click", arguments, options.provider_json);
    }
    return fail("background cua click --button must be left, right, or double");
}

OperationResult macos_cua_type_text(const MacCuaTextOptions& options) {
    if (options.pid <= 0 || options.text.empty()) {
        return fail("background cua text requires positive --pid and non-empty --text or --file");
    }
    if (options.has_element_index && options.element_token.empty() && !options.has_window_id) {
        return fail("background cua text with --element-index requires --window-id");
    }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"text", options.text},
        {"delay_ms", options.delay_ms},
    };
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    add_element_index(arguments, options.element_index, options.has_element_index);
    if (!options.snapshot_id.empty()) arguments["snapshot_id"] = options.snapshot_id;
    if (!options.element_token.empty()) arguments["element_token"] = options.element_token;
    return run_cua_with_options("type_text", arguments, options.provider_json);
}

OperationResult macos_cua_press_key(const MacCuaKeyOptions& options) {
    if (options.pid <= 0 || options.key.empty()) {
        return fail("background cua key requires positive --pid and --key");
    }
    if (options.has_element_index && options.element_token.empty() && !options.has_window_id) {
        return fail("background cua key with --element-index requires --window-id");
    }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"key", options.key},
    };
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    add_element_index(arguments, options.element_index, options.has_element_index);
    if (!options.snapshot_id.empty()) arguments["snapshot_id"] = options.snapshot_id;
    if (!options.element_token.empty()) arguments["element_token"] = options.element_token;
    if (!options.modifiers.empty()) {
        arguments["modifiers"] = options.modifiers;
    }
    return run_cua_with_options("press_key", arguments, options.provider_json);
}

OperationResult macos_cua_hotkey(const MacCuaHotkeyOptions& options) {
    if (options.pid <= 0 || options.keys.size() < 2) {
        return fail("background cua hotkey requires positive --pid and at least two --keys entries");
    }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"keys", options.keys},
    };
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    return run_cua_with_options("hotkey", arguments, options.provider_json);
}

OperationResult macos_cua_drag(const MacCuaDragOptions& options) {
    if (options.pid <= 0) {
        return fail("background cua drag requires positive --pid");
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
    return run_cua_with_options("drag", arguments, options.provider_json);
}

OperationResult macos_cua_draw(const MacCuaDrawOptions& options) {
    if (options.pid <= 0) {
        return fail("background cua draw requires positive --pid");
    }
    if (options.window_id == 0) {
        return fail("background cua draw requires --window-id");
    }
    if (options.points.size() < 2) {
        return fail("background cua draw requires at least two points");
    }
    if (options.duration_ms < 0) {
        return fail("background cua draw --duration-ms must be non-negative");
    }
    if (options.steps < 1) {
        return fail("background cua draw --steps must be at least 1");
    }
    if (options.stroke_gap_ms < 0) {
        return fail("background cua draw --stroke-gap-ms must be non-negative");
    }
    if (options.max_segments < 1) {
        return fail("background cua draw --max-segments must be at least 1");
    }
    const auto segment_count = options.points.size() - 1;
    if (segment_count > static_cast<std::size_t>(options.max_segments)) {
        return fail(
            "background cua draw path has " + std::to_string(segment_count) +
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

        const auto result = run_cua_with_options("drag", arguments, options.provider_json);
        if (!result.ok) {
            return fail(
                "background cua draw segment " + std::to_string(index) + " failed: " + result.error,
                result.code);
        }
        if (options.stroke_gap_ms > 0 && index + 1 < options.points.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.stroke_gap_ms));
        }
    }

    return ok(
        "background cua draw sent " + std::to_string(options.points.size() - 1) +
        " drag segment(s)");
}

OperationResult macos_cua_feedback_state() {
    nlohmann::json arguments = nlohmann::json::object();
    add_cursor_id(arguments);
    return run_cua_tool("get_agent_cursor_state", arguments);
}

OperationResult macos_cua_feedback_enable(const MacCuaFeedbackEnableOptions& options) {
    nlohmann::json arguments = {{"enabled", options.enabled}};
    add_cursor_id(arguments);
    return run_cua_tool("set_agent_cursor_enabled", arguments);
}

OperationResult macos_cua_feedback_motion(const MacCuaFeedbackMotionOptions& options) {
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
        return fail("background cua feedback motion requires at least one motion option");
    }
    return run_cua_tool("set_agent_cursor_motion", arguments);
}

OperationResult macos_cua_feedback_style(const MacCuaFeedbackStyleOptions& options) {
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
        return fail("background cua feedback style requires --reset or at least one style option");
    }
    return run_cua_tool("set_agent_cursor_style", arguments);
}

OperationResult macos_cua_feedback_preset(const MacCuaFeedbackPresetOptions& options) {
    const std::string name = options.name.empty() ? "natural" : options.name;
    if (name == "quiet") {
        nlohmann::json enabled_arguments = {{"enabled", false}};
        add_cursor_id(enabled_arguments);
        const auto enabled = run_cua_tool("set_agent_cursor_enabled", enabled_arguments);
        if (!enabled.ok) {
            return enabled;
        }
        return ok("background cua feedback preset quiet applied");
    }

    const auto style_schema = cua_driver_command({"describe", "set_agent_cursor_style"});
    if (!style_schema.ok) {
        return fail("This provider does not expose the legacy set_agent_cursor_style tool required by this preset. "
                    "No preset changes were sent. Use feedback motion and an installed theme through "
                    "background cua call set_agent_cursor_theme; inspect its schema with background cua describe. " + style_schema.error);
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
        return fail("background cua feedback preset must be natural, fast, recording, or quiet");
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
    return ok("background cua feedback preset " + name + " applied");
}

}
