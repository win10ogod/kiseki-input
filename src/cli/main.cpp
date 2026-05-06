#include <iostream>
#include <string>
#include <vector>

#include "cli/app.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));

    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }

    return kiseki::cli::run(args, {}, kiseki::cli::Io{std::cout, std::cerr});
}
