#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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

}

TEST_CASE("config path prints injected config path") {
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    const auto result = run_cli({"config", "path"}, path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out == path.string() + "\n");
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

TEST_CASE("config validate reports valid config") {
    const TempConfigDirectory temp;
    const auto path = temp.file("missing.json");

    const auto result = run_cli({"config", "validate"}, path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out == "configuration is valid\n");
    REQUIRE(result.err.empty());
}

TEST_CASE("capabilities prints foundation capability json") {
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    const auto result = run_cli({"capabilities"}, path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out.find("\"backgroundWindow\"") != std::string::npos);
    REQUIRE(result.out.find("\"limitations\"") != std::string::npos);
    REQUIRE(result.err.empty());
}

TEST_CASE("doctor prints diagnostic text") {
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    const auto result = run_cli({"doctor"}, path);

    REQUIRE(result.code == 0);
    REQUIRE(result.out.find("Kiseki Input doctor") != std::string::npos);
    REQUIRE(result.out.find("Config path:") != std::string::npos);
    REQUIRE(result.err.empty());
}
