#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "platform/capture/bitmap_writer.hpp"
#include "platform/result.hpp"

namespace kiseki::platform::capture {

enum class ImageFileFormat {
    bmp,
    png,
};

// The output extension selects the file format: ".png" writes PNG, ".bmp" or
// no extension writes BMP. Other extensions are rejected so that the bytes on
// disk always match the requested name.
std::optional<ImageFileFormat> image_format_for_path(const std::filesystem::path& path);
const char* image_format_name(ImageFileFormat format);

// Returns an empty string when the path has a supported image extension.
std::string image_output_path_error(const std::filesystem::path& path);

// Name of the PNG encoder used by this build: "wic", "imageio", "gdk-pixbuf",
// or "portable" (uncompressed deflate blocks, used when no platform encoder exists).
const char* png_encoder_name();

// Writes BGRA pixels (top-down rows, alpha ignored) using the format selected by the extension.
CaptureResult write_bgra_image(
    const std::filesystem::path& output_path,
    int width,
    int height,
    std::span<const std::uint8_t> bgra_pixels);

// Writes an opaque 8-bit RGB PNG.
CaptureResult write_bgra_png(
    const std::filesystem::path& output_path,
    int width,
    int height,
    std::span<const std::uint8_t> bgra_pixels);

// Self-contained PNG encoding with stored (uncompressed) deflate blocks. Exposed for tests.
std::vector<std::uint8_t> encode_png_uncompressed(int width, int height, std::span<const std::uint8_t> bgra_pixels);

}
