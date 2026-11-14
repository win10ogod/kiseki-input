#include "platform/capture/image_writer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#elif defined(KISEKI_HAS_GDK_PIXBUF)
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

namespace kiseki::platform::capture {

namespace {

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

CaptureResult succeed(const std::filesystem::path& output_path, int width, int height) {
    return CaptureResult{
        .ok = true,
        .code = 0,
        .output_path = output_path,
        .width = width,
        .height = height,
        .error = "",
    };
}

std::string lower_extension(const std::filesystem::path& path) {
    const auto extension = path.extension().u8string();
    std::string result{extension.begin(), extension.end()};
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string validate_pixels(int width, int height, std::span<const std::uint8_t> bgra_pixels) {
    if (width <= 0 || height <= 0) {
        return "invalid image dimensions";
    }
    const auto expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    if (bgra_pixels.size() != expected) {
        return "image pixel buffer size does not match dimensions";
    }
    return {};
}

std::string create_parent_directory(const std::filesystem::path& output_path) {
    const auto parent = output_path.parent_path();
    if (parent.empty()) {
        return {};
    }
    std::error_code error_code;
    std::filesystem::create_directories(parent, error_code);
    return error_code ? "failed to create output directory: " + error_code.message() : std::string{};
}

void remove_partial_file(const std::filesystem::path& output_path) {
    std::error_code ignored;
    std::filesystem::remove(output_path, ignored);
}

std::vector<std::uint8_t> bgra_to_rgb(int width, int height, std::span<const std::uint8_t> bgra, bool bgr_order) {
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> rgb(count * 3U);
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t blue = bgra[index * 4U];
        const std::uint8_t green = bgra[index * 4U + 1U];
        const std::uint8_t red = bgra[index * 4U + 2U];
        rgb[index * 3U] = bgr_order ? blue : red;
        rgb[index * 3U + 1U] = green;
        rgb[index * 3U + 2U] = bgr_order ? red : blue;
    }
    return rgb;
}

const std::array<std::uint32_t, 256>& crc_table() {
    static const auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0 ? 0xedb88320U ^ (c >> 1) : c >> 1;
            }
            values[n] = c;
        }
        return values;
    }();
    return table;
}

std::uint32_t crc32_update(std::uint32_t crc, const std::uint8_t* data, std::size_t size) {
    const auto& table = crc_table();
    for (std::size_t index = 0; index < size; ++index) {
        crc = table[(crc ^ data[index]) & 0xffU] ^ (crc >> 8);
    }
    return crc;
}

