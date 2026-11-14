#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "platform/capture/capture_geometry.hpp"
#include "platform/capture/image_writer.hpp"
#include "platform/capture/screenshot.hpp"

namespace {

namespace capture = kiseki::platform::capture;

class TempDirectory {
public:
    TempDirectory() : path_{std::filesystem::temp_directory_path() / unique_name()} {
        std::filesystem::create_directories(path_);
    }
    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const {
        return path_;
    }

private:
    static std::string unique_name() {
        static std::atomic<int> counter{0};
        return "kiseki-image-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
               std::to_string(counter.fetch_add(1));
    }
    std::filesystem::path path_;
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::uint32_t be32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | static_cast<std::uint32_t>(bytes[offset + 3]);
}

// 3x2 BGRA test pattern with distinct channels and a zero alpha byte, as X11 captures produce.
std::vector<std::uint8_t> pattern() {
    std::vector<std::uint8_t> pixels;
    for (int index = 0; index < 6; ++index) {
        pixels.push_back(static_cast<std::uint8_t>(10 + index));  // blue
        pixels.push_back(static_cast<std::uint8_t>(100 + index)); // green
        pixels.push_back(static_cast<std::uint8_t>(200 + index)); // red
        pixels.push_back(0);
    }
    return pixels;
}

const std::vector<std::uint8_t> png_signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

void require_png_header(const std::vector<std::uint8_t>& bytes, std::uint32_t width, std::uint32_t height) {
    REQUIRE(bytes.size() > 33);
    REQUIRE(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 8) == png_signature);
    REQUIRE(std::string(bytes.begin() + 12, bytes.begin() + 16) == "IHDR");
    REQUIRE(be32(bytes, 16) == width);
    REQUIRE(be32(bytes, 20) == height);
    REQUIRE(bytes[24] == 8);  // bit depth
    REQUIRE(bytes[25] == 2);  // RGB: opaque even though capture alpha bytes are zero
}

}

TEST_CASE("screenshot format follows the output extension") {
    REQUIRE(capture::image_format_for_path("shot.png") == capture::ImageFileFormat::png);
    REQUIRE(capture::image_format_for_path("dir/shot.PNG") == capture::ImageFileFormat::png);
    REQUIRE(capture::image_format_for_path("shot.bmp") == capture::ImageFileFormat::bmp);
    REQUIRE(capture::image_format_for_path("shot") == capture::ImageFileFormat::bmp);
    REQUIRE_FALSE(capture::image_format_for_path("shot.jpg").has_value());
    REQUIRE(capture::image_output_path_error("shot.png").empty());
    REQUIRE(capture::image_output_path_error("shot.jpeg").find(".jpeg") != std::string::npos);
}

TEST_CASE("image writer writes BMP and PNG bytes that match the extension") {
    const TempDirectory temp;
    const auto pixels = pattern();

    const auto bmp = capture::write_bgra_image(temp.path() / "shot.bmp", 3, 2, pixels);
    REQUIRE(bmp.ok);
    const auto bmp_bytes = read_file(temp.path() / "shot.bmp");
    REQUIRE(bmp_bytes.size() == 54U + pixels.size());
    REQUIRE(bmp_bytes[0] == 'B');
    REQUIRE(bmp_bytes[1] == 'M');

    const auto png_path = temp.path() / "nested" / "shot.png";
    const auto png = capture::write_bgra_image(png_path, 3, 2, pixels);
    INFO(png.error);
    REQUIRE(png.ok);
    REQUIRE(png.width == 3);
    REQUIRE(png.height == 2);
    require_png_header(read_file(png_path), 3, 2);

    const auto rejected = capture::write_bgra_image(temp.path() / "shot.jpg", 3, 2, pixels);
    REQUIRE_FALSE(rejected.ok);
    REQUIRE_FALSE(std::filesystem::exists(temp.path() / "shot.jpg"));
}

