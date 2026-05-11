#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli/app.hpp"

namespace {

class TempConfigDirectory {
public:
    TempConfigDirectory()
        : path_{std::filesystem::temp_directory_path() / unique_name()} {
        std::filesystem::create_directories(path_);
    }

    ~TempConfigDirectory() {
        std::error_code error_code;
        std::filesystem::remove_all(path_, error_code);
    }

    std::filesystem::path file(std::string_view filename) const {
        return path_ / filename;
    }

private:
    static std::string unique_name() {
        static std::atomic<std::uint64_t> counter{0};
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        return "kiseki-cli-app-test-" + std::to_string(ticks) + "-" +
               std::to_string(counter.fetch_add(1));
    }

    std::filesystem::path path_;
};

struct CliResult {
    int code;
    std::string out;
    std::string err;
};

CliResult run_cli(const std::vector<std::string>& args, const std::filesystem::path& config_path) {
    std::ostringstream out;
    std::ostringstream err;

    const int code = kiseki::cli::run(args, config_path, kiseki::cli::Io{out, err});

    return CliResult{
        .code = code,
        .out = out.str(),
        .err = err.str(),
    };
}

void write_text(const std::filesystem::path& path, std::string_view content) {
    std::ofstream file{path};
    file << content;
}

}

TEST_CASE("config path prints injected config path") {
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    const auto result = run_cli({"config", "path"}, path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out == path.string() + "\n");
    REQUIRE(result.err.empty());
}

TEST_CASE("global config option overrides active config path") {
    const TempConfigDirectory temp;
    const auto injected_path = temp.file("injected.json");
    const auto option_path = temp.file("option.json");

    const auto result = run_cli({"--config", option_path.string(), "config", "path"}, injected_path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out == option_path.string() + "\n");
    REQUIRE(result.err.empty());
}

TEST_CASE("config show prints default json when file is absent") {
    const TempConfigDirectory temp;
    const auto path = temp.file("missing.json");

    const auto result = run_cli({"config", "show"}, path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out.find("\"schemaVersion\": 1") != std::string::npos);
    REQUIRE(result.out.find("\"webui\"") != std::string::npos);
    REQUIRE(result.err.empty());
}

TEST_CASE("config show reports malformed config errors") {
    const TempConfigDirectory temp;
    const auto path = temp.file("malformed.json");
    write_text(path, "{\"webui\":");

    const auto result = run_cli({"config", "show"}, path);

    REQUIRE(result.code == 2);
    REQUIRE(result.out.empty());
    REQUIRE_FALSE(result.err.empty());
    REQUIRE(result.err.find("failed to parse config file") != std::string::npos);
}

TEST_CASE("config validate reports valid config") {
    const TempConfigDirectory temp;
    const auto path = temp.file("missing.json");

    const auto result = run_cli({"config", "validate"}, path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out == "configuration is valid\n");
    REQUIRE(result.err.empty());
}

TEST_CASE("config validate reports invalid config errors") {
    const TempConfigDirectory temp;
    const auto path = temp.file("invalid.json");
    write_text(path, "{\"webui\":{\"port\":0}}");

    const auto result = run_cli({"config", "validate"}, path);

    REQUIRE(result.code == 2);
    REQUIRE(result.out.empty());
    REQUIRE(result.err.find("webui.port") != std::string::npos);
}

TEST_CASE("capabilities prints foundation capability json") {
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    const auto result = run_cli({"capabilities"}, path);
    const auto json = nlohmann::json::parse(result.out);

    REQUIRE(result.code == 0);
    REQUIRE(json.at("input").at("backgroundWindow").is_boolean());
    REQUIRE(json.at("capture").at("window").is_boolean());
    REQUIRE(json.at("limitations").is_array());
    REQUIRE_FALSE(json.at("limitations").empty());
    REQUIRE(result.err.empty());
}

