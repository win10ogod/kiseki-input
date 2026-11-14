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
        for (const auto* name : {"CUA_DRIVER_NO_CONNECT", "CUA_DRIVER_NO_TEST", "CUA_DRIVER_NO_MODIFY_PATH", "CUA_DRIVER_RS_NO_MODIFY_PATH"}) env(name, "");
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
    // The JSON requests sent to TOOL through `cua-driver call`, in order.
    std::vector<json> requests(const std::string& tool) {
        std::vector<json> result;
        for (const auto& call : calls())
            if (call["args"][0] == "call" && call["args"][1] == tool) result.push_back(json::parse(call["stdin"].get<std::string>()));
        return result;
    }
    std::size_t count(const std::string& first_argument) {
        std::size_t total = 0;
        for (const auto& call : calls()) if (call["args"][0] == first_argument) ++total;
        return total;
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

TEST_CASE("CUA indexed actions are sent as the element token of their own snapshot") {
    Fixture fixture;
    MacCuaClickOptions options;
    options.pid = 42;
    options.window_id = 1099511627793ULL;
    options.has_window_id = true;
    options.element_index = 8;
    options.has_element_index = true;
    options.snapshot_id = "s0000002a";
    const auto clicked = macos_cua_click(options);
    REQUIRE(clicked.ok);
    const auto sent = json::parse(clicked.message);
    REQUIRE(sent["window_id"] == options.window_id);
    REQUIRE(sent["element_token"] == "s0000002a:8");
    REQUIRE_FALSE(sent.contains("element_index"));
    REQUIRE_FALSE(sent.contains("snapshot_id"));
    REQUIRE(sent["session"] == "precision session");

    // text, key and hotkey bind an index the same way; an explicit token is sent unchanged.
    MacCuaTextOptions text{.pid = 42, .text = "x", .element_index = 3, .has_element_index = true, .snapshot_id = "s00000001"};
    REQUIRE(json::parse(macos_cua_type_text(text).message)["element_token"] == "s00000001:3");
    MacCuaKeyOptions key{.pid = 42, .key = "return", .element_token = "s00000001:4"};
    REQUIRE(json::parse(macos_cua_press_key(key).message)["element_token"] == "s00000001:4");
    MacCuaHotkeyOptions hotkey{.pid = 42, .keys = {"ctrl", "end"}, .element_index = 0, .has_element_index = true, .snapshot_id = "s00000001"};
    REQUIRE(json::parse(macos_cua_hotkey(hotkey).message)["element_token"] == "s00000001:0");

    // An index without its snapshot, or one that disagrees with the token, never reaches the provider.
    const auto before = fixture.calls().size();
    options.snapshot_id.clear();
    REQUIRE(macos_cua_click(options).error.find("--snapshot-id") != std::string::npos);
    options.snapshot_id = "s0000002a";
    options.element_token = "s0000002a:9";
    REQUIRE(macos_cua_click(options).error.find("does not match") != std::string::npos);
    text.snapshot_id.clear();
    REQUIRE_FALSE(macos_cua_type_text(text).ok);
    REQUIRE(fixture.calls().size() == before);

    MacCuaWindowStateOptions state{.pid = 42, .window_id = options.window_id};
    REQUIRE(json::parse(macos_cua_window_state(state).message)["session"] == "precision session");
    REQUIRE(json::parse(macos_cua_feedback_state().message)["session"] == "precision session");
}

TEST_CASE("CUA element tokens follow the 0.49 snapshot:index form") {
    REQUIRE(cua_element_token(false, 0, "", "") == "");
    REQUIRE(cua_element_token(false, 0, "", "s00000001:2") == "s00000001:2");
    REQUIRE(cua_element_token(true, 11, "s0000002a", "") == "s0000002a:11");
    REQUIRE(cua_element_token(true, 11, "s0000002a", "s0000002a:11") == "s0000002a:11");
    REQUIRE_THROWS_AS(cua_element_token(true, 11, "", ""), std::invalid_argument);
    REQUIRE_THROWS_AS(cua_element_token(true, -1, "s0000002a", ""), std::invalid_argument);
    REQUIRE_THROWS_AS(cua_element_token(false, 0, "s0000002a", ""), std::invalid_argument);
    REQUIRE_THROWS_AS(cua_element_token(true, 11, "s0000002a", "s0000002b:11"), std::invalid_argument);
}

TEST_CASE("CUA text sends a character delay only when one was requested") {
    Fixture fixture;
    MacCuaTextOptions text{.pid = 42, .text = "batched"};
    REQUIRE_FALSE(json::parse(macos_cua_type_text(text).message).contains("delay_ms"));
    text.delay_ms = 15;
    REQUIRE(json::parse(macos_cua_type_text(text).message)["delay_ms"] == 15);
}

TEST_CASE("CUA automatic update check runs at most once a day and applies an available update") {
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
    REQUIRE(report["update"]["status"] == "applied");
    REQUIRE(report["update"]["installed_version"] == "cua-driver 0.25.0");
    REQUIRE(report["update"]["check"]["selected_channel"] == "nightly");
    REQUIRE(report["daemon_owner"] == "kiseki");
    // The installed version is the one recorded for the daemon Kiseki started.
    std::ifstream daemon_stream(fixture.directory / "state" / "daemon.json");
    REQUIRE(json::parse(daemon_stream)["version"] == "cua-driver 0.25.0");

    // The next workflow start is within a day of a successful check: no check, no daemon probe.
    const auto second = cua_setup(true, 30000, false);
    REQUIRE(second.ok);
    REQUIRE(json::parse(second.message)["update"]["status"] == "not_due");
    std::size_t checks = 0;
    for (const auto& call : fixture.calls()) {
        if (call["args"][0] != "check-update") continue;
        ++checks;
        // Automatic checks use the Driver's own cache.
        REQUIRE(call["args"] == json::array({"check-update", "--json"}));
    }
    REQUIRE(checks == 1);
    REQUIRE(fixture.count("update") == 1);

    // An explicit update bypasses the cache.
    REQUIRE(cua_update(false).ok);
    REQUIRE(fixture.calls().back()["args"] == json::array({"check-update", "--json", "--no-cache"}));
}

TEST_CASE("CUA updates started by Kiseki opt out of agent connection, self-test and shell edits") {
    Fixture fixture;
    fixture.config({{"current", "0.24.0"}, {"latest", "0.25.0"}});
    REQUIRE(cua_setup().ok);
    json environment;
    for (const auto& call : fixture.calls()) if (call["args"][0] == "update") environment = call["env"];
    REQUIRE(environment["CUA_DRIVER_NO_CONNECT"] == "1");
    REQUIRE(environment["CUA_DRIVER_NO_TEST"] == "1");
    REQUIRE(environment["CUA_DRIVER_NO_MODIFY_PATH"] == "1");

    // A value the user chose is kept.
    Fixture chosen;
    chosen.env("CUA_DRIVER_NO_CONNECT", "0");
    chosen.config({{"current", "0.24.0"}, {"latest", "0.25.0"}});
    REQUIRE(cua_update(true).ok);
    for (const auto& call : chosen.calls()) if (call["args"][0] == "update") environment = call["env"];
    REQUIRE(environment["CUA_DRIVER_NO_CONNECT"] == "0");
    REQUIRE(environment["CUA_DRIVER_NO_TEST"] == "1");
}

TEST_CASE("CUA judges an update by the installed version when the installer stopped the updater") {
    Fixture fixture;
    fixture.config({{"current", "0.25.0"}, {"latest", "0.49.9"}, {"updater_killed", true}});
    const auto result = cua_setup();
    INFO(result.error);
    REQUIRE(result.ok);
    const auto report = json::parse(result.message);
    REQUIRE(report["updated"] == true);
    REQUIRE(report["update"]["installed_version"] == "cua-driver 0.49.9");
    REQUIRE(report["warnings"].empty());

    // A failed update reports the version that is actually installed.
    Fixture failing;
    failing.config({{"current", "0.25.0"}, {"latest", "0.49.9"}, {"update_error", "download failed"}});
    const auto failed = cua_setup();
    REQUIRE(failed.ok);
    const auto failed_report = json::parse(failed.message);
    REQUIRE_FALSE(failed_report["updated"].get<bool>());
    REQUIRE(failed_report["update"]["status"] == "apply_failed");
    REQUIRE(failed_report["warnings"][0].get<std::string>().find("installed version: cua-driver 0.25.0") != std::string::npos);
}

TEST_CASE("CUA automatic update failure is visible and does not suppress installed runtime") {
    Fixture fixture;
    fixture.config({{"check_error", "network unavailable"}});
    const auto result = cua_setup();
    REQUIRE(result.ok);
    REQUIRE(json::parse(result.message)["warnings"][0].get<std::string>().find("network unavailable") != std::string::npos);
    REQUIRE_FALSE(json::parse(result.message)["updated"].get<bool>());
    REQUIRE(json::parse(result.message)["update"]["status"] == "check_failed");
    // A failed check is not recorded, so the next workflow start checks again.
    REQUIRE(cua_setup().ok);
    REQUIRE(fixture.count("check-update") == 2);
}

TEST_CASE("CUA setup drops a stale daemon record and reports who owns the running daemon") {
    Fixture fixture;
    std::filesystem::create_directories(fixture.directory / "state");
    std::ofstream(fixture.directory / "state" / "daemon.json") << json{{"binary", "old"}, {"version", "cua-driver 0.30.1"}, {"pid", 2688}}.dump();
    const auto result = cua_setup(false);
    REQUIRE(result.ok);
    const auto report = json::parse(result.message);
    REQUIRE(report["started"] == false);
    REQUIRE(report["daemon_owner"] == "external");
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "state" / "daemon.json"));
    REQUIRE(fixture.count("stop") == 0);
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

