#include "platform/session/cua_runtime.hpp"
#include "platform/session/cua_process.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace kiseki::platform::session {
namespace {
using nlohmann::json;
using detail::path_text;
using detail::run_process;

std::string environment(const char* name) {
    return detail::environment_text(name);
}

std::filesystem::path state_directory() {
    if (const auto path = environment("KISEKI_CUA_STATE_DIR"); !path.empty()) return std::filesystem::u8path(path);
#ifdef _WIN32
    return std::filesystem::u8path(environment("LOCALAPPDATA")) / "Kiseki" / "cua";
#elif defined(__APPLE__)
    return std::filesystem::u8path(environment("HOME")) / "Library" / "Application Support" / "Kiseki" / "cua";
#else
    const auto xdg = environment("XDG_STATE_HOME");
    return (xdg.empty() ? std::filesystem::u8path(environment("HOME")) / ".local" / "state" : std::filesystem::u8path(xdg)) / "kiseki" / "cua";
#endif
}

// Kernel-owned lock is released if a setup process crashes. Concurrent workflow
// starts cannot run two installers or start two daemons.
struct SetupLock {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
    explicit SetupLock(const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        OVERLAPPED overlapped{};
        if (handle == INVALID_HANDLE_VALUE || !LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlapped)) {
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            throw std::runtime_error("cannot acquire CUA setup lock");
        }
#else
        handle = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (handle < 0 || flock(handle, LOCK_EX) != 0) {
            if (handle >= 0) close(handle);
            throw std::runtime_error("cannot acquire CUA setup lock");
        }
#endif
    }
    ~SetupLock() {
#ifdef _WIN32
        CloseHandle(handle);
#else
        close(handle);
#endif
    }
};

OperationResult result(const detail::ProcessResult& process) {
    return {.ok = process.code == 0, .code = process.code,
        .message = process.code == 0 ? process.output : "",
        .error = process.code == 0 ? process.error : process.error + process.output};
}

detail::ProcessResult driver(const std::filesystem::path& binary, std::vector<std::string> arguments) {
    arguments.insert(arguments.begin(), path_text(binary));
    return run_process(arguments);
}

std::vector<std::string> daemon_arguments(const std::string& command) {
    std::vector<std::string> arguments{command};
    if (const auto socket = environment("KISEKI_CUA_SOCKET"); !socket.empty()) {
        arguments.insert(arguments.end(), {"--socket", socket});
    }
    return arguments;
}

detail::ProcessResult install_driver() {
#ifdef _WIN32
    // Fixed script only: no caller text is interpolated into PowerShell.
    // Installation itself is per-user; daemon startup is handled below.
    return run_process({"powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command",
        "$ErrorActionPreference='Stop'; $p=Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString()+'.ps1'); "
        "try { Invoke-WebRequest -UseBasicParsing 'https://cua.ai/driver/install.ps1' -OutFile $p; & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $p -NoAutoStart; "
        "if ($LASTEXITCODE) { exit $LASTEXITCODE } } finally { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }"});
#else
    // pipefail makes a failed download fail installation rather than run an empty script.
    return run_process({"/bin/bash", "-o", "pipefail", "-c", "curl -fsSL https://cua.ai/driver/install.sh | /bin/bash"});
#endif
}

bool automatic_updates_enabled() {
    const auto preference = environment("KISEKI_CUA_AUTO_UPDATE");
    return preference != "0" && preference != "false" && preference != "off";
}

std::string automatic_update_exception() {
    if (!environment("KISEKI_CUA_DRIVER").empty()) return "explicit KISEKI_CUA_DRIVER is not automatically replaced";
    if (!environment("CUA_DRIVER_RS_VERSION").empty() || !environment("CUA_DRIVER_VERSION").empty()) return "explicit CUA version pin is preserved";
    if (!environment("KISEKI_CUA_SOCKET").empty()) return "custom daemon endpoint is managed by its owner";
    return {};
}

