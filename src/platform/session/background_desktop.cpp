#include "platform/session/background_desktop.hpp"

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "platform/capture/screenshot.hpp"
#include "platform/input/input.hpp"

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef KISEKI_HAS_X11
#include <X11/Xlib.h>
#endif
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

OperationResult fail(std::string error) {
    return OperationResult{
        .ok = false,
        .code = 2,
        .message = "",
        .error = std::move(error),
    };
}

CaptureResult fail_capture(const std::filesystem::path& path, std::string error) {
    return CaptureResult{
        .ok = false,
        .code = 2,
        .output_path = path,
        .width = 0,
        .height = 0,
        .error = std::move(error),
    };
}

bool valid_display(const std::string& display) {
    return !display.empty() && display[0] == ':';
}

std::string sanitized_display_name(const std::string& display) {
    std::string name;
    name.reserve(display.size());
    for (const char c : display) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            name.push_back(c);
        } else {
            name.push_back('_');
        }
    }
    return name.empty() ? "_default" : name;
}

std::filesystem::path state_directory_or_default(const std::filesystem::path& requested) {
    return requested.empty() ? default_state_directory() : requested;
}

std::filesystem::path pid_file_for(const std::filesystem::path& state_directory, const std::string& display) {
    return state_directory / ("xvfb" + sanitized_display_name(display) + ".pid");
}

#ifndef _WIN32
bool command_available(const std::string& name) {
    const char* path = std::getenv("PATH");
    if (path == nullptr) {
        return false;
    }

    std::string current;
    std::istringstream stream{path};
    while (std::getline(stream, current, ':')) {
        if (current.empty()) {
            current = ".";
        }
        const auto candidate = std::filesystem::path{current} / name;
        if (access(candidate.c_str(), X_OK) == 0) {
            return true;
        }
    }
    return false;
}

bool process_alive(pid_t pid) {
    return pid > 0 && kill(pid, 0) == 0;
}

bool wait_until_process_exits(pid_t pid, int attempts, std::chrono::milliseconds delay) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (!process_alive(pid)) {
            return true;
        }
        std::this_thread::sleep_for(delay);
    }
    return !process_alive(pid);
}

std::optional<pid_t> read_pid_file(const std::filesystem::path& path) {
    std::ifstream file{path};
    long long value = 0;
    if (!(file >> value) || value <= 0) {
        return std::nullopt;
    }
    return static_cast<pid_t>(value);
}

void redirect_stdio_to_null() {
    const int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0) {
        return;
    }
    dup2(null_fd, STDIN_FILENO);
    dup2(null_fd, STDOUT_FILENO);
    dup2(null_fd, STDERR_FILENO);
    if (null_fd > STDERR_FILENO) {
        close(null_fd);
    }
}

#ifdef KISEKI_HAS_X11
bool wait_for_display(const std::string& display) {
    for (int attempt = 0; attempt < 30; ++attempt) {
        Display* xdisplay = XOpenDisplay(display.c_str());
        if (xdisplay != nullptr) {
            XCloseDisplay(xdisplay);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    return false;
}
#endif

class ScopedDisplay {
public:
    explicit ScopedDisplay(std::string display)
        : had_old_{std::getenv("DISPLAY") != nullptr},
          old_{had_old_ ? std::getenv("DISPLAY") : ""},
          active_{setenv("DISPLAY", display.c_str(), 1) == 0} {
    }

    ~ScopedDisplay() {
        if (!active_) {
            return;
        }
        if (had_old_) {
            setenv("DISPLAY", old_.c_str(), 1);
        } else {
            unsetenv("DISPLAY");
        }
    }

    bool active() const {
        return active_;
    }

private:
    bool had_old_ = false;
    std::string old_;
    bool active_ = false;
};
#endif

std::string unsupported_message() {
#ifdef _WIN32
    return "Linux background desktop requires a Linux host with X11/Xvfb; Windows uses selected-window background screenshots";
#else
#ifndef KISEKI_HAS_X11
    return "Linux background desktop requires a build with X11 support";
#else
    return "";
#endif
#endif
}

}

bool background_desktop_available() {
#ifdef _WIN32
    return false;
#else
#ifdef KISEKI_HAS_X11
    return command_available("Xvfb");
#else
    return false;
#endif
#endif
}

std::filesystem::path default_state_directory() {
#ifdef _WIN32
    const char* temp = std::getenv("TEMP");
    return std::filesystem::path{temp != nullptr ? temp : "."} / "kiseki-input" / "background-desktops";
#else
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime != nullptr && runtime[0] != '\0') {
        return std::filesystem::path{runtime} / "kiseki-input" / "background-desktops";
    }
    return std::filesystem::temp_directory_path() / "kiseki-input" / "background-desktops";
#endif
}