TEST_CASE("doctor prints diagnostic text with foundation limitations") {
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    const auto result = run_cli({"doctor"}, path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out.find("Kiseki Input doctor") != std::string::npos);
    REQUIRE(result.out.find("Config path:") != std::string::npos);
    REQUIRE(result.out.find("Input driver backend:") != std::string::npos);
    REQUIRE(result.out.find("System input backend:") != std::string::npos);
    REQUIRE(result.out.find("Background-window input:") != std::string::npos);
    REQUIRE(result.out.find("Desktop screenshot:") != std::string::npos);
    REQUIRE(result.out.find("Window screenshot:") != std::string::npos);
    REQUIRE(result.out.find("Screenshot burst:") != std::string::npos);
    REQUIRE(result.out.find("Limitations:") != std::string::npos);
    REQUIRE(result.out.find("WebUI is configuration-only") != std::string::npos);
    REQUIRE(result.err.empty());
}

TEST_CASE("doctor reports invalid config details") {
    const TempConfigDirectory temp;
    const auto path = temp.file("invalid.json");
    write_text(path, "{\"webui\":{\"port\":0}}");

    const auto result = run_cli({"doctor"}, path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out.find("Config: invalid") != std::string::npos);
    REQUIRE(result.out.find("webui.port") != std::string::npos);
    REQUIRE(result.err.empty());
}

TEST_CASE("run rejects argv style args containing executable name") {
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    const auto result = run_cli({"kiseki", "doctor"}, path);

    REQUIRE(result.code != 0);
    REQUIRE(result.out.empty());
    REQUIRE_FALSE(result.err.empty());
}

TEST_CASE("unknown command reports parse error") {
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    const auto result = run_cli({"unknown"}, path);

    REQUIRE(result.code != 0);
    REQUIRE(result.out.empty());
    REQUIRE_FALSE(result.err.empty());
}

TEST_CASE("config-ui launches configured local web server") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    kiseki::cli::Dependencies dependencies;
    dependencies.launch_config_ui = [&](const kiseki::cli::WebUiLaunchOptions& options,
                                        const std::filesystem::path& config_path,
                                        kiseki::cli::Io io) {
        REQUIRE(options.host == "127.0.0.1");
        REQUIRE(options.port == 8787);
        REQUIRE(config_path == path);
        io.out << "fake launch\n";
        return 0;
    };

    const int code = kiseki::cli::run({"config-ui"}, path, kiseki::cli::Io{out, err}, dependencies);

    REQUIRE(code == 0);
    REQUIRE(out.str() == "fake launch\n");
    REQUIRE(err.str().empty());
}

TEST_CASE("config-ui command line options override configured host and port") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    kiseki::cli::Dependencies dependencies;
    dependencies.launch_config_ui = [&](const kiseki::cli::WebUiLaunchOptions& options,
                                        const std::filesystem::path&,
                                        kiseki::cli::Io) {
        REQUIRE(options.host == "0.0.0.0");
        REQUIRE(options.port == 9001);
        return 0;
    };

    const int code = kiseki::cli::run(
        {"config-ui", "--host", "0.0.0.0", "--port", "9001"},
        path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(out.str().empty());
    REQUIRE(err.str().empty());
}

TEST_CASE("config-ui reports invalid config without launching") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto path = temp.file("invalid.json");
    write_text(path, "{\"webui\":{\"port\":0}}");
    bool launched = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.launch_config_ui = [&](const kiseki::cli::WebUiLaunchOptions&,
                                        const std::filesystem::path&,
                                        kiseki::cli::Io) {
        launched = true;
        return 0;
    };

    const int code = kiseki::cli::run({"config-ui"}, path, kiseki::cli::Io{out, err}, dependencies);

    REQUIRE(code == 2);
    REQUIRE_FALSE(launched);
    REQUIRE(out.str().empty());
    REQUIRE(err.str().find("config error:") != std::string::npos);
    REQUIRE(err.str().find("webui.port") != std::string::npos);
}

