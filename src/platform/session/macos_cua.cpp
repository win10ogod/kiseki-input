#include "platform/session/macos_cua.hpp"
#include "core/utf8_path.hpp"
#include "platform/session/cua_process.hpp"
#include "platform/session/cua_runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
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

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool mentions(const std::string& text, const std::string& needle) {
    return lowercase(text).find(lowercase(needle)) != std::string::npos;
}

// "Unknown tool: NAME" (exit 64) from cua-driver: nothing was executed.
bool unknown_tool(const std::string& error, const std::string& tool) {
    return mentions(error, "unknown tool") && error.find(tool) != std::string::npos;
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

// A symlinked launcher (~/.local/bin/cua-driver -> CuaDriver.app/Contents/MacOS/cua-driver) is run as its target: the
// Driver finds its companion tools (cursor-theme compiler) next to its own path, and every shell then names one binary.
std::filesystem::path resolved(const std::filesystem::path& path) {
#ifdef _WIN32
    return path;
#else
    std::error_code error;
    auto target = std::filesystem::canonical(path, error);
    return error ? path : target;
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
        const auto candidate = kiseki::core::path_from_utf8(segment) / name;
        if (executable_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::filesystem::path cua_driver_binary() {
    if (const auto override_path = detail::environment_text("KISEKI_CUA_DRIVER"); !override_path.empty()) {
        return executable_file(kiseki::core::path_from_utf8(override_path)) ? resolved(kiseki::core::path_from_utf8(override_path)) : std::filesystem::path{};
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
            kiseki::core::path_from_utf8(local_app_data) / "Programs" / "Cua" / "cua-driver" / "bin" / "cua-driver.exe";
        if (executable_file(visible_binary)) {
            return visible_binary;
        }
    }
#else
    if (const auto path = find_on_path("cua-driver"); !path.empty()) {
        return resolved(path);
    }
#ifdef __APPLE__
    const auto app_binary = std::filesystem::path{"/Applications/CuaDriver.app/Contents/MacOS/cua-driver"};
    if (executable_file(app_binary)) {
        return app_binary;
    }
#else
    if (const auto home = detail::environment_text("HOME"); !home.empty()) {
        const auto local_binary = kiseki::core::path_from_utf8(home) / ".local" / "bin" / "cua-driver";
        if (executable_file(local_binary)) {
            return resolved(local_binary);
        }
    }
#endif
#endif
    return {};
}

// "jpeg" for .jpg/.jpeg names, "png" for .png, "" otherwise.
std::string image_format_of(const std::filesystem::path& path) {
    const auto extension = lowercase(detail::path_text(path.extension()));
    if (extension == ".jpg" || extension == ".jpeg") return "jpeg";
    if (extension == ".png") return "png";
    return {};
}

// A file named as JPEG must hold JPEG bytes, and one named as PNG must not; other names are not checked.
bool image_signature(const std::filesystem::path& path, const std::string& format) {
    if (format != "jpeg" && format != "png") return true;
    std::ifstream stream(path, std::ios::binary);
    std::array<unsigned char, 3> head{};
    stream.read(reinterpret_cast<char*>(head.data()), head.size());
    const bool jpeg = stream.gcount() == 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF;
    return format == "jpeg" ? jpeg : !jpeg;
}

struct CaptureOutput {
    std::filesystem::path target, directory, staged;
    std::string format; // when "jpeg", the new file must carry a JPEG signature
    explicit CaptureOutput(const std::filesystem::path& output, std::string image_format = "")
        : target(std::filesystem::weakly_canonical(std::filesystem::absolute(output))), format(std::move(image_format)) {
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
        if (!image_signature(staged, format))
            throw std::runtime_error("CUA wrote an image that is not " + format + "; nothing was written to " + detail::path_text(target));
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
        if (value.is_string() && value.get_ref<const std::string&>().ends_with(detail::path_text(staged.filename())) && normalized_path(kiseki::core::path_from_utf8(value.get<std::string>())) == normalized_path(staged)) value = detail::path_text(target);
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
    // A short answer that is only a code saying the action could not be delivered. Found on Cua Driver 0.28.1: a drag into a Qt canvas in background
    // mode answers {"code":"background_unavailable"} and nothing else, and nothing was done; Driver 0.49 answers an x/y action on a window it
    // never captured with {"code":"screenshot_context_missing",...}. Codes that name what happened
    // ("bring_to_front_exact_window_verified") and long answers (a window's state) are not refusals.
    if (value.contains("code") && value["code"].is_string() && !value.contains("effect") && value.size() <= 4) {
        const auto code = value["code"].get<std::string>();
        for (const auto* suffix : {"_unavailable", "_refused", "_denied", "_failed", "_missing"})
            if (code.ends_with(suffix)) return true;
    }
    // Driver transports may wrap the same ActionResult in structuredContent.
    for (const auto* field : {"structuredContent", "structured_content"})
        if (value.contains(field) && refused_result(value[field])) return true;
    return false;
}

// Next steps appended to failures whose provider advice does not fit a Kiseki workflow.
std::string failure_hint(const std::string& text, const std::string& tool) {
    if (mentions(text, "daemon is not running")) {
        // The Driver's own advice names its bare `serve` command.
        return detail::environment_text("KISEKI_CUA_SOCKET").empty()
            ? "\nKiseki: run `kiseki background cua setup` to start the installed Driver's daemon in this desktop session."
            : "\nKiseki: KISEKI_CUA_SOCKET names a custom endpoint; its owner starts that daemon.";
    }
    if (mentions(text, "screenshot_context_missing")) {
        return "\nKiseki: x/y are pixels of this window's latest capture; make `background cua state --output FILE` or "
               "`background cua screenshot` the latest read of this window (a state read without --output, or a restarted daemon, leaves none).";
    }
    if (mentions(text, "unknown argument")) {
        return "\nKiseki: the installed Driver's " + tool + " does not take a field this request sent; `background cua describe " + tool +
            "` lists what it accepts.";
    }
    return {};
}

// A failure keeps the provider's stdout in .message (it is not printed) so callers can read a structured refusal.
OperationResult run_cua_tool_once(
    const std::string& tool,
    const nlohmann::json& arguments,
    const std::filesystem::path& screenshot_output = {},
    const std::string& image_format = "") {
    const auto binary = cua_driver_binary();
    if (binary.empty()) {
        return fail("cua-driver was not found. Run kiseki background cua setup; an explicit KISEKI_CUA_DRIVER must point to an executable");
    }
    try {
        auto tool_arguments = arguments;
        std::vector<std::unique_ptr<CaptureOutput>> captures;
        const bool argument_capture = arguments.contains("screenshot_out_file") && arguments["screenshot_out_file"].is_string();
        if (argument_capture) {
            captures.push_back(std::make_unique<CaptureOutput>(kiseki::core::path_from_utf8(arguments["screenshot_out_file"].get<std::string>()), image_format));
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
        const auto payload = nlohmann::json::parse(result.output, nullptr, false);
        if (result.code != 0 || refused_result(payload)) {
            const auto text = result.error + result.output;
            return OperationResult{.ok = false, .code = result.code != 0 ? result.code : 2, .message = result.output, .error = text + failure_hint(text, tool)};
        }
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

// A provider session ends after a while without use, or when end_session ended it. A call that names it is rejected with
// "session 'X' has ended; ... Call start_session with this id ..." (Driver up to 0.4x) or
// "session has ended; ... Call start_session with session 'X' to start it again ..." (Driver 0.49). Returns X, or "".
std::string ended_session_id(const std::string& text) {
    const std::string head = "session '";
    const std::string tail = "' has ended";
    if (const auto end = text.find(tail); end != std::string::npos) {
        const auto start = text.rfind(head, end);
        if (start != std::string::npos) return text.substr(start + head.size(), end - (start + head.size()));
    }
    if (text.find("session has ended") == std::string::npos) return {};
    const std::string named = "start_session with session '";
    const auto start = text.find(named);
    if (start == std::string::npos) return {};
    const auto end = text.find('\'', start + named.size());
    return end == std::string::npos ? std::string{} : text.substr(start + named.size(), end - (start + named.size()));
}

// Calls the provider; when it says the session the caller named has ended, starts that session again and repeats the call once, unchanged.
// Only the caller's own session is revived (never another one the provider mentions), and a failing start_session is reported with the original error.
OperationResult run_cua_tool(
    const std::string& tool,
    const nlohmann::json& arguments,
    const std::filesystem::path& screenshot_output = {},
    const std::string& image_format = "") {
    auto result = run_cua_tool_once(tool, arguments, screenshot_output, image_format);
    if (result.ok || tool == "start_session") return result;
    const auto ended = ended_session_id(result.error);
    if (ended.empty() || !arguments.contains("session") || !arguments["session"].is_string() || arguments["session"].get<std::string>() != ended) return result;
    const auto revived = run_cua_tool_once("start_session", nlohmann::json{{"session", ended}});
    if (!revived.ok) {
        result.error += "\nKiseki tried start_session for '" + ended + "': " + revived.error;
        return result;
    }
    return run_cua_tool_once(tool, arguments, screenshot_output, image_format);
}

nlohmann::json provider_fields(const std::string& provider_json) {
    auto extra = nlohmann::json::parse(provider_json);
    if (!extra.is_object()) throw std::invalid_argument("--provider-json must be a JSON object");
    return extra;
}

void merge_provider_fields(nlohmann::json& arguments, const nlohmann::json& extra) {
    for (const auto& [key, value] : extra.items()) {
        if (arguments.contains(key)) throw std::invalid_argument("--provider-json conflicts with explicit field: " + key);
        arguments[key] = value;
    }
}

OperationResult run_cua_with_options(const std::string& tool, nlohmann::json arguments, const std::string& provider_json,
    const std::string& image_format = "") {
    try {
        merge_provider_fields(arguments, provider_fields(provider_json));
        return run_cua_tool(tool, arguments, {}, image_format);
    } catch (const std::exception& error) { return fail(error.what()); }
}

// A JPEG request refused because this platform's get_window_state has no screenshot_format field.
OperationResult jpeg_unavailable_or(OperationResult result) {
    if (!result.ok && mentions(result.error, "screenshot_format") &&
        (mentions(result.error, "unknown argument") || mentions(result.error, "invalid_arguments"))) {
        result.error = "This Cua Driver cannot capture JPEG on this platform: its get_window_state has no screenshot_format field "
                       "(Driver 0.49.9 offers it on macOS, not on Windows or Linux). Nothing was written; use a .png output.\n" + result.error;
    }
    return result;
}

bool daemon_missing(const OperationResult& result) {
    return !result.ok && mentions(result.error, "daemon is not running");
}

// Workflow boundary for windows/launch: install if missing and update when the check is due, then call. The daemon is not
// probed separately; a call that finds none starts it (setup) and is repeated once, which is safe because nothing ran.
OperationResult begin_workflow_and_call(const std::string& tool, const nlohmann::json& arguments) {
    const auto prepared = cua_setup(true, 30000, false);
    if (!prepared.ok) return prepared;
    auto called = run_cua_tool(tool, arguments);
    std::string started_report;
    if (daemon_missing(called)) {
        const auto started = cua_setup(false, 30000, true);
        if (!started.ok) {
            called.error += "\nKiseki tried to start the daemon: " + started.error;
            return called;
        }
        started_report = started.message;
        called = run_cua_tool(tool, arguments);
    }
    auto report = nlohmann::json::parse(prepared.message, nullptr, false);
    if (report.is_object() && (report.value("installed", false) || report.value("updated", false) ||
        (report.contains("warnings") && !report["warnings"].empty()))) {
        called.error += "CUA setup: " + report.dump() + "\n";
    }
    if (const auto started = nlohmann::json::parse(started_report, nullptr, false); started.is_object() && started.value("started", false)) {
        called.error += "CUA daemon started: " + started.value("daemon", std::string{}) + "\n";
    }
    return called;
}

void add_window_id(nlohmann::json& json, std::uint64_t window_id, bool has_window_id) {
    if (has_window_id) {
        json["window_id"] = window_id;
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

std::string decimal(double value) {
    std::ostringstream stream;
    stream << std::setprecision(4) << value;
    return stream.str();
}

std::optional<double> number_field(const nlohmann::json& object, const char* key) {
    if (object.is_object() && object.contains(key) && object[key].is_number()) return object[key].get<double>();
    return std::nullopt;
}

OperationResult with_observation_notes(OperationResult result) {
    if (!result.ok) return result;
    const auto parsed = nlohmann::json::parse(result.message, nullptr, false);
    if (const auto notes = cua_observation_notes(parsed); !notes.empty()) {
        if (!result.error.empty() && result.error.back() != '\n') result.error += '\n';
        result.error += notes;
    }
    return result;
}

}

std::string cua_element_token(bool has_element_index, int element_index, const std::string& snapshot_id, const std::string& element_token) {
    if (!has_element_index) {
        if (!snapshot_id.empty())
            throw std::invalid_argument("--snapshot-id names the snapshot of an --element-index; pass both, or the --element-token from state");
        return element_token;
    }
    if (element_index < 0) throw std::invalid_argument("--element-index must be non-negative");
    if (snapshot_id.empty())
        throw std::invalid_argument("--element-index needs the --snapshot-id of the state read it came from (or that read's --element-token); "
                                    "an index is never sent without its snapshot");
    const auto token = snapshot_id + ":" + std::to_string(element_index);
    if (!element_token.empty() && element_token != token)
        throw std::invalid_argument("--element-token " + element_token + " does not match --snapshot-id and --element-index (" + token + ")");
    return token;
}

std::string cua_observation_notes(const nlohmann::json& response) {
    if (!response.is_object()) return {};
    const auto& value = response.contains("structuredContent") && response["structuredContent"].is_object() ? response["structuredContent"] : response;
    std::string notes;
    if (value.contains("truncated") && value["truncated"] == true) {
        notes += "CUA note: the element tree is truncated";
        if (value.contains("truncation_reason") && value["truncation_reason"].is_string()) notes += " (" + value["truncation_reason"].get<std::string>() + ")";
        if (const auto returned = number_field(value, "returned_element_count"), total = number_field(value, "total_element_count"); returned && total)
            notes += "; " + decimal(*returned) + " of " + decimal(*total) + " walked elements returned";
        notes += ". Read the rest with --max-elements, --max-depth or --query, a larger --timeout-ms, or --full-output.\n";
    }
    const auto width = number_field(value, "screenshot_width");
    const auto height = number_field(value, "screenshot_height");
    const auto bounds = value.contains("window_bounds") ? value["window_bounds"] : nlohmann::json{};
    const auto window_width = number_field(bounds, "width");
    const auto window_height = number_field(bounds, "height");
    if (width && height && window_width && window_height && *width > 0 && *window_width > 0) {
        const auto ratio = *width / *window_width;
        const auto reported = number_field(value, "screenshot_scale");
        if (reported ? ratio < *reported - 0.01 : ratio < 0.99) {
            notes += "CUA note: the screenshot is " + decimal(*width) + "x" + decimal(*height) + " px for window bounds " + decimal(*window_width) +
                "x" + decimal(*window_height) + " (" + decimal(ratio) + " px per window unit";
            if (reported) notes += "; the reported screenshot_scale " + decimal(*reported) + " is the display scale, not this image's";
            notes += "). x/y actions use this screenshot's pixels; --max-image-dimension 0 returns native resolution.\n";
        }
    }
    if (!notes.empty()) notes.pop_back();
    return notes;
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

nlohmann::json cua_launch_arguments(const MacCuaLaunchOptions& options, bool macos_app_paths) {
    nlohmann::json arguments = nlohmann::json::object();
    if (!options.bundle_id.empty()) {
        arguments["bundle_id"] = options.bundle_id;
    }
    if (!options.aumid.empty()) arguments["aumid"] = options.aumid;
    if (!options.path.empty()) arguments["path"] = options.path;
    auto bundle_path = options.launch_path;
    while (bundle_path.size() > 1 && bundle_path.back() == '/') bundle_path.pop_back();
    const bool app_bundle = macos_app_paths && options.name.empty() && bundle_path.size() > 4 &&
        bundle_path.compare(bundle_path.size() - 4, 4, ".app") == 0;
    if (app_bundle) {
        arguments["name"] = bundle_path;
    } else if (!options.name.empty()) {
        arguments["name"] = options.name;
    }
    if (!options.launch_path.empty() && !app_bundle) arguments["launch_path"] = options.launch_path;
    if (!options.urls.empty()) {
        arguments["urls"] = options.urls;
    }
    if (options.creates_new_instance) {
        arguments["creates_new_application_instance"] = true;
    }
    if (!options.additional_arguments.empty()) {
        arguments["additional_arguments"] = options.additional_arguments;
    }
    return arguments;
}

OperationResult macos_cua_launch(const MacCuaLaunchOptions& options) {
    if (options.bundle_id.empty() && options.name.empty() && options.launch_path.empty() && options.aumid.empty() && options.path.empty()) {
        return fail("background cua launch requires --bundle-id, --name, --launch-path, --aumid, or --path");
    }
#ifdef __APPLE__
    constexpr bool macos_app_paths = true;
#else
    constexpr bool macos_app_paths = false;
#endif
    return begin_workflow_and_call("launch_app", cua_launch_arguments(options, macos_app_paths));
}

OperationResult macos_cua_list_windows(const MacCuaWindowListOptions& options) {
    nlohmann::json arguments = nlohmann::json::object();
    if (options.has_pid) {
        arguments["pid"] = options.pid;
    }
    if (!options.app.empty()) arguments["app"] = options.app;
    if (options.on_screen_only) {
        arguments["on_screen_only"] = true;
    }
    return begin_workflow_and_call("list_windows", arguments);
}

OperationResult macos_cua_window_state(const MacCuaWindowStateOptions& options) {
    if (options.pid <= 0 || options.window_id == 0) {
        return fail("background cua state requires positive --pid and --window-id");
    }
    if (options.no_tree && options.output_path.empty()) {
        return fail("background cua state --no-tree reads only the screenshot; pass --output");
    }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"window_id", options.window_id},
    };
    add_session_if_configured(arguments);
    if (!options.query.empty()) {
        arguments["query"] = options.query;
    }
    if (options.max_elements) arguments["max_elements"] = *options.max_elements;
    if (options.max_depth) arguments["max_depth"] = *options.max_depth;
    if (options.timeout_ms) arguments["timeout_ms"] = *options.timeout_ms;
    if (options.max_image_dimension) arguments["max_image_dimension"] = *options.max_image_dimension;
    if (!options.tree_format.empty()) arguments["tree_format"] = options.tree_format;
    if (!options.since.empty()) arguments["since"] = options.since;
    if (options.full_output) arguments["full_output"] = true;
    if (options.no_tree) arguments["include_accessibility_tree"] = false;
    if (options.verbose) arguments["verbose"] = true;
    const auto extra = nlohmann::json::parse(options.provider_json, nullptr, false);
    const auto provided = [&](const char* key) { return extra.is_object() && extra.contains(key); };
    std::string image_format;
    if (!options.output_path.empty()) {
        arguments["screenshot_out_file"] = detail::path_text(std::filesystem::absolute(options.output_path));
        // The file name decides the encoding: a .jpg is requested as JPEG and must hold JPEG bytes; a .png must not.
        image_format = image_format_of(options.output_path);
        if (image_format == "jpeg" && !provided("screenshot_format")) arguments["screenshot_format"] = "jpeg";
    } else if (!provided("include_screenshot")) {
        // Without --output, state is structural: the provider would otherwise inline a base64 screenshot.
        arguments["include_screenshot"] = false;
    }
    return with_observation_notes(jpeg_unavailable_or(run_cua_with_options("get_window_state", arguments, options.provider_json, image_format)));
}

OperationResult macos_cua_screenshot(const MacCuaScreenshotOptions& options) {
    if (options.window_id == 0) {
        return fail("background cua screenshot requires --window-id");
    }
    if (options.output_path.empty()) {
        return fail("background cua screenshot requires --output");
    }
    const auto named = image_format_of(options.output_path);
    auto format = lowercase(options.format.empty() ? (named.empty() ? "png" : named) : options.format);
    if (format == "jpg") format = "jpeg";
    if (format != "png" && format != "jpeg") return fail("background cua screenshot --format must be png or jpeg");
    if (!named.empty() && named != format) {
        return fail("background cua screenshot --format " + format + " does not match the output name " + detail::path_text(options.output_path.filename()));
    }
    if (options.quality && format != "jpeg") return fail("background cua screenshot --quality applies to --format jpeg");
    int pid = options.pid;
    if (pid <= 0) {
        std::string error;
        pid = resolve_pid_for_window_id(options.window_id, error);
        if (pid <= 0) return fail(error);
    }
    nlohmann::json arguments{{"pid", pid}, {"window_id", options.window_id},
        {"include_accessibility_tree", false}, {"include_screenshot", true},
        {"screenshot_out_file", detail::path_text(std::filesystem::absolute(options.output_path))}};
    add_session_if_configured(arguments);
    if (options.max_image_dimension) arguments["max_image_dimension"] = *options.max_image_dimension;
    if (format == "jpeg") {
        arguments["screenshot_format"] = "jpeg";
        if (options.quality) arguments["screenshot_quality"] = *options.quality;
    }
    const auto captured = jpeg_unavailable_or(run_cua_tool("get_window_state", arguments, {}, format));
    if (!captured.ok) return captured;
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(options.output_path, file_error) ||
        std::filesystem::file_size(options.output_path, file_error) == 0) {
        return fail("CUA returned without creating the requested screenshot: " + detail::path_text(options.output_path));
    }
    return with_observation_notes(captured);
}

OperationResult macos_cua_click(const MacCuaClickOptions& options) {
    if (options.pid <= 0) {
        return fail("background cua click requires positive --pid");
    }
    std::string token;
    try {
        token = cua_element_token(options.has_element_index, options.element_index, options.snapshot_id, options.element_token);
    } catch (const std::invalid_argument& error) { return fail(std::string{"background cua click "} + error.what()); }
    if (token.empty() == !options.has_xy) {
        return fail("background cua click requires either --element-token (or --element-index with --snapshot-id) or both --x and --y");
    }

    nlohmann::json arguments = {{"pid", options.pid}};
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    if (!token.empty()) arguments["element_token"] = token;
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
    std::string token;
    try {
        token = cua_element_token(options.has_element_index, options.element_index, options.snapshot_id, options.element_token);
    } catch (const std::invalid_argument& error) { return fail(std::string{"background cua text "} + error.what()); }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"text", options.text},
    };
    if (options.delay_ms) arguments["delay_ms"] = *options.delay_ms;
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    if (!token.empty()) arguments["element_token"] = token;
    return run_cua_with_options("type_text", arguments, options.provider_json);
}

OperationResult macos_cua_press_key(const MacCuaKeyOptions& options) {
    if (options.pid <= 0 || options.key.empty()) {
        return fail("background cua key requires positive --pid and --key");
    }
    std::string token;
    try {
        token = cua_element_token(options.has_element_index, options.element_index, options.snapshot_id, options.element_token);
    } catch (const std::invalid_argument& error) { return fail(std::string{"background cua key "} + error.what()); }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"key", options.key},
    };
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    if (!token.empty()) arguments["element_token"] = token;
    if (!options.modifiers.empty()) {
        // press_key names the field `modifiers`; click and drag use `modifier`.
        arguments["modifiers"] = options.modifiers;
    }
    return run_cua_with_options("press_key", arguments, options.provider_json);
}

OperationResult macos_cua_hotkey(const MacCuaHotkeyOptions& options) {
    if (options.pid <= 0 || options.keys.size() < 2) {
        return fail("background cua hotkey requires positive --pid and at least two --keys entries");
    }
    std::string token;
    try {
        token = cua_element_token(options.has_element_index, options.element_index, options.snapshot_id, options.element_token);
    } catch (const std::invalid_argument& error) { return fail(std::string{"background cua hotkey "} + error.what()); }
    nlohmann::json arguments = {
        {"pid", options.pid},
        {"keys", options.keys},
    };
    add_session_if_configured(arguments);
    add_window_id(arguments, options.window_id, options.has_window_id);
    if (!token.empty()) arguments["element_token"] = token;
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

// Each segment is one provider drag (press, move, release); a polyline is therefore a series of strokes, not one held stroke.
// Driver 0.48+ run_steps carries up to 64 segments per call with the requested gap as its delay_ms, so timing between segments
// is the requested gap instead of a process start per segment. Older Drivers (no run_steps) get one drag call per segment.
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
    if (!options.segment_durations_ms.empty() && options.segment_durations_ms.size() != segment_count) {
        return fail("background cua draw needs one duration per segment");
    }
    if (std::any_of(options.segment_durations_ms.begin(), options.segment_durations_ms.end(), [](int value) { return value < 0; })) {
        return fail("background cua draw segment durations must be non-negative");
    }

    nlohmann::json extra;
    try {
        extra = provider_fields(options.provider_json);
        if (extra.contains("session")) throw std::invalid_argument("--provider-json conflicts with explicit field: session");
    } catch (const std::exception& error) { return fail(error.what()); }
    const auto duration = [&](std::size_t segment) {
        return options.segment_durations_ms.empty() ? options.duration_ms : options.segment_durations_ms[segment - 1];
    };
    // Segment N goes from point N-1 to point N.
    const auto segment_arguments = [&](std::size_t segment) {
        nlohmann::json arguments = {
            {"pid", options.pid},
            {"window_id", options.window_id},
            {"from_x", options.points[segment - 1].x},
            {"from_y", options.points[segment - 1].y},
            {"to_x", options.points[segment].x},
            {"to_y", options.points[segment].y},
            {"duration_ms", duration(segment)},
            {"steps", options.steps},
            {"button", options.button.empty() ? "left" : options.button},
        };
        add_modifiers(arguments, options.modifiers);
        merge_provider_fields(arguments, extra);
        return arguments;
    };
    try {
        for (std::size_t segment = 1; segment <= segment_count; ++segment) static_cast<void>(segment_arguments(segment));
    } catch (const std::exception& error) { return fail(error.what()); }

    constexpr std::size_t max_batch_steps = 64;   // run_steps maxItems
    constexpr int max_batch_delay_ms = 2000;      // run_steps delay_ms maximum
    constexpr long long batch_budget_ms = 40000;  // a batch must finish within the provider's 60 s; leave room for per-step work
    constexpr long long per_step_allowance_ms = 150;
    const auto session = configured_cua_session_id();
    if (options.stroke_gap_ms <= max_batch_delay_ms) {
        std::size_t next = 1;
        std::size_t calls = 0;
        bool batching = true;
        while (batching && next <= segment_count) {
            const auto first = next;
            nlohmann::json steps = nlohmann::json::array();
            long long budget = 0;
            while (next <= segment_count && steps.size() < max_batch_steps) {
                const long long cost = duration(next) + options.stroke_gap_ms + per_step_allowance_ms;
                if (!steps.empty() && budget + cost > batch_budget_ms) break;
                budget += cost;
                steps.push_back({{"tool", "drag"}, {"args", segment_arguments(next)}});
                ++next;
            }
            nlohmann::json request{{"steps", steps}, {"pid", options.pid}, {"window_id", options.window_id}};
            if (!session.empty()) request["session"] = session;
            if (options.stroke_gap_ms > 0) request["delay_ms"] = options.stroke_gap_ms;
            if (first > 1 && options.stroke_gap_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(options.stroke_gap_ms));
            const auto result = run_cua_tool("run_steps", request);
            ++calls;
            if (result.ok) continue;
            const auto payload = nlohmann::json::parse(result.message, nullptr, false);
            const bool steps_ran = payload.is_object() && (payload.contains("steps") || payload.contains("executed"));
            if (first == 1 && !steps_ran &&
                (unknown_tool(result.error, "run_steps") || mentions(result.error, "unknown argument"))) {
                batching = false; // nothing ran: this Driver has no (compatible) run_steps
                break;
            }
            auto failed = first;
            std::string reason;
            if (payload.is_object() && payload.contains("failed_step") && payload["failed_step"].is_number_unsigned()) {
                const auto index = payload["failed_step"].get<std::size_t>();
                failed = first + index;
                if (payload.contains("steps") && payload["steps"].is_array())
                    for (const auto& step : payload["steps"])
                        if (step.value("index", -1) == static_cast<int>(index)) {
                            if (step.contains("code") && step["code"].is_string()) reason = step["code"].get<std::string>() + ": ";
                            if (step.contains("message") && step["message"].is_string()) reason += step["message"].get<std::string>();
                        }
            }
            return fail("background cua draw segment " + std::to_string(failed) + " failed" + (reason.empty() ? "" : ": " + reason) +
                    (failed > 1 ? "; segments 1-" + std::to_string(failed - 1) + " were delivered" : std::string{}) + ".\n" + result.error,
                result.code);
        }
        if (batching) {
            return ok("background cua draw sent " + std::to_string(segment_count) + " drag segment(s) in " + std::to_string(calls) +
                " run_steps call(s); each segment is its own press-drag-release");
        }
    }

    for (std::size_t segment = 1; segment <= segment_count; ++segment) {
        auto arguments = segment_arguments(segment);
        add_session_if_configured(arguments);
        const auto result = run_cua_tool("drag", arguments);
        if (!result.ok) {
            return fail(
                "background cua draw segment " + std::to_string(segment) + " failed: " + result.error,
                result.code);
        }
        if (options.stroke_gap_ms > 0 && segment < segment_count) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.stroke_gap_ms));
        }
    }

    return ok(
        "background cua draw sent " + std::to_string(segment_count) +
        " drag segment(s), one drag call each; each segment is its own press-drag-release");
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
    if (!options.style.empty()) arguments["style"] = options.style;
    if (!options.timing.empty()) arguments["timing"] = options.timing;
    if (options.has_turn_radius) arguments["turn_radius"] = options.turn_radius;
    if (!options.effects.empty()) {
        nlohmann::json effects = nlohmann::json::object();
        for (const auto& [name, state] : options.effects) effects[name] = state;
        arguments["effects"] = effects;
    }
    if (arguments.size() == 1) {
        return fail("background cua feedback motion requires at least one motion option");
    }
    return run_cua_tool("set_agent_cursor_motion", arguments);
}