TEST_CASE("CUA validates JSON and CLI screenshot outputs before replacing either") {
    Fixture fixture;
    const auto json_path = fixture.directory / "json.png";
    const auto cli_path = fixture.directory / "cli.png";
    std::ofstream(json_path) << "previous JSON";
    std::ofstream(cli_path) << "previous CLI";
    const json arguments{{"screenshot_out_file", detail::path_text(json_path)}};
    fixture.config({{"write_screenshot", true}});
    REQUIRE_FALSE(cua_call("get_window_state", arguments, cli_path).ok);
    std::string content;
    { std::ifstream stream(json_path); std::getline(stream, content); }
    REQUIRE(content == "previous JSON");
    { std::ifstream stream(cli_path); std::getline(stream, content); }
    REQUIRE(content == "previous CLI");
    fixture.config({{"write_screenshot", true}, {"write_cli_screenshot", true}});
    REQUIRE(cua_call("get_window_state", arguments, cli_path).ok);
    { std::ifstream stream(cli_path); std::getline(stream, content); }
    REQUIRE(content == "new CLI screenshot");
    REQUIRE(cua_call("get_window_state", arguments, json_path).ok);
}

TEST_CASE("CUA 0.30 action refusals fail across plain and structured envelopes") {
    Fixture fixture;
    for (const auto& payload : std::vector<json>{
        {{"effect", "refused"}, {"code", "capture_not_found"}, {"detail", "expired capture"}},
        {{"structuredContent", {{"effect", "refused"}, {"code", "background_unavailable"}}}},
        {{"structured_content", {{"ok", false}}}},
        // Cua Driver 0.28.1 answers a background drag into a Qt canvas with only a code, and nothing was done.
        {{"code", "background_unavailable"}},
        {{"code", "foreground_unavailable"}, {"message", "no focus"}}
    }) {
        fixture.config({{"stdout", payload.dump()}});
        const auto result = cua_call("click", {{"capture_id", "capture-1"}});
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.error.find(payload.dump()) != std::string::npos);
    }
    for (const auto* effect : {"confirmed", "submitted", "unverifiable"}) {
        const json payload{{"effect", effect}, {"detail", "retained"}};
        fixture.config({{"stdout", payload.dump()}});
        const auto result = cua_call("click", json::object());
        REQUIRE(result.ok);
        REQUIRE(json::parse(result.message) == payload);
    }
    // A code that names what happened is not a refusal, and neither is a long answer that happens to carry a code.
    for (const auto& payload : std::vector<json>{
        {{"code", "bring_to_front_exact_window_verified"}, {"activated", true}},
        {{"code", "screenshot_unavailable"}, {"elements", json::array()}, {"window_id", 7}, {"pid", 42}, {"snapshot_id", "s1"}}
    }) {
        fixture.config({{"stdout", payload.dump()}});
        const auto result = cua_call("get_window_state", json::object());
        REQUIRE(result.ok);
        REQUIRE(json::parse(result.message) == payload);
    }
}

