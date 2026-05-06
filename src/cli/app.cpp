#include "cli/app.hpp"

#include <CLI/CLI.hpp>

#include <exception>
#include <string>
#include <utility>

#include "core/capabilities/capabilities_model.hpp"
#include "core/config/config_model.hpp"
#include "core/config/config_store.hpp"
#include "core/version.hpp"
#include "platform/capture/screenshot.hpp"
#include "platform/input/input.hpp"
#include "platform/notification/notification.hpp"
#include "platform/runtime_capabilities.hpp"
#include "webui/web_server.hpp"

namespace kiseki::cli {

namespace {

using kiseki::core::capabilities::foundation_capabilities;
using kiseki::core::capabilities::to_json;
using kiseki::core::config::ConfigStore;
using kiseki::core::config::current_environment;
using kiseki::core::config::current_platform;
using kiseki::core::config::default_config_path;

int show_config(const std::filesystem::path& config_path, Io io) {
    const ConfigStore store{config_path};
    const auto result = store.load_or_default();
    if (!result.ok) {
        io.err << result.error << '\n';
        return 2;
    }

    io.out << kiseki::core::config::to_json(result.config).dump(2) << '\n';
    return 0;
}

int validate_config_command(const std::filesystem::path& config_path, Io io) {
    const ConfigStore store{config_path};
    const auto result = store.load_or_default();
    if (!result.ok) {
        io.err << result.error << '\n';
        return 2;
    }

    io.out << "configuration is valid\n";
    return 0;
}

const char* availability(bool available) {
    return available ? "available" : "unavailable";
}

int print_operation_result(const kiseki::platform::OperationResult& result, Io io) {
    if (result.ok) {
        if (!result.message.empty()) {
            io.out << result.message << '\n';
        }
    } else {
        io.err << result.error << '\n';
    }
    return result.code;
}

int print_capture_result(const kiseki::platform::CaptureResult& result, Io io) {
    if (result.ok) {
        io.out << "captured " << result.output_path.string() << " " << result.width << "x" << result.height << '\n';
    } else {
        io.err << result.error << '\n';
    }
    return result.code;
}

}

Dependencies default_dependencies() {
    return Dependencies{
        .launch_config_ui = [](const WebUiLaunchOptions& options, const std::filesystem::path& config_path, Io io) {
            io.out << "Serving configuration UI at "
                   << kiseki::webui::build_listen_url(options.host, options.port) << '\n';
            kiseki::webui::WebServer server{config_path};
            return server.listen(options.host, options.port);
        },
        .capture_desktop = [](const ScreenshotDesktopOptions& options, Io io) {
            return print_capture_result(kiseki::platform::capture::capture_desktop_bmp(options.output_path), io);
        },
        .capture_burst = [](const ScreenshotBurstOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::capture::capture_burst_bmp(kiseki::platform::capture::BurstOptions{
                    .output_directory = options.output_directory,
                    .prefix = options.prefix,
                    .frames = options.frames,
                    .fps = options.fps,
                }),
                io);
        },
        .input_key = [](const InputKeyOptions& options, Io io) {
            return print_operation_result(kiseki::platform::input::tap_key(options.key), io);
        },
        .input_combo = [](const InputComboOptions& options, Io io) {
            return print_operation_result(kiseki::platform::input::key_combo(options.keys), io);
        },
        .input_text = [](const InputTextOptions& options, Io io) {
            return print_operation_result(kiseki::platform::input::type_text(options.text), io);
        },
        .input_mouse = [](const InputMouseOptions& options, Io io) {
            return print_operation_result(
                kiseki::platform::input::mouse_action(kiseki::platform::input::MouseOptions{
                    .dx = options.dx,
                    .dy = options.dy,
                    .click = options.click,
                }),
                io);
        },
        .run_daemon = [](const DaemonOptions& options, const std::filesystem::path& config_path, Io io) {
            return kiseki::platform::notification::run_heartbeat_daemon(config_path, options.once, io.out, io.err);
        },
    };
}

std::filesystem::path resolve_config_path(std::filesystem::path override_path) {
    if (!override_path.empty()) {
        return override_path;
    }

    return default_config_path(current_environment(), current_platform());
}