json read_state(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) return json::object();
    auto value = json::parse(stream, nullptr, false);
    return value.is_object() ? value : json::object();
}

std::string string_field(const json& object, const char* key) {
    return object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : "";
}

unsigned long long daemon_pid(const std::string& status) {
    const auto position = status.find("pid:");
    if (position == std::string::npos) return 0;
    try { return std::stoull(status.substr(position + 4)); } catch (...) { return 0; }
}

long long now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

json check_update(const std::filesystem::path& binary, bool force) {
    std::vector<std::string> args{"check-update", "--json"};
    if (force) args.push_back("--no-cache");
    const auto checked = driver(binary, args);
    auto payload = json::parse(checked.output, nullptr, false);
    if (checked.code != 0 || !payload.is_object()) {
        throw std::runtime_error("CUA update check failed: " + checked.error + checked.output);
    }
    if (payload.contains("error") && !payload["error"].is_null() && payload["error"] != "") {
        throw std::runtime_error("CUA update check failed: " + payload["error"].dump());
    }
    return payload;
}

void write_state(const std::filesystem::path& path, const json& value) {
    const auto temporary = path.parent_path() / "update-state.tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << value.dump(2);
    stream.close();
    if (!stream) throw std::runtime_error("cannot save CUA update state");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("cannot replace CUA update state");
#else
    std::filesystem::rename(temporary, path);
#endif
}

OperationResult failure(const std::exception& error) {
    return {.ok = false, .code = 2, .message = "", .error = error.what()};
}
}

OperationResult cua_driver_command(const std::vector<std::string>& arguments) {
    const auto binary = cua_driver_path();
    if (binary.empty()) return {.ok = false, .code = 2, .message = "", .error = "cua-driver was not found; run kiseki background cua setup"};
    return result(driver(binary, arguments));
}

OperationResult cua_update(bool apply, bool force_check) {
    try {
        const auto directory = state_directory();
        SetupLock lock(directory / "setup.lock");
        const auto binary = cua_driver_path();
        if (binary.empty()) throw std::runtime_error("cua-driver was not found; run kiseki background cua setup");
        auto state = check_update(binary, force_check);
        if (apply && state.value("update_available", false)) {
            const auto updated = driver(binary, {"update", "--apply"});
            state["apply_output"] = updated.output;
            state["apply_error"] = updated.error;
            state["applied"] = updated.code == 0;
            if (updated.code != 0) return result(updated);
            state["restart_required"] = true;
            state["next_step"] = "Run background cua setup before starting a fresh observation/action workflow";
        } else state["applied"] = false;
        return {.ok = true, .code = 0, .message = state.dump(2), .error = ""};
    } catch (const std::exception& error) { return failure(error); }
}

