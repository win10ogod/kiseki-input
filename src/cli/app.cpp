#include "cli/app.hpp"

#include <CLI/CLI.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/capabilities/capabilities_model.hpp"
#include "core/config/config_model.hpp"
#include "core/config/config_store.hpp"
#include "core/version.hpp"
#include "platform/capture/screenshot.hpp"
#include "platform/input/input.hpp"
#include "platform/notification/notification.hpp"
#include "platform/runtime_capabilities.hpp"
#include "platform/session/background_desktop.hpp"
#include "platform/session/macos_cua.hpp"
#include "platform/target/target.hpp"
#include "webui/web_server.hpp"

namespace kiseki::cli {

namespace {

using kiseki::core::capabilities::foundation_capabilities;
using kiseki::core::capabilities::to_json;
using kiseki::core::config::ConfigStore;
using kiseki::core::config::current_environment;
using kiseki::core::config::current_platform;
using kiseki::core::config::default_config_path;

int show_config(const std::filesystem::path& config_path, Io io) {
    const ConfigStore store{config_path};
    const auto result = store.load_or_default();
    if (!result.ok) {
        io.err << result.error << '\n';
        return 2;
    }

    io.out << kiseki::core::config::to_json(result.config).dump(2) << '\n';
    return 0;
}

int validate_config_command(const std::filesystem::path& config_path, Io io) {
    const ConfigStore store{config_path};
    const auto result = store.load_or_default();
    if (!result.ok) {
        io.err << result.error << '\n';
        return 2;
    }

    io.out << "configuration is valid\n";
    return 0;
}

const char* availability(bool available) {
    return available ? "available" : "unavailable";
}

int print_operation_result(const kiseki::platform::OperationResult& result, Io io) {
    if (result.ok) {
        if (!result.message.empty()) {
            io.out << result.message << '\n';
        }
    } else {
        io.err << result.error << '\n';
    }
    return result.code;
}

int print_capture_result(const kiseki::platform::CaptureResult& result, Io io) {
    if (result.ok) {
        io.out << "captured " << result.output_path.string() << " " << result.width << "x" << result.height << '\n';
    } else {
        io.err << result.error << '\n';
    }
    return result.code;
}

nlohmann::json target_window_to_json(const kiseki::platform::target::TargetWindow& window) {
    return nlohmann::json{
        {"id", window.id},
        {"title", window.title},
        {"pid", window.pid},
        {"x", window.x},
        {"y", window.y},
        {"width", window.width},
        {"height", window.height},
    };
}

nlohmann::json target_child_window_to_json(const kiseki::platform::target::TargetChildWindow& window) {
    return nlohmann::json{
        {"id", window.id},
        {"parentId", window.parent_id},
        {"title", window.title},
        {"className", window.class_name},
        {"x", window.x},
        {"y", window.y},
        {"width", window.width},
        {"height", window.height},
    };
}

int print_target_list_result(const kiseki::platform::target::ListResult& result, Io io) {
    if (!result.ok) {
        io.err << result.error << '\n';
        return result.code;
    }

    nlohmann::json targets = nlohmann::json::array();
    for (const auto& window : result.windows) {
        targets.push_back(target_window_to_json(window));
    }
    io.out << nlohmann::json{{"targets", std::move(targets)}}.dump(2) << '\n';
    return 0;
}

int print_target_inspect_result(const kiseki::platform::target::InspectResult& result, Io io) {
    if (!result.ok) {
        io.err << result.error << '\n';
        return result.code;
    }

    nlohmann::json children = nlohmann::json::array();
    for (const auto& child : result.children) {
        children.push_back(target_child_window_to_json(child));
    }
    io.out << nlohmann::json{
        {"target", target_window_to_json(result.window)},
        {"children", std::move(children)},
    }.dump(2) << '\n';
    return 0;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error{"failed to open text file: " + path.string()};
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

kiseki::platform::target::TargetQuery to_target_query(const TargetOptions& options) {
    return kiseki::platform::target::TargetQuery{
        .title = options.title,
        .pid = options.pid,
        .window_id = options.window_id,
    };
}

void add_target_options(CLI::App* command, TargetOptions& options) {
    command->add_option("--target-title", options.title, "Target window title substring");
    command->add_option("--target-pid", options.pid, "Target process id");
    command->add_option("--target-window-id", options.window_id, "Target platform window id");
}

std::vector<std::string> split_delimited_values(const std::string& value) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : value) {
        if (c == ',' || c == '+') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

std::vector<kiseki::platform::input::MousePoint> read_mouse_points_file(const std::filesystem::path& path) {
    std::ifstream file{path};
    if (!file) {
        throw std::runtime_error{"failed to open mouse path file: " + path.string()};
    }

    std::vector<kiseki::platform::input::MousePoint> points;
    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream stream{line};
        int x = 0;
        int y = 0;
        if (!(stream >> x >> y)) {
            throw std::runtime_error{"invalid mouse path line " + std::to_string(line_number) + ": " + line};
        }
        points.push_back(kiseki::platform::input::MousePoint{.x = x, .y = y});
    }

    if (points.size() < 2) {
        throw std::runtime_error{"mouse path file requires at least two points"};
    }
    return points;
}

struct MacroStep {
    std::string type;
    std::string key;
    std::string keys;
    std::string text;
    std::filesystem::path text_file;
    std::filesystem::path path;
    std::filesystem::path output_path;
    std::string backend = "auto";
    std::string click = "none";
    TargetOptions target;
    int dx = 0;
    int dy = 0;
    int x = 0;
    int y = 0;
    bool absolute = false;
    bool has_x = false;
    bool has_y = false;
    std::uint32_t ms = 0;
};

std::string required_string(const nlohmann::json& object, const char* key, std::string_view context) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        throw std::runtime_error{std::string{context} + " requires string field '" + key + "'"};
    }
    return object.at(key).get<std::string>();
}

std::string optional_string(const nlohmann::json& object, const char* key, std::string fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object.at(key).is_string()) {
        throw std::runtime_error{"macro field '" + std::string{key} + "' must be a string"};
    }
    return object.at(key).get<std::string>();
}

int optional_int(const nlohmann::json& object, const char* key, int fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object.at(key).is_number_integer()) {
        throw std::runtime_error{"macro field '" + std::string{key} + "' must be an integer"};
    }
    return object.at(key).get<int>();
}

std::uint32_t required_non_negative_ms(const nlohmann::json& object) {
    const int value = optional_int(object, "ms", -1);
    if (value < 0) {
        throw std::runtime_error{"sleep step requires non-negative integer field 'ms'"};
    }
    return static_cast<std::uint32_t>(value);
}

bool optional_bool(const nlohmann::json& object, const char* key, bool fallback) {
    if (!object.contains(key)) {
        return fallback;
    }
    if (!object.at(key).is_boolean()) {
        throw std::runtime_error{"macro field '" + std::string{key} + "' must be a boolean"};
    }
    return object.at(key).get<bool>();
}

TargetOptions optional_target_options(const nlohmann::json& object) {
    return TargetOptions{
        .title = optional_string(object, "targetTitle", ""),
        .pid = static_cast<std::uint32_t>(optional_int(object, "targetPid", 0)),
        .window_id = optional_string(object, "targetWindowId", ""),
    };
}

void require_no_partial_mouse_position(const MacroStep& step) {
    if (step.has_x != step.has_y) {
        throw std::runtime_error{"mouse step requires both x and y"};
    }
    if (step.absolute && !(step.has_x && step.has_y)) {
        throw std::runtime_error{"mouse step requires both x and y when absolute is true"};
    }
}

