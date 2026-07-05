#include "platform/teach/recording.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "platform/capture/screenshot.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef KISEKI_HAS_X11
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <cstdlib>
#endif
#endif

namespace kiseki::platform::teach {

namespace {

using Clock = std::chrono::steady_clock;

OperationResult ok(std::string message) {
    return OperationResult{
        .ok = true,
        .code = 0,
        .message = std::move(message),
        .error = "",
    };
}

OperationResult fail(std::string error) {
    return OperationResult{
        .ok = false,
        .code = 2,
        .message = "",
        .error = std::move(error),
    };
}

std::uint64_t elapsed_ms(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

std::string utc_stamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string file_stamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
    return stream.str();
}

std::string frame_filename(std::size_t index) {
    std::ostringstream stream;
    stream << "frame_" << std::setw(6) << std::setfill('0') << index << ".bmp";
    return stream.str();
}

std::string video_keyframe_filename(std::size_t index) {
    std::ostringstream stream;
    stream << "video_frame_" << std::setw(6) << std::setfill('0') << index << ".jpg";
    return stream.str();
}

std::filesystem::path make_relative(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code error;
    const auto relative = std::filesystem::relative(path, root, error);
    if (error) {
        return path.filename();
    }
    return relative;
}

void write_json_file(const std::filesystem::path& path, const nlohmann::json& json) {
    std::ofstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error{"failed to open " + path.string()};
    }
    file << json.dump(2) << '\n';
}

void write_text_file(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error{"failed to open " + path.string()};
    }
    file << text;
}

std::string shell_quote(const std::filesystem::path& path) {
    std::string value = path.string();
#ifdef _WIN32
    std::string quoted = "\"";
    for (char c : value) {
        if (c == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(c);
        }
    }
    quoted += "\"";
    return quoted;
#else
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(c);
        }
    }
    quoted += "'";
    return quoted;
#endif
}

std::string shell_quote_arg(std::string_view value) {
    return shell_quote(std::filesystem::path{std::string{value}});
}

std::filesystem::path absolute_path(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return error ? path : absolute;
}

std::filesystem::path user_state_directory() {
#ifdef _WIN32
    if (const char* local_app_data = std::getenv("LOCALAPPDATA"); local_app_data != nullptr && local_app_data[0] != '\0') {
        return std::filesystem::path{local_app_data} / "KisekiInput";
    }
    if (const char* app_data = std::getenv("APPDATA"); app_data != nullptr && app_data[0] != '\0') {
        return std::filesystem::path{app_data} / "KisekiInput";
    }
    return std::filesystem::temp_directory_path() / "KisekiInput";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path{home} / "Library" / "Application Support" / "KisekiInput";
    }
    return std::filesystem::temp_directory_path() / "kiseki-input";
#else
    if (const char* state_home = std::getenv("XDG_STATE_HOME"); state_home != nullptr && state_home[0] != '\0') {
        return std::filesystem::path{state_home} / "kiseki-input";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path{home} / ".local" / "state" / "kiseki-input";
    }
    return std::filesystem::temp_directory_path() / "kiseki-input";
#endif
}

std::filesystem::path default_state_file() {
    return user_state_directory() / "teach-recording-state.json";
}

std::filesystem::path default_output_directory() {
    return std::filesystem::path{"artifacts"} / "teach" / ("teach-" + file_stamp());
}

std::filesystem::path default_stop_file(const std::filesystem::path& output_directory) {
    return output_directory / ".kiseki-teach-stop";
}

std::filesystem::path default_log_file(const std::filesystem::path& output_directory) {
    return output_directory / "recording-worker.log";
}

std::string path_string_for_arg(const std::filesystem::path& path) {
    return path.string();
}

struct ActiveRecording {
    bool valid = false;
    std::uint64_t pid = 0;
    std::filesystem::path output_directory;
    std::filesystem::path stop_file;
    std::filesystem::path log_file;
    std::filesystem::path state_file;
};

nlohmann::json read_json_file(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error{"failed to open " + path.string()};
    }
    return nlohmann::json::parse(file);
}

ActiveRecording read_active_recording(const std::filesystem::path& state_file) {
    ActiveRecording active;
    active.state_file = state_file;
    if (!std::filesystem::exists(state_file)) {
        return active;
    }
    try {
        const auto json = read_json_file(state_file);
        if (!json.is_object()) {
            return active;
        }
        active.pid = json.value("pid", static_cast<std::uint64_t>(0));
        active.output_directory = json.value("outputDirectory", std::string{});
        active.stop_file = json.value("stopFile", std::string{});
        active.log_file = json.value("logFile", std::string{});
        active.valid = active.pid != 0 && !active.stop_file.empty();
    } catch (...) {
        active.valid = false;
    }
    return active;
}

bool process_alive(std::uint64_t pid) {
    if (pid == 0) {
        return false;
    }
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    DWORD exit_code = 0;
    const bool alive = GetExitCodeProcess(process, &exit_code) != 0 && exit_code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    if (kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
#endif
}

bool remove_file_if_exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::remove(path, error);
}

void write_active_recording_state(
    const std::filesystem::path& state_file,
    std::uint64_t pid,
    const std::filesystem::path& output_directory,
    const std::filesystem::path& stop_file,
    const std::filesystem::path& log_file) {
    if (!state_file.parent_path().empty()) {
        std::filesystem::create_directories(state_file.parent_path());
    }
    write_json_file(
        state_file,
        {
            {"schemaVersion", 1},
            {"kind", "kiseki-teach-active-recording"},
            {"pid", pid},
            {"outputDirectory", absolute_path(output_directory).string()},
            {"stopFile", absolute_path(stop_file).string()},
            {"logFile", absolute_path(log_file).string()},
            {"startedAtUtc", utc_stamp()},
        });
}

