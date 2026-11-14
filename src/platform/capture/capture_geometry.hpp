#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Platform-independent rectangle math for window captures (unit-tested on every platform).
namespace kiseki::platform::capture::geometry {

// Half-open rectangle [left, right) x [top, bottom).
struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width() const {
        return right - left;
    }
    int height() const {
        return bottom - top;
    }
    bool empty() const {
        return width() <= 0 || height() <= 0;
    }
};

inline std::optional<Rect> intersect(const Rect& a, const Rect& b) {
    const Rect result{
        (std::max)(a.left, b.left),
        (std::max)(a.top, b.top),
        (std::min)(a.right, b.right),
        (std::min)(a.bottom, b.bottom),
    };
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

// Copies `crop` (image-relative, must lie inside the image) out of top-down BGRA pixels.
inline std::vector<std::uint8_t> crop_bgra(std::span<const std::uint8_t> pixels, int width, int height, const Rect& crop) {
    if (crop.empty() || crop.left < 0 || crop.top < 0 || crop.right > width || crop.bottom > height ||
        pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
        return {};
    }
    const auto row_bytes = static_cast<std::size_t>(crop.width()) * 4U;
    std::vector<std::uint8_t> result(row_bytes * static_cast<std::size_t>(crop.height()));
    for (int y = 0; y < crop.height(); ++y) {
        const auto source = (static_cast<std::size_t>(crop.top + y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(crop.left)) * 4U;
        std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(source), row_bytes,
                    result.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * row_bytes));
    }
    return result;
}

struct ScreenMapping {
    double origin_x = 0;
    double origin_y = 0;
    double width = 0;
    double height = 0;
    double pixels_per_unit_x = 1;
    double pixels_per_unit_y = 1;
};

// `window_pixels` and `captured` are in the capture's pixel space; `window_input`
// is the same window in the coordinate space accepted by absolute input (they are
// equal unless the process is DPI-virtualized). Returns the transform for
// screen = origin + pixel / pixelsPerUnit over the captured rectangle.
inline ScreenMapping map_capture_to_input_space(const Rect& window_pixels, const Rect& window_input, const Rect& captured) {
    const double scale_x = window_pixels.width() > 0 && window_input.width() > 0
        ? static_cast<double>(window_input.width()) / static_cast<double>(window_pixels.width())
        : 1.0;
    const double scale_y = window_pixels.height() > 0 && window_input.height() > 0
        ? static_cast<double>(window_input.height()) / static_cast<double>(window_pixels.height())
        : 1.0;
    return ScreenMapping{
        .origin_x = window_input.left + (captured.left - window_pixels.left) * scale_x,
        .origin_y = window_input.top + (captured.top - window_pixels.top) * scale_y,
        .width = captured.width() * scale_x,
        .height = captured.height() * scale_y,
        .pixels_per_unit_x = 1.0 / scale_x,
        .pixels_per_unit_y = 1.0 / scale_y,
    };
}

}