TEST_CASE("screenshot desktop command captures to requested output") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto output_path = temp.file("desktop.bmp");

    kiseki::cli::Dependencies dependencies;
    dependencies.capture_desktop = [&](const kiseki::cli::ScreenshotDesktopOptions& options,
                                       kiseki::cli::Io io) {
        REQUIRE(options.output_path == output_path);
        io.out << "captured " << options.output_path.string() << '\n';
        return 0;
    };

    const int code = kiseki::cli::run(
        {"screenshot", "desktop", "--output", output_path.string()},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(out.str().find("captured") != std::string::npos);
    REQUIRE(err.str().empty());
}

TEST_CASE("screenshot burst command passes frame count and fps") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto output_dir = temp.file("frames");

    kiseki::cli::Dependencies dependencies;
    dependencies.capture_burst = [&](const kiseki::cli::ScreenshotBurstOptions& options,
                                     kiseki::cli::Io io) {
        REQUIRE(options.output_directory == output_dir);
        REQUIRE(options.prefix == "shot");
        REQUIRE(options.frames == 8);
        REQUIRE(options.fps == 60);
        io.out << "burst ok\n";
        return 0;
    };

    const int code = kiseki::cli::run(
        {"screenshot", "burst", "--directory", output_dir.string(), "--prefix", "shot", "--frames", "8", "--fps", "60"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(out.str() == "burst ok\n");
    REQUIRE(err.str().empty());
}

TEST_CASE("screenshot window command is available with target selectors") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto output_path = temp.file("notepad.bmp");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.capture_window = [&](const kiseki::cli::ScreenshotWindowOptions& options, kiseki::cli::Io io) {
        REQUIRE(options.target.title == "Untitled");
        REQUIRE(options.target.pid == 0);
        REQUIRE(options.target.window_id.empty());
        REQUIRE(options.output_path == output_path);
        called = true;
        io.out << "window ok\n";
        return 0;
    };

    const int code = kiseki::cli::run(
        {"screenshot", "window", "--target-title", "Untitled", "--output", output_path.string()},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str() == "window ok\n");
    REQUIRE(err.str().empty());
}

TEST_CASE("screenshot window burst command is available with target selectors") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto output_dir = temp.file("frames");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.capture_window_burst = [&](const kiseki::cli::ScreenshotWindowBurstOptions& options, kiseki::cli::Io io) {
        REQUIRE(options.target.title == "Untitled");
        REQUIRE(options.output_directory == output_dir);
        REQUIRE(options.prefix == "frame");
        REQUIRE(options.frames == 8);
        REQUIRE(options.fps == 60);
        called = true;
        io.out << "window burst ok\n";
        return 0;
    };

    const int code = kiseki::cli::run(
        {"screenshot", "window-burst", "--target-title", "Untitled", "--directory", output_dir.string(), "--frames", "8", "--fps", "60"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str() == "window burst ok\n");
    REQUIRE(err.str().empty());
}

TEST_CASE("target list command is available") {
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");

    const auto result = run_cli({"target", "list", "--help"}, config_path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out.find("List target windows") != std::string::npos);
    REQUIRE(result.out.find("--target-title") != std::string::npos);
    REQUIRE(result.err.empty());
}

TEST_CASE("target list command passes filters to backend") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.list_targets = [&](const kiseki::cli::TargetListOptions& options, kiseki::cli::Io io) {
        REQUIRE(options.filter.title == "Notepad");
        REQUIRE(options.filter.pid == 1234);
        REQUIRE(options.filter.window_id == "0x123");
        called = true;
        io.out << "target list ok\n";
        return 0;
    };

    const int code = kiseki::cli::run(
        {"target", "list", "--target-title", "Notepad", "--target-pid", "1234", "--target-window-id", "0x123"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str() == "target list ok\n");
    REQUIRE(err.str().empty());
}

