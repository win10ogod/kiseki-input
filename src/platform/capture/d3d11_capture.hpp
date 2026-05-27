#pragma once

#include <filesystem>

#include "platform/result.hpp"

#ifdef _WIN32
namespace kiseki::platform::capture {

bool d3d11_desktop_capture_available();
CaptureResult capture_desktop_bmp_d3d11(const std::filesystem::path& output_path);

}
#endif