TEST_CASE("CUA draw stops at the first segment the driver cannot deliver and says why") {
    MacCuaDrawOptions options;
    options.pid = 42;
    options.window_id = 7;
    options.points = {{10, 10}, {20, 20}, {30, 30}, {40, 40}};
    options.duration_ms = 10;

    // Batched: run_steps stops at the failing step, and the segment is named.
    Fixture batched;
    batched.config({{"fail_step", 1}});
    auto result = macos_cua_draw(options);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("segment 2 failed: background_unavailable") != std::string::npos);
    REQUIRE(batched.requests("run_steps").size() == 1);
    REQUIRE(batched.requests("drag").empty());

    // A Driver without run_steps gets one drag per segment, and the first refusal stops the path.
    Fixture fixture;
    fixture.config({{"unknown_tools", {"run_steps"}}, {"tool_stdout", {{"drag", R"({"code":"background_unavailable"})"}}}});
    result = macos_cua_draw(options);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("segment 1 failed") != std::string::npos);
    REQUIRE(result.error.find("background_unavailable") != std::string::npos);
    REQUIRE(fixture.requests("drag").size() == 1);

    // Delivered segments are all sent, and the count is the truth.
    Fixture working;
    options.points = {{10, 10}, {20, 20}, {30, 30}};
    const auto done = macos_cua_draw(options);
    REQUIRE(done.ok);
    REQUIRE(done.message.find("sent 2 drag segment(s)") != std::string::npos);
}

