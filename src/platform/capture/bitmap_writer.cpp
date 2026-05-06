#include "platform/capture/bitmap_writer.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>

namespace kiseki::platform::capture {

namespace {

void write_u16(std::ofstream& file, std::uint16_t value) {
    file.put(static_cast<char>(value & 0xff));
    file.put(static_cast<char>((value >> 8) & 0xff));
}

void write_u32(std::ofstream& file, std::uint32_t value) {
    file.put(static_cast<char>(value & 0xff));
    file.put(static_cast<char>((value >> 8) & 0xff));
    file.put(static_cast<char>((value >> 16) & 0xff));
    file.put(static_cast<char>((value >> 24) & 0xff));
}

void write_i32(std::ofstream& file, std::int32_t value) {
    write_u32(file, static_cast<std::uint32_t>(value));
}

CaptureResult fail(const std::filesystem::path& output_path, std::string error) {
    return CaptureResult{
        .ok = false,
        .code = 2,
        .output_path = output_path,
        .width = 0,
        .height = 0,
        .error = std::move(error),
    };
}

}

CaptureResult write_bgra_bmp(
    const std::filesystem::path& output_path,
    int width,
    int height,
    std::span<const std::uint8_t> bgra_pixels) {
    if (width <= 0 || height <= 0) {
        return fail(output_path, "invalid bitmap dimensions");
    }

    const auto expected_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    if (bgra_pixels.size() != expected_size) {
        return fail(output_path, "bitmap pixel buffer size does not match dimensions");
    }

    std::error_code error_code;
    const auto parent = output_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error_code);
        if (error_code) {
            return fail(output_path, "failed to create output directory: " + error_code.message());
        }
    }

    std::ofstream file{output_path, std::ios::binary};
    if (!file) {
        return fail(output_path, "failed to open output file");
    }

    const std::uint32_t pixel_bytes = static_cast<std::uint32_t>(expected_size);
    const std::uint32_t file_header_size = 14;
    const std::uint32_t dib_header_size = 40;
    const std::uint32_t pixel_offset = file_header_size + dib_header_size;

    file.put('B');
    file.put('M');
    write_u32(file, pixel_offset + pixel_bytes);
    write_u16(file, 0);
    write_u16(file, 0);
    write_u32(file, pixel_offset);

    write_u32(file, dib_header_size);
    write_i32(file, width);
    write_i32(file, -height);
    write_u16(file, 1);
    write_u16(file, 32);
    write_u32(file, 0);
    write_u32(file, pixel_bytes);
    write_i32(file, 2835);
    write_i32(file, 2835);
    write_u32(file, 0);
    write_u32(file, 0);

    file.write(reinterpret_cast<const char*>(bgra_pixels.data()), static_cast<std::streamsize>(bgra_pixels.size()));
    const bool write_ok = static_cast<bool>(file);
    file.close();
    if (!write_ok || !file) {
        return fail(output_path, "failed to write output file");
    }

    return CaptureResult{
        .ok = true,
        .code = 0,
        .output_path = output_path,
        .width = width,
        .height = height,
        .error = "",
    };
}

}