TEST_CASE("input commands call injected backend") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    int calls = 0;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_key = [&](const kiseki::cli::InputKeyOptions& options, kiseki::cli::Io) {
        REQUIRE(options.key == "shift");
        REQUIRE(options.backend == "auto");
        ++calls;
        return 0;
    };
    dependencies.input_combo = [&](const kiseki::cli::InputComboOptions& options, kiseki::cli::Io) {
        REQUIRE(options.keys == "ctrl+shift+esc");
        REQUIRE(options.backend == "auto");
        ++calls;
        return 0;
    };
    dependencies.input_text = [&](const kiseki::cli::InputTextOptions& options, kiseki::cli::Io) {
        REQUIRE(options.text == "abc");
        ++calls;
        return 0;
    };
    dependencies.input_mouse = [&](const kiseki::cli::InputMouseOptions& options, kiseki::cli::Io) {
        REQUIRE(options.dx == 0);
        REQUIRE(options.dy == 0);
        REQUIRE(options.x == 0);
        REQUIRE(options.y == 0);
        REQUIRE(options.absolute == false);
        REQUIRE(options.backend == "auto");
        REQUIRE(options.click == "none");
        ++calls;
        return 0;
    };

    REQUIRE(kiseki::cli::run({"input", "key", "--key", "shift"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"input", "combo", "--keys", "ctrl+shift+esc"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"input", "text", "--text", "abc"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"input", "mouse", "--dx", "0", "--dy", "0"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(calls == 4);
    REQUIRE(err.str().empty());
}

TEST_CASE("background input commands are available with target selectors") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    std::vector<std::string> calls;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_background_text = [&](const kiseki::cli::BackgroundTextOptions& options, kiseki::cli::Io) {
        REQUIRE(options.target.title == "Untitled");
        REQUIRE(options.text == "abc");
        calls.push_back("text");
        return 0;
    };
    dependencies.input_background_key = [&](const kiseki::cli::BackgroundKeyOptions& options, kiseki::cli::Io) {
        REQUIRE(options.target.title == "Untitled");
        REQUIRE(options.key == "enter");
        calls.push_back("key");
        return 0;
    };
    dependencies.input_background_mouse = [&](const kiseki::cli::BackgroundMouseOptions& options, kiseki::cli::Io) {
        REQUIRE(options.target.title == "Untitled");
        REQUIRE(options.x == 10);
        REQUIRE(options.y == 20);
        REQUIRE(options.click == "left");
        calls.push_back("mouse");
        return 0;
    };

    REQUIRE(kiseki::cli::run(
        {"input", "background-text", "--target-title", "Untitled", "--text", "abc"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies) == 0);

    REQUIRE(kiseki::cli::run(
        {"input", "background-key", "--target-title", "Untitled", "--key", "enter"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies) == 0);

    REQUIRE(kiseki::cli::run(
        {"input", "background-mouse", "--target-title", "Untitled", "--x", "10", "--y", "20", "--click", "left"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies) == 0);

    REQUIRE(calls == std::vector<std::string>{"text", "key", "mouse"});
    REQUIRE(out.str().empty());
    REQUIRE(err.str().empty());
}

TEST_CASE("input mouse command supports absolute coordinates and button transitions") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_mouse = [&](const kiseki::cli::InputMouseOptions& options, kiseki::cli::Io) {
        REQUIRE(options.dx == 0);
        REQUIRE(options.dy == 0);
        REQUIRE(options.x == 640);
        REQUIRE(options.y == 360);
        REQUIRE(options.absolute == true);
        REQUIRE(options.backend == "auto");
        REQUIRE(options.click == "left-down");
        called = true;
        return 0;
    };

    const int code = kiseki::cli::run(
        {"input", "mouse", "--x", "640", "--y", "360", "--click", "left-down"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str().empty());
    REQUIRE(err.str().empty());
}

TEST_CASE("input combo command accepts explicit backend") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_combo = [&](const kiseki::cli::InputComboOptions& options, kiseki::cli::Io) {
        REQUIRE(options.keys == "win+r");
        REQUIRE(options.backend == "system");
        called = true;
        return 0;
    };

    const int code = kiseki::cli::run(
        {"input", "combo", "--keys", "win+r", "--backend", "system"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str().empty());
    REQUIRE(err.str().empty());
}

TEST_CASE("input drag command passes path and backend") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto points_path = temp.file("points.txt");
    write_text(points_path, "10 20\n30 40\n");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_drag = [&](const kiseki::cli::InputDragOptions& options, kiseki::cli::Io) {
        REQUIRE(options.path == points_path);
        REQUIRE(options.backend == "system");
        called = true;
        return 0;
    };

    const int code = kiseki::cli::run(
        {"input", "drag", "--file", points_path.string(), "--backend", "system"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str().empty());
    REQUIRE(err.str().empty());
}

TEST_CASE("input mouse command requires complete absolute coordinates") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_mouse = [&](const kiseki::cli::InputMouseOptions&, kiseki::cli::Io) {
        called = true;
        return 0;
    };

    const int code = kiseki::cli::run(
        {"input", "mouse", "--x", "640"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 2);
    REQUIRE_FALSE(called);
    REQUIRE(out.str().empty());
    REQUIRE(err.str().find("requires both --x and --y") != std::string::npos);
}

TEST_CASE("input mouse command requires coordinates when absolute flag is present") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_mouse = [&](const kiseki::cli::InputMouseOptions&, kiseki::cli::Io) {
        called = true;
        return 0;
    };

    const int code = kiseki::cli::run(
        {"input", "mouse", "--absolute"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 2);
    REQUIRE_FALSE(called);
    REQUIRE(out.str().empty());
    REQUIRE(err.str().find("requires both --x and --y") != std::string::npos);
}

TEST_CASE("input text command reads utf8 text from file") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto text_path = temp.file("input.txt");
    write_text(text_path, "WSADFGHJKL, \xE4\xBD\xA0\xE5\xA5\xBD");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_text = [&](const kiseki::cli::InputTextOptions& options, kiseki::cli::Io) {
        REQUIRE(options.text == "WSADFGHJKL, \xE4\xBD\xA0\xE5\xA5\xBD");
        REQUIRE(options.text_file == text_path);
        called = true;
        return 0;
    };

    const int code = kiseki::cli::run(
        {"input", "text", "--file", text_path.string()},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str().empty());
    REQUIRE(err.str().empty());
}

TEST_CASE("macro validate accepts supported step sequence") {
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto macro_path = temp.file("macro.json");
    write_text(
        macro_path,
        R"({
  "name": "paint-demo",
  "steps": [
    {"type": "combo", "keys": "win+r", "backend": "system"},
    {"type": "text", "text": "mspaint.exe"},
    {"type": "key", "key": "enter", "backend": "system"},
    {"type": "sleep", "ms": 10},
    {"type": "mouse", "x": 640, "y": 360, "click": "left", "backend": "system"},
    {"type": "drag", "file": "heart-points.txt", "backend": "system"},
    {"type": "screenshot", "output": "paint.bmp"}
  ]
})");

    const auto result = run_cli({"macro", "validate", "--file", macro_path.string()}, config_path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out == "macro is valid\n");
    REQUIRE(result.err.empty());
}

TEST_CASE("macro run executes steps through injected dependencies") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto macro_path = temp.file("macro.json");
    const auto text_path = temp.file("text.txt");
    const auto points_path = temp.file("points.txt");
    const auto screenshot_path = temp.file("macro.bmp");
    write_text(text_path, "WSADFGHJKL, \xE4\xBD\xA0\xE5\xA5\xBD");
    write_text(points_path, "10 20\n30 40\n");
    write_text(
        macro_path,
        R"({
  "steps": [
    {"type": "combo", "keys": "win+r", "backend": "system"},
    {"type": "text", "file": ")" + text_path.generic_string() + R"("},
    {"type": "key", "key": "enter", "backend": "system"},
    {"type": "mouse", "x": 640, "y": 360, "click": "left-down", "backend": "system"},
    {"type": "drag", "file": ")" + points_path.generic_string() + R"(", "backend": "system"},
    {"type": "screenshot", "output": ")" + screenshot_path.generic_string() + R"("}
  ]
})");
    std::vector<std::string> calls;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_combo = [&](const kiseki::cli::InputComboOptions& options, kiseki::cli::Io) {
        REQUIRE(options.keys == "win+r");
        REQUIRE(options.backend == "system");
        calls.push_back("combo");
        return 0;
    };
    dependencies.input_text = [&](const kiseki::cli::InputTextOptions& options, kiseki::cli::Io) {
        REQUIRE(options.text == "WSADFGHJKL, \xE4\xBD\xA0\xE5\xA5\xBD");
        REQUIRE(options.text_file == text_path);
        calls.push_back("text");
        return 0;
    };
    dependencies.input_key = [&](const kiseki::cli::InputKeyOptions& options, kiseki::cli::Io) {
        REQUIRE(options.key == "enter");
        REQUIRE(options.backend == "system");
        calls.push_back("key");
        return 0;
    };
    dependencies.input_mouse = [&](const kiseki::cli::InputMouseOptions& options, kiseki::cli::Io) {
        REQUIRE(options.x == 640);
        REQUIRE(options.y == 360);
        REQUIRE(options.absolute);
        REQUIRE(options.click == "left-down");
        REQUIRE(options.backend == "system");
        calls.push_back("mouse");
        return 0;
    };
    dependencies.input_drag = [&](const kiseki::cli::InputDragOptions& options, kiseki::cli::Io) {
        REQUIRE(options.path == points_path);
        REQUIRE(options.backend == "system");
        calls.push_back("drag");
        return 0;
    };
    dependencies.capture_desktop = [&](const kiseki::cli::ScreenshotDesktopOptions& options, kiseki::cli::Io) {
        REQUIRE(options.output_path == screenshot_path);
        calls.push_back("screenshot");
        return 0;
    };

    const int code = kiseki::cli::run(
        {"macro", "run", "--file", macro_path.string()},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(calls == std::vector<std::string>{"combo", "text", "key", "mouse", "drag", "screenshot"});
    REQUIRE(out.str() == "macro completed 6 steps\n");
    REQUIRE(err.str().empty());
}

TEST_CASE("macro run stops when a step fails") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto macro_path = temp.file("macro.json");
    write_text(
        macro_path,
        R"({
  "steps": [
    {"type": "key", "key": "enter", "backend": "system"},
    {"type": "text", "text": "unreached"}
  ]
})");
    bool text_called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_key = [&](const kiseki::cli::InputKeyOptions&, kiseki::cli::Io io) {
        io.err << "input failed\n";
        return 2;
    };
    dependencies.input_text = [&](const kiseki::cli::InputTextOptions&, kiseki::cli::Io) {
        text_called = true;
        return 0;
    };

    const int code = kiseki::cli::run(
        {"macro", "run", "--file", macro_path.string()},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 2);
    REQUIRE_FALSE(text_called);
    REQUIRE(out.str().empty());
    REQUIRE(err.str().find("macro step 1 failed") != std::string::npos);
    REQUIRE(err.str().find("input failed") != std::string::npos);
}

TEST_CASE("macro validate rejects malformed steps") {
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto macro_path = temp.file("bad-macro.json");
    write_text(macro_path, R"({"steps":[{"type":"mouse","x":640}]})");

    const auto result = run_cli({"macro", "validate", "--file", macro_path.string()}, config_path);

    REQUIRE(result.code == 2);
    REQUIRE(result.out.empty());
    REQUIRE(result.err.find("mouse step requires both x and y") != std::string::npos);
}

TEST_CASE("daemon once sends configured heartbeat notification") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");

    kiseki::cli::Dependencies dependencies;
    dependencies.run_daemon = [&](const kiseki::cli::DaemonOptions& options,
                                  const std::filesystem::path& path,
                                  kiseki::cli::Io io) {
        REQUIRE(options.once);
        REQUIRE(path == config_path);
        io.out << "daemon once\n";
        return 0;
    };

    const int code = kiseki::cli::run({"daemon", "run", "--once"}, config_path, kiseki::cli::Io{out, err}, dependencies);

    REQUIRE(code == 0);
    REQUIRE(out.str() == "daemon once\n");
    REQUIRE(err.str().empty());
}