TEST_CASE("CUA draw batches exact segments, per-segment timing and the requested gap through run_steps") {
    Fixture fixture;
    MacCuaDrawOptions options;
    options.pid = 42;
    options.window_id = 7;
    for (int index = 0; index <= 70; ++index) options.points.push_back({10.5 + index, 20.25 + 2 * index});
    options.segment_durations_ms.assign(70, 15);
    options.segment_durations_ms[0] = 40;
    options.steps = 3;
    options.stroke_gap_ms = 25;
    options.modifiers = {"shift"};
    options.provider_json = R"({"delivery_mode":"background"})";
    const auto done = macos_cua_draw(options);
    INFO(done.error);
    REQUIRE(done.ok);
    const auto batches = fixture.requests("run_steps");
    REQUIRE(batches.size() == 2); // at most 64 steps per call
    REQUIRE(batches[0]["steps"].size() == 64);
    REQUIRE(batches[1]["steps"].size() == 6);
    REQUIRE(batches[0]["delay_ms"] == 25);
    REQUIRE(batches[0]["session"] == "precision session");
    const auto& first = batches[0]["steps"][0];
    REQUIRE(first["tool"] == "drag");
    REQUIRE(first["args"]["from_x"] == 10.5);
    REQUIRE(first["args"]["to_y"] == 22.25);
    REQUIRE(first["args"]["duration_ms"] == 40);
    REQUIRE(first["args"]["steps"] == 3);
    REQUIRE(first["args"]["modifier"] == json::array({"shift"}));
    REQUIRE(first["args"]["delivery_mode"] == "background");
    const auto& last = batches[1]["steps"][5];
    REQUIRE(last["args"]["to_x"] == 80.5);
    REQUIRE(last["args"]["duration_ms"] == 15);

    // A gap longer than run_steps allows keeps one drag call per segment.
    Fixture slow;
    options.points.resize(3);
    options.segment_durations_ms.resize(2);
    options.stroke_gap_ms = 2001;
    REQUIRE(macos_cua_draw(options).ok);
    REQUIRE(slow.requests("run_steps").empty());
    REQUIRE(slow.requests("drag").size() == 2);
    REQUIRE(slow.requests("drag")[0]["session"] == "precision session");

    // A provider field that would replace the session is rejected before anything is sent.
    Fixture conflict;
    options.provider_json = R"({"session":"other"})";
    REQUIRE_FALSE(macos_cua_draw(options).ok);
    REQUIRE(conflict.calls().empty());
}

