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
    REQUIRE(json.at("session").at("backgroundDesktop").is_boolean());
    REQUIRE(json.at("session").at("macosCuaBackground").is_boolean());
    REQUIRE(json.at("observation").at("windowTree").is_boolean());
    REQUIRE(json.at("observation").at("windowsUia").is_boolean());
    REQUIRE(json.at("observation").at("macosAx").is_boolean());
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
    REQUIRE(result.out.find("Background desktop session:") != std::string::npos);
    REQUIRE(result.out.find("macOS CUA background operation:") != std::string::npos);
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

TEST_CASE("screenshot background-window command uses the dedicated background capture backend") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto output_path = temp.file("paint-background.bmp");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.capture_background_window = [&](const kiseki::cli::ScreenshotBackgroundWindowOptions& options, kiseki::cli::Io io) {
        REQUIRE(options.target.title == "Paint");
        REQUIRE(options.target.pid == 1234);
        REQUIRE(options.target.window_id == "0x123");
        REQUIRE(options.output_path == output_path);
        called = true;
        io.out << "background window ok\n";
        return 0;
    };

    const int code = kiseki::cli::run(
        {"screenshot", "background-window", "--target-title", "Paint", "--target-pid", "1234", "--target-window-id", "0x123", "--output", output_path.string()},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str() == "background window ok\n");
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

TEST_CASE("target inspect command passes selected window to backend") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.inspect_target = [&](const kiseki::cli::TargetInspectOptions& options, kiseki::cli::Io io) {
        REQUIRE(options.target.title == "Notepad");
        REQUIRE(options.target.pid == 4321);
        REQUIRE(options.target.window_id == "0x456");
        called = true;
        io.out << "target inspect ok\n";
        return 0;
    };

    const int code = kiseki::cli::run(
        {"target", "inspect", "--target-title", "Notepad", "--target-pid", "4321", "--target-window-id", "0x456"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str() == "target inspect ok\n");
    REQUIRE(err.str().empty());
}

TEST_CASE("observe ui command passes selected target to non-visual backend") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.observe_ui = [&](const kiseki::cli::ObserveUiOptions& options, kiseki::cli::Io io) {
        REQUIRE(options.target.title == "Krita");
        REQUIRE(options.target.pid == 17532);
        REQUIRE(options.target.window_id == "610");
        REQUIRE(options.provider == "uia");
        REQUIRE(options.max_depth == 6);
        REQUIRE(options.max_elements == 512);
        called = true;
        io.out << "observe ui ok\n";
        return 0;
    };

    const int code = kiseki::cli::run(
        {
            "observe", "ui",
            "--target-title", "Krita",
            "--target-pid", "17532",
            "--target-window-id", "610",
            "--provider", "uia",
            "--max-depth", "6",
            "--max-elements", "512",
        },
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str() == "observe ui ok\n");
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
    dependencies.input_background_drag = [&](const kiseki::cli::BackgroundDragOptions& options, kiseki::cli::Io) {
        REQUIRE(options.target.title == "Untitled");
        REQUIRE(options.path.filename() == "points.txt");
        calls.push_back("drag");
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

    REQUIRE(kiseki::cli::run(
        {"input", "background-drag", "--target-title", "Untitled", "--file", "points.txt"},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies) == 0);

    REQUIRE(calls == std::vector<std::string>{"text", "key", "mouse", "drag"});
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
        REQUIRE(options.step_delay_ms == 7);
        REQUIRE(options.start_hold_ms == 20);
        REQUIRE(options.end_hold_ms == 30);
        called = true;
        return 0;
    };

    const int code = kiseki::cli::run(
        {
            "input", "drag",
            "--file", points_path.string(),
            "--backend", "system",
            "--step-delay-ms", "7",
            "--start-hold-ms", "20",
            "--end-hold-ms", "30",
        },
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str().empty());
    REQUIRE(err.str().empty());
}

