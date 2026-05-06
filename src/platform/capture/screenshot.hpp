#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/result.hpp"

namespace kiseki::platform::capture {

struct BurstOptions {
    std::filesystem::path output_directory;
    std::string prefix;
    std::uint32_t frames;
    std::uint32_t fps;
};

bool desktop_capture_available();
CaptureResult capture_desktop_bmp(const std::filesystem::path& output_path);
OperationResult capture_burst_bmp(const BurstOptions& options);

}
