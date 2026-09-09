#include "platform/session/cua_process.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace kiseki::platform::session::detail {
namespace {
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        static std::atomic<unsigned long long> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (;;) {
            auto candidate = std::filesystem::temp_directory_path() /
                ("kiseki-cua-" + std::to_string(stamp) + "-" + std::to_string(counter++));
            if (std::filesystem::create_directory(candidate)) {
                path = std::move(candidate);
#ifndef _WIN32
                std::filesystem::permissions(path, std::filesystem::perms::owner_all);
#endif
                break;
            }
        }
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

#ifdef _WIN32
std::wstring wide(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size == 0 && !text.empty()) throw std::runtime_error("invalid UTF-8 process argument");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring quote_argument(const std::string& argument) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (const auto c : wide(argument)) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#endif

ProcessResult launch(const std::vector<std::string>& args, const std::filesystem::path& input,
    const std::filesystem::path& output, const std::filesystem::path& error, bool wait) {
    if (args.empty()) return {2, "", "empty process command"};
#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const HANDLE in = CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const HANDLE out = CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const HANDLE err = CreateFileW(error.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (in == INVALID_HANDLE_VALUE || out == INVALID_HANDLE_VALUE || err == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        for (const auto handle : {in, out, err}) if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        return {2, "", "cannot open CUA process streams: " + std::to_string(code)};
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = in; startup.hStdOutput = out; startup.hStdError = err;
    std::wstring command;
    for (const auto& arg : args) { if (!command.empty()) command += L' '; command += quote_argument(arg); }
    PROCESS_INFORMATION process{};
    const bool launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
    const auto launch_error = GetLastError();
    for (const auto handle : {in, out, err}) CloseHandle(handle);
    if (!launched) return {2, "", "cannot launch " + args.front() + ": Windows error " + std::to_string(launch_error)};
    CloseHandle(process.hThread);
    DWORD code = 0;
    if (wait) {
        if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_FAILED || !GetExitCodeProcess(process.hProcess, &code)) code = 2;
    }
    CloseHandle(process.hProcess);
    return {static_cast<int>(code), "", ""};
#else
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    int setup = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, input.c_str(), O_RDONLY, 0600);
    if (!setup) setup = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (!setup) setup = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, error.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    pid_t pid = 0;
    const int launched = setup ? setup : posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (launched != 0) return {2, "", "cannot launch " + args.front() + ": " + std::strerror(launched)};
    if (!wait) return {0, "", ""};
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return {2, "", "cannot wait for " + args.front() + ": " + std::strerror(errno)};
    }
    return {WIFEXITED(status) ? WEXITSTATUS(status) : 2, "", ""};
#endif
}
}

std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::string environment_text(const char* name) {
#ifdef _WIN32
    const auto key = wide(name);
    const auto length = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (length == 0) return {};
    std::wstring value(length, L'\0');
    const auto copied = GetEnvironmentVariableW(key.c_str(), value.data(), length);
    if (copied == 0 || copied >= length) return {};
    value.resize(copied);
    const auto size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
#else
    const auto value = std::getenv(name);
    return value ? value : "";
#endif
}

ProcessResult run_process(const std::vector<std::string>& arguments, const std::string& input) {
    try {
        TemporaryDirectory temp;
        const auto stdin_path = temp.path / "stdin";
        std::ofstream stream(stdin_path, std::ios::binary);
        stream << input;
        stream.close();
        if (!stream) return {2, "", "cannot write CUA process input"};
        auto result = launch(arguments, stdin_path, temp.path / "stdout", temp.path / "stderr", true);
        result.output = read_file(temp.path / "stdout");
        result.error += read_file(temp.path / "stderr");
        return result;
    } catch (const std::exception& error) { return {2, "", error.what()}; }
}

ProcessResult start_process(const std::vector<std::string>& arguments, const std::filesystem::path& log) {
    try {
        std::filesystem::create_directories(log.parent_path());
        auto errors = log; errors += ".stderr";
#ifdef _WIN32
        return launch(arguments, L"NUL", log, errors, false);
#else
        return launch(arguments, "/dev/null", log, errors, false);
#endif
    } catch (const std::exception& error) { return {2, "", error.what()}; }
}
}