MacroStep parse_macro_step(const nlohmann::json& step_json, std::size_t index) {
    if (!step_json.is_object()) {
        throw std::runtime_error{"macro step " + std::to_string(index + 1) + " must be an object"};
    }

    MacroStep step;
    const std::string context = "macro step " + std::to_string(index + 1);
    step.type = required_string(step_json, "type", context);
    step.backend = optional_string(step_json, "backend", "auto");

    if (step.type == "key") {
        step.key = required_string(step_json, "key", "key step");
    } else if (step.type == "combo") {
        step.keys = required_string(step_json, "keys", "combo step");
    } else if (step.type == "text") {
        const bool has_text = step_json.contains("text");
        const bool has_file = step_json.contains("file");
        if (has_text == has_file) {
            throw std::runtime_error{"text step requires exactly one of text or file"};
        }
        if (has_text) {
            step.text = required_string(step_json, "text", "text step");
        } else {
            step.text_file = required_string(step_json, "file", "text step");
        }
    } else if (step.type == "mouse") {
        step.dx = optional_int(step_json, "dx", 0);
        step.dy = optional_int(step_json, "dy", 0);
        step.has_x = step_json.contains("x");
        step.has_y = step_json.contains("y");
        step.x = optional_int(step_json, "x", 0);
        step.y = optional_int(step_json, "y", 0);
        step.absolute = optional_bool(step_json, "absolute", step.has_x && step.has_y);
        step.click = optional_string(step_json, "click", "none");
        require_no_partial_mouse_position(step);
    } else if (step.type == "drag") {
        step.path = required_string(step_json, "file", "drag step");
    } else if (step.type == "background-drag") {
        step.path = required_string(step_json, "file", "background-drag step");
        step.target = optional_target_options(step_json);
    } else if (step.type == "background-screenshot") {
        step.output_path = required_string(step_json, "output", "background-screenshot step");
        step.target = optional_target_options(step_json);
    } else if (step.type == "screenshot") {
        step.output_path = required_string(step_json, "output", "screenshot step");
    } else if (step.type == "sleep") {
        step.ms = required_non_negative_ms(step_json);
    } else {
        throw std::runtime_error{"unsupported macro step type: " + step.type};
    }

    return step;
}

std::vector<MacroStep> read_macro_file(const std::filesystem::path& path) {
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(read_text_file(path));
    } catch (const std::exception& error) {
        throw std::runtime_error{"failed to parse macro file: " + std::string{error.what()}};
    }

    if (!json.is_object()) {
        throw std::runtime_error{"macro file must contain a JSON object"};
    }
    if (!json.contains("steps") || !json.at("steps").is_array()) {
        throw std::runtime_error{"macro requires steps array"};
    }
    const auto& steps_json = json.at("steps");
    if (steps_json.empty()) {
        throw std::runtime_error{"macro requires at least one step"};
    }

    std::vector<MacroStep> steps;
    steps.reserve(steps_json.size());
    for (std::size_t index = 0; index < steps_json.size(); ++index) {
        steps.push_back(parse_macro_step(steps_json.at(index), index));
    }
    return steps;
}

int validate_macro_command(const MacroOptions& options, Io io) {
    try {
        static_cast<void>(read_macro_file(options.path));
    } catch (const std::exception& error) {
        io.err << error.what() << '\n';
        return 2;
    }

    io.out << "macro is valid\n";
    return 0;
}

int run_macro_step(
    const MacroStep& step,
    std::size_t index,
    Dependencies& dependencies,
    Io io) {
    const auto missing_backend = [&](std::string_view name) {
        io.err << "macro step " << (index + 1) << " failed: " << name << " backend is not configured\n";
        return 2;
    };

    int code = 0;
    if (step.type == "key") {
        if (!dependencies.input_key) return missing_backend("input key");
        code = dependencies.input_key(InputKeyOptions{.key = step.key, .backend = step.backend}, io);
    } else if (step.type == "combo") {
        if (!dependencies.input_combo) return missing_backend("input combo");
        code = dependencies.input_combo(InputComboOptions{.keys = step.keys, .backend = step.backend}, io);
    } else if (step.type == "text") {
        if (!dependencies.input_text) return missing_backend("input text");
        InputTextOptions options{
            .text = step.text,
            .text_file = step.text_file,
        };
        if (!options.text_file.empty()) {
            try {
                options.text = read_text_file(options.text_file);
            } catch (const std::exception& error) {
                io.err << "macro step " << (index + 1) << " failed: " << error.what() << '\n';
                return 2;
            }
        }
        code = dependencies.input_text(options, io);
    } else if (step.type == "mouse") {
        if (!dependencies.input_mouse) return missing_backend("input mouse");
        code = dependencies.input_mouse(
            InputMouseOptions{
                .dx = step.dx,
                .dy = step.dy,
                .x = step.x,
                .y = step.y,
                .absolute = step.absolute,
                .backend = step.backend,
                .click = step.click,
            },
            io);
    } else if (step.type == "drag") {
        if (!dependencies.input_drag) return missing_backend("input drag");
        code = dependencies.input_drag(InputDragOptions{.path = step.path, .backend = step.backend}, io);
    } else if (step.type == "background-drag") {
        if (!dependencies.input_background_drag) return missing_backend("background drag");
        code = dependencies.input_background_drag(BackgroundDragOptions{.target = step.target, .path = step.path}, io);
    } else if (step.type == "background-screenshot") {
        if (!dependencies.capture_background_window) return missing_backend("background screenshot");
        code = dependencies.capture_background_window(ScreenshotBackgroundWindowOptions{.target = step.target, .output_path = step.output_path}, io);
    } else if (step.type == "screenshot") {
        if (!dependencies.capture_desktop) return missing_backend("screenshot");
        code = dependencies.capture_desktop(ScreenshotDesktopOptions{.output_path = step.output_path}, io);
    } else if (step.type == "sleep") {
        std::this_thread::sleep_for(std::chrono::milliseconds{step.ms});
        code = 0;
    }

    if (code != 0) {
        io.err << "macro step " << (index + 1) << " failed\n";
    }
    return code;
}

int run_macro_command(const MacroOptions& options, Dependencies& dependencies, Io io) {
    std::vector<MacroStep> steps;
    try {
        steps = read_macro_file(options.path);
    } catch (const std::exception& error) {
        io.err << error.what() << '\n';
        return 2;
    }

    for (std::size_t index = 0; index < steps.size(); ++index) {
        const int code = run_macro_step(steps[index], index, dependencies, io);
        if (code != 0) {
            return code;
        }
    }

    io.out << "macro completed " << steps.size() << " steps\n";
    return 0;
}

}