TEST_CASE("unsupported screenshot extensions fail before capture") {
    const TempDirectory temp;
    const auto output = temp.path() / "desktop.gif";
    const auto result = capture::capture_desktop_bmp(output);
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("unsupported screenshot file extension") != std::string::npos);
    REQUIRE_FALSE(std::filesystem::exists(output));
}

TEST_CASE("portable PNG encoder stores exact RGB rows") {
    const auto pixels = pattern();
    const auto png = capture::encode_png_uncompressed(3, 2, pixels);
    require_png_header(png, 3, 2);

    // IHDR is 25 bytes from offset 8; the IDAT chunk follows.
    const std::size_t idat = 8 + 25;
    const auto length = be32(png, idat);
    REQUIRE(std::string(png.begin() + idat + 4, png.begin() + idat + 8) == "IDAT");
    const std::vector<std::uint8_t> zlib(png.begin() + idat + 8, png.begin() + idat + 8 + length);
    REQUIRE(zlib[0] == 0x78);
    REQUIRE(zlib[2] == 0x01); // single final stored block
    const std::size_t stored = zlib[3] | (zlib[4] << 8);
    REQUIRE(stored == 2 * (1 + 3 * 3));
    const std::vector<std::uint8_t> raw(zlib.begin() + 7, zlib.begin() + 7 + static_cast<std::ptrdiff_t>(stored));
    const std::vector<std::uint8_t> expected{
        0, 200, 100, 10, 201, 101, 11, 202, 102, 12,
        0, 203, 103, 13, 204, 104, 14, 205, 105, 15,
    };
    REQUIRE(raw == expected);
    REQUIRE(std::string(png.end() - 8, png.end() - 4) == "IEND");
}

TEST_CASE("window capture geometry crops to the visible frame and maps pixels to input coordinates") {
    using capture::geometry::Rect;
    // GetWindowRect includes 5px invisible borders left/right/bottom (as measured: 1148x591 vs 1138x586).
    const Rect window{52, 52, 1200, 643};
    const Rect frame{57, 52, 1195, 638};
    const auto visible = capture::geometry::intersect(frame, window);
    REQUIRE(visible.has_value());
    REQUIRE(visible->width() == 1138);
    REQUIRE(visible->height() == 586);

    const auto identity = capture::geometry::map_capture_to_input_space(window, window, frame);
    REQUIRE(identity.origin_x == 57.0);
    REQUIRE(identity.origin_y == 52.0);
    REQUIRE(identity.width == 1138.0);
    REQUIRE(identity.height == 586.0);
    REQUIRE(identity.pixels_per_unit_x == 1.0);
    REQUIRE(identity.pixels_per_unit_y == 1.0);

    // A DPI-virtualized process sees the same window at 2/3 scale (150% display).
    const Rect physical{300, 150, 1500, 1050};
    const Rect logical{200, 100, 1000, 700};
    const Rect captured{306, 150, 1494, 1044};
    const auto scaled = capture::geometry::map_capture_to_input_space(physical, logical, captured);
    REQUIRE(scaled.origin_x == Catch::Approx(204.0));
    REQUIRE(scaled.origin_y == Catch::Approx(100.0));
    REQUIRE(scaled.width == Catch::Approx(792.0));
    REQUIRE(scaled.pixels_per_unit_x == Catch::Approx(1.5));
    REQUIRE(scaled.pixels_per_unit_y == Catch::Approx(1.5));
    // The last captured pixel column maps inside the window's logical bounds.
    REQUIRE(scaled.origin_x + (captured.width() - 1) / scaled.pixels_per_unit_x < 1000.0);

    std::vector<std::uint8_t> pixels(4 * 3 * 4);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        pixels[index] = static_cast<std::uint8_t>(index);
    }
    const auto cropped = capture::geometry::crop_bgra(pixels, 4, 3, Rect{1, 1, 3, 3});
    REQUIRE(cropped.size() == 2 * 2 * 4);
    REQUIRE(cropped[0] == 20); // row 1, column 1
    REQUIRE(cropped[8] == 36); // row 2, column 1
    REQUIRE(capture::geometry::crop_bgra(pixels, 4, 3, Rect{3, 0, 5, 1}).empty());
}