void cleanup_state_if_current(const std::filesystem::path& state_file, const std::filesystem::path& output_directory) {
    if (state_file.empty() || !std::filesystem::exists(state_file)) {
        return;
    }
    try {
        const auto active = read_active_recording(state_file);
        if (!active.valid) {
            remove_file_if_exists(state_file);
            return;
        }
        std::error_code error_a;
        std::error_code error_b;
        const auto active_output = std::filesystem::weakly_canonical(active.output_directory, error_a);
        const auto current_output = std::filesystem::weakly_canonical(output_directory, error_b);
        if (!error_a && !error_b && active_output == current_output) {
            remove_file_if_exists(state_file);
        }
    } catch (...) {
    }
}

std::filesystem::path current_executable_path() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        throw std::runtime_error{"failed to resolve current executable path"};
    }
    buffer.resize(length);
    return std::filesystem::path{buffer};
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        throw std::runtime_error{"failed to resolve current executable path"};
    }
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    return std::filesystem::weakly_canonical(buffer);
#else
    std::array<char, 4096> buffer{};
    const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) {
        throw std::runtime_error{"failed to resolve current executable path"};
    }
    return std::filesystem::path{std::string{buffer.data(), static_cast<std::size_t>(length)}};
#endif
}

#ifdef _WIN32
std::wstring utf8_to_wide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), required);
    return wide;
}

std::wstring quote_windows_arg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    bool needs_quotes = false;
    for (wchar_t c : arg) {
        if (c == L' ' || c == L'\t' || c == L'"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return arg;
    }
    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(c);
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(c);
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}
#endif

std::vector<std::string> build_worker_args(
    const RecordOptions& options,
    const std::filesystem::path& output_directory,
    const std::filesystem::path& state_file,
    const std::filesystem::path& stop_file) {
    std::vector<std::string> args{
        "teach",
        "record",
        "--worker",
        "--output",
        path_string_for_arg(output_directory),
        "--state-file",
        path_string_for_arg(state_file),
        "--stop-file",
        path_string_for_arg(stop_file),
        "--frame-interval-ms",
        std::to_string(options.frame_interval_ms),
        "--event-poll-ms",
        std::to_string(options.event_poll_ms),
        "--video-keyframe-interval-ms",
        std::to_string(options.video_keyframe_interval_ms),
        "--video-keyframe-max",
        std::to_string(options.video_keyframe_max),
    };
    if (options.duration_ms > 0) {
        args.push_back("--duration-ms");
        args.push_back(std::to_string(options.duration_ms));
    }
    if (options.no_video_keyframes) {
        args.push_back("--no-video-keyframes");
    }
    if (!options.title.empty()) {
        args.push_back("--title");
        args.push_back(options.title);
    }
    if (!options.instruction_text.empty()) {
        args.push_back("--text");
        args.push_back(options.instruction_text);
    }
    if (!options.video_file.empty()) {
        args.push_back("--video-file");
        args.push_back(path_string_for_arg(options.video_file));
    }
    if (!options.audio_file.empty()) {
        args.push_back("--audio-file");
        args.push_back(path_string_for_arg(options.audio_file));
    }
    if (!options.transcript_file.empty()) {
        args.push_back("--transcript-file");
        args.push_back(path_string_for_arg(options.transcript_file));
    }
    return args;
}