TEST_CASE("CUA provider options preserve capture delivery and observation controls") {
    Fixture fixture;
    MacCuaClickOptions click;
    click.pid = 42; click.has_window_id = true; click.window_id = 123;
    click.has_xy = true; click.x = 12.5; click.y = 31.75;
    click.provider_json = R"({"capture_id":"capture-1","delivery_mode":"background","future_field":{"keep":[1,2,3]}})";
    auto result = macos_cua_click(click);
    REQUIRE(result.ok);
    auto sent = json::parse(result.message);
    REQUIRE(sent["capture_id"] == "capture-1");
    REQUIRE(sent["delivery_mode"] == "background");
    REQUIRE(sent["future_field"]["keep"] == json::array({1,2,3}));
    REQUIRE(sent["x"] == 12.5);
    MacCuaWindowStateOptions state{.pid = 42, .window_id = 123};
    state.provider_json = R"({"include_screenshot":false,"max_image_dimension":0,"max_elements":900000,"timeout_ms":120000})";
    result = macos_cua_window_state(state);
    REQUIRE(result.ok);
    sent = json::parse(result.message);
    REQUIRE(sent["max_image_dimension"] == 0);
    REQUIRE(sent["max_elements"] == 900000);
    REQUIRE(sent["timeout_ms"] == 120000);
    REQUIRE(sent["include_screenshot"] == false);
    const auto count = fixture.calls().size();
    click.provider_json = R"({"pid":999})";
    REQUIRE_FALSE(macos_cua_click(click).ok);
    click.provider_json = "[]";
    REQUIRE_FALSE(macos_cua_click(click).ok);
    REQUIRE(fixture.calls().size() == count);
}

TEST_CASE("CUA named convenience calls retain explicit session and semantic snapshot fields") {
    Fixture fixture;
    const json request{{"target_id","bt-target"},{"tab_id","tab-1"},{"snapshot_format","semantic_v2"},{"continuation","bc-next"}};
    const auto sent = json::parse(cua_session_call("get_browser_state", request).message);
    REQUIRE(sent["session"] == "precision session");
    REQUIRE(sent["snapshot_format"] == "semantic_v2");
    REQUIRE(sent["continuation"] == "bc-next");
    REQUIRE(json::parse(cua_session_call("get_browser_state", {{"session","explicit"}}).message)["session"] == "explicit");
    REQUIRE_FALSE(json::parse(cua_call("parse_visual_regions", {{"capture_id","cap-1"}}).message).contains("session"));
}

TEST_CASE("CUA 0.38 launch takes a macOS .app path as the name and keeps native paths as launch_path") {
    MacCuaLaunchOptions app;
    app.launch_path = "/Applications/Calculator.app";
    const auto macos = cua_launch_arguments(app, true);
    REQUIRE(macos["name"] == "/Applications/Calculator.app");
    REQUIRE_FALSE(macos.contains("launch_path"));

    app.launch_path = "/Applications/Calculator.app///";
    REQUIRE(cua_launch_arguments(app, true)["name"] == "/Applications/Calculator.app");

    // Another target does not know .app paths: the provider gets the field it documents there.
    const auto other = cua_launch_arguments(app, false);
    REQUIRE_FALSE(other.contains("name"));
    REQUIRE(other["launch_path"] == "/Applications/Calculator.app///");

    // An explicit name wins; a path that is not a .app bundle (or a bare ".app") is never rewritten.
    MacCuaLaunchOptions named;
    named.name = "Calculator";
    named.launch_path = "/Applications/Calculator.app";
    const auto both = cua_launch_arguments(named, true);
    REQUIRE(both["name"] == "Calculator");
    REQUIRE(both["launch_path"] == "/Applications/Calculator.app");
    MacCuaLaunchOptions exe;
    exe.launch_path = "/usr/local/bin/tool";
    REQUIRE(cua_launch_arguments(exe, true)["launch_path"] == "/usr/local/bin/tool");
    MacCuaLaunchOptions bare;
    bare.launch_path = ".app";
    REQUIRE(cua_launch_arguments(bare, true)["launch_path"] == ".app");

    // The other launch fields are carried unchanged.
    MacCuaLaunchOptions rich;
    rich.bundle_id = "com.apple.Safari";
    rich.urls = {"https://example.com"};
    rich.creates_new_instance = true;
    rich.additional_arguments = {"--incognito"};
    const auto carried = cua_launch_arguments(rich, true);
    REQUIRE(carried["bundle_id"] == "com.apple.Safari");
    REQUIRE(carried["urls"] == json::array({"https://example.com"}));
    REQUIRE(carried["creates_new_application_instance"] == true);
    REQUIRE(carried["additional_arguments"] == json::array({"--incognito"}));
}