Dependencies default_dependencies() {
    return Dependencies{
        .launch_config_ui = [](const WebUiLaunchOptions& options, const std::filesystem::path& config_path, Io io) {
            io.out << "Serving configuration UI at "
                   << kiseki::webui::build_listen_url(options.host, options.port) << '\n';
            kiseki::webui::WebServer server{config_path};
            return server.listen(options.host, options.port);
        },
        .list_targets = [](const TargetListOptions& options, Io io) {
            return print_target_list_result(kiseki::platform::target::list_windows(to_target_query(options.filter)), io);
        },
        .inspect_target = [](const TargetInspectOptions& options, Io io) {
            return print_target_inspect_result(kiseki::platform::target::inspect_window(to_target_query(options.target)), io);
        },
        .capture_desktop = [](const ScreenshotDesktopOptions& options, Io io) {
            return print_capture_result(kiseki::platform::capture::capture_desktop_bmp(options.output_path), io);
        },
        .capture_burst = [](const ScreenshotBurstOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::capture::capture_burst_bmp(kiseki::platform::capture::BurstOptions{
                    .output_directory = options.output_directory,
                    .prefix = options.prefix,
                    .frames = options.frames,
                    .fps = options.fps,
                }),
                io);
        },
        .capture_window = [](const ScreenshotWindowOptions& options, Io io) {
            return print_capture_result(
                kiseki::platform::capture::capture_window_bmp(to_target_query(options.target), options.output_path),
                io);
        },
        .capture_background_window = [](const ScreenshotBackgroundWindowOptions& options, Io io) {
            return print_capture_result(
                kiseki::platform::capture::capture_background_window_bmp(to_target_query(options.target), options.output_path),
                io);
        },
        .capture_window_burst = [](const ScreenshotWindowBurstOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::capture::capture_window_burst_bmp(kiseki::platform::capture::WindowBurstOptions{
                    .target = to_target_query(options.target),
                    .output_directory = options.output_directory,
                    .prefix = options.prefix,
                    .frames = options.frames,
                    .fps = options.fps,
                }),
                io);
        },
        .input_key = [](const InputKeyOptions& options, Io io) {
            return print_operation_result(kiseki::platform::input::tap_key(options.key, options.backend), io);
        },
        .input_combo = [](const InputComboOptions& options, Io io) {
            return print_operation_result(kiseki::platform::input::key_combo(options.keys, options.backend), io);
        },
        .input_text = [](const InputTextOptions& options, Io io) {
            return print_operation_result(kiseki::platform::input::type_text(options.text), io);
        },
        .input_mouse = [](const InputMouseOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::input::mouse_action(kiseki::platform::input::MouseOptions{
                    .dx = options.dx,
                    .dy = options.dy,
                    .x = options.x,
                    .y = options.y,
                    .absolute = options.absolute,
                    .backend = options.backend,
                    .click = options.click,
                }),
                io);
        },
        .input_drag = [](const InputDragOptions& options, Io io) {
            try {
                return print_operation_result(
                    kiseki::platform::input::mouse_drag_absolute(read_mouse_points_file(options.path), options.backend),
                    io);
            } catch (const std::exception& error) {
                io.err << error.what() << '\n';
                return 2;
            }
        },
        .input_background_text = [](const BackgroundTextOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::input::background_type_text(to_target_query(options.target), options.text),
                io);
        },
        .input_background_key = [](const BackgroundKeyOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::input::background_tap_key(to_target_query(options.target), options.key),
                io);
        },
        .input_background_mouse = [](const BackgroundMouseOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::input::background_mouse_action(kiseki::platform::input::BackgroundMouseOptions{
                    .target = to_target_query(options.target),
                    .x = options.x,
                    .y = options.y,
                    .click = options.click,
                }),
                io);
        },
        .input_background_drag = [](const BackgroundDragOptions& options, Io io) {
            try {
                return print_operation_result(
                    kiseki::platform::input::background_mouse_drag(kiseki::platform::input::BackgroundDragOptions{
                        .target = to_target_query(options.target),
                        .points = read_mouse_points_file(options.path),
                    }),
                    io);
            } catch (const std::exception& error) {
                io.err << error.what() << '\n';
                return 2;
            }
        },
        .background_desktop_start = [](const BackgroundDesktopStartOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::start_background_desktop(kiseki::platform::session::BackgroundDesktopStartOptions{
                    .display = options.display,
                    .state_directory = options.state_directory,
                    .width = options.width,
                    .height = options.height,
                    .depth = options.depth,
                }),
                io);
        },
        .background_desktop_stop = [](const BackgroundDesktopStopOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::stop_background_desktop(kiseki::platform::session::BackgroundDesktopStopOptions{
                    .display = options.display,
                    .state_directory = options.state_directory,
                }),
                io);
        },
        .background_desktop_launch = [](const BackgroundDesktopLaunchOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::launch_in_background_desktop(kiseki::platform::session::BackgroundDesktopLaunchOptions{
                    .display = options.display,
                    .command = options.command,
                }),
                io);
        },
        .background_desktop_screenshot = [](const BackgroundDesktopScreenshotOptions& options, Io io) {
            return print_capture_result(
                kiseki::platform::session::screenshot_background_desktop(kiseki::platform::session::BackgroundDesktopScreenshotOptions{
                    .display = options.display,
                    .output_path = options.output_path,
                }),
                io);
        },
        .background_desktop_text = [](const BackgroundDesktopTextOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::text_background_desktop(kiseki::platform::session::BackgroundDesktopTextOptions{
                    .display = options.display,
                    .text = options.text,
                }),
                io);
        },
        .background_desktop_key = [](const BackgroundDesktopKeyOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::key_background_desktop(kiseki::platform::session::BackgroundDesktopKeyOptions{
                    .display = options.display,
                    .key = options.key,
                }),
                io);
        },
        .background_desktop_mouse = [](const BackgroundDesktopMouseOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::mouse_background_desktop(kiseki::platform::session::BackgroundDesktopMouseOptions{
                    .display = options.display,
                    .x = options.x,
                    .y = options.y,
                    .click = options.click,
                }),
                io);
        },
        .mac_background_status = [](const MacBackgroundStatusOptions& options, Io io) {
            return print_operation_result(kiseki::platform::session::macos_cua_status(options.prompt), io);
        },
        .mac_background_launch = [](const MacBackgroundLaunchOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_launch(kiseki::platform::session::MacCuaLaunchOptions{
                    .bundle_id = options.bundle_id,
                    .name = options.name,
                    .urls = options.urls,
                    .creates_new_instance = options.new_instance,
                    .additional_arguments = options.arguments,
                }),
                io);
        },
        .mac_background_windows = [](const MacBackgroundWindowsOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_list_windows(kiseki::platform::session::MacCuaWindowListOptions{
                    .pid = options.pid,
                    .has_pid = options.has_pid,
                    .on_screen_only = options.on_screen_only,
                }),
                io);
        },
        .mac_background_state = [](const MacBackgroundStateOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_window_state(kiseki::platform::session::MacCuaWindowStateOptions{
                    .pid = options.pid,
                    .window_id = options.window_id,
                    .output_path = options.output_path,
                    .query = options.query,
                }),
                io);
        },
        .mac_background_screenshot = [](const MacBackgroundScreenshotOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_screenshot(kiseki::platform::session::MacCuaScreenshotOptions{
                    .window_id = options.window_id,
                    .output_path = options.output_path,
                    .format = options.format,
                    .quality = options.quality,
                }),
                io);
        },
        .mac_background_click = [](const MacBackgroundClickOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_click(kiseki::platform::session::MacCuaClickOptions{
                    .pid = options.pid,
                    .window_id = options.window_id,
                    .has_window_id = options.has_window_id,
                    .element_index = options.element_index,
                    .has_element_index = options.has_element_index,
                    .x = options.x,
                    .y = options.y,
                    .has_xy = options.has_xy,
                    .button = options.button,
                    .modifiers = options.modifiers,
                }),
                io);
        },
        .mac_background_text = [](const MacBackgroundTextOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_type_text(kiseki::platform::session::MacCuaTextOptions{
                    .pid = options.pid,
                    .text = options.text,
                    .window_id = options.window_id,
                    .has_window_id = options.has_window_id,
                    .element_index = options.element_index,
                    .has_element_index = options.has_element_index,
                    .delay_ms = options.delay_ms,
                }),
                io);
        },
        .mac_background_key = [](const MacBackgroundKeyOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_press_key(kiseki::platform::session::MacCuaKeyOptions{
                    .pid = options.pid,
                    .key = options.key,
                    .window_id = options.window_id,
                    .has_window_id = options.has_window_id,
                    .element_index = options.element_index,
                    .has_element_index = options.has_element_index,
                    .modifiers = options.modifiers,
                }),
                io);
        },
        .mac_background_hotkey = [](const MacBackgroundHotkeyOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_hotkey(kiseki::platform::session::MacCuaHotkeyOptions{
                    .pid = options.pid,
                    .keys = options.keys,
                    .window_id = options.window_id,
                    .has_window_id = options.has_window_id,
                }),
                io);
        },
        .mac_background_drag = [](const MacBackgroundDragOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_drag(kiseki::platform::session::MacCuaDragOptions{
                    .pid = options.pid,
                    .window_id = options.window_id,
                    .has_window_id = options.has_window_id,
                    .from_x = options.from_x,
                    .from_y = options.from_y,
                    .to_x = options.to_x,
                    .to_y = options.to_y,
                    .duration_ms = options.duration_ms,
                    .steps = options.steps,
                    .button = options.button,
                    .modifiers = options.modifiers,
                }),
                io);
        },
        .run_daemon = [](const DaemonOptions& options, const std::filesystem::path& config_path, Io io) {
            return kiseki::platform::notification::run_heartbeat_daemon(config_path, options.once, io.out, io.err);
        },
    };
}

