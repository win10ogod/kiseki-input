#pragma once

#include <filesystem>

#include "platform/result.hpp"

namespace kiseki::platform::capture {

bool wayland_portal_capture_available();
CaptureResult capture_desktop_bmp_wayland_portal(const std::filesystem::path& output_path);

}