OperationResult start_background_desktop(const BackgroundDesktopStartOptions& options) {
    if (!valid_display(options.display)) {
        return fail("background desktop display must look like :99");
    }
    if (options.width <= 0 || options.height <= 0 || options.depth <= 0) {
        return fail("background desktop width, height, and depth must be positive");
    }

    const auto unsupported = unsupported_message();
    if (!unsupported.empty()) {
        return fail(unsupported);
    }

#ifndef _WIN32
    if (!command_available("Xvfb")) {
        return fail("Xvfb was not found on PATH");
    }

    const auto state_directory = state_directory_or_default(options.state_directory);
    std::error_code error_code;
    std::filesystem::create_directories(state_directory, error_code);
    if (error_code) {
        return fail("failed to create background desktop state directory: " + error_code.message());
    }

    const auto pid_file = pid_file_for(state_directory, options.display);
    if (const auto pid = read_pid_file(pid_file); pid && process_alive(*pid)) {
        return ok("background desktop already running on " + options.display);
    }

    const pid_t pid = fork();
    if (pid < 0) {
        return fail(std::string{"fork failed: "} + std::strerror(errno));
    }
    if (pid == 0) {
        setsid();
        redirect_stdio_to_null();
        const std::string screen = std::to_string(options.width) + "x" +
                                   std::to_string(options.height) + "x" +
                                   std::to_string(options.depth);
        execlp("Xvfb", "Xvfb", options.display.c_str(), "-screen", "0", screen.c_str(), "-nolisten", "tcp", static_cast<char*>(nullptr));
        _exit(127);
    }

    {
        std::ofstream file{pid_file};
        if (!file) {
            kill(pid, SIGTERM);
            return fail("failed to write background desktop pid file");
        }
        file << pid << '\n';
    }

#ifdef KISEKI_HAS_X11
    if (!wait_for_display(options.display)) {
        kill(pid, SIGTERM);
        std::filesystem::remove(pid_file, error_code);
        return fail("Xvfb started but the background DISPLAY did not become available");
    }
#endif

    return ok("background desktop started on " + options.display);
#else
    return fail(unsupported);
#endif
}

OperationResult stop_background_desktop(const BackgroundDesktopStopOptions& options) {
    if (!valid_display(options.display)) {
        return fail("background desktop display must look like :99");
    }

    const auto unsupported = unsupported_message();
    if (!unsupported.empty()) {
        return fail(unsupported);
    }

#ifndef _WIN32
    const auto pid_file = pid_file_for(state_directory_or_default(options.state_directory), options.display);
    const auto pid = read_pid_file(pid_file);
    if (!pid) {
        return ok("background desktop was not running on " + options.display);
    }
    if (process_alive(*pid) && kill(*pid, SIGTERM) != 0 && errno != ESRCH) {
        return fail(std::string{"failed to stop Xvfb: "} + std::strerror(errno));
    }
    if (!wait_until_process_exits(*pid, 20, std::chrono::milliseconds{50})) {
        if (kill(*pid, SIGKILL) != 0 && errno != ESRCH) {
            return fail(std::string{"failed to force-stop Xvfb: "} + std::strerror(errno));
        }
        if (!wait_until_process_exits(*pid, 20, std::chrono::milliseconds{50})) {
            return fail("Xvfb did not exit after stop request");
        }
    }
    std::error_code error_code;
    std::filesystem::remove(pid_file, error_code);
    return ok("background desktop stopped on " + options.display);
#else
    return fail(unsupported);
#endif
}

