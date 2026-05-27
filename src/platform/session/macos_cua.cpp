#include "platform/session/macos_cua.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
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
    return run_cua_tool("screenshot", arguments, options.output_path);
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
    add_window_id(arguments, options.window_id, options.has_window_id);
    add_modifiers(arguments, options.modifiers);
    return run_cua_tool("drag", arguments);
#else
    (void)options;
    return unsupported();
#endif
}

}