namespace {
const char* const theme_hint =
    "this Cua Driver has no set_agent_cursor_style (removed in 0.25): the cursor's look comes from installed themes. "
    "List them with `background cua cursor-theme -- list` and select one with "
    "`background cua theme --json '{\"theme_id\":\"cua.default\"}'`; motion stays on `feedback motion`.";
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
    auto result = run_cua_tool("set_agent_cursor_style", arguments);
    if (!result.ok && unknown_tool(result.error, "set_agent_cursor_style")) {
        result.error = std::string{"background cua feedback style: "} + theme_hint;
    }
    return result;
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

    // Older Drivers also take the preset's colors (set_agent_cursor_style); current ones apply visibility and motion only.
    const auto style_schema = cua_driver_command({"describe", "set_agent_cursor_style"});
    const bool legacy_style = style_schema.ok;
    if (!legacy_style && !unknown_tool(style_schema.error, "set_agent_cursor_style")) {
        return fail("Kiseki could not read this Cua Driver's tools; no preset changes were sent.\n" + style_schema.error, style_schema.code);
    }

    nlohmann::json enabled_arguments = {{"enabled", true}};
    add_cursor_id(enabled_arguments);
    add_cursor_id(motion);
    const auto enabled = run_cua_tool("set_agent_cursor_enabled", enabled_arguments);
    if (!enabled.ok) {
        return enabled;
    }
    const auto motion_result = run_cua_tool("set_agent_cursor_motion", motion);
    if (!motion_result.ok) {
        return motion_result;
    }
    if (!legacy_style) {
        return ok(nlohmann::json{{"preset", name}, {"applied", {"set_agent_cursor_enabled", "set_agent_cursor_motion"}},
            {"colors", std::string{"not applied: "} + theme_hint}}.dump(2));
    }
    add_cursor_id(style);
    const auto style_result = run_cua_tool("set_agent_cursor_style", style);
    if (!style_result.ok) {
        return style_result;
    }
    return ok("background cua feedback preset " + name + " applied");
}

}