void append_u32_be(std::vector<std::uint8_t>& output, std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void append_chunk(std::vector<std::uint8_t>& output, const char type[4], const std::uint8_t* data, std::size_t size) {
    append_u32_be(output, static_cast<std::uint32_t>(size));
    const auto type_offset = output.size();
    output.insert(output.end(), type, type + 4);
    if (size > 0) {
        output.insert(output.end(), data, data + size);
    }
    const std::uint32_t crc = crc32_update(0xffffffffU, output.data() + type_offset, size + 4U) ^ 0xffffffffU;
    append_u32_be(output, crc);
}

#if !defined(_WIN32) && !defined(__APPLE__)
std::optional<std::string> write_bytes(const std::filesystem::path& output_path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file{output_path, std::ios::binary};
    if (!file) {
        return "failed to open output file";
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const bool write_ok = static_cast<bool>(file);
    file.close();
    if (!write_ok || !file) {
        return "failed to write output file";
    }
    return std::nullopt;
}
#endif

#ifdef _WIN32
template <typename T>
struct ComPtr {
    T* value = nullptr;
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() {
        if (value != nullptr) {
            value->Release();
        }
    }
    T** put() {
        return &value;
    }
    T* operator->() const {
        return value;
    }
};

std::string hresult_text(const char* step, HRESULT result) {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "0x%08lx", static_cast<unsigned long>(result));
    return std::string{"WIC PNG encoding failed at "} + step + " (" + buffer + ")";
}

std::optional<std::string> encode_png_wic(const std::filesystem::path& output_path, int width, int height, std::span<const std::uint8_t> bgra) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE: this thread already joined an STA, which WIC also supports.
    const bool uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        return hresult_text("CoInitializeEx", initialized);
    }

    std::optional<std::string> error;
    {
        const auto bgr = bgra_to_rgb(width, height, bgra, true);
        const UINT stride = static_cast<UINT>(width) * 3U;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        ComPtr<IPropertyBag2> properties;
        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()));
        if (FAILED(result)) {
            error = hresult_text("CoCreateInstance", result);
        }
        if (!error && FAILED(result = factory->CreateStream(stream.put()))) {
            error = hresult_text("CreateStream", result);
        }
        if (!error && FAILED(result = stream->InitializeFromFilename(output_path.c_str(), GENERIC_WRITE))) {
            error = hresult_text("open output file", result);
        }
        if (!error && FAILED(result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()))) {
            error = hresult_text("CreateEncoder", result);
        }
        if (!error && FAILED(result = encoder->Initialize(stream.value, WICBitmapEncoderNoCache))) {
            error = hresult_text("encoder Initialize", result);
        }
        if (!error && FAILED(result = encoder->CreateNewFrame(frame.put(), properties.put()))) {
            error = hresult_text("CreateNewFrame", result);
        }
        if (!error && FAILED(result = frame->Initialize(properties.value))) {
            error = hresult_text("frame Initialize", result);
        }
        if (!error && FAILED(result = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height)))) {
            error = hresult_text("SetSize", result);
        }
        WICPixelFormatGUID pixel_format = GUID_WICPixelFormat24bppBGR;
        if (!error && FAILED(result = frame->SetPixelFormat(&pixel_format))) {
            error = hresult_text("SetPixelFormat", result);
        }
        if (!error && !IsEqualGUID(pixel_format, GUID_WICPixelFormat24bppBGR)) {
            error = std::string{"WIC PNG encoder does not accept 24-bit BGR pixels"};
        }
        if (!error && FAILED(result = frame->WritePixels(static_cast<UINT>(height), stride, static_cast<UINT>(bgr.size()),
                                                         const_cast<BYTE*>(bgr.data())))) {
            error = hresult_text("WritePixels", result);
        }
        if (!error && FAILED(result = frame->Commit())) {
            error = hresult_text("frame Commit", result);
        }
        if (!error && FAILED(result = encoder->Commit())) {
            error = hresult_text("encoder Commit", result);
        }
    }

    if (uninitialize) {
        CoUninitialize();
    }
    return error;
}
#elif defined(__APPLE__)
std::optional<std::string> encode_png_imageio(const std::filesystem::path& output_path, int width, int height, std::span<const std::uint8_t> bgra) {
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, bgra.data(), static_cast<CFIndex>(bgra.size()));
    if (data == nullptr) {
        return "CFDataCreate failed";
    }
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CFRelease(data);
    if (provider == nullptr) {
        return "CGDataProviderCreateWithCFData failed";
    }
    // The capture backend produced these bytes in a device RGB bitmap context;
    // keep the same color space so the PNG stores the same byte values as BMP.
    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    CGImageRef image = color_space == nullptr ? nullptr : CGImageCreate(
        static_cast<std::size_t>(width),
        static_cast<std::size_t>(height),
        8,
        32,
        static_cast<std::size_t>(width) * 4U,
        color_space,
        static_cast<CGBitmapInfo>(kCGImageAlphaNoneSkipFirst) | kCGBitmapByteOrder32Little,
        provider,
        nullptr,
        false,
        kCGRenderingIntentDefault);
    if (color_space != nullptr) {
        CGColorSpaceRelease(color_space);
    }
    CGDataProviderRelease(provider);
    if (image == nullptr) {
        return "CGImageCreate failed";
    }

    const std::string native = output_path.string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(native.c_str()), static_cast<CFIndex>(native.size()), false);
    if (url == nullptr) {
        CGImageRelease(image);
        return "failed to create output file URL";
    }
    CGImageDestinationRef destination = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
    CFRelease(url);
    if (destination == nullptr) {
        CGImageRelease(image);
        return "ImageIO could not create a PNG destination";
    }
    CGImageDestinationAddImage(destination, image, nullptr);
    const bool finalized = CGImageDestinationFinalize(destination);
    CFRelease(destination);
    CGImageRelease(image);
    if (!finalized) {
        return "ImageIO PNG encoding failed";
    }
    return std::nullopt;
}
#elif defined(KISEKI_HAS_GDK_PIXBUF)
std::optional<std::string> encode_png_gdk_pixbuf(const std::filesystem::path& output_path, int width, int height, std::span<const std::uint8_t> bgra) {
    auto rgb = bgra_to_rgb(width, height, bgra, false);
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(rgb.data(), GDK_COLORSPACE_RGB, FALSE, 8, width, height, width * 3, nullptr, nullptr);
    if (pixbuf == nullptr) {
        return "gdk_pixbuf_new_from_data failed";
    }
    GError* error = nullptr;
    const gboolean saved = gdk_pixbuf_save(pixbuf, output_path.c_str(), "png", &error, nullptr);
    g_object_unref(pixbuf);
    if (!saved) {
        std::string message = "gdk-pixbuf PNG encoding failed";
        if (error != nullptr) {
            if (error->message != nullptr) {
                message += ": ";
                message += error->message;
            }
            g_error_free(error);
        }
        return message;
    }
    return std::nullopt;
}
#endif

}

std::optional<ImageFileFormat> image_format_for_path(const std::filesystem::path& path) {
    const auto extension = lower_extension(path);
    if (extension.empty() || extension == ".bmp") {
        return ImageFileFormat::bmp;
    }
    if (extension == ".png") {
        return ImageFileFormat::png;
    }
    return std::nullopt;
}