TEST_CASE("CUA starts a session again when the provider says it has ended, and repeats the call once") {
    Fixture fixture;
    fixture.config({{"ended_session", "precision session"}});
    MacCuaWindowStateOptions state{.pid = 42, .window_id = 7};
    const auto result = macos_cua_window_state(state);
    REQUIRE(result.ok);
    REQUIRE(json::parse(result.message)["session"] == "precision session");
    const auto calls = fixture.calls();
    std::vector<std::string> tools;
    for (const auto& call : calls)
        if (call["args"][0] == "call") tools.push_back(call["args"][1]);
    REQUIRE(tools == std::vector<std::string>{"get_window_state", "start_session", "get_window_state"});
    for (const auto& call : calls)
        if (call["args"][0] == "call" && call["args"][1] == "start_session") REQUIRE(json::parse(call["stdin"].get<std::string>()) == json{{"session", "precision session"}});
    // Revived: the next call goes straight through.
    REQUIRE(macos_cua_window_state(state).ok);
    std::size_t states = 0;
    for (const auto& call : fixture.calls())
        if (call["args"][0] == "call" && call["args"][1] == "get_window_state") ++states;
    REQUIRE(states == 3);
}

TEST_CASE("CUA does not revive a session the caller did not name, and reports a start_session that fails") {
    Fixture fixture;
    fixture.config({{"exit_code", 1}, {"stderr", "session 'somebody-else' has ended; tool call 'get_window_state' was rejected. Call start_session with this id to revive it"}});
    MacCuaWindowStateOptions state{.pid = 42, .window_id = 7};
    const auto other = macos_cua_window_state(state);
    REQUIRE_FALSE(other.ok);
    REQUIRE(fixture.calls().size() == 1);

    Fixture failing;
    failing.config({{"ended_session", "precision session"}, {"revive_fails", true}});
    const auto result = macos_cua_window_state(state);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("has ended") != std::string::npos);
    REQUIRE(result.error.find("Kiseki tried start_session for 'precision session'") != std::string::npos);
    std::size_t attempts = 0;
    for (const auto& call : failing.calls())
        if (call["args"][0] == "call") ++attempts;
    REQUIRE(attempts == 2);
}

TEST_CASE("CUA revives a session named in the Driver 0.49 ended-session message") {
    Fixture fixture;
    fixture.config({{"ended_session", "precision session"}, {"ended_text_049", true}});
    MacCuaWindowStateOptions state{.pid = 42, .window_id = 7};
    REQUIRE(macos_cua_window_state(state).ok);
    REQUIRE(fixture.requests("start_session").size() == 1);
    REQUIRE(fixture.requests("get_window_state").size() == 2);
}

TEST_CASE("CUA state maps observation controls and takes no screenshot without an output") {
    Fixture fixture;
    MacCuaWindowStateOptions state{.pid = 42, .window_id = 7};
    auto sent = json::parse(macos_cua_window_state(state).message);
    REQUIRE(sent["include_screenshot"] == false);
    REQUIRE_FALSE(sent.contains("max_elements"));
    REQUIRE_FALSE(sent.contains("tree_format"));

    state.max_elements = 900;
    state.max_depth = 12;
    state.timeout_ms = 5000;
    state.max_image_dimension = 0;
    state.tree_format = "elements";
    state.since = "s00000003";
    state.full_output = true;
    state.verbose = true;
    sent = json::parse(macos_cua_window_state(state).message);
    REQUIRE(sent["max_elements"] == 900);
    REQUIRE(sent["max_depth"] == 12);
    REQUIRE(sent["timeout_ms"] == 5000);
    REQUIRE(sent["max_image_dimension"] == 0);
    REQUIRE(sent["tree_format"] == "elements");
    REQUIRE(sent["since"] == "s00000003");
    REQUIRE(sent["full_output"] == true);
    REQUIRE(sent["verbose"] == true);

    // An explicit provider choice is kept; --no-tree needs an output file.
    MacCuaWindowStateOptions inline_image{.pid = 42, .window_id = 7, .provider_json = R"({"include_screenshot":true})"};
    REQUIRE(json::parse(macos_cua_window_state(inline_image).message)["include_screenshot"] == true);
    MacCuaWindowStateOptions no_tree{.pid = 42, .window_id = 7, .no_tree = true};
    const auto before = fixture.calls().size();
    REQUIRE_FALSE(macos_cua_window_state(no_tree).ok);
    REQUIRE(fixture.calls().size() == before);
}