TEST_CASE("background desktop commands call injected backend") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto state_dir = temp.file("sessions");
    const auto screenshot_path = temp.file("desktop.bmp");
    std::vector<std::string> calls;

    kiseki::cli::Dependencies dependencies;
    dependencies.background_desktop_start = [&](const kiseki::cli::BackgroundDesktopStartOptions& options, kiseki::cli::Io) {
        REQUIRE(options.display == ":99");
        REQUIRE(options.width == 1280);
        REQUIRE(options.height == 720);
        REQUIRE(options.depth == 24);
        REQUIRE(options.state_directory == state_dir);
        calls.push_back("start");
        return 0;
    };
    dependencies.background_desktop_stop = [&](const kiseki::cli::BackgroundDesktopStopOptions& options, kiseki::cli::Io) {
        REQUIRE(options.display == ":99");
        REQUIRE(options.state_directory == state_dir);
        calls.push_back("stop");
        return 0;
    };
    dependencies.background_desktop_launch = [&](const kiseki::cli::BackgroundDesktopLaunchOptions& options, kiseki::cli::Io) {
        REQUIRE(options.display == ":99");
        REQUIRE(options.command == "xterm");
        calls.push_back("launch");
        return 0;
    };
    dependencies.background_desktop_screenshot = [&](const kiseki::cli::BackgroundDesktopScreenshotOptions& options, kiseki::cli::Io) {
        REQUIRE(options.display == ":99");
        REQUIRE(options.output_path == screenshot_path);
        calls.push_back("screenshot");
        return 0;
    };
    dependencies.background_desktop_text = [&](const kiseki::cli::BackgroundDesktopTextOptions& options, kiseki::cli::Io) {
        REQUIRE(options.display == ":99");
        REQUIRE(options.text == "abc");
        calls.push_back("text");
        return 0;
    };
    dependencies.background_desktop_key = [&](const kiseki::cli::BackgroundDesktopKeyOptions& options, kiseki::cli::Io) {
        REQUIRE(options.display == ":99");
        REQUIRE(options.key == "enter");
        calls.push_back("key");
        return 0;
    };
    dependencies.background_desktop_mouse = [&](const kiseki::cli::BackgroundDesktopMouseOptions& options, kiseki::cli::Io) {
        REQUIRE(options.display == ":99");
        REQUIRE(options.x == 10);
        REQUIRE(options.y == 20);
        REQUIRE(options.click == "left");
        calls.push_back("mouse");
        return 0;
    };

    REQUIRE(kiseki::cli::run(
                {"background-desktop", "start", "--display", ":99", "--width", "1280", "--height", "720", "--depth", "24", "--state-dir", state_dir.string()},
                config_path,
                kiseki::cli::Io{out, err},
                dependencies) == 0);
    REQUIRE(kiseki::cli::run(
                {"background-desktop", "launch", "--display", ":99", "--command", "xterm"},
                config_path,
                kiseki::cli::Io{out, err},
                dependencies) == 0);
    REQUIRE(kiseki::cli::run(
                {"background-desktop", "screenshot", "--display", ":99", "--output", screenshot_path.string()},
                config_path,
                kiseki::cli::Io{out, err},
                dependencies) == 0);
    REQUIRE(kiseki::cli::run(
                {"background-desktop", "text", "--display", ":99", "--text", "abc"},
                config_path,
                kiseki::cli::Io{out, err},
                dependencies) == 0);
    REQUIRE(kiseki::cli::run(
                {"background-desktop", "key", "--display", ":99", "--key", "enter"},
                config_path,
                kiseki::cli::Io{out, err},
                dependencies) == 0);
    REQUIRE(kiseki::cli::run(
                {"background-desktop", "mouse", "--display", ":99", "--x", "10", "--y", "20", "--click", "left"},
                config_path,
                kiseki::cli::Io{out, err},
                dependencies) == 0);
    REQUIRE(kiseki::cli::run(
                {"background-desktop", "stop", "--display", ":99", "--state-dir", state_dir.string()},
                config_path,
                kiseki::cli::Io{out, err},
                dependencies) == 0);

    REQUIRE(calls == std::vector<std::string>{"start", "launch", "screenshot", "text", "key", "mouse", "stop"});
    REQUIRE(out.str().empty());
    REQUIRE(err.str().empty());
}