const char* image_format_name(ImageFileFormat format) {
    return format == ImageFileFormat::png ? "png" : "bmp";
}

std::string image_output_path_error(const std::filesystem::path& path) {
    if (image_format_for_path(path)) {
        return {};
    }
    return "unsupported screenshot file extension '" + lower_extension(path) + "'; use .png or .bmp";
}

const char* png_encoder_name() {
#ifdef _WIN32
    return "wic";
#elif defined(__APPLE__)
    return "imageio";
#elif defined(KISEKI_HAS_GDK_PIXBUF)
    return "gdk-pixbuf";
#else
    return "portable";
#endif
}

std::vector<std::uint8_t> encode_png_uncompressed(int width, int height, std::span<const std::uint8_t> bgra_pixels) {
    if (!validate_pixels(width, height, bgra_pixels).empty()) {
        return {};
    }

    const auto rgb = bgra_to_rgb(width, height, bgra_pixels, false);
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 3U;
    std::vector<std::uint8_t> raw;
    raw.reserve((row_bytes + 1U) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0); // filter type None
        const auto* row = rgb.data() + static_cast<std::size_t>(y) * row_bytes;
        raw.insert(raw.end(), row, row + row_bytes);
    }

    constexpr std::size_t max_stored_block = 65535;
    std::vector<std::uint8_t> zlib;
    zlib.reserve(raw.size() + (raw.size() / max_stored_block + 1U) * 5U + 6U);
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    std::size_t offset = 0;
    do {
        const std::size_t length = std::min(max_stored_block, raw.size() - offset);
        const bool final_block = offset + length == raw.size();
        zlib.push_back(final_block ? 0x01 : 0x00);
        zlib.push_back(static_cast<std::uint8_t>(length & 0xffU));
        zlib.push_back(static_cast<std::uint8_t>((length >> 8) & 0xffU));
        zlib.push_back(static_cast<std::uint8_t>(~length & 0xffU));
        zlib.push_back(static_cast<std::uint8_t>((~length >> 8) & 0xffU));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
    } while (offset < raw.size());

    std::uint32_t adler_a = 1;
    std::uint32_t adler_b = 0;
    for (const std::uint8_t byte : raw) {
        adler_a = (adler_a + byte) % 65521U;
        adler_b = (adler_b + adler_a) % 65521U;
    }
    append_u32_be(zlib, (adler_b << 16) | adler_a);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<std::uint8_t> header;
    append_u32_be(header, static_cast<std::uint32_t>(width));
    append_u32_be(header, static_cast<std::uint32_t>(height));
    header.insert(header.end(), {8, 2, 0, 0, 0}); // 8-bit RGB, deflate, adaptive filtering, no interlace
    append_chunk(png, "IHDR", header.data(), header.size());

    constexpr std::size_t max_idat = std::size_t{1} << 24;
    for (std::size_t chunk = 0; chunk < zlib.size(); chunk += max_idat) {
        append_chunk(png, "IDAT", zlib.data() + chunk, std::min(max_idat, zlib.size() - chunk));
    }
    append_chunk(png, "IEND", nullptr, 0);
    return png;
}

CaptureResult write_bgra_png(
    const std::filesystem::path& output_path,
    int width,
    int height,
    std::span<const std::uint8_t> bgra_pixels) {
    if (const auto error = validate_pixels(width, height, bgra_pixels); !error.empty()) {
        return fail(output_path, error);
    }
    if (const auto error = create_parent_directory(output_path); !error.empty()) {
        return fail(output_path, error);
    }

    std::optional<std::string> error;
#ifdef _WIN32
    error = encode_png_wic(output_path, width, height, bgra_pixels);
#elif defined(__APPLE__)
    error = encode_png_imageio(output_path, width, height, bgra_pixels);
#else
#ifdef KISEKI_HAS_GDK_PIXBUF
    // A missing gdk-pixbuf PNG saver falls back to the portable encoder below.
    if (!encode_png_gdk_pixbuf(output_path, width, height, bgra_pixels)) {
        return succeed(output_path, width, height);
    }
#endif
    error = write_bytes(output_path, encode_png_uncompressed(width, height, bgra_pixels));
#endif
    if (error) {
        remove_partial_file(output_path);
        return fail(output_path, *error);
    }
    return succeed(output_path, width, height);
}

CaptureResult write_bgra_image(
    const std::filesystem::path& output_path,
    int width,
    int height,
    std::span<const std::uint8_t> bgra_pixels) {
    const auto format = image_format_for_path(output_path);
    if (!format) {
        return fail(output_path, image_output_path_error(output_path));
    }
    if (*format == ImageFileFormat::png) {
        return write_bgra_png(output_path, width, height, bgra_pixels);
    }
    return write_bgra_bmp(output_path, width, height, bgra_pixels);
}

}