int run(
    const std::vector<std::string>& args,
    std::filesystem::path config_path,
    Io io,
    Dependencies dependencies) {
    std::filesystem::path config_path_override = std::move(config_path);
    const auto active_config_path = [&]() {
        return resolve_config_path(config_path_override);
    };
    const auto make_store = [&]() {
        return ConfigStore{active_config_path()};
    };
    int exit_code = 0;
    WebUiLaunchOptions webui_options{
        .host = "",
        .port = 0,
    };
    ScreenshotDesktopOptions desktop_options{
        .output_path = {},
    };
    ScreenshotBurstOptions burst_options{
        .output_directory = {},
        .prefix = "frame",
        .frames = 0,
        .fps = 0,
    };
    InputKeyOptions key_options{
        .key = "",
    };
    InputComboOptions combo_options{
        .keys = "",
    };
    InputTextOptions text_options{
        .text = "",
    };
    InputMouseOptions mouse_options{
        .dx = 0,
        .dy = 0,
        .click = "none",
    };
    DaemonOptions daemon_options{
        .once = false,
    };

    CLI::App app{"Kiseki Input"};
    app.set_version_flag("--version", std::string{kiseki::core::version()});
    app.add_option("--config", config_path_override, "Config file path");
    app.require_subcommand(1);

    CLI::App* config = app.add_subcommand("config", "Configuration commands");
    config->require_subcommand(1);
    config->add_subcommand("path", "Print active config path")->callback([&]() {
        io.out << active_config_path().string() << '\n';
    });
    config->add_subcommand("show", "Print active config as JSON")->callback([&]() {
        exit_code = show_config(active_config_path(), io);
    });
    config->add_subcommand("validate", "Validate active config")->callback([&]() {
        exit_code = validate_config_command(active_config_path(), io);
    });

    auto* config_ui = app.add_subcommand("config-ui", "Launch local configuration WebUI");
    config_ui->add_option("--host", webui_options.host, "Listen host");
    config_ui->add_option("--port", webui_options.port, "Listen port");
    config_ui->callback([&]() {
        const auto store = make_store();
        const auto loaded = store.load_or_default();
        if (!loaded.ok) {
            io.err << "config error: " << loaded.error << '\n';
            exit_code = 2;
            return;
        }

        if (webui_options.host.empty()) {
            webui_options.host = loaded.config.webui.host;
        }
        if (webui_options.port == 0) {
            webui_options.port = loaded.config.webui.port;
        }

        if (!dependencies.launch_config_ui) {
            io.err << "config-ui launcher is not configured\n";
            exit_code = 2;
            return;
        }

        exit_code = dependencies.launch_config_ui(webui_options, store.path(), io);
    });

    auto* screenshot = app.add_subcommand("screenshot", "Screenshot commands");
    screenshot->require_subcommand(1);
    auto* screenshot_desktop = screenshot->add_subcommand("desktop", "Capture the desktop to a BMP file");
    screenshot_desktop->add_option("-o,--output", desktop_options.output_path, "Output BMP path")->required();
    screenshot_desktop->callback([&]() {
        if (!dependencies.capture_desktop) {
            io.err << "screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.capture_desktop(desktop_options, io);
    });

    auto* screenshot_burst = screenshot->add_subcommand("burst", "Capture a burst of desktop BMP frames");
    screenshot_burst->add_option("-d,--directory", burst_options.output_directory, "Output directory");
    screenshot_burst->add_option("--prefix", burst_options.prefix, "Frame filename prefix");
    screenshot_burst->add_option("--frames", burst_options.frames, "Frame count");
    screenshot_burst->add_option("--fps", burst_options.fps, "Target frames per second");
    screenshot_burst->callback([&]() {
        if (!dependencies.capture_burst) {
            io.err << "screenshot backend is not configured\n";
            exit_code = 2;
            return;
        }
        const auto loaded = make_store().load_or_default();
        if (!loaded.ok) {
            io.err << "config error: " << loaded.error << '\n';
            exit_code = 2;
            return;
        }
        if (burst_options.output_directory.empty()) {
            burst_options.output_directory = loaded.config.screenshot.default_output_directory.empty()
                                                 ? std::filesystem::path{"."}
                                                 : std::filesystem::path{loaded.config.screenshot.default_output_directory};
        }
        if (burst_options.frames == 0) {
            burst_options.frames = loaded.config.screenshot.burst_frames;
        }
        if (burst_options.fps == 0) {
            burst_options.fps = loaded.config.screenshot.burst_fps;
        }
        exit_code = dependencies.capture_burst(burst_options, io);
    });

    auto* input = app.add_subcommand("input", "Keyboard and mouse input commands");
    input->require_subcommand(1);
    auto* input_key = input->add_subcommand("key", "Tap a key");
    input_key->add_option("--key", key_options.key, "Key name")->required();
    input_key->callback([&]() {
        if (!dependencies.input_key) {
            io.err << "input backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_key(key_options, io);
    });

    auto* input_combo = input->add_subcommand("combo", "Press a key combo such as win+r");
    input_combo->add_option("--keys", combo_options.keys, "Key combo joined by +")->required();
    input_combo->callback([&]() {
        if (!dependencies.input_combo) {
            io.err << "input backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_combo(combo_options, io);
    });

    auto* input_text = input->add_subcommand("text", "Type text");
    input_text->add_option("--text", text_options.text, "Text to type")->required();
    input_text->callback([&]() {
        if (!dependencies.input_text) {
            io.err << "input backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_text(text_options, io);
    });

    auto* input_mouse = input->add_subcommand("mouse", "Move and optionally click the mouse");
    input_mouse->add_option("--dx", mouse_options.dx, "Relative X movement");
    input_mouse->add_option("--dy", mouse_options.dy, "Relative Y movement");
    input_mouse->add_option("--click", mouse_options.click, "none, left, right, or middle");
    input_mouse->callback([&]() {
        if (!dependencies.input_mouse) {
            io.err << "input backend is not configured\n";
            exit_code = 2;
            return;
        }
        exit_code = dependencies.input_mouse(mouse_options, io);
    });

    auto* daemon = app.add_subcommand("daemon", "Background daemon commands");
    daemon->require_subcommand(1);
    auto* daemon_run = daemon->add_subcommand("run", "Run heartbeat notification daemon");
    daemon_run->add_flag("--once", daemon_options.once, "Run one heartbeat cycle and exit");
    daemon_run->callback([&]() {
        if (!dependencies.run_daemon) {
            io.err << "daemon backend is not configured\n";
            exit_code = 2;
            return;
        }
        const auto store = make_store();
        exit_code = dependencies.run_daemon(daemon_options, store.path(), io);
    });

    app.add_subcommand("capabilities", "Print foundation capabilities")->callback([&]() {
        io.out << to_json(kiseki::platform::runtime_capabilities()).dump(2) << '\n';
    });

    app.add_subcommand("doctor", "Print diagnostics")->callback([&]() {
        io.out << "Kiseki Input doctor\n";
        io.out << "Version: " << kiseki::core::version() << '\n';
        io.out << "Config path: " << active_config_path().string() << '\n';

        const auto result = make_store().load_or_default();
        if (result.ok) {
            io.out << "Config: valid\n";
        } else {
            io.out << "Config: invalid: " << result.error << '\n';
        }

        const auto capabilities = kiseki::platform::runtime_capabilities();
        io.out << "Capabilities:\n";
        io.out << "  Input driver backend: " << availability(capabilities.input.driver) << '\n';
        io.out << "  System input backend: " << availability(capabilities.input.system) << '\n';
        io.out << "  Background-window input: " << availability(capabilities.input.background_window) << '\n';
        io.out << "  Desktop screenshot: " << availability(capabilities.capture.desktop) << '\n';
        io.out << "  Screenshot burst: " << availability(capabilities.capture.burst) << '\n';

        io.out << "Limitations:\n";
        for (const auto& limitation : capabilities.limitations) {
            io.out << "  - " << limitation << '\n';
        }
    });

    std::vector<std::string> parse_args;
    parse_args.reserve(args.size() + 1);
    parse_args.emplace_back("kiseki");
    parse_args.insert(parse_args.end(), args.begin(), args.end());

    std::vector<char*> argv;
    argv.reserve(parse_args.size());
    for (std::string& arg : parse_args) {
        argv.push_back(arg.data());
    }

    try {
        app.parse(static_cast<int>(argv.size()), argv.data());
    } catch (const CLI::ParseError& error) {
        return app.exit(error, io.out, io.err);
    } catch (const std::exception& error) {
        io.err << error.what() << '\n';
        return 2;
    }

    return exit_code;
}

}
