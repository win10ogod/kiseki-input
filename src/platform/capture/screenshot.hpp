#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/result.hpp"
#include "platform/target/target.hpp"

namespace kiseki::platform::capture {

struct BurstOptions {
    std::filesystem::path output_directory;
    std::string prefix;
    std::uint32_t frames;
    std::uint32_t fps;
};

struct WindowBurstOptions {
    kiseki::platform::target::TargetQuery target;
    std::filesystem::path output_directory;
    std::string prefix;
    std::uint32_t frames;
    std::uint32_t fps;
};

bool desktop_capture_available();
bool window_capture_available();
// Output format follows the extension (.png or .bmp; see image_writer.hpp).
CaptureResult capture_desktop_bmp(const std::filesystem::path& output_path);
// Captures only the named X11 display (for example an isolated Xvfb ":99"). Unlike
// capture_desktop_bmp it never falls back to $DISPLAY or the Wayland portal.
CaptureResult capture_x11_display_image(const std::string& display_name, const std::filesystem::path& output_path);
CaptureResult capture_window_bmp(const kiseki::platform::target::TargetQuery& target, const std::filesystem::path& output_path);
CaptureResult capture_background_window_bmp(const kiseki::platform::target::TargetQuery& target, const std::filesystem::path& output_path);
OperationResult capture_burst_bmp(const BurstOptions& options);
OperationResult capture_window_burst_bmp(const WindowBurstOptions& options);

}
