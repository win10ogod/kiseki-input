#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

#include "platform/result.hpp"

namespace kiseki::platform::capture {

CaptureResult write_bgra_bmp(
    const std::filesystem::path& output_path,
    int width,
    int height,
    std::span<const std::uint8_t> bgra_pixels);

}
