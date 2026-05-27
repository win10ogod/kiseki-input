#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "core/config/config_store.hpp"

using kiseki::core::config::ConfigStore;
using kiseki::core::config::EnvironmentSnapshot;
using kiseki::core::config::PlatformKind;
using kiseki::core::config::default_config;
using kiseki::core::config::default_config_path;

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
        return "kiseki-config-store-test-" + std::to_string(ticks) + "-" +
               std::to_string(counter.fetch_add(1));
    }

    std::filesystem::path path_;
};

void write_text(const std::filesystem::path& path, std::string_view content) {
    std::ofstream file{path};
    file << content;
}

}

TEST_CASE("default path uses appdata on windows") {
    EnvironmentSnapshot env;
    env.appdata = "C:/Users/A/AppData/Roaming";

    const auto path = default_config_path(env, PlatformKind::Windows);

    REQUIRE(path.generic_string() == "C:/Users/A/AppData/Roaming/KisekiInput/config.json");
}

TEST_CASE("default path uses xdg config home on linux") {
    EnvironmentSnapshot env;
    env.xdg_config_home = "/home/a/.config-custom";
    env.home = "/home/a";

    const auto path = default_config_path(env, PlatformKind::Linux);

    REQUIRE(path.generic_string() == "/home/a/.config-custom/kiseki-input/config.json");
}

TEST_CASE("default path uses application support on macos") {
    EnvironmentSnapshot env;
    env.home = "/Users/a";

    const auto path = default_config_path(env, PlatformKind::MacOS);

    REQUIRE(path.generic_string() == "/Users/a/Library/Application Support/KisekiInput/config.json");
}

TEST_CASE("default path falls back to home on linux") {
    EnvironmentSnapshot env;
    env.home = "/home/a";

    const auto path = default_config_path(env, PlatformKind::Linux);

    REQUIRE(path.generic_string() == "/home/a/.config/kiseki-input/config.json");
}

TEST_CASE("store saves and loads config") {
    const TempConfigDirectory temp;
    const auto path = temp.file("config.json");

    ConfigStore store{path};
    auto config = default_config();
    config.webui.port = 9999;

    const auto save_result = store.save(config);
    REQUIRE(save_result.ok);

    const auto load_result = store.load_or_default();
    REQUIRE(load_result.ok);
    REQUIRE(load_result.config.webui.port == 9999);
}

TEST_CASE("missing saved config returns defaults") {
    const TempConfigDirectory temp;
    ConfigStore store{temp.file("missing.json")};

    const auto load_result = store.load_or_default();

    REQUIRE(load_result.ok);
    REQUIRE(load_result.config.webui.port == default_config().webui.port);
    REQUIRE(load_result.error.empty());
}

TEST_CASE("invalid saved config returns validation error") {
    const TempConfigDirectory temp;
    const auto path = temp.file("invalid.json");
    write_text(path, "{\"webui\":{\"port\":0}}");

    ConfigStore store{path};
    const auto load_result = store.load_or_default();

    REQUIRE_FALSE(load_result.ok);
    REQUIRE(load_result.error.find("webui.port") != std::string::npos);
}

TEST_CASE("malformed config reports parse context") {
    const TempConfigDirectory temp;
    const auto path = temp.file("malformed.json");
    write_text(path, "{\"webui\":");

    ConfigStore store{path};
    const auto load_result = store.load_or_default();

    REQUIRE_FALSE(load_result.ok);
    REQUIRE(load_result.error.find("failed to parse config file") != std::string::npos);
    REQUIRE(load_result.error.find(path.string()) != std::string::npos);
}

TEST_CASE("save failure reports operation and path context") {
    const TempConfigDirectory temp;
    const auto parent_file = temp.file("not-a-directory");
    write_text(parent_file, "");

    ConfigStore store{parent_file / "config.json"};
    const auto save_result = store.save(default_config());

    REQUIRE_FALSE(save_result.ok);
    REQUIRE(save_result.error.find("failed to create config directory") != std::string::npos);
    REQUIRE(save_result.error.find(parent_file.string()) != std::string::npos);
}
