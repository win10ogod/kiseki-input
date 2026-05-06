#include "webui/web_server.hpp"

#include <utility>

#include <httplib.h>

#include "webui/config_api.hpp"
#include "webui/static_assets.hpp"

namespace kiseki::webui {

WebServer::WebServer(std::filesystem::path config_path)
    : config_path_{std::move(config_path)} {}

int WebServer::listen(const std::string& host, std::uint16_t port) {
    ConfigApi api{config_path_};
    httplib::Server server;

    server.Get("/", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(std::string{index_html()}, "text/html; charset=utf-8");
    });
    server.Get("/styles.css", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(std::string{styles_css()}, "text/css; charset=utf-8");
    });
    server.Get("/app.js", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(std::string{app_js()}, "application/javascript; charset=utf-8");
    });
    server.Get("/api/config", [&api](const httplib::Request&, httplib::Response& response) {
        const auto result = api.get_config();
        response.status = result.status;
        response.set_content(result.body, result.content_type);
    });
    server.Put("/api/config", [&api](const httplib::Request& request, httplib::Response& response) {
        const auto result = api.put_config(request.body);
        response.status = result.status;
        response.set_content(result.body, result.content_type);
    });
    server.Get("/api/capabilities", [&api](const httplib::Request&, httplib::Response& response) {
        const auto result = api.get_capabilities();
        response.status = result.status;
        response.set_content(result.body, result.content_type);
    });

    return server.listen(host, port) ? 0 : 2;
}

std::string build_listen_url(const std::string& host, std::uint16_t port) {
    return "http://" + host + ":" + std::to_string(port);
}

}