TEST_CASE("mac background commands call injected CUA backend") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto screenshot_path = temp.file("safari.png");
    const auto text_path = temp.file("input.txt");
    const auto points_path = temp.file("points.txt");
    write_text(text_path, "hello");
    write_text(points_path, "1 2\n3 4\n5 6\n");
    std::vector<std::string> calls;

    kiseki::cli::Dependencies dependencies;
    dependencies.mac_background_status = [&](const kiseki::cli::MacBackgroundStatusOptions& options, kiseki::cli::Io) {
        REQUIRE(options.prompt);
        calls.push_back("status");
        return 0;
    };
    dependencies.mac_background_launch = [&](const kiseki::cli::MacBackgroundLaunchOptions& options, kiseki::cli::Io) {
        REQUIRE(options.bundle_id == "com.apple.Safari");
        REQUIRE(options.urls == std::vector<std::string>{"about:blank"});
        REQUIRE(options.new_instance);
        REQUIRE(options.arguments == std::vector<std::string>{"--test"});
        calls.push_back("launch");
        return 0;
    };
    dependencies.mac_background_windows = [&](const kiseki::cli::MacBackgroundWindowsOptions& options, kiseki::cli::Io) {
        REQUIRE(options.pid == 123);
        REQUIRE(options.has_pid);
        REQUIRE(options.on_screen_only);
        calls.push_back("windows");
        return 0;
    };
    dependencies.mac_background_state = [&](const kiseki::cli::MacBackgroundStateOptions& options, kiseki::cli::Io) {
        REQUIRE(options.pid == 123);
        REQUIRE(options.window_id == 456);
        REQUIRE(options.output_path == screenshot_path);
        REQUIRE(options.query == "button");
        calls.push_back("state");
        return 0;
    };
    dependencies.mac_background_screenshot = [&](const kiseki::cli::MacBackgroundScreenshotOptions& options, kiseki::cli::Io) {
        REQUIRE(options.window_id == 456);
        REQUIRE(options.output_path == screenshot_path);
        REQUIRE(options.format == "jpeg");
        REQUIRE(options.quality == 80);
        calls.push_back("screenshot");
        return 0;
    };
    dependencies.mac_background_click = [&](const kiseki::cli::MacBackgroundClickOptions& options, kiseki::cli::Io) {
        REQUIRE(options.pid == 123);
        REQUIRE(options.window_id == 456);
        REQUIRE(options.has_window_id);
        REQUIRE(options.x == 10.0);
        REQUIRE(options.y == 20.0);
        REQUIRE(options.has_xy);
        REQUIRE_FALSE(options.has_element_index);
        REQUIRE(options.button == "double");
        REQUIRE(options.modifiers == std::vector<std::string>{"cmd", "shift"});
        calls.push_back("click");
        return 0;
    };
    dependencies.mac_background_text = [&](const kiseki::cli::MacBackgroundTextOptions& options, kiseki::cli::Io) {
        REQUIRE(options.pid == 123);
        REQUIRE(options.text == "hello");
        REQUIRE(options.window_id == 456);
        REQUIRE(options.has_window_id);
        REQUIRE(options.element_index == 7);
        REQUIRE(options.has_element_index);
        REQUIRE(options.delay_ms == 15);
        calls.push_back("text");
        return 0;
    };
    dependencies.mac_background_key = [&](const kiseki::cli::MacBackgroundKeyOptions& options, kiseki::cli::Io) {
        REQUIRE(options.pid == 123);
        REQUIRE(options.key == "return");
        REQUIRE(options.window_id == 456);
        REQUIRE(options.has_window_id);
        REQUIRE(options.modifiers == std::vector<std::string>{"cmd"});
        calls.push_back("key");
        return 0;
    };
    dependencies.mac_background_hotkey = [&](const kiseki::cli::MacBackgroundHotkeyOptions& options, kiseki::cli::Io) {
        REQUIRE(options.pid == 123);
        REQUIRE(options.window_id == 456);
        REQUIRE(options.has_window_id);
        REQUIRE(options.keys == std::vector<std::string>{"cmd", "c"});
        calls.push_back("hotkey");
        return 0;
    };
    dependencies.mac_background_drag = [&](const kiseki::cli::MacBackgroundDragOptions& options, kiseki::cli::Io) {
        REQUIRE(options.pid == 123);
        REQUIRE(options.window_id == 456);
        REQUIRE(options.has_window_id);
        REQUIRE(options.from_x == 1.0);
        REQUIRE(options.from_y == 2.0);
        REQUIRE(options.to_x == 3.0);
        REQUIRE(options.to_y == 4.0);
        REQUIRE(options.duration_ms == 250);
        REQUIRE(options.steps == 12);
        REQUIRE(options.button == "left");
        REQUIRE(options.modifiers == std::vector<std::string>{"option"});
        calls.push_back("drag");
        return 0;
    };
    dependencies.mac_background_draw = [&](const kiseki::cli::MacBackgroundDrawOptions& options, kiseki::cli::Io) {
        REQUIRE(options.pid == 123);
        REQUIRE(options.window_id == 456);
        REQUIRE(options.path == points_path);
        REQUIRE(options.duration_ms == 80);
        REQUIRE(options.steps == 5);
        REQUIRE(options.stroke_gap_ms == 10);
        REQUIRE(options.max_segments == 24);
        REQUIRE(options.button == "left");
        REQUIRE(options.modifiers == std::vector<std::string>{"shift"});
        calls.push_back("draw");
        return 0;
    };
    dependencies.mac_background_feedback_status = [&](const kiseki::cli::MacBackgroundFeedbackStatusOptions&, kiseki::cli::Io) {
        calls.push_back("feedback-status");
        return 0;
    };
    dependencies.mac_background_feedback_enable = [&](const kiseki::cli::MacBackgroundFeedbackEnableOptions& options, kiseki::cli::Io) {
        REQUIRE_FALSE(options.enabled);
        calls.push_back("feedback-enable");
        return 0;
    };
    dependencies.mac_background_feedback_motion = [&](const kiseki::cli::MacBackgroundFeedbackMotionOptions& options, kiseki::cli::Io) {
        REQUIRE(options.has_start_handle);
        REQUIRE(options.start_handle == 0.25);
        REQUIRE_FALSE(options.has_end_handle);
        REQUIRE(options.has_arc_size);
        REQUIRE(options.arc_size == 0.35);
        REQUIRE(options.has_glide_duration_ms);
        REQUIRE(options.glide_duration_ms == 900.0);
        REQUIRE(options.has_dwell_after_click_ms);
        REQUIRE(options.dwell_after_click_ms == 250.0);
        REQUIRE(options.has_idle_hide_ms);
        REQUIRE(options.idle_hide_ms == 5000.0);
        calls.push_back("feedback-motion");
        return 0;
    };
    dependencies.mac_background_feedback_style = [&](const kiseki::cli::MacBackgroundFeedbackStyleOptions& options, kiseki::cli::Io) {
        REQUIRE_FALSE(options.reset);
        REQUIRE(options.has_gradient_colors);
        REQUIRE(options.gradient_colors == std::vector<std::string>{"#00AAFF", "#22CC88"});
        REQUIRE(options.has_bloom_color);
        REQUIRE(options.bloom_color == "#00AAFF");
        REQUIRE_FALSE(options.has_image_path);
        calls.push_back("feedback-style");
        return 0;
    };
    dependencies.mac_background_feedback_preset = [&](const kiseki::cli::MacBackgroundFeedbackPresetOptions& options, kiseki::cli::Io) {
        REQUIRE(options.name == "natural");
        calls.push_back("feedback-preset");
        return 0;
    };

    REQUIRE(kiseki::cli::run({"mac-background", "status", "--prompt"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "launch", "--bundle-id", "com.apple.Safari", "--url", "about:blank", "--new-instance", "--arg=--test"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "windows", "--pid", "123", "--on-screen-only"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "state", "--pid", "123", "--window-id", "456", "--output", screenshot_path.string(), "--query", "button"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "screenshot", "--window-id", "456", "--output", screenshot_path.string(), "--format", "jpeg", "--quality", "80"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "click", "--pid", "123", "--window-id", "456", "--x", "10", "--y", "20", "--button", "double", "--modifiers", "cmd+shift"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "text", "--pid", "123", "--window-id", "456", "--element-index", "7", "--file", text_path.string(), "--delay-ms", "15"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "key", "--pid", "123", "--window-id", "456", "--key", "return", "--modifiers", "cmd"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "hotkey", "--pid", "123", "--window-id", "456", "--keys", "cmd+c"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "drag", "--pid", "123", "--window-id", "456", "--from-x", "1", "--from-y", "2", "--to-x", "3", "--to-y", "4", "--duration-ms", "250", "--steps", "12", "--button", "left", "--modifiers", "option"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "draw", "--pid", "123", "--window-id", "456", "--file", points_path.string(), "--duration-ms", "80", "--steps", "5", "--stroke-gap-ms", "10", "--max-segments", "24", "--button", "left", "--modifiers", "shift"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "feedback", "status"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "feedback", "enable", "--enabled", "false"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "feedback", "motion", "--start-handle", "0.25", "--arc-size", "0.35", "--glide-duration-ms", "900", "--dwell-after-click-ms", "250", "--idle-hide-ms", "5000"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "feedback", "style", "--gradient-colors", "#00AAFF,#22CC88", "--bloom-color", "#00AAFF"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);
    REQUIRE(kiseki::cli::run({"mac-background", "feedback", "preset", "--name", "natural"}, config_path, kiseki::cli::Io{out, err}, dependencies) == 0);

    REQUIRE(calls == std::vector<std::string>{
        "status",
        "launch",
        "windows",
        "state",
        "screenshot",
        "click",
        "text",
        "key",
        "hotkey",
        "drag",
        "draw",
        "feedback-status",
        "feedback-enable",
        "feedback-motion",
        "feedback-style",
        "feedback-preset"});
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
        REQUIRE(options.step_delay_ms == 2);
        REQUIRE(options.start_hold_ms == 0);
        REQUIRE(options.end_hold_ms == 0);
        calls.push_back("drag");
        return 0;
    };
    dependencies.input_background_drag = [&](const kiseki::cli::BackgroundDragOptions& options, kiseki::cli::Io) {
        REQUIRE(options.target.title == "Paint");
        REQUIRE(options.path == points_path);
        calls.push_back("background-drag");
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

TEST_CASE("macro run supports background drag steps") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto macro_path = temp.file("macro.json");
    const auto points_path = temp.file("points.txt");
    write_text(points_path, "10 20\n30 40\n");
    write_text(
        macro_path,
        R"({
  "steps": [
    {"type": "background-drag", "targetTitle": "Paint", "file": ")" + points_path.generic_string() + R"("}
  ]
})");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.input_background_drag = [&](const kiseki::cli::BackgroundDragOptions& options, kiseki::cli::Io) {
        REQUIRE(options.target.title == "Paint");
        REQUIRE(options.target.pid == 0);
        REQUIRE(options.target.window_id.empty());
        REQUIRE(options.path == points_path);
        called = true;
        return 0;
    };

    const int code = kiseki::cli::run(
        {"macro", "run", "--file", macro_path.string()},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str() == "macro completed 1 steps\n");
    REQUIRE(err.str().empty());
}

TEST_CASE("macro run supports background screenshot steps") {
    std::ostringstream out;
    std::ostringstream err;
    const TempConfigDirectory temp;
    const auto config_path = temp.file("config.json");
    const auto macro_path = temp.file("macro.json");
    const auto output_path = temp.file("paint-background.bmp");
    write_text(
        macro_path,
        R"({
  "steps": [
    {"type": "background-screenshot", "targetWindowId": "0x123", "output": ")" + output_path.generic_string() + R"("}
  ]
})");
    bool called = false;

    kiseki::cli::Dependencies dependencies;
    dependencies.capture_background_window = [&](const kiseki::cli::ScreenshotBackgroundWindowOptions& options, kiseki::cli::Io) {
        REQUIRE(options.target.title.empty());
        REQUIRE(options.target.pid == 0);
        REQUIRE(options.target.window_id == "0x123");
        REQUIRE(options.output_path == output_path);
        called = true;
        return 0;
    };

    const int code = kiseki::cli::run(
        {"macro", "run", "--file", macro_path.string()},
        config_path,
        kiseki::cli::Io{out, err},
        dependencies);

    REQUIRE(code == 0);
    REQUIRE(called);
    REQUIRE(out.str() == "macro completed 1 steps\n");
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
