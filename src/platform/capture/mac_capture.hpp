#pragma once

#include <cstdint>
#include <filesystem>

#include "platform/result.hpp"

namespace kiseki::platform::capture {

CaptureResult capture_desktop_bmp_screencapturekit(const std::filesystem::path& output_path);
CaptureResult capture_window_bmp_screencapturekit(std::uint32_t window_id, const std::filesystem::path& output_path);

}
