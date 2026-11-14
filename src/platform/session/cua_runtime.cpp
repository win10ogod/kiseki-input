#include "platform/session/cua_runtime.hpp"
#include "platform/session/cua_process.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
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

// Automatic checks run at most once per this interval for an installed binary and version.
constexpr std::int64_t automatic_check_interval_seconds = 24 * 60 * 60;

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

detail::ProcessResult driver(const std::filesystem::path& binary, std::vector<std::string> arguments,
    const detail::EnvironmentOverrides& overrides = {}) {
    arguments.insert(arguments.begin(), path_text(binary));
    return run_process(arguments, "", overrides);
}

std::vector<std::string> daemon_arguments(const std::string& command) {
    std::vector<std::string> arguments{command};
    if (const auto socket = environment("KISEKI_CUA_SOCKET"); !socket.empty()) {
        arguments.insert(arguments.end(), {"--socket", socket});
    }
    return arguments;
}

// Installs and updates that Kiseki starts do not connect the user's agents (`cua-driver connect` edits their MCP
// configuration) and do not run `cua-driver test` (it types into an app on the live desktop). Updates also leave shell
// startup files alone (the first install set up PATH; a shell without it would get the block appended again). The
// official installers honor these variables; a value the user set is kept.
detail::EnvironmentOverrides installer_environment(bool update) {
    std::vector<const char*> names{"CUA_DRIVER_NO_CONNECT", "CUA_DRIVER_NO_TEST"};
    if (update) names.insert(names.end(), {"CUA_DRIVER_NO_MODIFY_PATH", "CUA_DRIVER_RS_NO_MODIFY_PATH"});
    detail::EnvironmentOverrides overrides;
    for (const auto* name : names)
        if (environment(name).empty()) overrides.emplace_back(name, "1");
    return overrides;
}

detail::ProcessResult install_driver(bool update = false) {
#ifdef _WIN32
    // Fixed script only: no caller text is interpolated into PowerShell. -NoAutoStart: Kiseki starts the daemon in
    // the caller's session and does not register an elevated logon task; an existing task is refreshed by the installer.
    return run_process({"powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command",
        "$ErrorActionPreference='Stop'; $p=Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString()+'.ps1'); "
        "try { Invoke-WebRequest -UseBasicParsing 'https://cua.ai/driver/install.ps1' -OutFile $p; & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $p -NoAutoStart; "
        "if ($LASTEXITCODE) { exit $LASTEXITCODE } } finally { Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue }"},
        "", installer_environment(update));
#else
    // pipefail makes a failed download fail installation rather than run an empty script.
    return run_process({"/bin/bash", "-o", "pipefail", "-c", "curl -fsSL https://cua.ai/driver/install.sh | /bin/bash"}, "", installer_environment(update));
#endif
}

#ifdef _WIN32
// The per-user layout that the official install.ps1 maintains (Kiseki's first install uses it too).
bool official_windows_layout(const std::filesystem::path& binary) {
    const auto local = environment("LOCALAPPDATA");
    if (local.empty()) return false;
    std::error_code error;
    const bool same = std::filesystem::equivalent(binary,
        std::filesystem::u8path(local) / "Programs" / "Cua" / "cua-driver" / "bin" / "cua-driver.exe", error);
    return same && !error;
}
#endif

// On Windows `cua-driver update --apply` runs install.ps1 without switches, which registers an elevated logon task
// (a UAC prompt). For the official per-user layout Kiseki runs the same official installer with -NoAutoStart, as for a
// first install. Elsewhere the Driver's own updater runs, with the installer opt-outs in its environment.
detail::ProcessResult run_updater(const std::filesystem::path& binary) {
#ifdef _WIN32
    if (official_windows_layout(binary)) return install_driver(true);
#endif
    return driver(binary, {"update", "--apply"}, installer_environment(true));
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

json field(const json& object, const char* key) {
    return object.is_object() && object.contains(key) ? object[key] : json(nullptr);
}

std::string trimmed(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    text.erase(0, first);
    text.erase(text.find_last_not_of(" \t\r\n") + 1);
    return text;
}

// "cua-driver 0.49.9" -> "0.49.9"; "v0.49.9" -> "0.49.9"
std::string version_number(const std::string& version_output) {
    const auto text = trimmed(version_output);
    const auto space = text.find_last_of(' ');
    auto number = space == std::string::npos ? text : text.substr(space + 1);
    if (number.size() > 1 && number[0] == 'v' && std::isdigit(static_cast<unsigned char>(number[1]))) number.erase(0, 1);
    return number;
}

unsigned long long daemon_pid(const std::string& status) {
    const auto position = status.find("pid:");
    if (position == std::string::npos) return 0;
    try { return std::stoull(status.substr(position + 4)); } catch (...) { return 0; }
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string iso_time(std::int64_t seconds) {
    const auto value = static_cast<std::time_t>(seconds);
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &value);
#else
    gmtime_r(&value, &parts);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &parts);
    return buffer;
}

