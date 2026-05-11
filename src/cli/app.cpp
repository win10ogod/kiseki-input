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
    ScreenshotWindowOptions window_options{
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
