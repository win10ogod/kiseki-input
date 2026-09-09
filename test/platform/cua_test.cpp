#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <memory>
#include <nlohmann/json.hpp>
#include "platform/session/cua_process.hpp"
#include "platform/session/cua_runtime.hpp"
#include "platform/session/macos_cua.hpp"

namespace {
using nlohmann::json;
using namespace kiseki::platform::session;

struct Environment {
    std::string key;
    std::optional<std::string> previous;
    Environment(std::string name, const std::string& value) : key(std::move(name)) {
        if (const auto old = std::getenv(key.c_str())) previous = old;
        set(value);
    }
    void set(const std::string& value) {
#ifdef _WIN32
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 1);
#endif
    }
    ~Environment() {
#ifdef _WIN32
        _putenv_s(key.c_str(), previous.value_or("").c_str());
#else
        if (previous) set(*previous); else unsetenv(key.c_str());
#endif
    }
};

struct Fixture {
    std::filesystem::path directory;
    std::filesystem::path binary;
    std::vector<std::unique_ptr<Environment>> environment;
    Fixture() {
        directory = std::filesystem::temp_directory_path() / ("kiseki cua tests " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
#ifdef _WIN32
        binary = directory / "cua-driver.exe";
        const std::string separator = ";";
#else
        binary = directory / "cua-driver";
        const std::string separator = ":";
#endif
        std::filesystem::copy_file(std::filesystem::u8path(KISEKI_CUA_STUB), binary);
        env("KISEKI_CUA_TEST_DIR", detail::path_text(directory));
        env("KISEKI_CUA_STATE_DIR", detail::path_text(directory / "state"));
        env("KISEKI_CUA_DRIVER", "");
        env("KISEKI_CUA_SESSION", "precision session");
        env("KISEKI_CUA_SOCKET", "");
        env("KISEKI_CUA_AUTO_UPDATE", "1");
        env("CUA_DRIVER_RS_VERSION", "");
        env("CUA_DRIVER_VERSION", "");
        env("PATH", detail::path_text(directory) + separator + (std::getenv("PATH") ? std::getenv("PATH") : ""));
        config({});
    }
    void env(const std::string& key, const std::string& value) { environment.push_back(std::make_unique<Environment>(key, value)); }
    void config(const json& value) { std::ofstream(directory / "config.json") << (value.is_null() ? json::object() : value); }
    std::vector<json> calls() {
        std::vector<json> result;
        std::ifstream stream(directory / "calls.jsonl");
        std::string line;
        while (std::getline(stream, line)) result.push_back(json::parse(line));
        return result;
    }
    ~Fixture() { while (!environment.empty()) environment.pop_back(); std::error_code error; std::filesystem::remove_all(directory, error); }
};
}

TEST_CASE("CUA process preserves literal arguments, long stdin, stdout and stderr") {
    Fixture fixture;
    fixture.config({{"stderr", "provider warning"}});
    const json input{{"text", std::string(200000, 'x') + "\" %PATH% & $(printf bad) `bad` \\"}, {"limit", 900000}};
    const auto result = cua_call("echo", input);
    REQUIRE(result.ok);
    REQUIRE(json::parse(result.message) == input);
    REQUIRE(result.error == "provider warning");
    const std::string literal = R"(name with "quotes" & %PATH% \\)";
    const auto args = cua_driver_command({"describe", literal});
    REQUIRE(args.ok);
    REQUIRE(json::parse(args.message)["args"][1] == literal);
}

TEST_CASE("CUA exact invalid override never falls back to installed binary") {
    Fixture fixture;
    fixture.env("KISEKI_CUA_DRIVER", detail::path_text(fixture.directory / "missing.exe"));
    REQUIRE_FALSE(cua_background_available());
    REQUIRE_FALSE(cua_setup().ok);
    REQUIRE(fixture.calls().empty());
}

TEST_CASE("CUA tool failures retain native exit code and both output streams") {
    Fixture fixture;
    fixture.config({{"exit_code", 19}, {"stdout", "partial result"}, {"stderr", "provider error"}});
    const auto result = cua_call("echo", json::object());
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.code == 19);
    REQUIRE(result.error.find("partial result") != std::string::npos);
    REQUIRE(result.error.find("provider error") != std::string::npos);
}