OperationResult cua_setup(bool update, int startup_wait_ms) {
    try {
        if (startup_wait_ms < 0) throw std::runtime_error("startup wait must be non-negative");
        const auto directory = state_directory();
        SetupLock lock(directory / "setup.lock");
        json report{{"installed", false}, {"updated", false}, {"warnings", json::array()}};
        auto binary = cua_driver_path();
        if (binary.empty()) {
            if (!environment("KISEKI_CUA_DRIVER").empty()) throw std::runtime_error("KISEKI_CUA_DRIVER does not point to an executable; exact overrides never fall back or install a different driver");
            const auto installed = install_driver();
            if (installed.code != 0) return result(installed);
            report["install_output"] = installed.output;
            report["install_diagnostics"] = installed.error;
            binary = cua_driver_path();
            if (binary.empty()) throw std::runtime_error("CUA installer completed but cua-driver was not found; inspect its installation output and set KISEKI_CUA_DRIVER for a custom location");
            report["installed"] = true;
        }

        const auto before = driver(binary, {"--version"});
        if (before.code != 0) return result(before);
        report["binary"] = path_text(binary);
        report["version"] = before.output;
        const auto exception = automatic_update_exception();
        if (update && automatic_updates_enabled() && exception.empty()) {
            const auto path = directory / "update-state.json";
            auto saved = read_state(path);
            const auto last = saved.contains("checked_at") && saved["checked_at"].is_number_integer() ? saved["checked_at"].get<long long>() : 0LL;
            const auto now = now_seconds();
            const bool due = string_field(saved, "binary") != path_text(binary) || string_field(saved, "version") != before.output || last <= 0 || last > now || now - last >= 86400;
            if (due) {
                // A network failure leaves the installed driver available and is
                // visible in the report; it never changes the action backend.
                try {
                    auto checked = check_update(binary, true);
                    report["update"] = checked;
                    if (checked.value("update_available", false)) {
                        const auto applied = driver(binary, {"update", "--apply"});
                        if (applied.code != 0) throw std::runtime_error("CUA update failed: " + applied.error + applied.output);
                        report["updated"] = true;
                        report["update_output"] = applied.output;
                        binary = cua_driver_path();
                        if (binary.empty()) throw std::runtime_error("updated CUA binary was not found");
                        report["version"] = driver(binary, {"--version"}).output;
                    }
                    write_state(path, {{"checked_at", now}, {"binary", path_text(binary)}, {"version", report["version"]}});
                } catch (const std::exception& error) { report["warnings"].push_back(error.what()); }
            } else report["update"] = {{"cached", true}, {"next_check_at", last + 86400}};
        } else report["update"] = {{"skipped", exception.empty() ? "automatic updates disabled for this workflow" : exception}};

        auto status = driver(binary, daemon_arguments("status"));
        const auto saved = read_state(directory / "daemon.json");
        const auto pid = daemon_pid(status.output);
        const bool owned = pid != 0 && saved.contains("pid") && saved["pid"] == pid && string_field(saved, "binary") == path_text(binary);
        const bool changed = owned && string_field(saved, "version") != report.value("version", "");
        if (status.code == 0 && changed) {
            const auto stopped = driver(binary, daemon_arguments("stop"));
            if (stopped.code != 0) return result(stopped);
            status.code = 1;
        }
        if (status.code != 0) {
            if (!environment("KISEKI_CUA_SOCKET").empty()) throw std::runtime_error("configured CUA daemon endpoint is unavailable: " + status.error + status.output);
            auto args = daemon_arguments("serve");
            detail::ProcessResult started;
#ifdef __APPLE__
            std::error_code resolve_error;
            const auto resolved = std::filesystem::canonical(binary, resolve_error);
            if (!resolve_error && resolved.parent_path().filename() == "MacOS" && resolved.parent_path().parent_path().filename() == "Contents") {
                args.insert(args.begin(), {"/usr/bin/open", "-n", "-g", "-a", path_text(resolved.parent_path().parent_path().parent_path()), "--args"});
                started = run_process(args);
            } else {
                args.insert(args.begin(), path_text(binary));
                started = detail::start_process(args, directory / "daemon.log");
            }
#else
            args.insert(args.begin(), path_text(binary));
            started = detail::start_process(args, directory / "daemon.log");
#endif
            if (started.code != 0) return result(started);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(startup_wait_ms);
            do {
                status = driver(binary, daemon_arguments("status"));
                if (status.code == 0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } while (std::chrono::steady_clock::now() < deadline);
            if (status.code != 0) throw std::runtime_error("CUA startup is not ready yet. Check macOS permission prompts or " + path_text(directory / "daemon.log.stderr") + "; background cua status can recheck without installing/restarting. " + status.error + status.output);
            write_state(directory / "daemon.json", {{"binary", path_text(binary)}, {"version", report["version"]}, {"pid", daemon_pid(status.output)}});
            report["started"] = true;
        } else report["started"] = false;
        report["daemon"] = status.output;
        report["ok"] = true;
        return {.ok = true, .code = 0, .message = report.dump(2), .error = ""};
    } catch (const std::exception& error) { return failure(error); }
}
}
