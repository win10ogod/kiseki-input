#include <catch2/catch_test_macros.hpp>

#include <string>

#include "webui/static_assets.hpp"
#include "webui/web_server.hpp"

using kiseki::webui::app_js;
using kiseki::webui::build_listen_url;
using kiseki::webui::index_html;

TEST_CASE("webui assets do not reference operational api routes") {
    const std::string html = std::string{index_html()};
    const std::string js = std::string{app_js()};
    const std::string combined = html + "\n" + js;

    REQUIRE(combined.find("/api/input") == std::string::npos);
    REQUIRE(combined.find("/api/screenshot") == std::string::npos);
    REQUIRE(combined.find("/api/execute") == std::string::npos);
    REQUIRE(combined.find("/api/notify") == std::string::npos);
    REQUIRE(combined.find("/api/daemon") == std::string::npos);
}

TEST_CASE("webui can load teaching bundles from local files") {
    const std::string html = std::string{index_html()};
    const std::string js = std::string{app_js()};

    REQUIRE(html.find("teach-files") != std::string::npos);
    REQUIRE(html.find("teach-video") != std::string::npos);
    REQUIRE(html.find("teach-audio") != std::string::npos);
    REQUIRE(html.find("teach-transcript") != std::string::npos);
    REQUIRE(js.find("manifest.json") != std::string::npos);
    REQUIRE(js.find("actions.json") != std::string::npos);
    REQUIRE(js.find("events.jsonl") != std::string::npos);
    REQUIRE(js.find("annotations.json") != std::string::npos);
    REQUIRE(js.find("videoKeyframes") != std::string::npos);
    REQUIRE(js.find("URL.createObjectURL") != std::string::npos);
}

TEST_CASE("webui listen url uses host and port") {
    REQUIRE(build_listen_url("127.0.0.1", 8787) == "http://127.0.0.1:8787");
}