std::filesystem::path resolve_config_path(std::filesystem::path override_path) {
    if (!override_path.empty()) {
        return override_path;
    }

    return default_config_path(current_environment(), current_platform());
}

int run(
    const std::vector<std::string>& args,
    std::filesystem::path config_path,
    Io io,
    Dependencies dependencies) {
    std::filesystem::path config_path_override = std::move(config_path);
    const auto active_config_path = [&]() {
        return resolve_config_path(config_path_override);
    };
    const auto make_store = [&]() {
        return ConfigStore{active_config_path()};
    };
    int exit_code = 0;
    WebUiLaunchOptions webui_options{
        .host = "",
        .port = 0,
    };
    ScreenshotDesktopOptions desktop_options{
        .output_path = {},
    };
    ScreenshotBurstOptions burst_options{
        .output_directory = {},
        .prefix = "frame",
        .frames = 0,
        .fps = 0,
    };
    TargetListOptions target_list_options{
        .filter = {},
    };
    TargetInspectOptions target_inspect_options{
        .target = {},
    };
    ScreenshotWindowOptions window_options{
        .target = {},
        .output_path = {},
    };
    ScreenshotBackgroundWindowOptions background_window_options{
        .target = {},
        .output_path = {},
    };
    ScreenshotWindowBurstOptions window_burst_options{
        .target = {},
        .output_directory = {},
        .prefix = "frame",
        .frames = 0,
        .fps = 0,
    };
    InputKeyOptions key_options{
        .key = "",
        .backend = "auto",
    };
    InputComboOptions combo_options{
        .keys = "",
        .backend = "auto",
    };
    InputTextOptions text_options{
        .text = "",
        .text_file = {},
    };
    InputMouseOptions mouse_options{
        .dx = 0,
        .dy = 0,
        .x = 0,
        .y = 0,
        .absolute = false,
        .backend = "auto",
        .click = "none",
    };
    InputDragOptions drag_options{
        .path = {},
        .backend = "auto",
    };
    BackgroundTextOptions background_text_options{
        .target = {},
        .text = "",
        .text_file = {},
    };
    BackgroundKeyOptions background_key_options{
        .target = {},
        .key = "",
    };
    BackgroundMouseOptions background_mouse_options{
        .target = {},
        .x = 0,
        .y = 0,
        .click = "none",
    };
    BackgroundDragOptions background_drag_options{
        .target = {},
        .path = {},
    };
    BackgroundDesktopStartOptions background_desktop_start_options{
        .display = ":99",
        .state_directory = {},
        .width = 1280,
        .height = 720,
        .depth = 24,
    };
    BackgroundDesktopStopOptions background_desktop_stop_options{
        .display = ":99",
        .state_directory = {},
    };
    BackgroundDesktopLaunchOptions background_desktop_launch_options{
        .display = ":99",
        .command = "",
    };
    BackgroundDesktopScreenshotOptions background_desktop_screenshot_options{
        .display = ":99",
        .output_path = {},
    };
    BackgroundDesktopTextOptions background_desktop_text_options{
        .display = ":99",
        .text = "",
        .text_file = {},
    };
    BackgroundDesktopKeyOptions background_desktop_key_options{
        .display = ":99",
        .key = "",
    };
    BackgroundDesktopMouseOptions background_desktop_mouse_options{
        .display = ":99",
        .x = 0,
        .y = 0,
        .click = "none",
    };
    MacBackgroundStatusOptions mac_background_status_options{
        .prompt = false,
    };
    MacBackgroundLaunchOptions mac_background_launch_options{
        .bundle_id = "",
        .name = "",
        .urls = {},
        .new_instance = false,
        .arguments = {},
    };
    MacBackgroundWindowsOptions mac_background_windows_options{
        .pid = 0,
        .has_pid = false,
        .on_screen_only = false,
    };
    MacBackgroundStateOptions mac_background_state_options{
        .pid = 0,
        .window_id = 0,
        .output_path = {},
        .query = "",
    };
    MacBackgroundScreenshotOptions mac_background_screenshot_options{
        .window_id = 0,
        .output_path = {},
        .format = "png",
        .quality = 95,
    };
    MacBackgroundClickOptions mac_background_click_options{
        .pid = 0,
        .window_id = 0,
        .has_window_id = false,
        .element_index = 0,
        .has_element_index = false,
        .x = 0.0,
        .y = 0.0,
        .has_xy = false,
        .button = "left",
        .modifiers = {},
    };
    MacBackgroundTextOptions mac_background_text_options{
        .pid = 0,
        .text = "",
        .text_file = {},
        .window_id = 0,
        .has_window_id = false,
        .element_index = 0,
        .has_element_index = false,
        .delay_ms = 30,
    };
    MacBackgroundKeyOptions mac_background_key_options{
        .pid = 0,
        .key = "",
        .window_id = 0,
        .has_window_id = false,
        .element_index = 0,
        .has_element_index = false,
        .modifiers = {},
    };
    MacBackgroundHotkeyOptions mac_background_hotkey_options{
        .pid = 0,
        .keys = {},
        .window_id = 0,
        .has_window_id = false,
    };
    MacBackgroundDragOptions mac_background_drag_options{
        .pid = 0,
        .window_id = 0,
        .has_window_id = false,
        .from_x = 0.0,
        .from_y = 0.0,
        .to_x = 0.0,
        .to_y = 0.0,
        .duration_ms = 500,
        .steps = 20,
        .button = "left",
        .modifiers = {},
    };
    std::string mac_background_click_modifiers;
    std::string mac_background_key_modifiers;
    std::string mac_background_hotkey_keys;
    std::string mac_background_drag_modifiers;
    DaemonOptions daemon_options{
        .once = false,
    };
    MacroOptions macro_options{
        .path = {},
    };

    CLI::App app{"Kiseki Input"};
    app.set_version_flag("--version", std::string{kiseki::core::version()});
    app.add_option("--config", config_path_override, "Config file path");
    app.require_subcommand(1);

    CLI::App* config = app.add_subcommand("config", "Configuration commands");
    config->require_subcommand(1);
    config->add_subcommand("path", "Print active config path")->callback([&]() {
        io.out << active_config_path().string() << '\n';
    });
    config->add_subcommand("show", "Print active config as JSON")->callback([&]() {
        exit_code = show_config(active_config_path(), io);
    });
    config->add_subcommand("validate", "Validate active config")->callback([&]() {
        exit_code = validate_config_command(active_config_path(), io);
    });

    auto* config_ui = app.add_subcommand("config-ui", "Launch local configuration WebUI");
    config_ui->add_option("--host", webui_options.host, "Listen host");
    config_ui->add_option("--port", webui_options.port, "Listen port");
    config_ui->callback([&]() {
        const auto store = make_store();
        const auto loaded = store.load_or_default();
        if (!loaded.ok) {
            io.err << "config error: " << loaded.error << '\n';
            exit_code = 2;
            return;
        }

        if (webui_options.host.empty()) {
            webui_options.host = loaded.config.webui.host;
        }
        if (webui_options.port == 0) {
            webui_options.port = loaded.config.webui.port;
        }

        if (!dependencies.launch_config_ui) {
            io.err << "config-ui launcher is not configured\n";
            exit_code = 2;
            return;
        }

        exit_code = dependencies.launch_config_ui(webui_options, store.path(), io);
    });

    auto* target = app.add_subcommand("target", "Target window commands");
    target->require_subcommand(1);
    auto* target_list = target->add_subcommand("list", "List target windows");
    add_target_options(target_list, target_list_options.filter);
    target_list->callback([&]() {
        if (!dependencies.list_targets) {
            io.err << "target list backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.list_targets(target_list_options, io);
    });
    auto* target_inspect = target->add_subcommand("inspect", "Inspect a selected target window and child receivers");
    add_target_options(target_inspect, target_inspect_options.target);
    target_inspect->callback([&]() {
        if (!dependencies.inspect_target) {
            io.err << "target inspect backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.inspect_target(target_inspect_options, io);
    });

    auto* screenshot = app.add_subcommand("screenshot", "Screenshot commands");
    screenshot->require_subcommand(1);
    auto* screenshot_desktop = screenshot->add_subcommand("desktop", "Capture the desktop to a BMP file");
    screenshot_desktop->add_option("-o,--output", desktop_options.output_path, "Output BMP path")->required();
    screenshot_desktop->callback([&]() {
        if (!dependencies.capture_desktop) {
            io.err << "screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.capture_desktop(desktop_options, io);
    });

    auto* screenshot_burst = screenshot->add_subcommand("burst", "Capture a burst of desktop BMP frames");
    screenshot_burst->add_option("-d,--directory", burst_options.output_directory, "Output directory");
    screenshot_burst->add_option("--prefix", burst_options.prefix, "Frame filename prefix");
    screenshot_burst->add_option("--frames", burst_options.frames, "Frame count");
    screenshot_burst->add_option("--fps", burst_options.fps, "Target frames per second");
    screenshot_burst->callback([&]() {
        if (!dependencies.capture_burst) {
            io.err << "screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        const auto loaded = make_store().load_or_default();
        if (!loaded.ok) {
            io.err << "config error: " << loaded.error << '\n';
            exit_code = 2;
            return;
        }
        if (burst_options.output_directory.empty()) {
            burst_options.output_directory = loaded.config.screenshot.default_output_directory.empty()
                                                 ? std::filesystem::path{"."}
                                                 : std::filesystem::path{loaded.config.screenshot.default_output_directory};
        }
        if (burst_options.frames == 0) {
            burst_options.frames = loaded.config.screenshot.burst_frames;
        }
        if (burst_options.fps == 0) {
            burst_options.fps = loaded.config.screenshot.burst_fps;
        }
        exit_code = dependencies.capture_burst(burst_options, io);
    });

    auto* screenshot_window = screenshot->add_subcommand("window", "Capture a target window to a BMP file");
    add_target_options(screenshot_window, window_options.target);
    screenshot_window->add_option("-o,--output", window_options.output_path, "Output BMP path")->required();
    screenshot_window->callback([&]() {
        if (!dependencies.capture_window) {
            io.err << "window screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.capture_window(window_options, io);
    });

    auto* screenshot_background_window = screenshot->add_subcommand("background-window", "Capture a target window without activating it");
    add_target_options(screenshot_background_window, background_window_options.target);
    screenshot_background_window->add_option("-o,--output", background_window_options.output_path, "Output BMP path")->required();
    screenshot_background_window->callback([&]() {
        if (!dependencies.capture_background_window) {
            io.err << "background-window screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.capture_background_window(background_window_options, io);
    });

    auto* screenshot_window_burst = screenshot->add_subcommand("window-burst", "Capture a burst of target-window BMP frames");
    add_target_options(screenshot_window_burst, window_burst_options.target);
    screenshot_window_burst->add_option("-d,--directory", window_burst_options.output_directory, "Output directory");
    screenshot_window_burst->add_option("--prefix", window_burst_options.prefix, "Frame filename prefix");
    screenshot_window_burst->add_option("--frames", window_burst_options.frames, "Frame count");
    screenshot_window_burst->add_option("--fps", window_burst_options.fps, "Target frames per second");
    screenshot_window_burst->callback([&]() {
        if (!dependencies.capture_window_burst) {
            io.err << "window burst screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        const auto loaded = make_store().load_or_default();
        if (!loaded.ok) {
            io.err << "config error: " << loaded.error << '\n';
            exit_code = 2;
            return;
        }
        if (window_burst_options.output_directory.empty()) {
            window_burst_options.output_directory = loaded.config.screenshot.default_output_directory.empty()
                                                       ? std::filesystem::path{"."}
                                                       : std::filesystem::path{loaded.config.screenshot.default_output_directory};
        }
        if (window_burst_options.frames == 0) {
            window_burst_options.frames = loaded.config.screenshot.burst_frames;
        }
        if (window_burst_options.fps == 0) {
            window_burst_options.fps = loaded.config.screenshot.burst_fps;
        }
        exit_code = dependencies.capture_window_burst(window_burst_options, io);
    });

    auto* input = app.add_subcommand("input", "Keyboard and mouse input commands");
    input->require_subcommand(1);
    auto* input_key = input->add_subcommand("key", "Tap a key");
    input_key->add_option("--key", key_options.key, "Key name")->required();
    input_key->add_option("--backend", key_options.backend, "auto, driver, or system");
    input_key->callback([&]() {
        if (!dependencies.input_key) {
            io.err << "input backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_key(key_options, io);
    });

    auto* input_combo = input->add_subcommand("combo", "Press a key combo such as win+r");
    input_combo->add_option("--keys", combo_options.keys, "Key combo joined by +")->required();
    input_combo->add_option("--backend", combo_options.backend, "auto, driver, or system");
    input_combo->callback([&]() {
        if (!dependencies.input_combo) {
            io.err << "input backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_combo(combo_options, io);
    });

    auto* input_text = input->add_subcommand("text", "Type text");
    input_text->add_option("--text", text_options.text, "Text to type");
    input_text->add_option("--file", text_options.text_file, "UTF-8 text file to type");
    input_text->callback([&]() {
        if (!dependencies.input_text) {
            io.err << "input backend is not configured\n";
            exit_code = 2;
            return;
        }
        if (!text_options.text_file.empty()) {
            try {
                text_options.text = read_text_file(text_options.text_file);
            } catch (const std::exception& error) {
                io.err << error.what() << '\n';
                exit_code = 2;
                return;
            }
        }
        if (text_options.text.empty()) {
            io.err << "input text requires --text or --file\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_text(text_options, io);
    });

    auto* input_mouse = input->add_subcommand("mouse", "Move and optionally click the mouse");
    input_mouse->add_option("--dx", mouse_options.dx, "Relative X movement");
    input_mouse->add_option("--dy", mouse_options.dy, "Relative Y movement");
    auto* mouse_x = input_mouse->add_option("--x", mouse_options.x, "Absolute virtual-screen X position");
    auto* mouse_y = input_mouse->add_option("--y", mouse_options.y, "Absolute virtual-screen Y position");
    input_mouse->add_flag("--absolute", mouse_options.absolute, "Use --x/--y as absolute virtual-screen coordinates");
    input_mouse->add_option("--backend", mouse_options.backend, "auto, driver, or system");
    input_mouse->add_option("--click", mouse_options.click, "none, left, right, middle, left-down, left-up, right-down, right-up, middle-down, or middle-up");
    input_mouse->callback([&]() {
        if (!dependencies.input_mouse) {
            io.err << "input backend is not configured\n";
            exit_code = 2;
            return;
        }
        const bool has_x = mouse_x->count() > 0;
        const bool has_y = mouse_y->count() > 0;
        if (has_x != has_y) {
            io.err << "absolute mouse movement requires both --x and --y\n";
            exit_code = 2;
            return;
        }
        if (mouse_options.absolute && !(has_x && has_y)) {
            io.err << "absolute mouse movement requires both --x and --y\n";
            exit_code = 2;
            return;
        }
        if (has_x && has_y) {
            mouse_options.absolute = true;
        }
        exit_code = dependencies.input_mouse(mouse_options, io);
    });

    auto* input_drag = input->add_subcommand("drag", "Drag the left mouse button through absolute points from a text file");
    input_drag->add_option("--file", drag_options.path, "Mouse path file: one 'x y' point per line")->required();
    input_drag->add_option("--backend", drag_options.backend, "auto, driver, or system");
    input_drag->callback([&]() {
        if (!dependencies.input_drag) {
            io.err << "input drag backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_drag(drag_options, io);
    });

    auto* input_background_text = input->add_subcommand("background-text", "Send text to a target window without switching foreground");
    add_target_options(input_background_text, background_text_options.target);
    input_background_text->add_option("--text", background_text_options.text, "Text to send");
    input_background_text->add_option("--file", background_text_options.text_file, "UTF-8 text file to send");
    input_background_text->callback([&]() {
        if (!dependencies.input_background_text) {
            io.err << "background text backend is not configured\n";
            exit_code = 2;
            return;
        }
        if (!background_text_options.text_file.empty()) {
            try {
                background_text_options.text = read_text_file(background_text_options.text_file);
            } catch (const std::exception& error) {
                io.err << error.what() << '\n';
                exit_code = 2;
                return;
            }
        }
        if (background_text_options.text.empty()) {
            io.err << "input background-text requires --text or --file\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_background_text(background_text_options, io);
    });

    auto* input_background_key = input->add_subcommand("background-key", "Tap a key in a target window without switching foreground");
    add_target_options(input_background_key, background_key_options.target);
    input_background_key->add_option("--key", background_key_options.key, "Key name")->required();
    input_background_key->callback([&]() {
        if (!dependencies.input_background_key) {
            io.err << "background key backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_background_key(background_key_options, io);
    });

    auto* input_background_mouse = input->add_subcommand("background-mouse", "Send a client-area mouse message to a target window");
    add_target_options(input_background_mouse, background_mouse_options.target);
    input_background_mouse->add_option("--x", background_mouse_options.x, "Target client-area X coordinate")->required();
    input_background_mouse->add_option("--y", background_mouse_options.y, "Target client-area Y coordinate")->required();
    input_background_mouse->add_option("--click", background_mouse_options.click, "none, left, right, middle, left-down, left-up, right-down, right-up, middle-down, or middle-up");
    input_background_mouse->callback([&]() {
        if (!dependencies.input_background_mouse) {
            io.err << "background mouse backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_background_mouse(background_mouse_options, io);
    });

    auto* input_background_drag = input->add_subcommand("background-drag", "Drag the left mouse button through target client points without switching foreground");
    add_target_options(input_background_drag, background_drag_options.target);
    input_background_drag->add_option("--file", background_drag_options.path, "Target client path file: one 'x y' point per line")->required();
    input_background_drag->callback([&]() {
        if (!dependencies.input_background_drag) {
            io.err << "background drag backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_background_drag(background_drag_options, io);
    });

    auto* background_desktop = app.add_subcommand("background-desktop", "Run and operate an isolated Linux X11 background desktop");
    background_desktop->require_subcommand(1);
    auto* background_desktop_start = background_desktop->add_subcommand("start", "Start an Xvfb background desktop");
    background_desktop_start->add_option("--display", background_desktop_start_options.display, "X11 display such as :99");
    background_desktop_start->add_option("--state-dir", background_desktop_start_options.state_directory, "Directory for background desktop state files");
    background_desktop_start->add_option("--width", background_desktop_start_options.width, "Screen width");
    background_desktop_start->add_option("--height", background_desktop_start_options.height, "Screen height");
    background_desktop_start->add_option("--depth", background_desktop_start_options.depth, "Screen depth");
    background_desktop_start->callback([&]() {
        if (!dependencies.background_desktop_start) {
            io.err << "background desktop start backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.background_desktop_start(background_desktop_start_options, io);
    });

    auto* background_desktop_stop = background_desktop->add_subcommand("stop", "Stop an Xvfb background desktop started by Kiseki");
    background_desktop_stop->add_option("--display", background_desktop_stop_options.display, "X11 display such as :99");
    background_desktop_stop->add_option("--state-dir", background_desktop_stop_options.state_directory, "Directory for background desktop state files");
    background_desktop_stop->callback([&]() {
        if (!dependencies.background_desktop_stop) {
            io.err << "background desktop stop backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.background_desktop_stop(background_desktop_stop_options, io);
    });

    auto* background_desktop_launch = background_desktop->add_subcommand("launch", "Launch a command inside the background desktop");
    background_desktop_launch->add_option("--display", background_desktop_launch_options.display, "X11 display such as :99");
    background_desktop_launch->add_option("--command", background_desktop_launch_options.command, "Shell command to launch")->required();
    background_desktop_launch->callback([&]() {
        if (!dependencies.background_desktop_launch) {
            io.err << "background desktop launch backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.background_desktop_launch(background_desktop_launch_options, io);
    });

    auto* background_desktop_screenshot = background_desktop->add_subcommand("screenshot", "Capture the background desktop to a BMP file");
    background_desktop_screenshot->add_option("--display", background_desktop_screenshot_options.display, "X11 display such as :99");
    background_desktop_screenshot->add_option("-o,--output", background_desktop_screenshot_options.output_path, "Output BMP path")->required();
    background_desktop_screenshot->callback([&]() {
        if (!dependencies.background_desktop_screenshot) {
            io.err << "background desktop screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.background_desktop_screenshot(background_desktop_screenshot_options, io);
    });

    auto* background_desktop_text = background_desktop->add_subcommand("text", "Type text into the background desktop");
    background_desktop_text->add_option("--display", background_desktop_text_options.display, "X11 display such as :99");
    background_desktop_text->add_option("--text", background_desktop_text_options.text, "Text to type");
    background_desktop_text->add_option("--file", background_desktop_text_options.text_file, "UTF-8 text file to type");
    background_desktop_text->callback([&]() {
        if (!dependencies.background_desktop_text) {
            io.err << "background desktop text backend is not configured\n";
            exit_code = 2;
            return;
        }
        if (!background_desktop_text_options.text_file.empty()) {
            try {
                background_desktop_text_options.text = read_text_file(background_desktop_text_options.text_file);
            } catch (const std::exception& error) {
                io.err << error.what() << '\n';
                exit_code = 2;
                return;
            }
        }
        if (background_desktop_text_options.text.empty()) {
            io.err << "background-desktop text requires --text or --file\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.background_desktop_text(background_desktop_text_options, io);
    });

    auto* background_desktop_key = background_desktop->add_subcommand("key", "Tap a key in the background desktop");
    background_desktop_key->add_option("--display", background_desktop_key_options.display, "X11 display such as :99");
    background_desktop_key->add_option("--key", background_desktop_key_options.key, "Key name")->required();
    background_desktop_key->callback([&]() {
        if (!dependencies.background_desktop_key) {
            io.err << "background desktop key backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.background_desktop_key(background_desktop_key_options, io);
    });

    auto* background_desktop_mouse = background_desktop->add_subcommand("mouse", "Move and optionally click in the background desktop");
    background_desktop_mouse->add_option("--display", background_desktop_mouse_options.display, "X11 display such as :99");
    background_desktop_mouse->add_option("--x", background_desktop_mouse_options.x, "Background desktop X coordinate")->required();
    background_desktop_mouse->add_option("--y", background_desktop_mouse_options.y, "Background desktop Y coordinate")->required();
    background_desktop_mouse->add_option("--click", background_desktop_mouse_options.click, "none, left, right, middle, left-down, left-up, right-down, right-up, middle-down, or middle-up");
    background_desktop_mouse->callback([&]() {
        if (!dependencies.background_desktop_mouse) {
            io.err << "background desktop mouse backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.background_desktop_mouse(background_desktop_mouse_options, io);
    });

    auto* mac_background = app.add_subcommand("mac-background", "Operate macOS background apps through optional Cua Driver");
    mac_background->require_subcommand(1);

    auto* mac_background_status = mac_background->add_subcommand("status", "Check Cua Driver permissions and availability");
    mac_background_status->add_flag("--prompt", mac_background_status_options.prompt, "Request missing Accessibility and Screen Recording permissions");
    mac_background_status->callback([&]() {
        if (!dependencies.mac_background_status) {
            io.err << "mac-background status backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_status(mac_background_status_options, io);
    });

    auto* mac_background_launch = mac_background->add_subcommand("launch", "Launch a macOS app without stealing focus through Cua Driver");
    mac_background_launch->add_option("--bundle-id", mac_background_launch_options.bundle_id, "macOS bundle id, such as com.apple.Safari");
    mac_background_launch->add_option("--name", mac_background_launch_options.name, "Application display name when bundle id is unknown");
    mac_background_launch->add_option("--url", mac_background_launch_options.urls, "URL or file path to hand to the app; repeat for multiple values");
    mac_background_launch->add_flag("--new-instance", mac_background_launch_options.new_instance, "Ask Cua Driver to create a new app instance when supported");
    mac_background_launch->add_option("--arg", mac_background_launch_options.arguments, "Additional argv entry for the launched process; repeat for multiple values");
    mac_background_launch->callback([&]() {
        if (!dependencies.mac_background_launch) {
            io.err << "mac-background launch backend is not configured\n";
            exit_code = 2;
            return;
        }
        if (mac_background_launch_options.bundle_id.empty() && mac_background_launch_options.name.empty()) {
            io.err << "mac-background launch requires --bundle-id or --name\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_launch(mac_background_launch_options, io);
    });

    auto* mac_background_windows = mac_background->add_subcommand("windows", "List Cua Driver target windows");
    auto* mac_background_windows_pid = mac_background_windows->add_option("--pid", mac_background_windows_options.pid, "Restrict windows to a process id");
    mac_background_windows->add_flag("--on-screen-only", mac_background_windows_options.on_screen_only, "Drop off-screen, minimized, or off-Space windows");
    mac_background_windows->callback([&]() {
        if (!dependencies.mac_background_windows) {
            io.err << "mac-background windows backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_windows_options.has_pid = mac_background_windows_pid->count() > 0;
        exit_code = dependencies.mac_background_windows(mac_background_windows_options, io);
    });

    auto* mac_background_state = mac_background->add_subcommand("state", "Read a Cua Driver AX/window snapshot and optionally write a screenshot");
    mac_background_state->add_option("--pid", mac_background_state_options.pid, "Target process id")->required();
    mac_background_state->add_option("--window-id", mac_background_state_options.window_id, "Target CGWindowID")->required();
    mac_background_state->add_option("-o,--output", mac_background_state_options.output_path, "Optional screenshot output path");
    mac_background_state->add_option("--query", mac_background_state_options.query, "Optional case-insensitive AX tree filter");
    mac_background_state->callback([&]() {
        if (!dependencies.mac_background_state) {
            io.err << "mac-background state backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_state(mac_background_state_options, io);
    });

    auto* mac_background_screenshot = mac_background->add_subcommand("screenshot", "Capture a Cua Driver target window to an image file");
    mac_background_screenshot->add_option("--window-id", mac_background_screenshot_options.window_id, "Target CGWindowID")->required();
    mac_background_screenshot->add_option("-o,--output", mac_background_screenshot_options.output_path, "Output image path")->required();
    mac_background_screenshot->add_option("--format", mac_background_screenshot_options.format, "png or jpeg");
    mac_background_screenshot->add_option("--quality", mac_background_screenshot_options.quality, "JPEG quality 1-95");
    mac_background_screenshot->callback([&]() {
        if (!dependencies.mac_background_screenshot) {
            io.err << "mac-background screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_screenshot(mac_background_screenshot_options, io);
    });

    auto* mac_background_click = mac_background->add_subcommand("click", "Click a macOS target pid by element index or window-local pixels");
    mac_background_click->add_option("--pid", mac_background_click_options.pid, "Target process id")->required();
    auto* mac_background_click_window_id = mac_background_click->add_option("--window-id", mac_background_click_options.window_id, "Target CGWindowID");
    auto* mac_background_click_element = mac_background_click->add_option("--element-index", mac_background_click_options.element_index, "Element index from the last state call");
    auto* mac_background_click_x = mac_background_click->add_option("--x", mac_background_click_options.x, "Window-local screenshot pixel X");
    auto* mac_background_click_y = mac_background_click->add_option("--y", mac_background_click_options.y, "Window-local screenshot pixel Y");
    mac_background_click->add_option("--button", mac_background_click_options.button, "left, right, or double");
    mac_background_click->add_option("--modifiers", mac_background_click_modifiers, "Comma or plus separated modifier keys");
    mac_background_click->callback([&]() {
        if (!dependencies.mac_background_click) {
            io.err << "mac-background click backend is not configured\n";
            exit_code = 2;
            return;
        }
        const bool has_x = mac_background_click_x->count() > 0;
        const bool has_y = mac_background_click_y->count() > 0;
        mac_background_click_options.has_window_id = mac_background_click_window_id->count() > 0;
        mac_background_click_options.has_element_index = mac_background_click_element->count() > 0;
        mac_background_click_options.has_xy = has_x && has_y;
        mac_background_click_options.modifiers = split_delimited_values(mac_background_click_modifiers);
        if (has_x != has_y) {
            io.err << "mac-background click requires both --x and --y\n";
            exit_code = 2;
            return;
        }
        if (mac_background_click_options.has_element_index == mac_background_click_options.has_xy) {
            io.err << "mac-background click requires either --element-index or both --x and --y\n";
            exit_code = 2;
            return;
        }
        if (mac_background_click_options.has_element_index && !mac_background_click_options.has_window_id) {
            io.err << "mac-background click with --element-index requires --window-id\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_click(mac_background_click_options, io);
    });

    auto* mac_background_text = mac_background->add_subcommand("text", "Type text into a macOS target pid through Cua Driver");
    mac_background_text->add_option("--pid", mac_background_text_options.pid, "Target process id")->required();
    mac_background_text->add_option("--text", mac_background_text_options.text, "Text to type");
    mac_background_text->add_option("--file", mac_background_text_options.text_file, "UTF-8 text file to type");
    auto* mac_background_text_window_id = mac_background_text->add_option("--window-id", mac_background_text_options.window_id, "Target CGWindowID");
    auto* mac_background_text_element = mac_background_text->add_option("--element-index", mac_background_text_options.element_index, "Element index from the last state call");
    mac_background_text->add_option("--delay-ms", mac_background_text_options.delay_ms, "Character delay for CGEvent fallback");
    mac_background_text->callback([&]() {
        if (!dependencies.mac_background_text) {
            io.err << "mac-background text backend is not configured\n";
            exit_code = 2;
            return;
        }
        if (!mac_background_text_options.text_file.empty()) {
            try {
                mac_background_text_options.text = read_text_file(mac_background_text_options.text_file);
            } catch (const std::exception& error) {
                io.err << error.what() << '\n';
                exit_code = 2;
                return;
            }
        }
        if (mac_background_text_options.text.empty()) {
            io.err << "mac-background text requires --text or --file\n";
            exit_code = 2;
            return;
        }
        mac_background_text_options.has_window_id = mac_background_text_window_id->count() > 0;
        mac_background_text_options.has_element_index = mac_background_text_element->count() > 0;
        if (mac_background_text_options.has_element_index && !mac_background_text_options.has_window_id) {
            io.err << "mac-background text with --element-index requires --window-id\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_text(mac_background_text_options, io);
    });

    auto* mac_background_key = mac_background->add_subcommand("key", "Press a key in a macOS target pid through Cua Driver");
    mac_background_key->add_option("--pid", mac_background_key_options.pid, "Target process id")->required();
    mac_background_key->add_option("--key", mac_background_key_options.key, "Key name")->required();
    auto* mac_background_key_window_id = mac_background_key->add_option("--window-id", mac_background_key_options.window_id, "Target CGWindowID");
    auto* mac_background_key_element = mac_background_key->add_option("--element-index", mac_background_key_options.element_index, "Element index from the last state call");
    mac_background_key->add_option("--modifiers", mac_background_key_modifiers, "Comma or plus separated modifier keys");
    mac_background_key->callback([&]() {
        if (!dependencies.mac_background_key) {
            io.err << "mac-background key backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_key_options.has_window_id = mac_background_key_window_id->count() > 0;
        mac_background_key_options.has_element_index = mac_background_key_element->count() > 0;
        mac_background_key_options.modifiers = split_delimited_values(mac_background_key_modifiers);
        if (mac_background_key_options.has_element_index && !mac_background_key_options.has_window_id) {
            io.err << "mac-background key with --element-index requires --window-id\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_key(mac_background_key_options, io);
    });

    auto* mac_background_hotkey = mac_background->add_subcommand("hotkey", "Press a key combination in a macOS target pid through Cua Driver");
    mac_background_hotkey->add_option("--pid", mac_background_hotkey_options.pid, "Target process id")->required();
    mac_background_hotkey->add_option("--keys", mac_background_hotkey_keys, "Comma or plus separated key combo, such as cmd+c")->required();
    auto* mac_background_hotkey_window_id = mac_background_hotkey->add_option("--window-id", mac_background_hotkey_options.window_id, "Target CGWindowID");
    mac_background_hotkey->callback([&]() {
        if (!dependencies.mac_background_hotkey) {
            io.err << "mac-background hotkey backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_hotkey_options.keys = split_delimited_values(mac_background_hotkey_keys);
        mac_background_hotkey_options.has_window_id = mac_background_hotkey_window_id->count() > 0;
        if (mac_background_hotkey_options.keys.size() < 2) {
            io.err << "mac-background hotkey requires at least two keys in --keys\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_hotkey(mac_background_hotkey_options, io);
    });

    auto* mac_background_drag = mac_background->add_subcommand("drag", "Drag inside a macOS target window through Cua Driver");
    mac_background_drag->add_option("--pid", mac_background_drag_options.pid, "Target process id")->required();
    auto* mac_background_drag_window_id = mac_background_drag->add_option("--window-id", mac_background_drag_options.window_id, "Target CGWindowID");
    mac_background_drag->add_option("--from-x", mac_background_drag_options.from_x, "Drag start X")->required();
    mac_background_drag->add_option("--from-y", mac_background_drag_options.from_y, "Drag start Y")->required();
    mac_background_drag->add_option("--to-x", mac_background_drag_options.to_x, "Drag end X")->required();
    mac_background_drag->add_option("--to-y", mac_background_drag_options.to_y, "Drag end Y")->required();
    mac_background_drag->add_option("--duration-ms", mac_background_drag_options.duration_ms, "Drag duration in milliseconds");
    mac_background_drag->add_option("--steps", mac_background_drag_options.steps, "Number of drag interpolation steps");
    mac_background_drag->add_option("--button", mac_background_drag_options.button, "left, right, or middle");
    mac_background_drag->add_option("--modifiers", mac_background_drag_modifiers, "Comma or plus separated modifier keys");
    mac_background_drag->callback([&]() {
        if (!dependencies.mac_background_drag) {
            io.err << "mac-background drag backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_drag_options.has_window_id = mac_background_drag_window_id->count() > 0;
        mac_background_drag_options.modifiers = split_delimited_values(mac_background_drag_modifiers);
        exit_code = dependencies.mac_background_drag(mac_background_drag_options, io);
    });

    auto* daemon = app.add_subcommand("daemon", "Background daemon commands");
    daemon->require_subcommand(1);
    auto* daemon_run = daemon->add_subcommand("run", "Run heartbeat notification daemon");
    daemon_run->add_flag("--once", daemon_options.once, "Run one heartbeat cycle and exit");
    daemon_run->callback([&]() {
        if (!dependencies.run_daemon) {
            io.err << "daemon backend is not configured\n";
            exit_code = 2;
            return;
        }
        const auto store = make_store();
        exit_code = dependencies.run_daemon(daemon_options, store.path(), io);
    });

    auto* macro = app.add_subcommand("macro", "Macro commands");
    macro->require_subcommand(1);
    auto* macro_validate = macro->add_subcommand("validate", "Validate a JSON macro file");
    macro_validate->add_option("--file", macro_options.path, "Macro JSON file")->required();
    macro_validate->callback([&]() {
        exit_code = validate_macro_command(macro_options, io);
    });

    auto* macro_run = macro->add_subcommand("run", "Run a JSON macro file");
    macro_run->add_option("--file", macro_options.path, "Macro JSON file")->required();
    macro_run->callback([&]() {
        exit_code = run_macro_command(macro_options, dependencies, io);
    });

    app.add_subcommand("capabilities", "Print foundation capabilities")->callback([&]() {
        io.out << to_json(kiseki::platform::runtime_capabilities()).dump(2) << '\n';
    });

    app.add_subcommand("doctor", "Print diagnostics")->callback([&]() {
        io.out << "Kiseki Input doctor\n";
        io.out << "Version: " << kiseki::core::version() << '\n';
        io.out << "Config path: " << active_config_path().string() << '\n';

        const auto result = make_store().load_or_default();
        if (result.ok) {
            io.out << "Config: valid\n";
        } else {
            io.out << "Config: invalid: " << result.error << '\n';
        }

        const auto capabilities = kiseki::platform::runtime_capabilities();
        io.out << "Capabilities:\n";
        io.out << "  Input driver backend: " << availability(capabilities.input.driver) << '\n';
        io.out << "  System input backend: " << availability(capabilities.input.system) << '\n';
        io.out << "  Background-window input: " << availability(capabilities.input.background_window) << '\n';
        io.out << "  Desktop screenshot: " << availability(capabilities.capture.desktop) << '\n';
        io.out << "  Window screenshot: " << availability(capabilities.capture.window) << '\n';
        io.out << "  Screenshot burst: " << availability(capabilities.capture.burst) << '\n';
        io.out << "  Background desktop session: " << availability(capabilities.session.background_desktop) << '\n';
        io.out << "  macOS CUA background operation: " << availability(capabilities.session.macos_cua_background) << '\n';

        io.out << "Limitations:\n";
        for (const auto& limitation : capabilities.limitations) {
            io.out << "  - " << limitation << '\n';
        }
    });

    std::vector<std::string> parse_args;
    parse_args.reserve(args.size() + 1);
    parse_args.emplace_back("kiseki");
    parse_args.insert(parse_args.end(), args.begin(), args.end());

    std::vector<char*> argv;
    argv.reserve(parse_args.size());
    for (std::string& arg : parse_args) {
        argv.push_back(arg.data());
    }

    try {
        app.parse(static_cast<int>(argv.size()), argv.data());
    } catch (const CLI::ParseError& error) {
        return app.exit(error, io.out, io.err);
    } catch (const std::exception& error) {
        io.err << error.what() << '\n';
        return 2;
    }

    return exit_code;
}

}