std::uint64_t spawn_recording_worker(
    const RecordOptions& options,
    const std::filesystem::path& output_directory,
    const std::filesystem::path& state_file,
    const std::filesystem::path& stop_file,
    const std::filesystem::path& log_file) {
    const auto executable = current_executable_path();
    const auto args = build_worker_args(options, output_directory, state_file, stop_file);

#ifdef _WIN32
    std::wstring command_line = quote_windows_arg(executable.wstring());
    for (const auto& arg : args) {
        command_line.push_back(L' ');
        command_line += quote_windows_arg(utf8_to_wide(arg));
    }

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE log = CreateFileW(
        log_file.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security_attributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        throw std::runtime_error{"failed to open recording worker log"};
    }
    SetHandleInformation(GetStdHandle(STD_INPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(GetStdHandle(STD_OUTPUT_HANDLE), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(GetStdHandle(STD_ERROR_HANDLE), HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security_attributes,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul == INVALID_HANDLE_VALUE ? GetStdHandle(STD_INPUT_HANDLE) : nul;
    startup.hStdOutput = log;
    startup.hStdError = log;

    PROCESS_INFORMATION process_information{};
    const BOOL created = CreateProcessW(
        nullptr,
        command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
        nullptr,
        nullptr,
        &startup,
        &process_information);
    if (nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }
    CloseHandle(log);
    if (created == 0) {
        throw std::runtime_error{"failed to start recording worker process"};
    }
    const std::uint64_t pid = process_information.dwProcessId;
    CloseHandle(process_information.hThread);
    CloseHandle(process_information.hProcess);
    return pid;
#else
    const pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error{"failed to fork recording worker process"};
    }
    if (pid == 0) {
        setsid();
        std::filesystem::create_directories(log_file.parent_path());
        FILE* log = std::freopen(log_file.string().c_str(), "w", stdout);
        if (log != nullptr) {
            std::freopen(log_file.string().c_str(), "a", stderr);
        }
        std::vector<std::string> argv_storage;
        argv_storage.reserve(args.size() + 1);
        argv_storage.push_back(executable.string());
        argv_storage.insert(argv_storage.end(), args.begin(), args.end());
        std::vector<char*> argv;
        argv.reserve(argv_storage.size() + 1);
        for (auto& arg : argv_storage) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        execv(executable.string().c_str(), argv.data());
        _exit(127);
    }
    return static_cast<std::uint64_t>(pid);
#endif
}

OperationResult stop_active_recording(const ActiveRecording& active, std::uint32_t timeout_ms) {
    try {
        if (!active.stop_file.parent_path().empty()) {
            std::filesystem::create_directories(active.stop_file.parent_path());
        }
        write_json_file(
            active.stop_file,
            {
                {"schemaVersion", 1},
                {"kind", "kiseki-teach-stop-request"},
                {"requestedAtUtc", utc_stamp()},
            });
    } catch (const std::exception& error) {
        return fail(std::string{"failed to write teach recording stop request: "} + error.what());
    }

    const auto started = Clock::now();
    const auto timeout = std::chrono::milliseconds{timeout_ms == 0 ? 30000 : timeout_ms};
    while (Clock::now() - started <= timeout) {
        if (!std::filesystem::exists(active.state_file)) {
            return ok("stopped teaching recording " + active.output_directory.string());
        }
        if (!process_alive(active.pid)) {
            remove_file_if_exists(active.state_file);
            return ok("stopped teaching recording " + active.output_directory.string());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    return fail(
        "stop requested but teaching recording did not finalize before timeout; output=" +
        active.output_directory.string() + " log=" + active.log_file.string());
}

std::optional<std::filesystem::path> copy_media_file(
    const std::filesystem::path& source,
    const std::filesystem::path& session_directory,
    const char* manifest_key,
    nlohmann::json& manifest) {
    if (source.empty()) {
        return std::nullopt;
    }
    if (!std::filesystem::exists(source)) {
        throw std::runtime_error{"media file does not exist: " + source.string()};
    }

    const auto media_directory = session_directory / "media";
    std::filesystem::create_directories(media_directory);
    const auto destination = media_directory / source.filename();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
    manifest["media"][manifest_key] = make_relative(session_directory, destination).generic_string();
    return destination;
}

nlohmann::json empty_annotations() {
    return {
        {"schemaVersion", 1},
        {"annotations", nlohmann::json::array()},
    };
}

void append_event(
    nlohmann::json event,
    std::ofstream& events_file,
    nlohmann::json& event_index,
    nlohmann::json& timeline,
    std::size_t& index) {
    event["index"] = index;
    events_file << event.dump() << '\n';
    event_index.push_back(event);
    timeline["items"].push_back({
        {"kind", "event"},
        {"timestampMs", event.value("timestampMs", 0)},
        {"eventIndex", index},
        {"type", event.value("type", "")},
    });
    ++index;
}

class NativeEventSampler {
public:
    NativeEventSampler() = default;
    NativeEventSampler(const NativeEventSampler&) = delete;
    NativeEventSampler& operator=(const NativeEventSampler&) = delete;

    ~NativeEventSampler() {
#if !defined(_WIN32) && !defined(__APPLE__) && defined(KISEKI_HAS_X11)
        if (display_ != nullptr) {
            XCloseDisplay(display_);
        }
#endif
    }

    std::optional<std::string> initialize() {
#ifdef _WIN32
        key_states_.fill(false);
        button_states_.fill(false);
        POINT point{};
        if (GetCursorPos(&point) != 0) {
            last_x_ = point.x;
            last_y_ = point.y;
            has_mouse_ = true;
        }
        return std::nullopt;
#elif defined(__APPLE__)
        key_states_.fill(false);
        button_states_.fill(false);
        const auto point = current_mouse_position();
        if (point) {
            last_x_ = point->first;
            last_y_ = point->second;
            has_mouse_ = true;
        }
        return std::nullopt;
#else
#ifdef KISEKI_HAS_X11
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) {
            return "XOpenDisplay failed; recording only captured keyframes";
        }
        root_ = DefaultRootWindow(display_);
        key_states_.fill(false);
        button_states_.fill(false);
        return std::nullopt;
#else
        return "native input event sampling was not compiled for this platform";
#endif
#endif
    }

    std::vector<nlohmann::json> poll(std::uint64_t timestamp_ms) {
        std::vector<nlohmann::json> events;
        poll_mouse(timestamp_ms, events);
        poll_buttons(timestamp_ms, events);
        poll_keys(timestamp_ms, events);
        return events;
    }

    std::optional<std::pair<int, int>> last_mouse_position() const {
        if (!has_mouse_) {
            return std::nullopt;
        }
        return std::pair<int, int>{last_x_, last_y_};
    }

private:
    void push_mouse_move(std::uint64_t timestamp_ms, int x, int y, std::vector<nlohmann::json>& events) {
        if (has_mouse_ && x == last_x_ && y == last_y_) {
            return;
        }
        has_mouse_ = true;
        last_x_ = x;
        last_y_ = y;
        events.push_back({
            {"timestampMs", timestamp_ms},
            {"type", "mouse_move"},
            {"x", x},
            {"y", y},
        });
    }

    void push_button(
        std::uint64_t timestamp_ms,
        std::size_t index,
        std::string_view button,
        bool down,
        std::vector<nlohmann::json>& events) {
        if (button_states_[index] == down) {
            return;
        }
        button_states_[index] = down;
        events.push_back({
            {"timestampMs", timestamp_ms},
            {"type", "mouse_button"},
            {"button", button},
            {"state", down ? "down" : "up"},
        });
    }

    void push_key(
        std::uint64_t timestamp_ms,
        std::size_t key_code,
        std::string key_name,
        bool down,
        std::vector<nlohmann::json>& events) {
        if (key_states_[key_code] == down) {
            return;
        }
        key_states_[key_code] = down;
        nlohmann::json event{
            {"timestampMs", timestamp_ms},
            {"type", "key"},
            {"keyCode", key_code},
            {"state", down ? "down" : "up"},
        };
        if (!key_name.empty()) {
            event["key"] = std::move(key_name);
        }
        events.push_back(std::move(event));
    }

    void poll_mouse(std::uint64_t timestamp_ms, std::vector<nlohmann::json>& events) {
#ifdef _WIN32
        POINT point{};
        if (GetCursorPos(&point) != 0) {
            push_mouse_move(timestamp_ms, point.x, point.y, events);
        }
#elif defined(__APPLE__)
        const auto point = current_mouse_position();
        if (point) {
            push_mouse_move(timestamp_ms, point->first, point->second, events);
        }
#else
#ifdef KISEKI_HAS_X11
        if (display_ == nullptr) {
            return;
        }
        Window root_return{};
        Window child_return{};
        int root_x = 0;
        int root_y = 0;
        int win_x = 0;
        int win_y = 0;
        unsigned int mask = 0;
        if (XQueryPointer(display_, root_, &root_return, &child_return, &root_x, &root_y, &win_x, &win_y, &mask) != 0) {
            push_mouse_move(timestamp_ms, root_x, root_y, events);
        }
#endif
#endif
    }

    void poll_buttons(std::uint64_t timestamp_ms, std::vector<nlohmann::json>& events) {
#ifdef _WIN32
        push_button(timestamp_ms, 0, "left", (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, events);
        push_button(timestamp_ms, 1, "right", (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0, events);
        push_button(timestamp_ms, 2, "middle", (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0, events);
#elif defined(__APPLE__)
        push_button(timestamp_ms, 0, "left", CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonLeft), events);
        push_button(timestamp_ms, 1, "right", CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonRight), events);
        push_button(timestamp_ms, 2, "middle", CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonCenter), events);
#else
#ifdef KISEKI_HAS_X11
        if (display_ == nullptr) {
            return;
        }
        Window root_return{};
        Window child_return{};
        int root_x = 0;
        int root_y = 0;
        int win_x = 0;
        int win_y = 0;
        unsigned int mask = 0;
        if (XQueryPointer(display_, root_, &root_return, &child_return, &root_x, &root_y, &win_x, &win_y, &mask) != 0) {
            push_button(timestamp_ms, 0, "left", (mask & Button1Mask) != 0, events);
            push_button(timestamp_ms, 1, "middle", (mask & Button2Mask) != 0, events);
            push_button(timestamp_ms, 2, "right", (mask & Button3Mask) != 0, events);
        }
#endif
#endif
    }

    void poll_keys(std::uint64_t timestamp_ms, std::vector<nlohmann::json>& events) {
#ifdef _WIN32
        for (std::size_t code = 1; code < key_states_.size(); ++code) {
            const bool down = (GetAsyncKeyState(static_cast<int>(code)) & 0x8000) != 0;
            push_key(timestamp_ms, code, windows_key_name(static_cast<unsigned int>(code)), down, events);
        }
#elif defined(__APPLE__)
        for (std::size_t code = 0; code < 128; ++code) {
            const bool down = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, static_cast<CGKeyCode>(code));
            push_key(timestamp_ms, code, "", down, events);
        }
#else
#ifdef KISEKI_HAS_X11
        if (display_ == nullptr) {
            return;
        }
        char keys[32]{};
        XQueryKeymap(display_, keys);
        for (std::size_t code = 8; code < 256; ++code) {
            const bool down = (keys[code / 8] & (1 << (code % 8))) != 0;
            std::string name;
            if (down != key_states_[code]) {
                const KeySym symbol = XkbKeycodeToKeysym(display_, static_cast<KeyCode>(code), 0, 0);
                if (const char* symbol_name = XKeysymToString(symbol); symbol_name != nullptr) {
                    name = symbol_name;
                }
            }
            push_key(timestamp_ms, code, std::move(name), down, events);
        }
#endif
#endif
    }

#ifdef __APPLE__
    std::optional<std::pair<int, int>> current_mouse_position() {
        CGEventRef event = CGEventCreate(nullptr);
        if (event == nullptr) {
            return std::nullopt;
        }
        const CGPoint point = CGEventGetLocation(event);
        CFRelease(event);
        return std::pair<int, int>{static_cast<int>(point.x), static_cast<int>(point.y)};
    }
#endif

#ifdef _WIN32
    std::string windows_key_name(unsigned int code) {
        UINT scan_code = MapVirtualKeyW(code, MAPVK_VK_TO_VSC);
        switch (code) {
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_END:
        case VK_HOME:
        case VK_INSERT:
        case VK_DELETE:
        case VK_DIVIDE:
        case VK_NUMLOCK:
            scan_code |= 0x100;
            break;
        default:
            break;
        }

        wchar_t name[128]{};
        const int length = GetKeyNameTextW(static_cast<LONG>(scan_code << 16), name, 128);
        if (length <= 0) {
            return {};
        }

        int required = WideCharToMultiByte(CP_UTF8, 0, name, length, nullptr, 0, nullptr, nullptr);
        if (required <= 0) {
            return {};
        }
        std::string utf8(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(CP_UTF8, 0, name, length, utf8.data(), required, nullptr, nullptr);
        return utf8;
    }
#endif

    std::array<bool, 256> key_states_{};
    std::array<bool, 3> button_states_{};
    int last_x_ = 0;
    int last_y_ = 0;
    bool has_mouse_ = false;

#if !defined(_WIN32) && !defined(__APPLE__) && defined(KISEKI_HAS_X11)
    Display* display_ = nullptr;
    Window root_{};
#endif
};

void write_skill_file(
    const std::filesystem::path& path,
    const std::string& title,
    const std::string& instruction_text,
    std::size_t frame_count,
    std::size_t keyframe_count,
    std::size_t event_count) {
    std::ofstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error{"failed to open " + path.string()};
    }

    file << "# " << (title.empty() ? "Recorded Kiseki Teaching" : title) << "\n\n";
    file << "This teaching bundle follows the action-sequence plus keyframe design: do not send a full video to a model when `actions.json`, `timeline.json`, and selected keyframes are enough.\n\n";
    file << "## Inputs\n\n";
    file << "- Manifest: `manifest.json`\n";
    file << "- Frames index: `frames.json` (" << frame_count << " captured frames)\n";
    file << "- Actions: `actions.json` (" << event_count << " actions/events)\n";
    file << "- Timeline: `timeline.json`\n";
    file << "- Raw events: `events.jsonl` (" << event_count << " events)\n";
    file << "- Selected keyframes: `keyframes/` (" << keyframe_count << " selected from captured frames)\n";
    file << "- Human annotations: `annotations.json`\n\n";
    if (!instruction_text.empty()) {
        file << "## Human Instruction\n\n";
        file << instruction_text << "\n\n";
    }
    file << "## Agent Reading Order\n\n";
    file << "1. Read `manifest.json` for artifact paths and capture settings.\n";
    file << "2. Read `instruction.txt` when present for the human teaching text.\n";
    file << "3. Read `actions.json` first; it is the compact action source of truth.\n";
    file << "4. Read `timeline.json` to align actions with selected keyframes.\n";
    file << "5. Inspect only the keyframes listed in `manifest.json` unless more visual detail is needed from `frames.json`.\n";
    file << "6. Apply `annotations.json` as frame/action-specific guidance.\n";
}

struct FrameRecord {
    std::size_t index = 0;
    std::uint64_t timestamp_ms = 0;
    std::filesystem::path path;
    int width = 0;
    int height = 0;
    std::optional<std::pair<int, int>> mouse;
};

bool is_anchor_event(const nlohmann::json& event) {
    const auto type = event.value("type", std::string{});
    if (type == "mouse_button") {
        return true;
    }
    if (type == "key") {
        const auto state = event.value("state", std::string{});
        return state == "down";
    }
    return false;
}

std::optional<std::size_t> nearest_frame_at_or_before(
    const std::vector<FrameRecord>& frames,
    std::uint64_t timestamp_ms) {
    if (frames.empty()) {
        return std::nullopt;
    }
    std::size_t selected = 0;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        if (frames[index].timestamp_ms <= timestamp_ms) {
            selected = index;
        } else {
            break;
        }
    }
    return selected;
}

std::map<std::size_t, std::string> select_keyframes(
    const std::vector<FrameRecord>& frames,
    const nlohmann::json& events,
    std::uint32_t minimum_interval_ms) {
    std::map<std::size_t, std::string> selected;
    if (frames.empty()) {
        return selected;
    }

    selected.emplace(0, "start");
    for (const auto& event : events) {
        if (!is_anchor_event(event)) {
            continue;
        }
        const auto frame_index = nearest_frame_at_or_before(frames, event.value("timestampMs", static_cast<std::uint64_t>(0)));
        if (!frame_index) {
            continue;
        }
        auto [iterator, inserted] = selected.emplace(*frame_index, "action-anchor");
        if (!inserted && iterator->second == "interval") {
            iterator->second = "action-anchor";
        }
    }

    const auto interval = std::max<std::uint32_t>(minimum_interval_ms, 1);
    for (std::size_t index = 0; index < frames.size(); ++index) {
        if (selected.contains(index)) {
            continue;
        }
        const auto timestamp = frames[index].timestamp_ms;
        std::uint64_t previous_gap = interval + 1;
        std::uint64_t next_gap = interval + 1;
        for (const auto& [selected_index, reason] : selected) {
            (void)reason;
            const auto selected_timestamp = frames[selected_index].timestamp_ms;
            if (selected_timestamp <= timestamp) {
                previous_gap = std::min(previous_gap, timestamp - selected_timestamp);
            } else {
                next_gap = std::min(next_gap, selected_timestamp - timestamp);
            }
        }
        if (previous_gap >= interval && next_gap >= interval) {
            selected.emplace(index, "interval");
        }
    }
    return selected;
}

nlohmann::json frame_to_json(const FrameRecord& frame, const std::filesystem::path& root) {
    nlohmann::json json{
        {"index", frame.index},
        {"timestampMs", frame.timestamp_ms},
        {"path", make_relative(root, frame.path).generic_string()},
        {"width", frame.width},
        {"height", frame.height},
    };
    if (frame.mouse) {
        json["mouse"] = {frame.mouse->first, frame.mouse->second};
        if (frame.width > 0 && frame.height > 0) {
            json["mouseNorm"] = {
                std::round((static_cast<double>(frame.mouse->first) / static_cast<double>(frame.width)) * 10000.0) / 10000.0,
                std::round((static_cast<double>(frame.mouse->second) / static_cast<double>(frame.height)) * 10000.0) / 10000.0,
            };
        }
    }
    return json;
}

nlohmann::json actions_from_events(const nlohmann::json& events) {
    nlohmann::json actions = nlohmann::json::array();
    for (const auto& event : events) {
        nlohmann::json action = event;
        action["actionIndex"] = actions.size();
        if (!action.contains("timestampMs")) {
            action["timestampMs"] = 0;
        }
        actions.push_back(std::move(action));
    }
    return actions;
}

std::string lowercase_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

bool is_video_file(const std::filesystem::path& path) {
    const auto extension = lowercase_extension(path);
    return extension == ".mp4" || extension == ".mov" || extension == ".mkv" || extension == ".webm" ||
           extension == ".avi" || extension == ".m4v";
}

std::vector<std::uint64_t> selected_video_timestamps(
    const std::vector<FrameRecord>& frames,
    const std::map<std::size_t, std::string>& selected,
    std::uint32_t max_frames) {
    std::vector<std::uint64_t> timestamps;
    for (const auto& [index, reason] : selected) {
        (void)reason;
        if (index < frames.size()) {
            timestamps.push_back(frames[index].timestamp_ms);
        }
    }
    if (max_frames == 0 || timestamps.size() <= max_frames) {
        return timestamps;
    }
    if (max_frames == 1) {
        return {timestamps.front()};
    }

    std::vector<std::uint64_t> limited;
    limited.reserve(max_frames);
    for (std::uint32_t i = 0; i < max_frames; ++i) {
        const auto source_index = static_cast<std::size_t>(
            std::round((static_cast<double>(i) / static_cast<double>(max_frames - 1)) * static_cast<double>(timestamps.size() - 1)));
        if (limited.empty() || limited.back() != timestamps[source_index]) {
            limited.push_back(timestamps[source_index]);
        }
    }
    return limited;
}

nlohmann::json extract_video_keyframes(
    const std::filesystem::path& video_path,
    const std::filesystem::path& session_directory,
    const std::vector<std::uint64_t>& timestamps_ms) {
    nlohmann::json extraction{
        {"schemaVersion", 1},
        {"source", make_relative(session_directory, video_path).generic_string()},
        {"tool", "ffmpeg"},
        {"frames", nlohmann::json::array()},
        {"warnings", nlohmann::json::array()},
    };
    if (timestamps_ms.empty()) {
        extraction["warnings"].push_back("no selected frame timestamps were available for video keyframe extraction");
        return extraction;
    }

    const auto output_directory = session_directory / "video_keyframes";
    std::filesystem::create_directories(output_directory);
    for (std::size_t index = 0; index < timestamps_ms.size(); ++index) {
        const auto output_path = output_directory / video_keyframe_filename(index);
        const double seconds = static_cast<double>(timestamps_ms[index]) / 1000.0;
        std::ostringstream seconds_stream;
        seconds_stream << std::fixed << std::setprecision(3) << seconds;
        std::ostringstream command;
        command << "ffmpeg -hide_banner -loglevel error -y"
                << " -ss " << shell_quote_arg(seconds_stream.str())
                << " -i " << shell_quote(video_path)
                << " -frames:v 1 -q:v 3 "
                << shell_quote(output_path);
        const int code = std::system(command.str().c_str());
        if (code != 0 || !std::filesystem::exists(output_path)) {
            extraction["warnings"].push_back(
                "ffmpeg could not extract a video keyframe at " + seconds_stream.str() +
                "s; install ffmpeg or disable extraction with --no-video-keyframes");
            break;
        }
        extraction["frames"].push_back({
            {"index", index},
            {"timestampMs", timestamps_ms[index]},
            {"path", make_relative(session_directory, output_path).generic_string()},
        });
    }
    return extraction;
}

nlohmann::json read_annotations(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return empty_annotations();
    }

    std::ifstream file{path, std::ios::binary};
    if (!file) {
        throw std::runtime_error{"failed to open " + path.string()};
    }
    nlohmann::json json = nlohmann::json::parse(file);
    if (!json.is_object() || !json.contains("annotations") || !json.at("annotations").is_array()) {
        throw std::runtime_error{"annotations.json has invalid shape"};
    }
    if (!json.contains("schemaVersion")) {
        json["schemaVersion"] = 1;
    }
    return json;
}

std::filesystem::path default_python() {
#ifdef _WIN32
    return "python";
#else
    return "python3";
#endif
}

OperationResult run_recording_worker_session(const RecordOptions& options) {
    const auto output_directory = options.output_directory.empty() ? default_output_directory() : options.output_directory;
    const auto state_file = options.state_file.empty() ? default_state_file() : options.state_file;
    const auto stop_file = options.stop_file.empty() ? default_stop_file(output_directory) : options.stop_file;

    if (options.frame_interval_ms == 0) {
        return fail("teach record frame-interval-ms must be greater than zero");
    }
    if (options.event_poll_ms == 0) {
        return fail("teach record event-poll-ms must be greater than zero");
    }
    if (options.video_keyframe_interval_ms == 0) {
        return fail("teach record video-keyframe-interval-ms must be greater than zero");
    }

    try {
        std::filesystem::create_directories(output_directory / "keyframes");
        remove_file_if_exists(stop_file);

        nlohmann::json manifest{
            {"schemaVersion", 2},
            {"kind", "kiseki-teach-recording"},
            {"format", "agivar-style-action-keyframe-bundle"},
            {"title", options.title},
            {"createdAtUtc", utc_stamp()},
            {"recordingControl", options.duration_ms > 0 ? "duration-or-stop-file" : "stop-file"},
            {"maxDurationMs", options.duration_ms},
            {"frameIntervalMs", options.frame_interval_ms},
            {"eventPollMs", options.event_poll_ms},
            {"videoKeyframeIntervalMs", options.video_keyframe_interval_ms},
            {"frameFormat", "bmp"},
            {"framesFile", "frames.json"},
            {"actionsFile", "actions.json"},
            {"eventsFile", "events.jsonl"},
            {"timelineFile", "timeline.json"},
            {"annotationsFile", "annotations.json"},
            {"keyframes", nlohmann::json::array()},
            {"media", nlohmann::json::object()},
            {"warnings", nlohmann::json::array()},
        };

        if (!options.instruction_text.empty()) {
            const auto instruction_path = output_directory / "instruction.txt";
            write_text_file(instruction_path, options.instruction_text);
            manifest["instructionFile"] = "instruction.txt";
        }

        const auto copied_video = copy_media_file(options.video_file, output_directory, "video", manifest);
        copy_media_file(options.audio_file, output_directory, "audio", manifest);
        if (!options.transcript_file.empty()) {
            copy_media_file(options.transcript_file, output_directory, "transcript", manifest);
        }

        write_json_file(output_directory / "annotations.json", empty_annotations());

        std::ofstream events_file{output_directory / "events.jsonl", std::ios::binary};
        if (!events_file) {
            return fail("failed to open events.jsonl");
        }

        nlohmann::json event_index = nlohmann::json::array();
        nlohmann::json raw_event_timeline{
            {"items", nlohmann::json::array()},
        };
        std::vector<FrameRecord> frames;
        std::size_t event_count = 0;

        NativeEventSampler sampler;
        if (const auto warning = sampler.initialize(); warning) {
            append_event(
                {
                    {"timestampMs", 0},
                    {"type", "recorder_status"},
                    {"level", "warning"},
                    {"message", *warning},
                },
                events_file,
                event_index,
                raw_event_timeline,
                event_count);
        }

        auto next_frame = Clock::now();
        auto next_event = Clock::now();
        const auto start = Clock::now();
        const auto stop_at = options.duration_ms > 0
            ? std::optional<Clock::time_point>{start + std::chrono::milliseconds{options.duration_ms}}
            : std::nullopt;

        while (true) {
            const auto now = Clock::now();
            if (std::filesystem::exists(stop_file)) {
                break;
            }
            if (stop_at && now >= *stop_at) {
                break;
            }

            const auto timestamp = elapsed_ms(start);
            if (now >= next_event) {
                for (auto& event : sampler.poll(timestamp)) {
                    append_event(std::move(event), events_file, event_index, raw_event_timeline, event_count);
                }
                next_event += std::chrono::milliseconds{options.event_poll_ms};
            }

            if (now >= next_frame) {
                const auto frame_index = frames.size();
                const auto frame_path = output_directory / "keyframes" / frame_filename(frame_index);
                const auto capture = kiseki::platform::capture::capture_desktop_bmp(frame_path);
                if (!capture.ok) {
                    return fail(capture.error);
                }
                frames.push_back(FrameRecord{
                    .index = frame_index,
                    .timestamp_ms = timestamp,
                    .path = frame_path,
                    .width = capture.width,
                    .height = capture.height,
                    .mouse = sampler.last_mouse_position(),
                });
                next_frame += std::chrono::milliseconds{options.frame_interval_ms};
            }

            auto sleep_until = std::min(next_frame, next_event);
            if (stop_at) {
                sleep_until = std::min(sleep_until, *stop_at);
            }
            const auto stop_poll = Clock::now() + std::chrono::milliseconds{100};
            sleep_until = std::min(sleep_until, stop_poll);
            if (sleep_until > Clock::now()) {
                std::this_thread::sleep_until(sleep_until);
            }
        }

        const auto actual_duration_ms = elapsed_ms(start);
        for (auto& event : sampler.poll(actual_duration_ms)) {
            append_event(std::move(event), events_file, event_index, raw_event_timeline, event_count);
        }
        append_event(
            {
                {"timestampMs", actual_duration_ms},
                {"type", "recorder_status"},
                {"level", "info"},
                {"message", std::filesystem::exists(stop_file) ? "stop requested" : "duration reached"},
            },
            events_file,
            event_index,
            raw_event_timeline,
            event_count);
        events_file.flush();

        const auto selected = select_keyframes(frames, event_index, options.video_keyframe_interval_ms);
        nlohmann::json frames_json{
            {"schemaVersion", 1},
            {"frameFormat", "bmp"},
            {"frames", nlohmann::json::array()},
        };
        for (const auto& frame : frames) {
            frames_json["frames"].push_back(frame_to_json(frame, output_directory));
        }

        nlohmann::json actions_json{
            {"schemaVersion", 1},
            {"actions", actions_from_events(event_index)},
        };

        nlohmann::json timeline{
            {"schemaVersion", 2},
            {"durationMs", actual_duration_ms},
            {"items", nlohmann::json::array()},
        };
        for (const auto& [frame_index, reason] : selected) {
            if (frame_index >= frames.size()) {
                continue;
            }
            auto frame_json = frame_to_json(frames[frame_index], output_directory);
            frame_json["selectionReason"] = reason;
            manifest["keyframes"].push_back(frame_json);
            timeline["items"].push_back({
                {"kind", "keyframe"},
                {"timestampMs", frames[frame_index].timestamp_ms},
                {"frameIndex", frame_index},
                {"path", make_relative(output_directory, frames[frame_index].path).generic_string()},
                {"selectionReason", reason},
            });
        }
        for (const auto& action : actions_json["actions"]) {
            timeline["items"].push_back({
                {"kind", "action"},
                {"timestampMs", action.value("timestampMs", 0)},
                {"actionIndex", action.value("actionIndex", 0)},
                {"eventIndex", action.value("index", 0)},
                {"type", action.value("type", "")},
            });
        }
        std::sort(timeline["items"].begin(), timeline["items"].end(), [](const nlohmann::json& left, const nlohmann::json& right) {
            const auto left_ts = left.value("timestampMs", 0);
            const auto right_ts = right.value("timestampMs", 0);
            if (left_ts != right_ts) {
                return left_ts < right_ts;
            }
            return left.value("kind", std::string{}) == "keyframe" && right.value("kind", std::string{}) != "keyframe";
        });

        if (copied_video && is_video_file(*copied_video) && !options.no_video_keyframes) {
            auto timestamps = selected_video_timestamps(frames, selected, options.video_keyframe_max);
            auto extraction = extract_video_keyframes(*copied_video, output_directory, timestamps);
            const auto extraction_index = output_directory / "video_keyframes" / "index.json";
            std::filesystem::create_directories(extraction_index.parent_path());
            write_json_file(extraction_index, extraction);
            manifest["media"]["videoKeyframes"] = make_relative(output_directory, extraction_index).generic_string();
            if (extraction.contains("warnings")) {
                for (const auto& warning : extraction["warnings"]) {
                    manifest["warnings"].push_back(warning);
                }
            }
        }

        manifest["actualDurationMs"] = actual_duration_ms;
        manifest["eventCount"] = event_count;
        manifest["actionCount"] = actions_json["actions"].size();
        manifest["frameCount"] = frames.size();
        manifest["keyframeCount"] = manifest["keyframes"].size();

        write_json_file(output_directory / "frames.json", frames_json);
        write_json_file(output_directory / "actions.json", actions_json);
        write_json_file(output_directory / "manifest.json", manifest);
        write_json_file(output_directory / "timeline.json", timeline);
        write_skill_file(
            output_directory / "SKILL.md",
            options.title,
            options.instruction_text,
            frames.size(),
            manifest["keyframes"].size(),
            event_count);

        remove_file_if_exists(stop_file);
        cleanup_state_if_current(state_file, output_directory);

        return ok(
            "recorded teaching bundle " + output_directory.string() + " frames=" +
            std::to_string(frames.size()) + " keyframes=" + std::to_string(manifest["keyframes"].size()) +
            " actions=" + std::to_string(actions_json["actions"].size()));
    } catch (const std::exception& error) {
        cleanup_state_if_current(state_file, output_directory);
        return fail(error.what());
    }
}

}

