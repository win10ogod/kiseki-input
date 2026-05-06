#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

#include "core/config/config_store.hpp"

using kiseki::core::config::ConfigStore;
using kiseki::core::config::EnvironmentSnapshot;
using kiseki::core::config::PlatformKind;
using kiseki::core::config::default_config;
using kiseki::core::config::default_config_path;

namespace {

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

TEST_CASE("default path falls back to home on linux") {
    EnvironmentSnapshot env;
    env.home = "/home/a";

    const auto path = default_config_path(env, PlatformKind::Linux);

    REQUIRE(path.generic_string() == "/home/a/.config/kiseki-input/config.json");
}

TEST_CASE("store saves and loads config") {
    const auto temp = std::filesystem::temp_directory_path() / "kiseki-config-store-test.json";
    std::filesystem::remove(temp);

    ConfigStore store{temp};
    auto config = default_config();
    config.webui.port = 9999;

    const auto save_result = store.save(config);
    REQUIRE(save_result.ok);

    const auto load_result = store.load_or_default();
    REQUIRE(load_result.ok);
    REQUIRE(load_result.config.webui.port == 9999);

    std::filesystem::remove(temp);
}

TEST_CASE("invalid saved config returns validation error") {
    const auto temp = std::filesystem::temp_directory_path() / "kiseki-invalid-config-store-test.json";
    write_text(temp, "{\"webui\":{\"port\":0}}");

    ConfigStore store{temp};
    const auto load_result = store.load_or_default();

    REQUIRE_FALSE(load_result.ok);
    REQUIRE(load_result.error.find("webui.port") != std::string::npos);

    std::filesystem::remove(temp);
}
