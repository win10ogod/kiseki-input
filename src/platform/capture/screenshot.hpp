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
CaptureResult capture_desktop_bmp(const std::filesystem::path& output_path);
CaptureResult capture_window_bmp(const kiseki::platform::target::TargetQuery& target, const std::filesystem::path& output_path);
CaptureResult capture_background_window_bmp(const kiseki::platform::target::TargetQuery& target, const std::filesystem::path& output_path);
OperationResult capture_burst_bmp(const BurstOptions& options);
OperationResult capture_window_burst_bmp(const WindowBurstOptions& options);

}
