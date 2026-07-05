#include "cli/app.hpp"

#include <CLI/CLI.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <initializer_list>
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
#include "platform/observe/ui_observation.hpp"
#include "platform/permissions/permissions.hpp"
#include "platform/runtime_capabilities.hpp"
#include "platform/session/background_desktop.hpp"
#include "platform/session/macos_cua.hpp"
#include "platform/target/target.hpp"
#include "platform/teach/recording.hpp"
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

nlohmann::json operation_modes_json() {
    return nlohmann::json{
        {"schemaVersion", 1},
        {"strictRules", nlohmann::json::array({
            "Commands starting with input are current-session operations and may use the real pointer or active focus.",
            "Commands starting with screenshot are current-session or target-window captures, not background operation verification.",
            "Commands starting with background are background, isolated-session, or target-routed commands.",
            "Verify a background action with a screenshot from the same background family, not with screenshot desktop.",
            "Use observe ui before screenshot-based verification when structured UI data can answer the task.",
        })},
        {"operationFamilies", {
            {"nonBackground", nlohmann::json::array({
                "kiseki input key|combo|text|mouse|drag",
            })},
            {"background", nlohmann::json::array({
                "kiseki background window text|key|mouse|drag",
                "kiseki background desktop launch|text|key|mouse",
                "kiseki background cua launch|click|text|key|hotkey|drag|draw",
            })},
            {"notOperational", nlohmann::json::array({
                "kiseki observe ui",
                "kiseki target list|inspect",
                "kiseki background cua feedback",
            })},
        }},
        {"screenshotFamilies", {
            {"nonBackground", nlohmann::json::array({
                "kiseki screenshot desktop",
                "kiseki screenshot burst",
                "kiseki screenshot window",
                "kiseki screenshot window-burst",
            })},
            {"background", nlohmann::json::array({
                "kiseki background window screenshot",
                "kiseki background desktop screenshot",
                "kiseki background cua screenshot",
                "kiseki background cua state --output",
            })},
            {"notScreenshot", nlohmann::json::array({
                "kiseki observe ui",
                "kiseki background cua state without --output",
            })},
        }},
        {"modeMatrix", nlohmann::json::array({
            {
                {"id", "current-session"},
                {"background", false},
                {"capturesScreenOrTarget", true},
                {"canMoveRealPointer", true},
                {"dependsOnFocus", true},
                {"commands", nlohmann::json::array({
                    "kiseki input ...",
                    "kiseki screenshot desktop|burst|window|window-burst ...",
                })},
                {"coordinateSpace", "screen or virtual-screen coordinates"},
                {"verifyWith", "kiseki screenshot desktop or kiseki screenshot window"},
            },
            {
                {"id", "selected-window-background"},
                {"background", true},
                {"capturesScreenOrTarget", true},
                {"canMoveRealPointer", false},
                {"dependsOnFocus", false},
                {"commands", nlohmann::json::array({
                    "kiseki background window screenshot ...",
                    "kiseki background window text|key|mouse|drag ...",
                })},
                {"coordinateSpace", "target client-area coordinates"},
                {"verifyWith", "kiseki background window screenshot"},
                {"limits", "Input works only when the target accepts platform window messages or public automation events."},
            },
            {
                {"id", "linux-isolated-display"},
                {"background", true},
                {"capturesScreenOrTarget", true},
                {"canMoveRealPointer", false},
                {"dependsOnFocus", false},
                {"commands", nlohmann::json::array({
                    "kiseki background desktop start|launch|screenshot|text|key|mouse|stop ...",
                })},
                {"coordinateSpace", "isolated Xvfb display coordinates"},
                {"verifyWith", "kiseki background desktop screenshot"},
            },
            {
                {"id", "cua-target-routed"},
                {"background", true},
                {"capturesScreenOrTarget", true},
                {"canMoveRealPointer", false},
                {"dependsOnFocus", false},
                {"commands", nlohmann::json::array({
                    "kiseki background cua status|launch|windows|state|screenshot|click|text|key|hotkey|drag|draw ...",
                })},
                {"coordinateSpace", "window-local screenshot coordinates when --window-id is used"},
                {"verifyWith", "kiseki background cua screenshot or kiseki background cua state --output"},
                {"limits", "Live support requires installed Cua Driver, platform permissions, and target action artifacts."},
            },
        })},
    };
}

