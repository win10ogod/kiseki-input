#pragma once

#include <filesystem>
#include <string>
#include <optional>

namespace kiseki::platform {

struct OperationResult {
    bool ok;
    int code;
    std::string message;
    std::string error;
};

struct CaptureCoordinates {
    std::string space; // Screen coordinates accepted by native absolute input.
    double origin_x = 0;
    double origin_y = 0;
    double width = 0;
    double height = 0;
    double pixels_per_unit_x = 1;
    double pixels_per_unit_y = 1;
    std::optional<std::string> window_id;
};

struct CaptureResult {
    bool ok;
    int code;
    std::filesystem::path output_path;
    int width;
    int height;
    std::string error;
    std::optional<CaptureCoordinates> coordinates;
};

}
