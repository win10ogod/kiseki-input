#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "webui/config_api.hpp"

using kiseki::webui::ConfigApi;

namespace {

std::filesystem::path temp_config_path(std::string_view name) {
    return std::filesystem::temp_directory_path() / std::string{name};
}

}

TEST_CASE("config api returns default config") {
    const auto path = temp_config_path("kiseki-webui-api-get-test.json");
    std::filesystem::remove(path);
    ConfigApi api{path};

    const auto response = api.get_config();
    const auto json = nlohmann::json::parse(response.body);

    REQUIRE(response.status == 200);
    REQUIRE(response.content_type == "application/json");
    REQUIRE(json["schemaVersion"].get<int>() == 1);
}

TEST_CASE("config api rejects invalid saved config") {
    const auto path = temp_config_path("kiseki-webui-api-put-test.json");
    std::filesystem::remove(path);
    ConfigApi api{path};

    const auto response = api.put_config(R"({"webui":{"port":0}})");

    REQUIRE(response.status == 400);
    REQUIRE(response.body.find("webui.port") != std::string::npos);
}

TEST_CASE("config api exposes only configuration routes") {
    const auto routes = ConfigApi::routes();

    REQUIRE(routes == std::vector<std::string>{
                          "GET /api/config",
                          "PUT /api/config",
                          "GET /api/capabilities",
                      });
}