TEST_CASE("CUA state notes a truncated tree and a reduced screenshot frame on stderr") {
    Fixture fixture;
    const json answer{{"truncated", true}, {"truncation_reason", "max_elements"}, {"returned_element_count", 250}, {"total_element_count", 250},
        {"screenshot_width", 1366}, {"screenshot_height", 956}, {"screenshot_scale", 2.0}, {"window_bounds", {{"width", 1000}, {"height", 700}}}};
    fixture.config({{"stdout", answer.dump()}});
    const auto result = macos_cua_window_state(MacCuaWindowStateOptions{.pid = 42, .window_id = 7});
    REQUIRE(result.ok);
    REQUIRE(json::parse(result.message) == answer); // stdout stays the provider's answer
    REQUIRE(result.error.find("truncated (max_elements)") != std::string::npos);
    REQUIRE(result.error.find("1.366 px per window unit") != std::string::npos);
    REQUIRE(result.error.find("--max-image-dimension 0") != std::string::npos);

    // A native-resolution frame, or a complete tree, needs no note.
    REQUIRE(cua_observation_notes({{"screenshot_width", 2000}, {"screenshot_height", 1400}, {"screenshot_scale", 2.0},
        {"window_bounds", {{"width", 1000}, {"height", 700}}}, {"truncated", false}}).empty());
    REQUIRE(cua_observation_notes({{"screenshot_width", 1138}, {"screenshot_height", 586}, {"window_bounds", {{"width", 1138}, {"height", 586}}}}).empty());
    REQUIRE(cua_observation_notes({{"screenshot_width", 1568}, {"screenshot_height", 806}, {"window_bounds", {{"width", 2000}, {"height", 1028}}}})
        .find("0.784 px per window unit") != std::string::npos);
}

TEST_CASE("CUA JPEG captures go through get_window_state and are never mislabelled") {
    Fixture fixture;
    const auto jpeg = fixture.directory / "window.jpg";
    std::ofstream(jpeg) << "previous capture";
    fixture.config({{"write_screenshot", true}, {"screenshot_bytes", "jpeg"}});
    MacCuaScreenshotOptions options{.window_id = 1099511627793ULL, .pid = 42, .output_path = jpeg, .quality = 80};
    const auto captured = macos_cua_screenshot(options);
    INFO(captured.error);
    REQUIRE(captured.ok);
    REQUIRE(fixture.requests("list_windows").empty()); // --pid skips the window lookup
    const auto sent = fixture.requests("get_window_state").back();
    REQUIRE(sent["screenshot_format"] == "jpeg");
    REQUIRE(sent["screenshot_quality"] == 80);
    REQUIRE(sent["include_accessibility_tree"] == false);

    // PNG bytes are not written under a JPEG name.
    fixture.config({{"write_screenshot", true}});
    REQUIRE_FALSE(macos_cua_screenshot(options).ok);
    {
        std::ifstream stream(jpeg, std::ios::binary);
        REQUIRE(stream.get() == 0xFF);
    }

    // A platform without screenshot_format says so precisely and writes nothing.
    fixture.config({{"tool_stdout", {{"get_window_state", R"({"refusal":{"code":"invalid_arguments","message":"get_window_state: unknown arguments screenshot_format, screenshot_quality"},"status":"refused"})"}}},
        {"tool_exit", {{"get_window_state", 1}}}});
    const auto refused = macos_cua_screenshot(options);
    REQUIRE_FALSE(refused.ok);
    REQUIRE(refused.error.find("cannot capture JPEG on this platform") != std::string::npos);

    // state follows the output name the same way; a mismatched --format is rejected.
    fixture.config({{"write_screenshot", true}, {"screenshot_bytes", "jpeg"}});
    REQUIRE(macos_cua_window_state(MacCuaWindowStateOptions{.pid = 42, .window_id = 7, .output_path = jpeg}).ok);
    REQUIRE(fixture.requests("get_window_state").back()["screenshot_format"] == "jpeg");
    options.format = "png";
    REQUIRE(macos_cua_screenshot(options).error.find("does not match") != std::string::npos);
}

