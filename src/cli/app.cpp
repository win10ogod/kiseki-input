#include "cli/app.hpp"

#include <CLI/CLI.hpp>

#include <exception>
#include <string>
#include <utility>

#include "core/capabilities/capabilities_model.hpp"
#include "core/config/config_model.hpp"
#include "core/config/config_store.hpp"
#include "core/version.hpp"
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

}

Dependencies default_dependencies() {
    return Dependencies{
        .launch_config_ui = [](const WebUiLaunchOptions& options, const std::filesystem::path& config_path, Io io) {
            io.out << "Serving configuration UI at "
                   << kiseki::webui::build_listen_url(options.host, options.port) << '\n';
            kiseki::webui::WebServer server{config_path};
            return server.listen(options.host, options.port);
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
    const std::filesystem::path active_config_path = resolve_config_path(std::move(config_path));
    const ConfigStore store{active_config_path};
    int exit_code = 0;
    WebUiLaunchOptions webui_options{
        .host = "",
        .port = 0,
    };

    CLI::App app{"Kiseki Input"};
    app.set_version_flag("--version", std::string{kiseki::core::version()});
    app.require_subcommand(1);

    CLI::App* config = app.add_subcommand("config", "Configuration commands");
    config->require_subcommand(1);
    config->add_subcommand("path", "Print active config path")->callback([&]() {
        io.out << active_config_path.string() << '\n';
    });
    config->add_subcommand("show", "Print active config as JSON")->callback([&]() {
        exit_code = show_config(active_config_path, io);
    });
    config->add_subcommand("validate", "Validate active config")->callback([&]() {
        exit_code = validate_config_command(active_config_path, io);
    });

    auto* config_ui = app.add_subcommand("config-ui", "Launch local configuration WebUI");
    config_ui->add_option("--host", webui_options.host, "Listen host");
    config_ui->add_option("--port", webui_options.port, "Listen port");
    config_ui->callback([&]() {
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

    app.add_subcommand("capabilities", "Print foundation capabilities")->callback([&]() {
        io.out << to_json(foundation_capabilities()).dump(2) << '\n';
    });

    app.add_subcommand("doctor", "Print diagnostics")->callback([&]() {
        io.out << "Kiseki Input doctor\n";
        io.out << "Version: " << kiseki::core::version() << '\n';
        io.out << "Config path: " << active_config_path.string() << '\n';

        const auto result = store.load_or_default();
        if (result.ok) {
            io.out << "Config: valid\n";
        } else {
            io.out << "Config: invalid: " << result.error << '\n';
        }

        const auto capabilities = foundation_capabilities();
        io.out << "Capabilities:\n";
        io.out << "  Input driver backend: " << availability(capabilities.input.driver) << '\n';
        io.out << "  Background-window input: " << availability(capabilities.input.background_window) << '\n';
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