void print_operation_modes(Io io) {
    io.out << "Kiseki operation mode guide\n";
    io.out << "Strict split:\n";
    io.out << "  Non-background operations: kiseki input key|combo|text|mouse|drag\n";
    io.out << "  Non-background screenshots: kiseki screenshot desktop|burst|window|window-burst\n";
    io.out << "  Background screenshots: kiseki background window screenshot; kiseki background desktop screenshot; kiseki background cua screenshot/state --output\n";
    io.out << "  Background operations: kiseki background window text|key|mouse|drag; kiseki background desktop launch|text|key|mouse; kiseki background cua launch|click|text|key|hotkey|drag|draw\n";
    io.out << "Stable selection rules:\n";
    io.out << "  1. Use kiseki observe ui before screenshots when structured UI data is enough.\n";
    io.out << "  2. Use kiseki input ... only for current-session work where focus/pointer use is acceptable.\n";
    io.out << "  3. Use kiseki background window screenshot for non-activating selected-window verification.\n";
    io.out << "  4. Use kiseki background desktop screenshot to verify Linux Xvfb background desktop actions.\n";
    io.out << "  5. Use kiseki background cua screenshot or state --output to verify CUA target-routed actions.\n";
    io.out << "  6. Do not verify background actions with kiseki screenshot desktop; it captures the current visible desktop.\n";
    io.out << "Coordinate spaces:\n";
    io.out << "  input ...: screen or virtual-screen coordinates\n";
    io.out << "  background window ...: target client-area coordinates\n";
    io.out << "  background desktop ...: isolated DISPLAY coordinates\n";
    io.out << "  background cua ...: window-local screenshot coordinates when --window-id is used\n";
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

int print_observe_ui_result(const kiseki::platform::observe::UiObservationResult& result, Io io) {
    if (!result.ok) {
        io.err << result.error << '\n';
        return result.code;
    }

    nlohmann::json elements = nlohmann::json::array();
    for (const auto& element : result.elements) {
        nlohmann::json item{
            {"kind", element.kind},
            {"id", element.id},
            {"parentId", element.parent_id},
            {"depth", element.depth},
        };
        if (!element.name.empty()) item["name"] = element.name;
        if (!element.title.empty()) item["title"] = element.title;
        if (!element.automation_id.empty()) item["automationId"] = element.automation_id;
        if (!element.class_name.empty()) item["className"] = element.class_name;
        if (!element.localized_control_type.empty()) item["localizedControlType"] = element.localized_control_type;
        if (!element.framework_id.empty()) item["frameworkId"] = element.framework_id;
        if (!element.role.empty()) item["role"] = element.role;
        if (!element.subrole.empty()) item["subrole"] = element.subrole;
        if (!element.description.empty()) item["description"] = element.description;
        if (!element.value.empty()) item["value"] = element.value;
        if (element.control_type != 0) item["controlType"] = element.control_type;
        if (element.process_id != 0) item["processId"] = element.process_id;
        if (element.has_enabled) item["enabled"] = element.enabled;
        if (element.has_offscreen) item["offscreen"] = element.offscreen;
        if (element.has_bounds) {
            item["bounds"] = {
                {"x", element.x},
                {"y", element.y},
                {"width", element.width},
                {"height", element.height},
            };
        }
        elements.push_back(std::move(item));
    }

    nlohmann::json output{
        {"source", "platform-window-tree"},
        {"visual", result.visual},
        {"coordinateSpace", result.coordinate_space},
        {"truncated", result.truncated},
        {"target", target_window_to_json(result.target)},
        {"elements", std::move(elements)},
    };
    output["source"] = result.source;
    if (!result.fallback_reason.empty()) {
        output["fallbackReason"] = result.fallback_reason;
    }
    io.out << output.dump(2) << '\n';
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

std::vector<std::string> replace_command_tail(
    const std::vector<std::string>& args,
    std::size_t command_index,
    std::size_t drop_count,
    std::initializer_list<std::string> replacement) {
    std::vector<std::string> normalized;
    normalized.reserve(args.size() + replacement.size());
    normalized.insert(normalized.end(), args.begin(), args.begin() + static_cast<std::ptrdiff_t>(command_index));
    normalized.insert(normalized.end(), replacement.begin(), replacement.end());
    normalized.insert(
        normalized.end(),
        args.begin() + static_cast<std::ptrdiff_t>(command_index + drop_count),
        args.end());
    return normalized;
}

std::size_t first_command_index(const std::vector<std::string>& args) {
    for (std::size_t index = 0; index < args.size(); ++index) {
        const auto& arg = args[index];
        if (arg == "--config") {
            if (index + 1 < args.size()) {
                ++index;
            }
            continue;
        }
        if (arg.rfind("--config=", 0) == 0) {
            continue;
        }
        return index;
    }
    return args.size();
}

bool has_help_flag(const std::vector<std::string>& args) {
    for (const auto& arg : args) {
        if (arg == "--help" || arg == "-h") {
            return true;
        }
    }
    return false;
}

std::vector<std::string> normalize_integrated_commands(const std::vector<std::string>& args) {
    if (has_help_flag(args)) {
        return args;
    }

    const auto command_index = first_command_index(args);
    if (command_index >= args.size() || args[command_index] != "background") {
        return args;
    }

    const auto tail_size = args.size() - command_index;
    if (tail_size >= 2 && args[command_index + 1] == "desktop") {
        return replace_command_tail(args, command_index, 2, {"background-desktop"});
    }
    if (tail_size >= 2 && args[command_index + 1] == "cua") {
        return replace_command_tail(args, command_index, 2, {"cua-background"});
    }
    if (tail_size >= 3 && args[command_index + 1] == "window") {
        const auto& action = args[command_index + 2];
        if (action == "screenshot" || action == "capture") {
            return replace_command_tail(args, command_index, 3, {"screenshot", "background-window"});
        }
        if (action == "text") {
            return replace_command_tail(args, command_index, 3, {"input", "background-text"});
        }
        if (action == "key") {
            return replace_command_tail(args, command_index, 3, {"input", "background-key"});
        }
        if (action == "mouse") {
            return replace_command_tail(args, command_index, 3, {"input", "background-mouse"});
        }
        if (action == "drag") {
            return replace_command_tail(args, command_index, 3, {"input", "background-drag"});
        }
    }

    return args;
}

std::string removed_background_command_message(const std::vector<std::string>& args) {
    const auto command_index = first_command_index(args);
    if (command_index >= args.size()) {
        return {};
    }

    const auto tail_size = args.size() - command_index;
    const auto& command = args[command_index];
    if (command == "background" && tail_size >= 2 && args[command_index + 1] == "driver") {
        return "background driver was removed; use background cua <subcommand>";
    }
    if (command == "background-desktop") {
        return "background-desktop was removed; use background desktop <subcommand>";
    }
    if (command == "cua-background" || command == "mac-background") {
        return command + " was removed; use background cua <subcommand>";
    }
    if (command == "screenshot" && tail_size >= 2 && args[command_index + 1] == "background-window") {
        return "screenshot background-window was removed; use background window screenshot";
    }
    if (command == "input" && tail_size >= 2) {
        const auto& subcommand = args[command_index + 1];
        if (subcommand == "background-text") {
            return "input background-text was removed; use background window text";
        }
        if (subcommand == "background-key") {
            return "input background-key was removed; use background window key";
        }
        if (subcommand == "background-mouse") {
            return "input background-mouse was removed; use background window mouse";
        }
        if (subcommand == "background-drag") {
            return "input background-drag was removed; use background window drag";
        }
    }

    return {};
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
        code = dependencies.input_drag(
            InputDragOptions{
                .path = step.path,
                .backend = step.backend,
                .step_delay_ms = 2,
                .start_hold_ms = 0,
                .end_hold_ms = 0,
            },
            io);
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
        .observe_ui = [](const ObserveUiOptions& options, Io io) {
            return print_observe_ui_result(
                kiseki::platform::observe::observe_ui(kiseki::platform::observe::UiObservationOptions{
                    .target = to_target_query(options.target),
                    .provider = options.provider,
                    .max_depth = options.max_depth,
                    .max_elements = options.max_elements,
                }),
                io);
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
                    kiseki::platform::input::mouse_drag_absolute(
                        read_mouse_points_file(options.path),
                        options.backend,
                        options.step_delay_ms,
                        options.start_hold_ms,
                        options.end_hold_ms),
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
        .mac_background_draw = [](const MacBackgroundDrawOptions& options, Io io) {
            try {
                std::vector<kiseki::platform::session::MacCuaPoint> points;
                for (const auto& point : read_mouse_points_file(options.path)) {
                    points.push_back(kiseki::platform::session::MacCuaPoint{
                        .x = static_cast<double>(point.x),
                        .y = static_cast<double>(point.y),
                    });
                }
                return print_operation_result(
                    kiseki::platform::session::macos_cua_draw(kiseki::platform::session::MacCuaDrawOptions{
                        .pid = options.pid,
                        .window_id = options.window_id,
                        .points = std::move(points),
                        .duration_ms = options.duration_ms,
                        .steps = options.steps,
                        .stroke_gap_ms = options.stroke_gap_ms,
                        .max_segments = options.max_segments,
                        .button = options.button,
                        .modifiers = options.modifiers,
                    }),
                    io);
            } catch (const std::exception& error) {
                io.err << error.what() << '\n';
                return 2;
            }
        },
        .mac_background_feedback_status = [](const MacBackgroundFeedbackStatusOptions&, Io io) {
            return print_operation_result(kiseki::platform::session::macos_cua_feedback_state(), io);
        },
        .mac_background_feedback_enable = [](const MacBackgroundFeedbackEnableOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_feedback_enable(kiseki::platform::session::MacCuaFeedbackEnableOptions{
                    .enabled = options.enabled,
                }),
                io);
        },
        .mac_background_feedback_motion = [](const MacBackgroundFeedbackMotionOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_feedback_motion(kiseki::platform::session::MacCuaFeedbackMotionOptions{
                    .has_start_handle = options.has_start_handle,
                    .start_handle = options.start_handle,
                    .has_end_handle = options.has_end_handle,
                    .end_handle = options.end_handle,
                    .has_arc_size = options.has_arc_size,
                    .arc_size = options.arc_size,
                    .has_arc_flow = options.has_arc_flow,
                    .arc_flow = options.arc_flow,
                    .has_spring = options.has_spring,
                    .spring = options.spring,
                    .has_glide_duration_ms = options.has_glide_duration_ms,
                    .glide_duration_ms = options.glide_duration_ms,
                    .has_dwell_after_click_ms = options.has_dwell_after_click_ms,
                    .dwell_after_click_ms = options.dwell_after_click_ms,
                    .has_idle_hide_ms = options.has_idle_hide_ms,
                    .idle_hide_ms = options.idle_hide_ms,
                }),
                io);
        },
        .mac_background_feedback_style = [](const MacBackgroundFeedbackStyleOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_feedback_style(kiseki::platform::session::MacCuaFeedbackStyleOptions{
                    .has_gradient_colors = options.has_gradient_colors,
                    .gradient_colors = options.gradient_colors,
                    .has_bloom_color = options.has_bloom_color,
                    .bloom_color = options.bloom_color,
                    .has_image_path = options.has_image_path,
                    .image_path = options.image_path,
                    .reset = options.reset,
                }),
                io);
        },
        .mac_background_feedback_preset = [](const MacBackgroundFeedbackPresetOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::session::macos_cua_feedback_preset(kiseki::platform::session::MacCuaFeedbackPresetOptions{
                    .name = options.name,
                }),
                io);
        },
        .macos_screen_recording_permission = [](const MacPermissionOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::permissions::request_macos_screen_recording(options.prompt, options.open_settings),
                io);
        },
        .macos_accessibility_permission = [](const MacPermissionOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::permissions::request_macos_accessibility(options.prompt, options.open_settings),
                io);
        },
        .run_daemon = [](const DaemonOptions& options, const std::filesystem::path& config_path, Io io) {
            return kiseki::platform::notification::run_heartbeat_daemon(config_path, options.once, io.out, io.err);
        },
        .teach_record = [](const TeachRecordOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::teach::record_teaching_session(kiseki::platform::teach::RecordOptions{
                    .output_directory = options.output_directory,
                    .video_file = options.video_file,
                    .audio_file = options.audio_file,
                    .transcript_file = options.transcript_file,
                    .state_file = options.state_file,
                    .stop_file = options.stop_file,
                    .duration_ms = options.duration_ms,
                    .frame_interval_ms = options.frame_interval_ms,
                    .event_poll_ms = options.event_poll_ms,
                    .stop_timeout_ms = options.stop_timeout_ms,
                    .video_keyframe_interval_ms = options.video_keyframe_interval_ms,
                    .video_keyframe_max = options.video_keyframe_max,
                    .worker = options.worker,
                    .no_video_keyframes = options.no_video_keyframes,
                    .title = options.title,
                    .instruction_text = options.text,
                }),
                io);
        },
        .teach_annotate = [](const TeachAnnotateOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::teach::add_text_annotation(kiseki::platform::teach::AnnotateOptions{
                    .session_directory = options.session_directory,
                    .frame_index = options.frame_index,
                    .event_index = options.event_index,
                    .has_frame_index = options.has_frame_index,
                    .has_event_index = options.has_event_index,
                    .text = options.text,
                }),
                io);
        },
        .teach_transcribe = [](const TeachTranscribeOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::teach::transcribe_audio(kiseki::platform::teach::TranscribeOptions{
                    .audio_file = options.audio_file,
                    .output_path = options.output_path,
                    .model_path = options.model_path,
                    .script_path = options.script_path,
                    .model_id = options.model_id,
                    .language = options.language,
                    .device = options.device,
                    .compute_type = options.compute_type,
                }),
                io);
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
    ObserveUiOptions observe_ui_options{
        .target = {},
        .provider = "auto",
        .max_depth = 4,
        .max_elements = 256,
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
        .step_delay_ms = 2,
        .start_hold_ms = 0,
        .end_hold_ms = 0,
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
    MacBackgroundDrawOptions mac_background_draw_options{
        .pid = 0,
        .window_id = 0,
        .path = {},
        .duration_ms = 120,
        .steps = 6,
        .stroke_gap_ms = 0,
        .max_segments = 96,
        .button = "left",
        .modifiers = {},
    };
    MacBackgroundFeedbackStatusOptions mac_background_feedback_status_options{};
    MacBackgroundFeedbackEnableOptions mac_background_feedback_enable_options{
        .enabled = true,
    };
    MacBackgroundFeedbackMotionOptions mac_background_feedback_motion_options{
        .has_start_handle = false,
        .start_handle = 0.0,
        .has_end_handle = false,
        .end_handle = 0.0,
        .has_arc_size = false,
        .arc_size = 0.0,
        .has_arc_flow = false,
        .arc_flow = 0.0,
        .has_spring = false,
        .spring = 0.0,
        .has_glide_duration_ms = false,
        .glide_duration_ms = 0.0,
        .has_dwell_after_click_ms = false,
        .dwell_after_click_ms = 0.0,
        .has_idle_hide_ms = false,
        .idle_hide_ms = 0.0,
    };
    MacBackgroundFeedbackStyleOptions mac_background_feedback_style_options{
        .reset = false,
        .has_gradient_colors = false,
        .gradient_colors = {},
        .has_bloom_color = false,
        .bloom_color = "",
        .has_image_path = false,
        .image_path = {},
    };
    MacBackgroundFeedbackPresetOptions mac_background_feedback_preset_options{
        .name = "natural",
    };
    MacPermissionOptions mac_screen_recording_options{
        .prompt = false,
        .open_settings = false,
    };
    MacPermissionOptions mac_accessibility_options{
        .prompt = false,
        .open_settings = false,
    };
    std::string mac_background_click_modifiers;
    std::string mac_background_key_modifiers;
    std::string mac_background_hotkey_keys;
    std::string mac_background_drag_modifiers;
    std::string mac_background_draw_modifiers;
    std::string mac_background_feedback_style_gradient_colors;
    DaemonOptions daemon_options{
        .once = false,
    };
    MacroOptions macro_options{
        .path = {},
    };
    TeachRecordOptions teach_record_options{
        .output_directory = {},
        .text_file = {},
        .video_file = {},
        .audio_file = {},
        .transcript_file = {},
        .state_file = {},
        .stop_file = {},
        .duration_ms = 0,
        .frame_interval_ms = 500,
        .event_poll_ms = 25,
        .stop_timeout_ms = 30000,
        .video_keyframe_interval_ms = 2000,
        .video_keyframe_max = 80,
        .worker = false,
        .no_video_keyframes = false,
        .title = "",
        .text = "",
    };
    TeachAnnotateOptions teach_annotate_options{
        .session_directory = {},
        .text_file = {},
        .frame_index = 0,
        .event_index = 0,
        .has_frame_index = false,
        .has_event_index = false,
        .text = "",
    };
    TeachTranscribeOptions teach_transcribe_options{
        .audio_file = {},
        .output_path = {},
        .model_path = "vendor/models/Systran/faster-whisper-large-v3",
        .script_path = "tools/teach_transcribe.py",
        .model_id = "Systran/faster-whisper-large-v3",
        .language = "",
        .device = "auto",
        .compute_type = "auto",
    };
    bool modes_json = false;

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

    auto* observe = app.add_subcommand("observe", "Non-visual observation commands");
    observe->require_subcommand(1);
    auto* observe_ui = observe->add_subcommand("ui", "Read non-visual UI/window structure for a target");
    add_target_options(observe_ui, observe_ui_options.target);
    observe_ui->add_option("--provider", observe_ui_options.provider, "auto, window-tree, uia, or ax");
    observe_ui->add_option("--max-depth", observe_ui_options.max_depth, "Maximum UI tree depth for structured providers");
    observe_ui->add_option("--max-elements", observe_ui_options.max_elements, "Maximum UI elements to return before marking output truncated");
    observe_ui->callback([&]() {
        if (!dependencies.observe_ui) {
            io.err << "observe ui backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.observe_ui(observe_ui_options, io);
    });

    auto* screenshot = app.add_subcommand("screenshot", "Current-session and target screenshot commands");
    screenshot->require_subcommand(1);
    auto* screenshot_desktop = screenshot->add_subcommand("desktop", "Capture the current visible desktop to a BMP file");
    screenshot_desktop->add_option("-o,--output", desktop_options.output_path, "Output BMP path")->required();
    screenshot_desktop->callback([&]() {
        if (!dependencies.capture_desktop) {
            io.err << "screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.capture_desktop(desktop_options, io);
    });

    auto* screenshot_burst = screenshot->add_subcommand("burst", "Capture a burst of current visible desktop BMP frames");
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

    auto* screenshot_window = screenshot->add_subcommand("window", "Capture a target window through the current-session screenshot path");
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
    screenshot_background_window->group("");
    add_target_options(screenshot_background_window, background_window_options.target);
    screenshot_background_window->add_option("-o,--output", background_window_options.output_path, "Output BMP path")->required();
    screenshot_background_window->callback([&]() {
        if (!dependencies.capture_background_window) {
            io.err << "background window screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.capture_background_window(background_window_options, io);
    });

    auto* screenshot_window_burst = screenshot->add_subcommand("window-burst", "Capture a burst of target-window BMP frames through the current-session screenshot path");
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

    auto* input = app.add_subcommand("input", "Current-session keyboard and mouse input commands");
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
    input_drag->add_option("--step-delay-ms", drag_options.step_delay_ms, "Delay after each drag point in milliseconds");
    input_drag->add_option("--start-hold-ms", drag_options.start_hold_ms, "Delay after mouse down before movement starts");
    input_drag->add_option("--end-hold-ms", drag_options.end_hold_ms, "Delay before mouse up after the last point");
    input_drag->callback([&]() {
        if (!dependencies.input_drag) {
            io.err << "input drag backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_drag(drag_options, io);
    });

    auto* input_background_text = input->add_subcommand("background-text", "Send text to a target window without switching foreground");
    input_background_text->group("");
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
            io.err << "background window text requires --text or --file\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_background_text(background_text_options, io);
    });

    auto* input_background_key = input->add_subcommand("background-key", "Tap a key in a target window without switching foreground");
    input_background_key->group("");
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
    input_background_mouse->group("");
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
    input_background_drag->group("");
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
    background_desktop->group("");
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
            io.err << "background desktop text requires --text or --file\n";
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

    auto* background = app.add_subcommand("background", "Background, isolated-session, and target-routed operation commands");
    background->require_subcommand(1);
    auto* background_window = background->add_subcommand("window", "Selected-window background screenshots and message/API helpers");
    background_window->require_subcommand(1);
    auto* background_window_screenshot_help = background_window->add_subcommand("screenshot", "Capture a target window without activating it");
    add_target_options(background_window_screenshot_help, background_window_options.target);
    background_window_screenshot_help->add_option("-o,--output", background_window_options.output_path, "Output BMP path")->required();
    auto* background_window_capture_help = background_window->add_subcommand("capture", "Capture a target window without activating it");
    add_target_options(background_window_capture_help, background_window_options.target);
    background_window_capture_help->add_option("-o,--output", background_window_options.output_path, "Output BMP path")->required();
    auto* background_window_text_help = background_window->add_subcommand("text", "Send text to a target window without switching foreground");
    add_target_options(background_window_text_help, background_text_options.target);
    background_window_text_help->add_option("--text", background_text_options.text, "Text to send");
    background_window_text_help->add_option("--file", background_text_options.text_file, "UTF-8 text file to send");
    auto* background_window_key_help = background_window->add_subcommand("key", "Tap a key in a target window without switching foreground");
    add_target_options(background_window_key_help, background_key_options.target);
    background_window_key_help->add_option("--key", background_key_options.key, "Key name")->required();
    auto* background_window_mouse_help = background_window->add_subcommand("mouse", "Send a client-area mouse message to a target window");
    add_target_options(background_window_mouse_help, background_mouse_options.target);
    background_window_mouse_help->add_option("--x", background_mouse_options.x, "Target client-area X coordinate")->required();
    background_window_mouse_help->add_option("--y", background_mouse_options.y, "Target client-area Y coordinate")->required();
    background_window_mouse_help->add_option("--click", background_mouse_options.click, "none, left, right, middle, left-down, left-up, right-down, right-up, middle-down, or middle-up");
    auto* background_window_drag_help = background_window->add_subcommand("drag", "Drag through target client points without switching foreground");
    add_target_options(background_window_drag_help, background_drag_options.target);
    background_window_drag_help->add_option("--file", background_drag_options.path, "Target client path file: one 'x y' point per line")->required();

    auto* background_desktop_alias = background->add_subcommand("desktop", "Isolated Linux X11 background desktop operations");
    background_desktop_alias->require_subcommand(1);
    auto* background_desktop_start_help = background_desktop_alias->add_subcommand("start", "Start an Xvfb background desktop");
    background_desktop_start_help->add_option("--display", background_desktop_start_options.display, "X11 display such as :99");
    background_desktop_start_help->add_option("--state-dir", background_desktop_start_options.state_directory, "Directory for background desktop state files");
    background_desktop_start_help->add_option("--width", background_desktop_start_options.width, "Screen width");
    background_desktop_start_help->add_option("--height", background_desktop_start_options.height, "Screen height");
    background_desktop_start_help->add_option("--depth", background_desktop_start_options.depth, "Screen depth");
    auto* background_desktop_stop_help = background_desktop_alias->add_subcommand("stop", "Stop an Xvfb background desktop started by Kiseki");
    background_desktop_stop_help->add_option("--display", background_desktop_stop_options.display, "X11 display such as :99");
    background_desktop_stop_help->add_option("--state-dir", background_desktop_stop_options.state_directory, "Directory for background desktop state files");
    auto* background_desktop_launch_help = background_desktop_alias->add_subcommand("launch", "Launch a command inside the background desktop");
    background_desktop_launch_help->add_option("--display", background_desktop_launch_options.display, "X11 display such as :99");
    background_desktop_launch_help->add_option("--command", background_desktop_launch_options.command, "Shell command to launch")->required();
    auto* background_desktop_screenshot_help = background_desktop_alias->add_subcommand("screenshot", "Capture the background desktop to a BMP file");
    background_desktop_screenshot_help->add_option("--display", background_desktop_screenshot_options.display, "X11 display such as :99");
    background_desktop_screenshot_help->add_option("-o,--output", background_desktop_screenshot_options.output_path, "Output BMP path")->required();
    auto* background_desktop_text_help = background_desktop_alias->add_subcommand("text", "Type text into the background desktop");
    background_desktop_text_help->add_option("--display", background_desktop_text_options.display, "X11 display such as :99");
    background_desktop_text_help->add_option("--text", background_desktop_text_options.text, "Text to type");
    background_desktop_text_help->add_option("--file", background_desktop_text_options.text_file, "UTF-8 text file to type");
    auto* background_desktop_key_help = background_desktop_alias->add_subcommand("key", "Tap a key in the background desktop");
    background_desktop_key_help->add_option("--display", background_desktop_key_options.display, "X11 display such as :99");
    background_desktop_key_help->add_option("--key", background_desktop_key_options.key, "Key name")->required();
    auto* background_desktop_mouse_help = background_desktop_alias->add_subcommand("mouse", "Move and optionally click in the background desktop");
    background_desktop_mouse_help->add_option("--display", background_desktop_mouse_options.display, "X11 display such as :99");
    background_desktop_mouse_help->add_option("--x", background_desktop_mouse_options.x, "Background desktop X coordinate")->required();
    background_desktop_mouse_help->add_option("--y", background_desktop_mouse_options.y, "Background desktop Y coordinate")->required();
    background_desktop_mouse_help->add_option("--click", background_desktop_mouse_options.click, "none, left, right, middle, left-down, left-up, right-down, right-up, middle-down, or middle-up");

    auto* background_cua_alias = background->add_subcommand("cua", "Cua Driver target-routed background operations");
    background_cua_alias->require_subcommand(1);
    auto* background_cua_status_help = background_cua_alias->add_subcommand("status", "Check Cua Driver permissions and availability");
    background_cua_status_help->add_flag("--prompt", mac_background_status_options.prompt, "Request missing Accessibility and Screen Recording permissions");
    auto* background_cua_launch_help = background_cua_alias->add_subcommand("launch", "Launch an app through Cua Driver");
    background_cua_launch_help->add_option("--bundle-id", mac_background_launch_options.bundle_id, "Application bundle id when supported, such as com.apple.Safari");
    background_cua_launch_help->add_option("--name", mac_background_launch_options.name, "Application display name when bundle id is unknown");
    background_cua_launch_help->add_option("--url", mac_background_launch_options.urls, "URL or file path to hand to the app; repeat for multiple values");
    background_cua_launch_help->add_flag("--new-instance", mac_background_launch_options.new_instance, "Ask Cua Driver to create a new app instance when supported");
    background_cua_launch_help->add_option("--arg", mac_background_launch_options.arguments, "Additional argv entry for the launched process; repeat for multiple values");
    auto* background_cua_windows_help = background_cua_alias->add_subcommand("windows", "List Cua Driver target windows");
    background_cua_windows_help->add_option("--pid", mac_background_windows_options.pid, "Restrict windows to a process id");
    background_cua_windows_help->add_flag("--on-screen-only", mac_background_windows_options.on_screen_only, "Drop off-screen, minimized, or off-Space windows");
    auto* background_cua_state_help = background_cua_alias->add_subcommand("state", "Read a Cua Driver window snapshot and optionally write a screenshot");
    background_cua_state_help->add_option("--pid", mac_background_state_options.pid, "Target process id")->required();
    background_cua_state_help->add_option("--window-id", mac_background_state_options.window_id, "Target Cua Driver window id")->required();
    background_cua_state_help->add_option("-o,--output", mac_background_state_options.output_path, "Optional screenshot output path");
    background_cua_state_help->add_option("--query", mac_background_state_options.query, "Optional case-insensitive AX tree filter");
    auto* background_cua_screenshot_help = background_cua_alias->add_subcommand("screenshot", "Capture a Cua Driver target window to an image file");
    background_cua_screenshot_help->add_option("--window-id", mac_background_screenshot_options.window_id, "Target Cua Driver window id")->required();
    background_cua_screenshot_help->add_option("-o,--output", mac_background_screenshot_options.output_path, "Output image path")->required();
    background_cua_screenshot_help->add_option("--format", mac_background_screenshot_options.format, "png or jpeg");
    background_cua_screenshot_help->add_option("--quality", mac_background_screenshot_options.quality, "JPEG quality 1-95");
    auto* background_cua_click_help = background_cua_alias->add_subcommand("click", "Click a Cua Driver target pid by element index or window-local pixels");
    background_cua_click_help->add_option("--pid", mac_background_click_options.pid, "Target process id")->required();
    background_cua_click_help->add_option("--window-id", mac_background_click_options.window_id, "Target Cua Driver window id");
    background_cua_click_help->add_option("--element-index", mac_background_click_options.element_index, "Element index from the last state call");
    background_cua_click_help->add_option("--x", mac_background_click_options.x, "Window-local screenshot pixel X");
    background_cua_click_help->add_option("--y", mac_background_click_options.y, "Window-local screenshot pixel Y");
    background_cua_click_help->add_option("--button", mac_background_click_options.button, "left, right, or double");
    background_cua_click_help->add_option("--modifiers", mac_background_click_modifiers, "Comma or plus separated modifier keys");
    auto* background_cua_text_help = background_cua_alias->add_subcommand("text", "Type text into a Cua Driver target pid");
    background_cua_text_help->add_option("--pid", mac_background_text_options.pid, "Target process id")->required();
    background_cua_text_help->add_option("--text", mac_background_text_options.text, "Text to type");
    background_cua_text_help->add_option("--file", mac_background_text_options.text_file, "UTF-8 text file to type");
    background_cua_text_help->add_option("--window-id", mac_background_text_options.window_id, "Target Cua Driver window id");
    background_cua_text_help->add_option("--element-index", mac_background_text_options.element_index, "Element index from the last state call");
    background_cua_text_help->add_option("--delay-ms", mac_background_text_options.delay_ms, "Character delay for CGEvent fallback");
    auto* background_cua_key_help = background_cua_alias->add_subcommand("key", "Press a key in a Cua Driver target pid");
    background_cua_key_help->add_option("--pid", mac_background_key_options.pid, "Target process id")->required();
    background_cua_key_help->add_option("--key", mac_background_key_options.key, "Key name")->required();
    background_cua_key_help->add_option("--window-id", mac_background_key_options.window_id, "Target Cua Driver window id");
    background_cua_key_help->add_option("--element-index", mac_background_key_options.element_index, "Element index from the last state call");
    background_cua_key_help->add_option("--modifiers", mac_background_key_modifiers, "Comma or plus separated modifier keys");
    auto* background_cua_hotkey_help = background_cua_alias->add_subcommand("hotkey", "Press a key combination in a Cua Driver target pid");
    background_cua_hotkey_help->add_option("--pid", mac_background_hotkey_options.pid, "Target process id")->required();
    background_cua_hotkey_help->add_option("--keys", mac_background_hotkey_keys, "Comma or plus separated key combo, such as cmd+c")->required();
    background_cua_hotkey_help->add_option("--window-id", mac_background_hotkey_options.window_id, "Target Cua Driver window id");
    auto* background_cua_drag_help = background_cua_alias->add_subcommand("drag", "Drag inside a Cua Driver target window");
    background_cua_drag_help->add_option("--pid", mac_background_drag_options.pid, "Target process id")->required();
    background_cua_drag_help->add_option("--window-id", mac_background_drag_options.window_id, "Target Cua Driver window id");
    background_cua_drag_help->add_option("--from-x", mac_background_drag_options.from_x, "Drag start X")->required();
    background_cua_drag_help->add_option("--from-y", mac_background_drag_options.from_y, "Drag start Y")->required();
    background_cua_drag_help->add_option("--to-x", mac_background_drag_options.to_x, "Drag end X")->required();
    background_cua_drag_help->add_option("--to-y", mac_background_drag_options.to_y, "Drag end Y")->required();
    background_cua_drag_help->add_option("--duration-ms", mac_background_drag_options.duration_ms, "Drag duration in milliseconds");
    background_cua_drag_help->add_option("--steps", mac_background_drag_options.steps, "Number of drag interpolation steps");
    background_cua_drag_help->add_option("--button", mac_background_drag_options.button, "left, right, or middle");
    background_cua_drag_help->add_option("--modifiers", mac_background_drag_modifiers, "Comma or plus separated modifier keys");
    auto* background_cua_draw_help = background_cua_alias->add_subcommand("draw", "Draw a point path inside a Cua Driver target window");
    background_cua_draw_help->add_option("--pid", mac_background_draw_options.pid, "Target process id")->required();
    background_cua_draw_help->add_option("--window-id", mac_background_draw_options.window_id, "Target Cua Driver window id")->required();
    background_cua_draw_help->add_option("--file", mac_background_draw_options.path, "Plain text point path file: one 'x y' point per line")->required();
    background_cua_draw_help->add_option("--duration-ms", mac_background_draw_options.duration_ms, "Duration for each drag segment in milliseconds");
    background_cua_draw_help->add_option("--steps", mac_background_draw_options.steps, "Interpolation steps for each drag segment");
    background_cua_draw_help->add_option("--stroke-gap-ms", mac_background_draw_options.stroke_gap_ms, "Delay between drag segments in milliseconds");
    background_cua_draw_help->add_option("--max-segments", mac_background_draw_options.max_segments, "Maximum CUA drag segments accepted from the point file");
    background_cua_draw_help->add_option("--button", mac_background_draw_options.button, "left, right, or middle");
    background_cua_draw_help->add_option("--modifiers", mac_background_draw_modifiers, "Comma or plus separated modifier keys");
    auto* background_cua_feedback_alias = background_cua_alias->add_subcommand("feedback", "Cua Driver visual agent-cursor feedback");
    background_cua_feedback_alias->require_subcommand(1);
    background_cua_feedback_alias->add_subcommand("status", "Print the current Cua Driver agent-cursor state");
    auto* background_cua_feedback_enable_help = background_cua_feedback_alias->add_subcommand("enable", "Enable or disable the Cua Driver visual agent cursor");
    background_cua_feedback_enable_help->add_option("--enabled", mac_background_feedback_enable_options.enabled, "true to show the overlay cursor, false to hide it")->required();
    auto* background_cua_feedback_motion_help = background_cua_feedback_alias->add_subcommand("motion", "Tune Cua Driver agent-cursor motion knobs");
    background_cua_feedback_motion_help->add_option("--start-handle", mac_background_feedback_motion_options.start_handle, "Bezier start handle fraction");
    background_cua_feedback_motion_help->add_option("--end-handle", mac_background_feedback_motion_options.end_handle, "Bezier end handle fraction");
    background_cua_feedback_motion_help->add_option("--arc-size", mac_background_feedback_motion_options.arc_size, "Perpendicular arc deflection fraction");
    background_cua_feedback_motion_help->add_option("--arc-flow", mac_background_feedback_motion_options.arc_flow, "Arc asymmetry bias");
    background_cua_feedback_motion_help->add_option("--spring", mac_background_feedback_motion_options.spring, "Settle damping");
    background_cua_feedback_motion_help->add_option("--glide-duration-ms", mac_background_feedback_motion_options.glide_duration_ms, "Cursor flight duration");
    background_cua_feedback_motion_help->add_option("--dwell-after-click-ms", mac_background_feedback_motion_options.dwell_after_click_ms, "Pause after click ripple");
    background_cua_feedback_motion_help->add_option("--idle-hide-ms", mac_background_feedback_motion_options.idle_hide_ms, "Overlay linger time after last action");
    auto* background_cua_feedback_style_help = background_cua_feedback_alias->add_subcommand("style", "Tune Cua Driver agent-cursor colors or custom image");
    background_cua_feedback_style_help->add_flag("--reset", mac_background_feedback_style_options.reset, "Reset agent-cursor style to Cua Driver defaults");
    background_cua_feedback_style_help->add_option("--gradient-colors", mac_background_feedback_style_gradient_colors, "Comma or plus separated CSS hex colors");
    background_cua_feedback_style_help->add_option("--bloom-color", mac_background_feedback_style_options.bloom_color, "CSS hex color for halo and focus rect");
    background_cua_feedback_style_help->add_option("--image-path", mac_background_feedback_style_options.image_path, "Path to PNG/JPEG/PDF/SVG cursor image, or empty path to clear");
    auto* background_cua_feedback_preset_help = background_cua_feedback_alias->add_subcommand("preset", "Apply a named Cua Driver agent-cursor feedback preset");
    background_cua_feedback_preset_help->add_option("--name", mac_background_feedback_preset_options.name, "natural, fast, recording, or quiet");

    auto* mac_background = app.add_subcommand("cua-background", "Operate target apps through optional Cua Driver");
    mac_background->group("");
    mac_background->require_subcommand(1);

    auto* mac_background_status = mac_background->add_subcommand("status", "Check Cua Driver permissions and availability");
    mac_background_status->add_flag("--prompt", mac_background_status_options.prompt, "Request missing Accessibility and Screen Recording permissions");
    mac_background_status->callback([&]() {
        if (!dependencies.mac_background_status) {
            io.err << "background cua status backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_status(mac_background_status_options, io);
    });

    auto* mac_background_launch = mac_background->add_subcommand("launch", "Launch an app through Cua Driver");
    mac_background_launch->add_option("--bundle-id", mac_background_launch_options.bundle_id, "Application bundle id when supported, such as com.apple.Safari");
    mac_background_launch->add_option("--name", mac_background_launch_options.name, "Application display name when bundle id is unknown");
    mac_background_launch->add_option("--url", mac_background_launch_options.urls, "URL or file path to hand to the app; repeat for multiple values");
    mac_background_launch->add_flag("--new-instance", mac_background_launch_options.new_instance, "Ask Cua Driver to create a new app instance when supported");
    mac_background_launch->add_option("--arg", mac_background_launch_options.arguments, "Additional argv entry for the launched process; repeat for multiple values");
    mac_background_launch->callback([&]() {
        if (!dependencies.mac_background_launch) {
            io.err << "background cua launch backend is not configured\n";
            exit_code = 2;
            return;
        }
        if (mac_background_launch_options.bundle_id.empty() && mac_background_launch_options.name.empty()) {
            io.err << "background cua launch requires --bundle-id or --name\n";
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
            io.err << "background cua windows backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_windows_options.has_pid = mac_background_windows_pid->count() > 0;
        exit_code = dependencies.mac_background_windows(mac_background_windows_options, io);
    });

    auto* mac_background_state = mac_background->add_subcommand("state", "Read a Cua Driver window snapshot and optionally write a screenshot");
    mac_background_state->add_option("--pid", mac_background_state_options.pid, "Target process id")->required();
    mac_background_state->add_option("--window-id", mac_background_state_options.window_id, "Target Cua Driver window id")->required();
    mac_background_state->add_option("-o,--output", mac_background_state_options.output_path, "Optional screenshot output path");
    mac_background_state->add_option("--query", mac_background_state_options.query, "Optional case-insensitive AX tree filter");
    mac_background_state->callback([&]() {
        if (!dependencies.mac_background_state) {
            io.err << "background cua state backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_state(mac_background_state_options, io);
    });

    auto* mac_background_screenshot = mac_background->add_subcommand("screenshot", "Capture a Cua Driver target window to an image file");
    mac_background_screenshot->add_option("--window-id", mac_background_screenshot_options.window_id, "Target Cua Driver window id")->required();
    mac_background_screenshot->add_option("-o,--output", mac_background_screenshot_options.output_path, "Output image path")->required();
    mac_background_screenshot->add_option("--format", mac_background_screenshot_options.format, "png or jpeg");
    mac_background_screenshot->add_option("--quality", mac_background_screenshot_options.quality, "JPEG quality 1-95");
    mac_background_screenshot->callback([&]() {
        if (!dependencies.mac_background_screenshot) {
            io.err << "background cua screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_screenshot(mac_background_screenshot_options, io);
    });

    auto* mac_background_click = mac_background->add_subcommand("click", "Click a Cua Driver target pid by element index or window-local pixels");
    mac_background_click->add_option("--pid", mac_background_click_options.pid, "Target process id")->required();
    auto* mac_background_click_window_id = mac_background_click->add_option("--window-id", mac_background_click_options.window_id, "Target Cua Driver window id");
    auto* mac_background_click_element = mac_background_click->add_option("--element-index", mac_background_click_options.element_index, "Element index from the last state call");
    auto* mac_background_click_x = mac_background_click->add_option("--x", mac_background_click_options.x, "Window-local screenshot pixel X");
    auto* mac_background_click_y = mac_background_click->add_option("--y", mac_background_click_options.y, "Window-local screenshot pixel Y");
    mac_background_click->add_option("--button", mac_background_click_options.button, "left, right, or double");
    mac_background_click->add_option("--modifiers", mac_background_click_modifiers, "Comma or plus separated modifier keys");
    mac_background_click->callback([&]() {
        if (!dependencies.mac_background_click) {
            io.err << "background cua click backend is not configured\n";
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
            io.err << "background cua click requires both --x and --y\n";
            exit_code = 2;
            return;
        }
        if (mac_background_click_options.has_element_index == mac_background_click_options.has_xy) {
            io.err << "background cua click requires either --element-index or both --x and --y\n";
            exit_code = 2;
            return;
        }
        if (mac_background_click_options.has_element_index && !mac_background_click_options.has_window_id) {
            io.err << "background cua click with --element-index requires --window-id\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_click(mac_background_click_options, io);
    });

    auto* mac_background_text = mac_background->add_subcommand("text", "Type text into a Cua Driver target pid");
    mac_background_text->add_option("--pid", mac_background_text_options.pid, "Target process id")->required();
    mac_background_text->add_option("--text", mac_background_text_options.text, "Text to type");
    mac_background_text->add_option("--file", mac_background_text_options.text_file, "UTF-8 text file to type");
    auto* mac_background_text_window_id = mac_background_text->add_option("--window-id", mac_background_text_options.window_id, "Target Cua Driver window id");
    auto* mac_background_text_element = mac_background_text->add_option("--element-index", mac_background_text_options.element_index, "Element index from the last state call");
    mac_background_text->add_option("--delay-ms", mac_background_text_options.delay_ms, "Character delay for CGEvent fallback");
    mac_background_text->callback([&]() {
        if (!dependencies.mac_background_text) {
            io.err << "background cua text backend is not configured\n";
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
            io.err << "background cua text requires --text or --file\n";
            exit_code = 2;
            return;
        }
        mac_background_text_options.has_window_id = mac_background_text_window_id->count() > 0;
        mac_background_text_options.has_element_index = mac_background_text_element->count() > 0;
        if (mac_background_text_options.has_element_index && !mac_background_text_options.has_window_id) {
            io.err << "background cua text with --element-index requires --window-id\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_text(mac_background_text_options, io);
    });

    auto* mac_background_key = mac_background->add_subcommand("key", "Press a key in a Cua Driver target pid");
    mac_background_key->add_option("--pid", mac_background_key_options.pid, "Target process id")->required();
    mac_background_key->add_option("--key", mac_background_key_options.key, "Key name")->required();
    auto* mac_background_key_window_id = mac_background_key->add_option("--window-id", mac_background_key_options.window_id, "Target Cua Driver window id");
    auto* mac_background_key_element = mac_background_key->add_option("--element-index", mac_background_key_options.element_index, "Element index from the last state call");
    mac_background_key->add_option("--modifiers", mac_background_key_modifiers, "Comma or plus separated modifier keys");
    mac_background_key->callback([&]() {
        if (!dependencies.mac_background_key) {
            io.err << "background cua key backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_key_options.has_window_id = mac_background_key_window_id->count() > 0;
        mac_background_key_options.has_element_index = mac_background_key_element->count() > 0;
        mac_background_key_options.modifiers = split_delimited_values(mac_background_key_modifiers);
        if (mac_background_key_options.has_element_index && !mac_background_key_options.has_window_id) {
            io.err << "background cua key with --element-index requires --window-id\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_key(mac_background_key_options, io);
    });

    auto* mac_background_hotkey = mac_background->add_subcommand("hotkey", "Press a key combination in a Cua Driver target pid");
    mac_background_hotkey->add_option("--pid", mac_background_hotkey_options.pid, "Target process id")->required();
    mac_background_hotkey->add_option("--keys", mac_background_hotkey_keys, "Comma or plus separated key combo, such as cmd+c")->required();
    auto* mac_background_hotkey_window_id = mac_background_hotkey->add_option("--window-id", mac_background_hotkey_options.window_id, "Target Cua Driver window id");
    mac_background_hotkey->callback([&]() {
        if (!dependencies.mac_background_hotkey) {
            io.err << "background cua hotkey backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_hotkey_options.keys = split_delimited_values(mac_background_hotkey_keys);
        mac_background_hotkey_options.has_window_id = mac_background_hotkey_window_id->count() > 0;
        if (mac_background_hotkey_options.keys.size() < 2) {
            io.err << "background cua hotkey requires at least two keys in --keys\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_hotkey(mac_background_hotkey_options, io);
    });

    auto* mac_background_drag = mac_background->add_subcommand("drag", "Drag inside a Cua Driver target window");
    mac_background_drag->add_option("--pid", mac_background_drag_options.pid, "Target process id")->required();
    auto* mac_background_drag_window_id = mac_background_drag->add_option("--window-id", mac_background_drag_options.window_id, "Target Cua Driver window id");
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
            io.err << "background cua drag backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_drag_options.has_window_id = mac_background_drag_window_id->count() > 0;
        mac_background_drag_options.modifiers = split_delimited_values(mac_background_drag_modifiers);
        exit_code = dependencies.mac_background_drag(mac_background_drag_options, io);
    });

    auto* mac_background_draw = mac_background->add_subcommand("draw", "Draw a point path inside a Cua Driver target window");
    mac_background_draw->add_option("--pid", mac_background_draw_options.pid, "Target process id")->required();
    mac_background_draw->add_option("--window-id", mac_background_draw_options.window_id, "Target Cua Driver window id")->required();
    mac_background_draw->add_option("--file", mac_background_draw_options.path, "Plain text point path file: one 'x y' point per line")->required();
    mac_background_draw->add_option("--duration-ms", mac_background_draw_options.duration_ms, "Duration for each drag segment in milliseconds");
    mac_background_draw->add_option("--steps", mac_background_draw_options.steps, "Interpolation steps for each drag segment");
    mac_background_draw->add_option("--stroke-gap-ms", mac_background_draw_options.stroke_gap_ms, "Delay between drag segments in milliseconds");
    mac_background_draw->add_option("--max-segments", mac_background_draw_options.max_segments, "Maximum CUA drag segments accepted from the point file");
    mac_background_draw->add_option("--button", mac_background_draw_options.button, "left, right, or middle");
    mac_background_draw->add_option("--modifiers", mac_background_draw_modifiers, "Comma or plus separated modifier keys");
    mac_background_draw->callback([&]() {
        if (!dependencies.mac_background_draw) {
            io.err << "background cua draw backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_draw_options.modifiers = split_delimited_values(mac_background_draw_modifiers);
        exit_code = dependencies.mac_background_draw(mac_background_draw_options, io);
    });

    auto* mac_background_feedback = mac_background->add_subcommand("feedback", "Tune Cua Driver's visual agent-cursor feedback");
    mac_background_feedback->require_subcommand(1);

    auto* mac_background_feedback_status = mac_background_feedback->add_subcommand("status", "Print the current Cua Driver agent-cursor state");
    mac_background_feedback_status->callback([&]() {
        if (!dependencies.mac_background_feedback_status) {
            io.err << "background cua feedback status backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_feedback_status(mac_background_feedback_status_options, io);
    });

    auto* mac_background_feedback_enable = mac_background_feedback->add_subcommand("enable", "Enable or disable the Cua Driver visual agent cursor");
    mac_background_feedback_enable->add_option("--enabled", mac_background_feedback_enable_options.enabled, "true to show the overlay cursor, false to hide it")->required();
    mac_background_feedback_enable->callback([&]() {
        if (!dependencies.mac_background_feedback_enable) {
            io.err << "background cua feedback enable backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_feedback_enable(mac_background_feedback_enable_options, io);
    });

    auto* mac_background_feedback_motion = mac_background_feedback->add_subcommand("motion", "Tune Cua Driver agent-cursor motion knobs");
    auto* mac_background_feedback_motion_start = mac_background_feedback_motion->add_option("--start-handle", mac_background_feedback_motion_options.start_handle, "Bezier start handle fraction");
    auto* mac_background_feedback_motion_end = mac_background_feedback_motion->add_option("--end-handle", mac_background_feedback_motion_options.end_handle, "Bezier end handle fraction");
    auto* mac_background_feedback_motion_arc_size = mac_background_feedback_motion->add_option("--arc-size", mac_background_feedback_motion_options.arc_size, "Perpendicular arc deflection fraction");
    auto* mac_background_feedback_motion_arc_flow = mac_background_feedback_motion->add_option("--arc-flow", mac_background_feedback_motion_options.arc_flow, "Arc asymmetry bias");
    auto* mac_background_feedback_motion_spring = mac_background_feedback_motion->add_option("--spring", mac_background_feedback_motion_options.spring, "Settle damping");
    auto* mac_background_feedback_motion_glide = mac_background_feedback_motion->add_option("--glide-duration-ms", mac_background_feedback_motion_options.glide_duration_ms, "Cursor flight duration");
    auto* mac_background_feedback_motion_dwell = mac_background_feedback_motion->add_option("--dwell-after-click-ms", mac_background_feedback_motion_options.dwell_after_click_ms, "Pause after click ripple");
    auto* mac_background_feedback_motion_idle = mac_background_feedback_motion->add_option("--idle-hide-ms", mac_background_feedback_motion_options.idle_hide_ms, "Overlay linger time after last action");
    mac_background_feedback_motion->callback([&]() {
        if (!dependencies.mac_background_feedback_motion) {
            io.err << "background cua feedback motion backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_feedback_motion_options.has_start_handle = mac_background_feedback_motion_start->count() > 0;
        mac_background_feedback_motion_options.has_end_handle = mac_background_feedback_motion_end->count() > 0;
        mac_background_feedback_motion_options.has_arc_size = mac_background_feedback_motion_arc_size->count() > 0;
        mac_background_feedback_motion_options.has_arc_flow = mac_background_feedback_motion_arc_flow->count() > 0;
        mac_background_feedback_motion_options.has_spring = mac_background_feedback_motion_spring->count() > 0;
        mac_background_feedback_motion_options.has_glide_duration_ms = mac_background_feedback_motion_glide->count() > 0;
        mac_background_feedback_motion_options.has_dwell_after_click_ms = mac_background_feedback_motion_dwell->count() > 0;
        mac_background_feedback_motion_options.has_idle_hide_ms = mac_background_feedback_motion_idle->count() > 0;
        if (!(mac_background_feedback_motion_options.has_start_handle ||
              mac_background_feedback_motion_options.has_end_handle ||
              mac_background_feedback_motion_options.has_arc_size ||
              mac_background_feedback_motion_options.has_arc_flow ||
              mac_background_feedback_motion_options.has_spring ||
              mac_background_feedback_motion_options.has_glide_duration_ms ||
              mac_background_feedback_motion_options.has_dwell_after_click_ms ||
              mac_background_feedback_motion_options.has_idle_hide_ms)) {
            io.err << "background cua feedback motion requires at least one motion option\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_feedback_motion(mac_background_feedback_motion_options, io);
    });

    auto* mac_background_feedback_style = mac_background_feedback->add_subcommand("style", "Tune Cua Driver agent-cursor colors or custom image");
    mac_background_feedback_style->add_flag("--reset", mac_background_feedback_style_options.reset, "Reset agent-cursor style to Cua Driver defaults");
    auto* mac_background_feedback_style_gradient = mac_background_feedback_style->add_option("--gradient-colors", mac_background_feedback_style_gradient_colors, "Comma or plus separated CSS hex colors");
    auto* mac_background_feedback_style_bloom = mac_background_feedback_style->add_option("--bloom-color", mac_background_feedback_style_options.bloom_color, "CSS hex color for halo and focus rect");
    auto* mac_background_feedback_style_image = mac_background_feedback_style->add_option("--image-path", mac_background_feedback_style_options.image_path, "Path to PNG/JPEG/PDF/SVG cursor image, or empty path to clear");
    mac_background_feedback_style->callback([&]() {
        if (!dependencies.mac_background_feedback_style) {
            io.err << "background cua feedback style backend is not configured\n";
            exit_code = 2;
            return;
        }
        mac_background_feedback_style_options.has_gradient_colors = mac_background_feedback_style_gradient->count() > 0;
        mac_background_feedback_style_options.gradient_colors = split_delimited_values(mac_background_feedback_style_gradient_colors);
        mac_background_feedback_style_options.has_bloom_color = mac_background_feedback_style_bloom->count() > 0;
        mac_background_feedback_style_options.has_image_path = mac_background_feedback_style_image->count() > 0;
        if (!(mac_background_feedback_style_options.reset ||
              mac_background_feedback_style_options.has_gradient_colors ||
              mac_background_feedback_style_options.has_bloom_color ||
              mac_background_feedback_style_options.has_image_path)) {
            io.err << "background cua feedback style requires --reset or at least one style option\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_feedback_style(mac_background_feedback_style_options, io);
    });

    auto* mac_background_feedback_preset = mac_background_feedback->add_subcommand("preset", "Apply a named Cua Driver agent-cursor feedback preset");
    mac_background_feedback_preset->add_option("--name", mac_background_feedback_preset_options.name, "natural, fast, recording, or quiet");
    mac_background_feedback_preset->callback([&]() {
        if (!dependencies.mac_background_feedback_preset) {
            io.err << "background cua feedback preset backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.mac_background_feedback_preset(mac_background_feedback_preset_options, io);
    });

    auto* permissions = app.add_subcommand("permissions", "Platform permission helper commands");
    permissions->require_subcommand(1);
    auto* mac_permissions = permissions->add_subcommand("macos", "macOS Screen Recording and Accessibility permission helpers");
    mac_permissions->require_subcommand(1);

    auto* mac_screen_recording = mac_permissions->add_subcommand("screen-recording", "Check or request macOS Screen Recording permission for the current CLI host");
    mac_screen_recording->add_flag("--prompt", mac_screen_recording_options.prompt, "Ask macOS to show the Screen Recording permission prompt when possible");
    mac_screen_recording->add_flag("--open-settings", mac_screen_recording_options.open_settings, "Open the Screen Recording settings pane if permission is missing");
    mac_screen_recording->callback([&]() {
        if (!dependencies.macos_screen_recording_permission) {
            io.err << "macOS Screen Recording permission backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.macos_screen_recording_permission(mac_screen_recording_options, io);
    });

    auto* mac_accessibility = mac_permissions->add_subcommand("accessibility", "Check or request macOS Accessibility permission for the current CLI host");
    mac_accessibility->add_flag("--prompt", mac_accessibility_options.prompt, "Ask macOS to show the Accessibility permission prompt when possible");
    mac_accessibility->add_flag("--open-settings", mac_accessibility_options.open_settings, "Open the Accessibility settings pane if permission is missing");
    mac_accessibility->callback([&]() {
        if (!dependencies.macos_accessibility_permission) {
            io.err << "macOS Accessibility permission backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.macos_accessibility_permission(mac_accessibility_options, io);
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

    auto* teach = app.add_subcommand("teach", "Teaching recording commands");
    teach->require_subcommand(1);

    auto* teach_record = teach->add_subcommand("record", "Toggle an Agivar-style teaching recording; run again to stop");
    teach_record->add_option("-o,--output", teach_record_options.output_directory, "Output teaching bundle directory; defaults to artifacts/teach/<timestamp>");
    teach_record->add_option("--duration-ms", teach_record_options.duration_ms, "Optional maximum recording duration in milliseconds; 0 records until stopped");
    teach_record->add_option("--frame-interval-ms", teach_record_options.frame_interval_ms, "Keyframe interval in milliseconds");
    teach_record->add_option("--event-poll-ms", teach_record_options.event_poll_ms, "Native input event polling interval in milliseconds");
    teach_record->add_option("--stop-timeout-ms", teach_record_options.stop_timeout_ms, "Milliseconds to wait for a stopped background recording to finalize");
    teach_record->add_option("--video-keyframe-interval-ms", teach_record_options.video_keyframe_interval_ms, "Minimum spacing for extracted video review keyframes");
    teach_record->add_option("--video-keyframe-max", teach_record_options.video_keyframe_max, "Maximum extracted video review keyframes");
    teach_record->add_flag("--no-video-keyframes", teach_record_options.no_video_keyframes, "Do not extract review keyframes from --video-file");
    teach_record->add_option("--title", teach_record_options.title, "Teaching title");
    teach_record->add_option("--text", teach_record_options.text, "Human teaching text");
    teach_record->add_option("--text-file", teach_record_options.text_file, "UTF-8 human teaching text file");
    teach_record->add_option("--video-file", teach_record_options.video_file, "Optional recorded video file for human review");
    teach_record->add_option("--audio-file", teach_record_options.audio_file, "Optional real audio file for human review or transcription");
    teach_record->add_option("--transcript-file", teach_record_options.transcript_file, "Optional existing transcript JSON/text file");
    teach_record->add_flag("--worker", teach_record_options.worker, "Internal teach recording worker process")->group("");
    teach_record->add_option("--state-file", teach_record_options.state_file, "Internal active recording state file")->group("");
    teach_record->add_option("--stop-file", teach_record_options.stop_file, "Internal recording stop request file")->group("");
    teach_record->callback([&]() {
        if (!dependencies.teach_record) {
            io.err << "teach record backend is not configured\n";
            exit_code = 2;
            return;
        }
        if (!teach_record_options.text_file.empty()) {
            try {
                teach_record_options.text = read_text_file(teach_record_options.text_file);
            } catch (const std::exception& error) {
                io.err << error.what() << '\n';
                exit_code = 2;
                return;
            }
        }
        exit_code = dependencies.teach_record(teach_record_options, io);
    });

    auto* teach_annotate = teach->add_subcommand("annotate", "Attach human guidance to a recorded keyframe or action");
    teach_annotate->add_option("--session", teach_annotate_options.session_directory, "Teaching bundle directory")->required();
    auto* teach_annotate_frame = teach_annotate->add_option("--frame-index", teach_annotate_options.frame_index, "Keyframe index");
    auto* teach_annotate_event = teach_annotate->add_option("--event-index", teach_annotate_options.event_index, "Event index");
    teach_annotate->add_option("--text", teach_annotate_options.text, "Annotation text");
    teach_annotate->add_option("--file", teach_annotate_options.text_file, "UTF-8 annotation text file");
    teach_annotate->callback([&]() {
        if (!dependencies.teach_annotate) {
            io.err << "teach annotate backend is not configured\n";
            exit_code = 2;
            return;
        }
        teach_annotate_options.has_frame_index = teach_annotate_frame->count() > 0;
        teach_annotate_options.has_event_index = teach_annotate_event->count() > 0;
        if (!teach_annotate_options.text_file.empty()) {
            try {
                teach_annotate_options.text = read_text_file(teach_annotate_options.text_file);
            } catch (const std::exception& error) {
                io.err << error.what() << '\n';
                exit_code = 2;
                return;
            }
        }
        exit_code = dependencies.teach_annotate(teach_annotate_options, io);
    });

    auto* teach_transcribe = teach->add_subcommand("transcribe", "Transcribe a real audio file with a local faster-whisper model, downloading it if missing");
    teach_transcribe->add_option("--audio-file", teach_transcribe_options.audio_file, "WAV or other faster-whisper-readable audio file")->required();
    teach_transcribe->add_option("-o,--output", teach_transcribe_options.output_path, "Transcript JSON output path")->required();
    teach_transcribe->add_option("--model", teach_transcribe_options.model_path, "Local faster-whisper model directory; downloaded here if missing");
    teach_transcribe->add_option("--model-id", teach_transcribe_options.model_id, "Hugging Face model id used when the local model is missing");
    teach_transcribe->add_option("--script", teach_transcribe_options.script_path, "Python helper script path");
    teach_transcribe->add_option("--language", teach_transcribe_options.language, "Optional language code");
    teach_transcribe->add_option("--device", teach_transcribe_options.device, "faster-whisper device, such as auto, cpu, or cuda");
    teach_transcribe->add_option("--compute-type", teach_transcribe_options.compute_type, "faster-whisper compute type, such as auto, int8, or float16");
    teach_transcribe->callback([&]() {
        if (!dependencies.teach_transcribe) {
            io.err << "teach transcribe backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.teach_transcribe(teach_transcribe_options, io);
    });

    auto* modes = app.add_subcommand("modes", "Print operation and screenshot mode selection guide");
    modes->add_flag("--json", modes_json, "Print machine-readable mode guide");
    modes->callback([&]() {
        if (modes_json) {
            io.out << operation_modes_json().dump(2) << '\n';
            return;
        }
        print_operation_modes(io);
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
        io.out << "  CUA background operation: " << availability(capabilities.session.cua_background) << '\n';
        io.out << "  macOS CUA wrapper: " << availability(capabilities.session.macos_cua_background) << '\n';
        io.out << "Mode split:\n";
        io.out << "  Current-session input: kiseki input ...\n";
        io.out << "  Current-session screenshots: kiseki screenshot desktop|burst|window|window-burst ...\n";
        io.out << "  Background screenshots: kiseki background window screenshot; background desktop screenshot; background cua screenshot/state --output\n";
        io.out << "  Machine-readable guide: kiseki modes --json\n";

        io.out << "Limitations:\n";
        for (const auto& limitation : capabilities.limitations) {
            io.out << "  - " << limitation << '\n';
        }
    });

    if (const auto removed_message = removed_background_command_message(args); !removed_message.empty()) {
        io.err << removed_message << '\n';
        return 2;
    }

    const auto normalized_args = normalize_integrated_commands(args);
    std::vector<std::string> parse_args;
    parse_args.reserve(normalized_args.size() + 1);
    parse_args.emplace_back("kiseki");
    parse_args.insert(parse_args.end(), normalized_args.begin(), normalized_args.end());

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
