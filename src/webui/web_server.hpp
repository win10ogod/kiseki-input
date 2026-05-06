#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace kiseki::webui {

class WebServer {
public:
    explicit WebServer(std::filesystem::path config_path);

    int listen(const std::string& host, std::uint16_t port);

private:
    std::filesystem::path config_path_;
};

std::string build_listen_url(const std::string& host, std::uint16_t port);

}
