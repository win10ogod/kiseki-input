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
    REQUIRE(json.at("input").at("backgroundWindow").get<bool>() == false);
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
    REQUIRE(result.out.find("Input driver backend: unavailable") != std::string::npos);
    REQUIRE(result.out.find("Background-window input: unavailable") != std::string::npos);
    REQUIRE(result.out.find("Screenshot burst: unavailable") != std::string::npos);
    REQUIRE(result.out.find("Limitations:") != std::string::npos);
    REQUIRE(result.out.find("foundation build exposes configuration and WebUI only") != std::string::npos);
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