TEST_CASE("CUA indexed actions retain full window IDs and snapshot identity") {
    Fixture fixture;
    MacCuaClickOptions options;
    options.pid = 42;
    options.window_id = 1099511627793ULL;
    options.has_window_id = true;
    options.element_index = 8;
    options.has_element_index = true;
    options.snapshot_id = "snapshot-7";
    options.element_token = "opaque-token";
    const auto clicked = macos_cua_click(options);
    REQUIRE(clicked.ok);
    const auto sent = json::parse(clicked.message);
    REQUIRE(sent["window_id"] == options.window_id);
    REQUIRE(sent["snapshot_id"] == options.snapshot_id);
    REQUIRE(sent["element_token"] == options.element_token);
    REQUIRE(sent["session"] == "precision session");
    MacCuaWindowStateOptions state{.pid = 42, .window_id = options.window_id};
    REQUIRE(json::parse(macos_cua_window_state(state).message)["session"] == "precision session");
    REQUIRE(json::parse(macos_cua_feedback_state().message)["session"] == "precision session");
}

TEST_CASE("CUA workflow checks latest selected channel at each start and starts daemon") {
    Fixture fixture;
    fixture.config({{"current", "0.24.0"}, {"latest", "0.25.0"}, {"channel", "nightly"}});
    const auto first = cua_setup();
    INFO(first.error);
    REQUIRE(first.ok);
    const auto report = json::parse(first.message);
    REQUIRE(report["updated"] == true);
    auto version = report["version"].get<std::string>();
    version.erase(std::remove(version.begin(), version.end(), '\r'), version.end());
    REQUIRE(version == "cua-driver 0.25.0\n");
    REQUIRE(report["update"]["selected_channel"] == "nightly");
    REQUIRE(cua_setup().ok);
    int checks = 0, applies = 0;
    for (const auto& call : fixture.calls()) {
        if (call["args"][0] == "check-update") { ++checks; REQUIRE(call["args"] == json::array({"check-update", "--json", "--no-cache"})); }
        if (call["args"][0] == "update") ++applies;
    }
    REQUIRE(checks == 2);
    REQUIRE(applies == 1);
}

TEST_CASE("CUA automatic update failure is visible and does not suppress installed runtime") {
    Fixture fixture;
    fixture.config({{"check_error", "network unavailable"}});
    const auto result = cua_setup();
    REQUIRE(result.ok);
    REQUIRE(json::parse(result.message)["warnings"][0].get<std::string>().find("network unavailable") != std::string::npos);
    REQUIRE_FALSE(json::parse(result.message)["updated"].get<bool>());
}

TEST_CASE("CUA explicit pins and no-update workflows do not run updater") {
    Fixture fixture;
    SECTION("exact path") { fixture.env("KISEKI_CUA_DRIVER", detail::path_text(fixture.binary)); }
    SECTION("version") { fixture.env("CUA_DRIVER_RS_VERSION", "0.24.0"); }
    SECTION("disabled") { fixture.env("KISEKI_CUA_AUTO_UPDATE", "0"); }
    SECTION("workflow") { REQUIRE(cua_setup(false).ok); return; }
    REQUIRE(cua_setup().ok);
    for (const auto& call : fixture.calls()) REQUIRE(call["args"][0] != "check-update");
}

TEST_CASE("CUA raw arguments must be an object before process dispatch") {
    Fixture fixture;
    REQUIRE_FALSE(cua_call("click", json::array({1, 2})).ok);
    REQUIRE(fixture.calls().empty());
}

TEST_CASE("CUA screenshot rejects stale files and atomically replaces a new capture") {
    Fixture fixture;
    const auto path = fixture.directory / "capture.png";
    std::ofstream(path) << "previous capture";
    MacCuaScreenshotOptions options{.window_id = 1099511627793ULL, .output_path = path};
    REQUIRE_FALSE(macos_cua_screenshot(options).ok);
    std::string contents;
    { std::ifstream stream(path); std::getline(stream, contents); }
    REQUIRE(contents == "previous capture");
    fixture.config({{"write_screenshot", true}, {"extended_path", true}});
    const auto result = macos_cua_screenshot(options);
    INFO(result.error);
    REQUIRE(result.ok);
    { std::ifstream stream(path); std::getline(stream, contents); }
    REQUIRE(contents == "new screenshot");
    REQUIRE(std::filesystem::equivalent(std::filesystem::u8path(json::parse(result.message)["screenshot_out_file"].get<std::string>()), path));
}

TEST_CASE("CUA structured refusal is a failure even when provider process exits zero") {
    Fixture fixture;
    fixture.config({{"stdout", R"({"status":"refused","refusal":{"code":"target_unavailable"}})"}});
    const auto result = cua_call("click", {{"pid", 42}, {"x", 20}, {"y", 30}});
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.code == 2);
    REQUIRE(result.error.find("target_unavailable") != std::string::npos);
}
