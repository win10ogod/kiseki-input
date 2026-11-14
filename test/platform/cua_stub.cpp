#include <filesystem>
#include "core/utf8_path.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>
#include <nlohmann/json.hpp>

// A process boundary fixture. It never installs software or sends desktop input.
int main(int argc, char** argv) {
    using nlohmann::json;
    const std::filesystem::path directory = kiseki::core::path_from_utf8(std::getenv("KISEKI_CUA_TEST_DIR"));
    json config;
    std::ifstream(directory / "config.json") >> config;
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string input{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
    json environment = json::object();
    for (const auto* name : {"CUA_DRIVER_NO_CONNECT", "CUA_DRIVER_NO_TEST", "CUA_DRIVER_NO_MODIFY_PATH", "CUA_DRIVER_RS_NO_MODIFY_PATH"})
        if (const auto* value = std::getenv(name)) environment[name] = value;
    std::ofstream(directory / "calls.jsonl", std::ios::app) << json{{"args", args}, {"stdin", input}, {"env", environment}}.dump() << '\n';
    if (args.empty()) return 2;
    auto current = config.value("current", "0.24.0");
    if (std::ifstream version(directory / "version"); version) std::getline(version, current);
    const auto unknown = [&](const std::string& tool) {
        if (!config.contains("unknown_tools")) return false;
        for (const auto& name : config["unknown_tools"]) if (name == tool) return true;
        return false;
    };
    if (args[0] == "--version") std::cout << "cua-driver " << current << '\n';
    else if (args[0] == "check-update") {
        if (config.contains("check_error")) { std::cerr << config["check_error"].get<std::string>(); return 7; }
        std::cout << json{{"current_version", current}, {"latest_version", config.value("latest", current)},
            {"selected_channel", config.value("channel", "stable")}, {"update_available", config.value("latest", current) != current}, {"error", nullptr}};
    } else if (args[0] == "update") {
        if (config.contains("update_error")) { std::cerr << config["update_error"].get<std::string>(); return 8; }
        std::ofstream(directory / "version") << config.value("latest", current);
        if (config.value("stop_on_update", true)) std::ofstream(directory / "stopped");
        std::cout << "updated";
        // The official POSIX installer stops every cua-driver process, including the updater that started it.
        if (config.value("updater_killed", false)) return 143;
    } else if (args[0] == "status") {
        if (std::filesystem::exists(directory / "stopped")) return 1;
        std::cout << "daemon running\n  pid: " << config.value("daemon_pid", 9001) << "\n";
    } else if (args[0] == "stop") std::ofstream(directory / "stopped");
    else if (args[0] == "serve") std::filesystem::remove(directory / "stopped");
    else if (args[0] == "describe" && args.size() > 1 && unknown(args[1])) {
        std::cout << "Unknown tool: " << args[1] << "\nAvailable tools:\n  click\n";
        return 64;
    } else if (args[0] == "call") {
        if (unknown(args[1])) { std::cerr << "Unknown tool: " << args[1]; return 64; }
        auto arguments = json::parse(input);
        // Scripted session lifetime: the named session has ended until start_session revives it.
        if (config.contains("ended_session") && arguments.value("session", std::string{}) == config["ended_session"].get<std::string>()) {
            if (args[1] == "start_session") {
                if (config.value("revive_fails", false)) { std::cerr << "cannot start session"; return 5; }
                std::ofstream(directory / "revived");
                std::cout << json{{"revived", true}, {"session", arguments["session"]}};
                return 0;
            }
            if (!std::filesystem::exists(directory / "revived")) {
                if (config.value("ended_text_049", false))
                    std::cerr << "session has ended; tool call '" << args[1] << "' was rejected. Call start_session with session '"
                              << arguments["session"].get<std::string>() << "' to start it again, or use a new session label.";
                else
                    std::cerr << "session '" << arguments["session"].get<std::string>() << "' has ended; tool call '" << args[1]
                              << "' was rejected. Call start_session with this id to revive it before issuing further actions, or use a new session id.";
                return 1;
            }
        }
        if (config.contains("daemon_down") || (config.value("calls_need_daemon", false) && std::filesystem::exists(directory / "stopped"))) {
            std::cerr << "Cua Driver daemon is not running on " << config.value("daemon_down", std::string{"socket"}) << ".\nStart it first with: cua-driver serve";
            return 1;
        }
#ifdef _WIN32
        if (config.value("extended_path", false) && arguments.contains("screenshot_out_file")) {
            arguments["screenshot_out_file"] = "\\\\?\\" + arguments["screenshot_out_file"].get<std::string>();
        }
#endif
        if (config.contains("tool_stdout") && config["tool_stdout"].contains(args[1])) {
            std::cout << config["tool_stdout"][args[1]].get<std::string>();
            return config.contains("tool_exit") && config["tool_exit"].contains(args[1]) ? config["tool_exit"][args[1]].get<int>() : 0;
        }
        if (config.contains("stdout")) std::cout << config["stdout"].get<std::string>();
        else if (args[1] == "list_windows") std::cout << json{{"windows", json::array({{{"pid", 42}, {"window_id", 1099511627793ULL}}})}};
        else if (args[1] == "get_config") std::cout << json{{"version", config.value("daemon_version", current)}};
        else if (args[1] == "run_steps") {
            // Runs the steps in order and stops at FAIL_STEP like the provider.
            const auto& steps = arguments["steps"];
            const int fail = config.value("fail_step", -1);
            json results = json::array();
            for (std::size_t index = 0; index < steps.size(); ++index) {
                if (static_cast<int>(index) == fail) {
                    results.push_back({{"index", index}, {"ok", false}, {"tool", "drag"}, {"code", "background_unavailable"}, {"message", "posted events are dropped"}});
                    std::cout << json{{"ok", false}, {"failed_step", index}, {"executed", index + 1}, {"total", steps.size()}, {"steps", results}};
                    return 1;
                }
                results.push_back({{"index", index}, {"ok", true}, {"tool", "drag"}});
            }
            std::cout << json{{"ok", true}, {"failed_step", nullptr}, {"executed", steps.size()}, {"total", steps.size()}, {"steps", results}};
        }
        else std::cout << arguments;
        if (config.value("write_screenshot", false) && arguments.contains("screenshot_out_file")) {
            std::ofstream image(kiseki::core::path_from_utf8(arguments["screenshot_out_file"].get<std::string>()), std::ios::binary);
            if (config.value("screenshot_bytes", std::string{}) == "jpeg") image << "\xFF\xD8\xFF\xE0 jpeg screenshot";
            else image << "new screenshot";
        }
        if (config.value("write_cli_screenshot", false)) {
            for (std::size_t i = 0; i + 1 < args.size(); ++i)
                if (args[i] == "--screenshot-out-file")
                    std::ofstream(kiseki::core::path_from_utf8(args[i + 1]), std::ios::binary) << "new CLI screenshot";
        }
    } else std::cout << json{{"args", args}};
    std::cerr << config.value("stderr", "");
    return config.value("exit_code", 0);
}