OperationResult launch_in_background_desktop(const BackgroundDesktopLaunchOptions& options) {
    if (!valid_display(options.display)) {
        return fail("background desktop display must look like :99");
    }
    if (options.command.empty()) {
        return fail("background desktop launch requires --command");
    }

    const auto unsupported = unsupported_message();
    if (!unsupported.empty()) {
        return fail(unsupported);
    }

#ifndef _WIN32
    const pid_t pid = fork();
    if (pid < 0) {
        return fail(std::string{"fork failed: "} + std::strerror(errno));
    }
    if (pid == 0) {
        setsid();
        setenv("DISPLAY", options.display.c_str(), 1);
        redirect_stdio_to_null();
        execlp("/bin/sh", "sh", "-lc", options.command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    return ok("launched command in background desktop with pid " + std::to_string(pid));
#else
    return fail(unsupported);
#endif
}

CaptureResult screenshot_background_desktop(const BackgroundDesktopScreenshotOptions& options) {
    if (!valid_display(options.display)) {
        return fail_capture(options.output_path, "background desktop display must look like :99");
    }

    const auto unsupported = unsupported_message();
    if (!unsupported.empty()) {
        return fail_capture(options.output_path, unsupported);
    }

#ifndef _WIN32
    ScopedDisplay display{options.display};
    if (!display.active()) {
        return fail_capture(options.output_path, "failed to select background DISPLAY");
    }
    return kiseki::platform::capture::capture_desktop_bmp(options.output_path);
#else
    return fail_capture(options.output_path, unsupported);
#endif
}

OperationResult text_background_desktop(const BackgroundDesktopTextOptions& options) {
    if (!valid_display(options.display)) {
        return fail("background desktop display must look like :99");
    }

    const auto unsupported = unsupported_message();
    if (!unsupported.empty()) {
        return fail(unsupported);
    }

#ifndef _WIN32
    ScopedDisplay display{options.display};
    if (!display.active()) {
        return fail("failed to select background DISPLAY");
    }
    return kiseki::platform::input::type_text(options.text);
#else
    return fail(unsupported);
#endif
}

OperationResult key_background_desktop(const BackgroundDesktopKeyOptions& options) {
    if (!valid_display(options.display)) {
        return fail("background desktop display must look like :99");
    }

    const auto unsupported = unsupported_message();
    if (!unsupported.empty()) {
        return fail(unsupported);
    }

#ifndef _WIN32
    ScopedDisplay display{options.display};
    if (!display.active()) {
        return fail("failed to select background DISPLAY");
    }
    return kiseki::platform::input::tap_key(options.key, "system");
#else
    return fail(unsupported);
#endif
}

OperationResult mouse_background_desktop(const BackgroundDesktopMouseOptions& options) {
    if (!valid_display(options.display)) {
        return fail("background desktop display must look like :99");
    }

    const auto unsupported = unsupported_message();
    if (!unsupported.empty()) {
        return fail(unsupported);
    }

#ifndef _WIN32
    ScopedDisplay display{options.display};
    if (!display.active()) {
        return fail("failed to select background DISPLAY");
    }
    return kiseki::platform::input::mouse_action(kiseki::platform::input::MouseOptions{
        .dx = 0,
        .dy = 0,
        .x = options.x,
        .y = options.y,
        .absolute = true,
        .backend = "system",
        .click = options.click,
    });
#else
    return fail(unsupported);
#endif
}

}
