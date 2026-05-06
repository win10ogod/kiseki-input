#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kiseki::webui {

struct ApiResponse {
    int status;
    std::string body;
    std::string content_type;
};

class ConfigApi {
public:
    explicit ConfigApi(std::filesystem::path config_path);

    ApiResponse get_config() const;
    ApiResponse put_config(std::string_view body) const;
    ApiResponse get_capabilities() const;

    static std::vector<std::string> routes();

private:
    std::filesystem::path config_path_;
};

}
