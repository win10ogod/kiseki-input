#include <CLI/CLI.hpp>

#include <iostream>

#include "core/version.hpp"

int main(int argc, char** argv) {
    CLI::App app{"Kiseki Input"};
    app.set_version_flag("--version", std::string{kiseki::core::version()});

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    std::cout << "Kiseki Input " << kiseki::core::version() << '\n';
    return 0;
}
