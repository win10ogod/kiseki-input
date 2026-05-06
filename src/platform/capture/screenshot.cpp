#include "platform/capture/screenshot.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "platform/capture/bitmap_writer.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#ifdef KISEKI_HAS_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdlib>
#endif
#endif

namespace kiseki::platform::capture {

namespace {

OperationResult fail_operation(std::string error) {
    return OperationResult{
        .ok = false,
        .code = 2,
        .message = "",
        .error = std::move(error),
    };
}

CaptureResult fail_capture(const std::filesystem::path& path, std::string error) {
    return CaptureResult{
        .ok = false,
        .code = 2,
        .output_path = path,
        .width = 0,
        .height = 0,
        .error = std::move(error),
    };
}

std::filesystem::path frame_path(const BurstOptions& options, std::uint32_t index) {
    std::ostringstream name;
    name << options.prefix << '_' << std::setw(4) << std::setfill('0') << index << ".bmp";
    return options.output_directory / name.str();
}

#ifndef _WIN32
#ifdef KISEKI_HAS_X11
std::uint8_t component_from_mask(unsigned long pixel, unsigned long mask) {
    if (mask == 0) {
        return 0;
    }

    unsigned int shift = 0;
    while (((mask >> shift) & 1UL) == 0) {
        ++shift;
    }

    unsigned long max_value = mask >> shift;
    const unsigned long value = (pixel & mask) >> shift;
    return static_cast<std::uint8_t>((value * 255UL) / max_value);
}

int last_x11_error_code = 0;

int capture_x11_error(Display*, XErrorEvent* event) {
    last_x11_error_code = event->error_code;
    return 0;
}

bool x11_get_image_supported(Display* display, Window root) {
    last_x11_error_code = 0;
    auto* old_handler = XSetErrorHandler(capture_x11_error);
    XImage* image = XGetImage(display, root, 0, 0, 1, 1, AllPlanes, ZPixmap);
    XSync(display, False);
    XSetErrorHandler(old_handler);
    if (image != nullptr) {
        XDestroyImage(image);
    }
    return last_x11_error_code == 0 && image != nullptr;
}
#endif
#endif

}

bool desktop_capture_available() {
#ifdef _WIN32
    return true;
#else
#ifdef KISEKI_HAS_X11
    const char* display = std::getenv("DISPLAY");
    if (display == nullptr || display[0] == '\0') {
        return false;
    }
    Display* xdisplay = XOpenDisplay(nullptr);
    if (xdisplay == nullptr) {
        return false;
    }
    const Window root = RootWindow(xdisplay, DefaultScreen(xdisplay));
    const bool supported = x11_get_image_supported(xdisplay, root);
    XCloseDisplay(xdisplay);
    return supported;
#else
    return false;
#endif
#endif
}

CaptureResult capture_desktop_bmp(const std::filesystem::path& output_path) {
#ifdef _WIN32
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        return fail_capture(output_path, "failed to read virtual screen dimensions");
    }

    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        return fail_capture(output_path, "GetDC failed");
    }

    HDC memory_dc = CreateCompatibleDC(screen_dc);
    if (memory_dc == nullptr) {
        ReleaseDC(nullptr, screen_dc);
        return fail_capture(output_path, "CreateCompatibleDC failed");
    }

    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, width, height);
    if (bitmap == nullptr) {
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        return fail_capture(output_path, "CreateCompatibleBitmap failed");
    }

    const HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
    const BOOL copied = BitBlt(memory_dc, 0, 0, width, height, screen_dc, left, top, SRCCOPY | CAPTUREBLT);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const int lines = copied ? GetDIBits(memory_dc, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS) : 0;

    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);

    if (!copied) {
        return fail_capture(output_path, "BitBlt failed");
    }
    if (lines != height) {
        return fail_capture(output_path, "GetDIBits failed");
    }

    return write_bgra_bmp(output_path, width, height, pixels);
#else
#ifdef KISEKI_HAS_X11
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return fail_capture(output_path, "XOpenDisplay failed; DISPLAY is not available");
    }

    const int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display, root, &attributes) == 0) {
        XCloseDisplay(display);
        return fail_capture(output_path, "XGetWindowAttributes failed");
    }

    last_x11_error_code = 0;
    auto* old_handler = XSetErrorHandler(capture_x11_error);
    XImage* image = XGetImage(
        display,
        root,
        0,
        0,
        static_cast<unsigned int>(attributes.width),
        static_cast<unsigned int>(attributes.height),
        AllPlanes,
        ZPixmap);
    XSync(display, False);
    XSetErrorHandler(old_handler);
    if (last_x11_error_code != 0 || image == nullptr) {
        XCloseDisplay(display);
        return fail_capture(output_path, "XGetImage failed; this WSL/X11 session does not allow root screenshot capture");
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(attributes.width) * static_cast<std::size_t>(attributes.height) * 4U);
    for (int y = 0; y < attributes.height; ++y) {
        for (int x = 0; x < attributes.width; ++x) {
            const unsigned long pixel = XGetPixel(image, x, y);
            const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(attributes.width) + static_cast<std::size_t>(x)) * 4U;
            pixels[offset + 0] = component_from_mask(pixel, image->blue_mask);
            pixels[offset + 1] = component_from_mask(pixel, image->green_mask);
            pixels[offset + 2] = component_from_mask(pixel, image->red_mask);
            pixels[offset + 3] = 0;
        }
    }

    XDestroyImage(image);
    XCloseDisplay(display);

    return write_bgra_bmp(output_path, attributes.width, attributes.height, pixels);
#else
    return fail_capture(output_path, "X11 desktop capture support was not compiled in");
#endif
#endif
}

OperationResult capture_burst_bmp(const BurstOptions& options) {
    if (options.frames == 0 || options.fps == 0) {
        return fail_operation("burst frames and fps must be greater than zero");
    }

    const auto frame_interval = std::chrono::microseconds{1'000'000 / options.fps};
    std::filesystem::path last_path;
    for (std::uint32_t index = 0; index < options.frames; ++index) {
        const auto start = std::chrono::steady_clock::now();
        last_path = frame_path(options, index);
        const auto result = capture_desktop_bmp(last_path);
        if (!result.ok) {
            return fail_operation(result.error);
        }

        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (index + 1 < options.frames && elapsed < frame_interval) {
            std::this_thread::sleep_for(frame_interval - elapsed);
        }
    }

    return OperationResult{
        .ok = true,
        .code = 0,
        .message = "captured " + std::to_string(options.frames) + " frames to " + options.output_directory.string(),
        .error = "",
    };
}

}