// BYPASS_CACHE asks GitHub again; otherwise the Driver answers from its own one-hour cache when it can.
json check_update(const std::filesystem::path& binary, bool bypass_cache) {
    std::vector<std::string> args{"check-update", "--json"};
    if (bypass_cache) args.push_back("--no-cache");
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
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << value.dump(2);
    stream.close();
    if (!stream) throw std::runtime_error("cannot save CUA state: " + path_text(path));
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("cannot replace CUA state: " + path_text(path));
#else
    // Kiseki's state files stay private to the user.
    std::filesystem::permissions(temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    std::filesystem::rename(temporary, path);
#endif
}

std::filesystem::path update_record(const std::filesystem::path& directory) { return directory / "update-state.json"; }

// A record says "this binary at this version was up to date at checked_at". Anything else makes a check due.
bool automatic_check_due(const json& record, const std::filesystem::path& binary, const std::string& version, std::int64_t now) {
    std::error_code error;
    const auto recorded = string_field(record, "binary");
    if (recorded.empty() || !std::filesystem::equivalent(std::filesystem::u8path(recorded), binary, error) || error) return true;
    if (version_number(string_field(record, "version")) != version_number(version)) return true;
    if (!record.contains("checked_at") || !record["checked_at"].is_number_integer()) return true;
    const auto checked = record["checked_at"].get<std::int64_t>();
    return checked > now || now - checked >= automatic_check_interval_seconds;
}

void record_up_to_date(const std::filesystem::path& directory, const std::filesystem::path& binary, const std::string& version,
    const json& checked, std::int64_t now) {
    write_state(update_record(directory), {{"binary", path_text(binary)}, {"version", trimmed(version)}, {"checked_at", now},
        {"selected_channel", field(checked, "selected_channel")}, {"latest_version", field(checked, "latest_version")}});
}

struct AppliedUpdate {
    bool ok = false;
    std::filesystem::path binary;
    std::string version; // `--version` of the installed binary afterwards
    detail::ProcessResult updater;
    std::string note;
};

// Runs the updater and judges it by the version installed afterwards, not only by its exit status: the official POSIX
// installer stops every process named cua-driver, including the `cua-driver update --apply` that started it, and keeps
// running on its own. When the updater was stopped by a signal, Kiseki waits for that installer to finish.
AppliedUpdate apply_update(const std::filesystem::path& binary, const std::string& before, const json& checked) {
    AppliedUpdate applied;
    applied.updater = run_updater(binary);
    const auto target = version_number(string_field(checked, "latest_version"));
    const auto reached = [&]() {
        applied.binary = cua_driver_path();
        if (applied.binary.empty()) return false;
        const auto version = driver(applied.binary, {"--version"});
        if (version.code != 0) return false;
        applied.version = version.output;
        const auto installed = version_number(version.output);
        return target.empty() ? installed != version_number(before) : installed == target;
    };
    bool done = reached();
#ifndef _WIN32
    if (!done && applied.updater.code > 128) {
        applied.note = "the updater was stopped by signal " + std::to_string(applied.updater.code - 128) +
            " (the installer stops every cua-driver process); Kiseki waited for the installer to finish";
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
        while (!done && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            done = reached();
        }
    }
#endif
    applied.ok = done || (applied.updater.code == 0 && !applied.version.empty());
    return applied;
}

// The desktop session a Linux daemon serves is fixed when it starts (X11 display, Wayland socket, session bus).
json session_environment() {
    json session = json::object();
#if defined(__linux__)
    for (const auto* name : {"DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "XDG_RUNTIME_DIR"})
        if (const auto value = environment(name); !value.empty()) session[name] = value;
#endif
    return session;
}

// A tool answer from the running daemon, or an empty object.
json daemon_answer(const std::filesystem::path& binary, const std::string& tool) {
    std::vector<std::string> arguments{path_text(binary), "call", tool};
    if (const auto socket = environment("KISEKI_CUA_SOCKET"); !socket.empty()) arguments.insert(arguments.end(), {"--socket", socket});
    const auto answer = run_process(arguments, "{}");
    const auto payload = json::parse(answer.output, nullptr, false);
    return answer.code == 0 && payload.is_object() ? payload : json::object();
}

// The version the running daemon reports (get_config), or "" when it cannot tell.
std::string running_daemon_version(const std::filesystem::path& binary) {
    return string_field(daemon_answer(binary, "get_config"), "version");
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
        const auto before = driver(binary, {"--version"});
        auto state = check_update(binary, force_check);
        const auto now = now_seconds();
        if (apply && state.value("update_available", false)) {
            const auto updated = apply_update(binary, before.output, state);
            state["apply_output"] = updated.updater.output;
            state["apply_error"] = updated.updater.error;
            state["applied"] = updated.ok;
            if (!updated.note.empty()) state["apply_note"] = updated.note;
            if (!updated.ok) {
                return {.ok = false, .code = updated.updater.code != 0 ? updated.updater.code : 2, .message = "",
                    .error = "CUA update failed; installed version: " + (updated.version.empty() ? std::string{"unknown"} : trimmed(updated.version)) +
                        "\n" + updated.updater.error + updated.updater.output};
            }
            state["previous_version"] = trimmed(before.output);
            state["installed_version"] = trimmed(updated.version);
            record_up_to_date(directory, updated.binary, updated.version, state, now);
            state["next_step"] = "Run background cua setup before starting a fresh observation/action workflow";
        } else {
            state["applied"] = false;
            if (before.code == 0 && !state.value("update_available", false)) record_up_to_date(directory, binary, before.output, state, now);
        }
        return {.ok = true, .code = 0, .message = state.dump(2), .error = ""};
    } catch (const std::exception& error) { return failure(error); }
}

OperationResult cua_setup(bool update, int startup_wait_ms, bool ensure_daemon) {
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
            const auto now = now_seconds();
            const auto record = read_state(update_record(directory));
            if (!automatic_check_due(record, binary, before.output, now)) {
                const auto checked_at = record["checked_at"].get<std::int64_t>();
                report["update"] = {{"status", "not_due"}, {"checked_at", iso_time(checked_at)},
                    {"next_check_after", iso_time(checked_at + automatic_check_interval_seconds)},
                    {"latest_version", field(record, "latest_version")}, {"selected_channel", field(record, "selected_channel")}};
            } else {
                // An offline or failed check stays visible, leaves the installed runtime usable and is retried at the next workflow start.
                json status{{"status", "check_failed"}};
                try {
                    const auto checked = check_update(binary, false);
                    status = {{"status", "up_to_date"}, {"check", checked}};
                    if (checked.value("update_available", false)) {
                        status["status"] = "apply_failed";
                        const auto applied = apply_update(binary, before.output, checked);
                        if (!applied.note.empty()) status["apply_note"] = applied.note;
                        if (!applied.ok) {
                            if (!applied.version.empty()) {
                                // Report what is installed now, even when it is not the version the check announced.
                                binary = applied.binary;
                                report["binary"] = path_text(binary);
                                report["version"] = applied.version;
                            }
                            throw std::runtime_error("CUA update failed; installed version: " +
                                (applied.version.empty() ? std::string{"unknown"} : trimmed(applied.version)) + "\n" +
                                applied.updater.error + applied.updater.output);
                        }
                        binary = applied.binary;
                        report["updated"] = true;
                        report["binary"] = path_text(binary);
                        report["previous_version"] = before.output;
                        report["version"] = applied.version;
                        // `check` keeps the pre-update payload (its current_version is the replaced one).
                        status["status"] = "applied";
                        status["previous_version"] = trimmed(before.output);
                        status["installed_version"] = trimmed(applied.version);
                        status["installer_output"] = applied.updater.output;
                        record_up_to_date(directory, binary, applied.version, checked, now);
                    } else {
                        record_up_to_date(directory, binary, before.output, checked, now);
                    }
                } catch (const std::exception& error) { report["warnings"].push_back(error.what()); }
                report["update"] = status;
            }
        } else report["update"] = {{"status", "skipped"}, {"reason", exception.empty() ? "automatic updates disabled for this workflow" : exception}};

        const bool replaced = report["installed"].get<bool>() || report["updated"].get<bool>();
        const auto daemon_record = directory / "daemon.json";
        const auto saved = read_state(daemon_record);
        const auto session = session_environment();
        const bool session_moved = saved.contains("session_environment") && saved["session_environment"] != session;
        if (!ensure_daemon && !replaced && !session_moved) {
            report["daemon"] = "not checked; started when a call finds no daemon";
            report["ok"] = true;
            return {.ok = true, .code = 0, .message = report.dump(2), .error = ""};
        }

        auto status = driver(binary, daemon_arguments("status"));
        const auto pid = daemon_pid(status.output);
        // The daemon Kiseki started is the one still serving when its recorded pid is the running one.
        const bool owned = status.code == 0 && pid != 0 && saved.contains("pid") && saved["pid"] == pid;
        if (status.code == 0 && !saved.empty() && !owned) {
            // Another daemon serves now (started by an installer, an autostart task or the user); the record is stale.
            std::error_code ignored;
            std::filesystem::remove(daemon_record, ignored);
        }
        const bool version_changed = owned && version_number(string_field(saved, "version")) != version_number(report.value("version", ""));
        const bool changed = version_changed || (owned && session_moved);
        if (changed) report["restart_reason"] = version_changed ? "installed version changed" : "desktop session changed";
#if defined(__linux__)
        if (status.code == 0 && !owned) {
            // A daemon Kiseki did not start may serve another desktop session; it is not restarted, but the mismatch is reported.
            const auto permissions = daemon_answer(binary, "check_permissions");
            const auto bus = string_field(permissions, "dbus_session_bus_address");
            const auto own_bus = environment("DBUS_SESSION_BUS_ADDRESS");
            const bool other_bus = !bus.empty() && !own_bus.empty() && bus != own_bus;
            const bool no_display = permissions.contains("x11") && permissions["x11"] == false && !environment("DISPLAY").empty();
            if (other_bus || no_display)
                report["warnings"].push_back("the running daemon was not started by Kiseki and does not serve this desktop session (its session bus " +
                    (bus.empty() ? std::string{"unknown"} : bus) + (no_display ? ", no X11 display" : "") + "; this shell has DISPLAY=" +
                    environment("DISPLAY") + "). Stop it with `background cua driver -- stop` and run setup in this session.");
        }
#endif
        if (status.code == 0 && changed) {
            const auto stopped = driver(binary, daemon_arguments("stop"));
            if (stopped.code != 0) return result(stopped);
            status.code = 1;
        }
        if (status.code != 0) {
            if (!environment("KISEKI_CUA_SOCKET").empty()) throw std::runtime_error("configured CUA daemon endpoint is unavailable: " + status.error + status.output);
            auto args = daemon_arguments("serve");
            detail::ProcessResult started;
            std::string readiness_hint = "inspect " + path_text(directory / "daemon.log.stderr");
#ifdef __APPLE__
            std::error_code resolve_error;
            const auto resolved = std::filesystem::canonical(binary, resolve_error);
            if (!resolve_error && resolved.parent_path().filename() == "MacOS" && resolved.parent_path().parent_path().filename() == "Contents") {
                args.insert(args.begin(), {"/usr/bin/open", "-n", "-g", "-a", path_text(resolved.parent_path().parent_path().parent_path()), "--args"});
                started = run_process(args);
                readiness_hint = "answer CuaDriver's macOS permission prompts (the app bundle owns its output)";
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
            if (status.code != 0) throw std::runtime_error("CUA startup is not ready yet: " + readiness_hint + "; background cua status or health rechecks without installing/restarting. " + status.error + status.output);
            // Record the version of the binary that was actually launched.
            const auto launched = driver(binary, {"--version"});
            json record{{"binary", path_text(binary)}, {"version", trimmed(launched.code == 0 ? launched.output : report["version"].get<std::string>())},
                {"pid", daemon_pid(status.output)}};
            if (const auto session = session_environment(); !session.empty()) record["session_environment"] = session;
            write_state(daemon_record, record);
            report["started"] = true;
            report["daemon_owner"] = "kiseki";
        } else {
            report["started"] = false;
            report["daemon_owner"] = owned ? "kiseki" : "external";
        }
        report["daemon"] = status.output;
        if (report["updated"].get<bool>() && !report["started"].get<bool>()) {
            // An installer may already have restarted the daemon; say which version actually serves.
            const auto running = running_daemon_version(binary);
            report["daemon_version"] = running.empty() ? json(nullptr) : json(running);
            if (!running.empty() && running != version_number(report["version"].get<std::string>()))
                report["warnings"].push_back("the running daemon reports " + running + " while " + trimmed(report["version"].get<std::string>()) +
                    " is installed; Kiseki restarts only a daemon it started, so restart this one the way it was started");
        }
        report["ok"] = true;
        return {.ok = true, .code = 0, .message = report.dump(2), .error = ""};
    } catch (const std::exception& error) { return failure(error); }
}
}