OperationResult record_teaching_session(const RecordOptions& options) {
    if (options.frame_interval_ms == 0) {
        return fail("teach record frame-interval-ms must be greater than zero");
    }
    if (options.event_poll_ms == 0) {
        return fail("teach record event-poll-ms must be greater than zero");
    }
    if (options.video_keyframe_interval_ms == 0) {
        return fail("teach record video-keyframe-interval-ms must be greater than zero");
    }
    if (options.worker) {
        return run_recording_worker_session(options);
    }

    try {
        const auto state_file = options.state_file.empty() ? default_state_file() : options.state_file;
        auto active = read_active_recording(state_file);
        if (active.valid) {
            return stop_active_recording(active, options.stop_timeout_ms);
        }

        const auto output_directory = absolute_path(options.output_directory.empty() ? default_output_directory() : options.output_directory);
        const auto stop_file = options.stop_file.empty() ? default_stop_file(output_directory) : absolute_path(options.stop_file);
        const auto log_file = default_log_file(output_directory);
        std::filesystem::create_directories(output_directory);
        remove_file_if_exists(stop_file);

        const auto pid = spawn_recording_worker(options, output_directory, absolute_path(state_file), stop_file, log_file);
        write_active_recording_state(absolute_path(state_file), pid, output_directory, stop_file, log_file);
        std::this_thread::sleep_for(std::chrono::milliseconds{150});
        if (!process_alive(pid)) {
            remove_file_if_exists(state_file);
            return fail("teach recording worker exited immediately; log=" + log_file.string());
        }
        return ok(
            "started teaching recording output=" + output_directory.string() + " pid=" + std::to_string(pid) +
            "; run `kiseki teach record` again to stop");
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

OperationResult add_text_annotation(const AnnotateOptions& options) {
    if (options.session_directory.empty()) {
        return fail("teach annotate requires --session");
    }
    if (!options.has_frame_index && !options.has_event_index) {
        return fail("teach annotate requires --frame-index or --event-index");
    }
    if (options.text.empty()) {
        return fail("teach annotate requires --text or --file");
    }

    try {
        const auto path = options.session_directory / "annotations.json";
        auto annotations = read_annotations(path);
        nlohmann::json item{
            {"id", "annotation-" + std::to_string(annotations.at("annotations").size() + 1)},
            {"createdAtUtc", utc_stamp()},
            {"text", options.text},
        };
        if (options.has_frame_index) {
            item["frameIndex"] = options.frame_index;
        }
        if (options.has_event_index) {
            item["eventIndex"] = options.event_index;
        }
        annotations["annotations"].push_back(std::move(item));
        write_json_file(path, annotations);
        return ok("annotation saved " + path.string());
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

OperationResult transcribe_audio(const TranscribeOptions& options) {
    if (options.audio_file.empty()) {
        return fail("teach transcribe requires --audio-file");
    }
    if (options.output_path.empty()) {
        return fail("teach transcribe requires --output");
    }
    if (options.model_path.empty()) {
        return fail("teach transcribe requires --model");
    }
    if (options.model_id.empty()) {
        return fail("teach transcribe requires --model-id");
    }
    if (!std::filesystem::exists(options.audio_file)) {
        return fail("audio file does not exist: " + options.audio_file.string());
    }
    if (!std::filesystem::exists(options.script_path)) {
        return fail("transcribe helper does not exist: " + options.script_path.string());
    }

    if (!options.output_path.parent_path().empty()) {
        std::filesystem::create_directories(options.output_path.parent_path());
    }
    std::ostringstream command;
    command << shell_quote(default_python())
            << ' ' << shell_quote(options.script_path)
            << " --model " << shell_quote(options.model_path)
            << " --model-id " << shell_quote_arg(options.model_id)
            << " --download-if-missing"
            << " --audio " << shell_quote(options.audio_file)
            << " --output " << shell_quote(options.output_path);
    if (!options.language.empty()) {
        command << " --language " << shell_quote_arg(options.language);
    }
    if (!options.device.empty()) {
        command << " --device " << shell_quote_arg(options.device);
    }
    if (!options.compute_type.empty()) {
        command << " --compute-type " << shell_quote_arg(options.compute_type);
    }

    const int code = std::system(command.str().c_str());
    if (code != 0) {
        return fail("faster-whisper transcription failed with exit code " + std::to_string(code));
    }
    return ok("transcribed audio to " + options.output_path.string());
}

}