TEST_CASE("CUA failures name the Kiseki next step") {
    Fixture fixture;
    fixture.config({{"daemon_down", "socket"}});
    auto result = cua_call("list_windows", json::object());
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("kiseki background cua setup") != std::string::npos);

    fixture.config({{"tool_stdout", {{"click", R"({"code":"screenshot_context_missing","reason":"window_never_read"})"}}}});
    result = macos_cua_click(MacCuaClickOptions{.pid = 42, .x = 5, .y = 6, .has_xy = true});
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("background cua state --output") != std::string::npos);
}

TEST_CASE("CUA window discovery probes the daemon only when a call finds none") {
    Fixture fixture;
    fixture.env("KISEKI_CUA_AUTO_UPDATE", "0");
    fixture.config({{"calls_need_daemon", true}});
    REQUIRE(macos_cua_list_windows(MacCuaWindowListOptions{}).ok);
    REQUIRE(fixture.count("status") == 0);
    REQUIRE(fixture.requests("list_windows").size() == 1);

    // No daemon: the first call fails without effect, setup starts the daemon, and the call is repeated once.
    Fixture stopped;
    stopped.env("KISEKI_CUA_AUTO_UPDATE", "0");
    stopped.config({{"calls_need_daemon", true}});
    std::ofstream(stopped.directory / "stopped");
    const auto listed = macos_cua_list_windows(MacCuaWindowListOptions{});
    INFO(listed.error);
    REQUIRE(listed.ok);
    REQUIRE(stopped.count("serve") == 1);
    REQUIRE(stopped.requests("list_windows").size() == 2);
    REQUIRE(listed.error.find("CUA daemon started") != std::string::npos);
}

TEST_CASE("CUA feedback presets apply motion on current Drivers and say what themes replaced") {
    Fixture fixture;
    fixture.config({{"unknown_tools", {"set_agent_cursor_style"}}});
    const auto applied = macos_cua_feedback_preset(MacCuaFeedbackPresetOptions{.name = "natural"});
    INFO(applied.error);
    REQUIRE(applied.ok);
    REQUIRE(json::parse(applied.message)["colors"].get<std::string>().find("cursor-theme") != std::string::npos);
    REQUIRE(fixture.requests("set_agent_cursor_enabled").size() == 1);
    REQUIRE(fixture.requests("set_agent_cursor_motion")[0]["glide_duration_ms"] == 550);
    REQUIRE(fixture.requests("set_agent_cursor_style").empty());

    const auto style = macos_cua_feedback_style(MacCuaFeedbackStyleOptions{.reset = true});
    REQUIRE_FALSE(style.ok);
    REQUIRE(style.error.find("background cua theme") != std::string::npos);

    MacCuaFeedbackMotionOptions motion;
    motion.style = "comet_swoop";
    motion.timing = "fitts";
    motion.has_turn_radius = true;
    motion.turn_radius = 40;
    motion.effects = {{"trail", "on"}, {"glow", "default"}};
    REQUIRE(macos_cua_feedback_motion(motion).ok);
    const auto sent = fixture.requests("set_agent_cursor_motion").back();
    REQUIRE(sent["style"] == "comet_swoop");
    REQUIRE(sent["timing"] == "fitts");
    REQUIRE(sent["turn_radius"] == 40.0);
    REQUIRE(sent["effects"] == json{{"trail", "on"}, {"glow", "default"}});
    REQUIRE(sent["session"] == "precision session");

    // An older Driver with custom styles also receives the preset's colors.
    Fixture legacy;
    REQUIRE(macos_cua_feedback_preset(MacCuaFeedbackPresetOptions{.name = "fast"}).ok);
    REQUIRE(legacy.requests("set_agent_cursor_style").size() == 1);
}

TEST_CASE("CUA child processes receive environment overrides without changing Kiseki's own") {
    Fixture fixture;
    const auto result = detail::run_process({detail::path_text(fixture.binary), "--version"}, "", {{"CUA_DRIVER_NO_TEST", "1"}});
    REQUIRE(result.code == 0);
    REQUIRE(fixture.calls().back()["env"]["CUA_DRIVER_NO_TEST"] == "1");
    REQUIRE(detail::environment_text("CUA_DRIVER_NO_TEST").empty());
}
