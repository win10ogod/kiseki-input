#pragma once

#include <filesystem>
#include <string>

namespace kiseki::platform {

struct OperationResult {
    bool ok;
    int code;
    std::string message;
    std::string error;
};

struct CaptureResult {
    bool ok;
    int code;
    std::filesystem::path output_path;
    int width;
    int height;
    std::string error;
};

}
