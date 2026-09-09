#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>
#include <nlohmann/json.hpp>

// A process boundary fixture. It never installs software or sends desktop input.
int main(int argc, char** argv) {
    using nlohmann::json;
    const std::filesystem::path directory = std::filesystem::u8path(std::getenv("KISEKI_CUA_TEST_DIR"));
    json config;
    std::ifstream(directory / "config.json") >> config;
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string input{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
    std::ofstream(directory / "calls.jsonl", std::ios::app) << json{{"args", args}, {"stdin", input}}.dump() << '\n';
    if (args.empty()) return 2;
    auto current = config.value("current", "0.24.0");
    if (std::ifstream version(directory / "version"); version) std::getline(version, current);
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
    } else if (args[0] == "status") {
        if (std::filesystem::exists(directory / "stopped")) return 1;
        std::cout << "daemon running\n  pid: 9001\n";
    } else if (args[0] == "stop") std::ofstream(directory / "stopped");
    else if (args[0] == "serve") std::filesystem::remove(directory / "stopped");
    else if (args[0] == "call") {
        auto arguments = json::parse(input);
#ifdef _WIN32
        if (config.value("extended_path", false) && arguments.contains("screenshot_out_file")) {
            arguments["screenshot_out_file"] = "\\\\?\\" + arguments["screenshot_out_file"].get<std::string>();
        }
#endif
        if (config.contains("stdout")) std::cout << config["stdout"].get<std::string>();
        else if (args[1] == "list_windows") std::cout << json{{"windows", json::array({{{"pid", 42}, {"window_id", 1099511627793ULL}}})}};
        else std::cout << arguments;
        if (config.value("write_screenshot", false) && arguments.contains("screenshot_out_file")) {
            std::ofstream(std::filesystem::u8path(arguments["screenshot_out_file"].get<std::string>()), std::ios::binary) << "new screenshot";
        }
    } else std::cout << json{{"args", args}};
    std::cerr << config.value("stderr", "");
    return config.value("exit_code", 0);
}
