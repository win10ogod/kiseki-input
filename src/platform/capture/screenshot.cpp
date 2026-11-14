#include "platform/capture/screenshot.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "platform/capture/capture_geometry.hpp"
#include "platform/capture/image_writer.hpp"

#ifdef _WIN32
#include "platform/capture/d3d11_capture.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#elif defined(__APPLE__)
#include "platform/capture/mac_capture.hpp"

#include <ApplicationServices/ApplicationServices.h>
#else
#ifdef KISEKI_HAS_XDG_DESKTOP_PORTAL
#include "platform/capture/wayland_portal_capture.hpp"
#endif
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

#ifdef _WIN32
std::optional<HWND> hwnd_from_id(const std::string& id) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(id, &consumed, 0);
        if (consumed != id.size()) {
            return std::nullopt;
        }
        return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value));
    } catch (...) {
        return std::nullopt;
    }
}

geometry::Rect to_rect(const RECT& rect) {
    return geometry::Rect{rect.left, rect.top, rect.right, rect.bottom};
}

// Makes this thread per-monitor DPI aware for the lifetime of the object, so
// window rectangles, DWM frame bounds and PrintWindow output all use physical
// pixels. The process's own (input) coordinate space is restored afterwards.
class ScopedPhysicalPixels {
public:
    ScopedPhysicalPixels() {
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            set_context_ = reinterpret_cast<SetContext>(
                reinterpret_cast<void*>(GetProcAddress(user32, "SetThreadDpiAwarenessContext")));
        }
        if (set_context_ != nullptr) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (-4), then PER_MONITOR_AWARE (-3).
            previous_ = set_context_(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-4)));
            if (previous_ == nullptr) {
                previous_ = set_context_(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-3)));
            }
        }
        active_ = previous_ != nullptr;
    }
    ScopedPhysicalPixels(const ScopedPhysicalPixels&) = delete;
    ScopedPhysicalPixels& operator=(const ScopedPhysicalPixels&) = delete;
    ~ScopedPhysicalPixels() {
        restore();
    }

    void restore() {
        if (previous_ != nullptr) {
            set_context_(previous_);
            previous_ = nullptr;
        }
    }

    bool active() const {
        return active_;
    }

private:
    using SetContext = HANDLE(WINAPI*)(HANDLE);
    SetContext set_context_ = nullptr;
    HANDLE previous_ = nullptr;
    bool active_ = false;
};

struct WindowPixels {
    std::vector<std::uint8_t> bgra;
    std::string error;
};

WindowPixels print_window(HWND hwnd, int width, int height) {
    HDC window_dc = GetWindowDC(hwnd);
    if (window_dc == nullptr) {
        return {{}, "GetWindowDC failed"};
    }

    HDC memory_dc = CreateCompatibleDC(window_dc);
    if (memory_dc == nullptr) {
        ReleaseDC(hwnd, window_dc);
        return {{}, "CreateCompatibleDC failed"};
    }

    HBITMAP bitmap = CreateCompatibleBitmap(window_dc, width, height);
    if (bitmap == nullptr) {
        DeleteDC(memory_dc);
        ReleaseDC(hwnd, window_dc);
        return {{}, "CreateCompatibleBitmap failed"};
    }

    const HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
    const UINT render_full_content = 0x00000002;
    const BOOL printed = PrintWindow(hwnd, memory_dc, render_full_content);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const int lines = printed ? GetDIBits(memory_dc, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS) : 0;

    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(hwnd, window_dc);

    if (!printed) {
        return {{}, "PrintWindow failed; target may not support OS window capture"};
    }
    if (lines != height) {
        return {{}, "GetDIBits failed"};
    }
    return {std::move(pixels), ""};
}

// Captures the visible window frame. GetWindowRect includes the invisible resize
// borders that DWM draws outside the frame; the image is cropped to
// DWMWA_EXTENDED_FRAME_BOUNDS, the rectangle other providers report as window bounds.
CaptureResult capture_hwnd_image(HWND hwnd, const std::string& window_id, const std::filesystem::path& output_path) {
    RECT input_rect{};
    if (GetWindowRect(hwnd, &input_rect) == 0) {
        return fail_capture(output_path, "GetWindowRect failed");
    }

    ScopedPhysicalPixels physical_pixels;
    RECT window_rect{};
    if (GetWindowRect(hwnd, &window_rect) == 0) {
        return fail_capture(output_path, "GetWindowRect failed");
    }
    const auto window = to_rect(window_rect);
    if (window.empty()) {
        return fail_capture(output_path, "target window has empty dimensions");
    }

    // DWM reports physical pixels; only compare them with a physical window rectangle.
    geometry::Rect frame = window;
    std::string bounds_source = "window-rect";
    RECT dwm_rect{};
    if (physical_pixels.active() &&
        SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &dwm_rect, sizeof(dwm_rect)))) {
        if (const auto visible = geometry::intersect(to_rect(dwm_rect), window)) {
            frame = *visible;
            bounds_source = "dwm-extended-frame-bounds";
        }
    }

    std::optional<CapturePixelRect> client_area;
    RECT client_rect{};
    POINT client_origin{0, 0};
    if (GetClientRect(hwnd, &client_rect) != 0 && ClientToScreen(hwnd, &client_origin) != 0) {
        const geometry::Rect client{client_origin.x, client_origin.y, client_origin.x + client_rect.right,
                                    client_origin.y + client_rect.bottom};
        if (const auto visible = geometry::intersect(client, frame)) {
            client_area = CapturePixelRect{
                .x = visible->left - frame.left,
                .y = visible->top - frame.top,
                .width = visible->width(),
                .height = visible->height(),
            };
        }
    }

    auto captured = print_window(hwnd, window.width(), window.height());
    physical_pixels.restore();
    if (!captured.error.empty()) {
        return fail_capture(output_path, captured.error);
    }

    const geometry::Rect crop{frame.left - window.left, frame.top - window.top, frame.right - window.left,
                              frame.bottom - window.top};
    const auto pixels = geometry::crop_bgra(captured.bgra, window.width(), window.height(), crop);
    auto result = write_bgra_image(output_path, frame.width(), frame.height(), pixels);
    if (result.ok) {
        const auto mapping = geometry::map_capture_to_input_space(window, to_rect(input_rect), frame);
        result.coordinates = CaptureCoordinates{
            .space = "windows-virtual-screen",
            .origin_x = mapping.origin_x,
            .origin_y = mapping.origin_y,
            .width = mapping.width,
            .height = mapping.height,
            .pixels_per_unit_x = mapping.pixels_per_unit_x,
            .pixels_per_unit_y = mapping.pixels_per_unit_y,
            .window_id = window_id,
            .bounds_source = bounds_source,
            .client_area = client_area,
        };
    }
    return result;
}
#endif

#ifdef __APPLE__
std::optional<CGWindowID> cg_window_id_from_string(const std::string& id) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(id, &consumed, 0);
        if (consumed != id.size() || value > static_cast<unsigned long long>(std::numeric_limits<std::uint32_t>::max())) {
            return std::nullopt;
        }
        return static_cast<CGWindowID>(value);
    } catch (...) {
        return std::nullopt;
    }
}

#endif

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

// Names the display that failed. `display_name` null means $DISPLAY.
std::string x11_open_error(const char* display_name) {
    if (display_name != nullptr) {
        return std::string{"XOpenDisplay failed for X11 display "} + display_name +
               "; no X server accepted the connection";
    }
    const char* environment = std::getenv("DISPLAY");
    if (environment == nullptr || environment[0] == '\0') {
        return "XOpenDisplay failed: DISPLAY is not set";
    }
    return std::string{"XOpenDisplay failed for DISPLAY="} + environment + "; no X server accepted the connection";
}

struct X11Pixels {
    std::vector<std::uint8_t> bgra;
    int width = 0;
    int height = 0;
    std::string error;
};

X11Pixels read_x11_window_pixels(Display* display, Window window, const char* get_image_failure) {
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display, window, &attributes) == 0) {
        return {{}, 0, 0, "XGetWindowAttributes failed"};
    }

    last_x11_error_code = 0;
    auto* old_handler = XSetErrorHandler(capture_x11_error);
    XImage* image = XGetImage(
        display,
        window,
        0,
        0,
        static_cast<unsigned int>(attributes.width),
        static_cast<unsigned int>(attributes.height),
        AllPlanes,
        ZPixmap);
    XSync(display, False);
    XSetErrorHandler(old_handler);
    if (last_x11_error_code != 0 || image == nullptr) {
        if (image != nullptr) {
            XDestroyImage(image);
        }
        return {{}, 0, 0, get_image_failure};
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
    return {std::move(pixels), attributes.width, attributes.height, ""};
}

CaptureCoordinates x11_coordinates(int x, int y, int width, int height, std::optional<std::string> window_id) {
    return CaptureCoordinates{
        .space = "x11-root-pixels",
        .origin_x = static_cast<double>(x),
        .origin_y = static_cast<double>(y),
        .width = static_cast<double>(width),
        .height = static_cast<double>(height),
        .pixels_per_unit_x = 1.0,
        .pixels_per_unit_y = 1.0,
        .window_id = std::move(window_id),
    };
}

// Captures the root window of one X11 display. `display_name` null means $DISPLAY.
CaptureResult capture_x11_root(const char* display_name, const std::filesystem::path& output_path) {
    Display* display = XOpenDisplay(display_name);
    if (display == nullptr) {
        return fail_capture(output_path, x11_open_error(display_name));
    }

    const Window root = RootWindow(display, DefaultScreen(display));
    const auto captured = read_x11_window_pixels(
        display, root, "XGetImage failed; this X11 session does not allow root screenshot capture");
    XCloseDisplay(display);
    if (!captured.error.empty()) {
        return fail_capture(output_path, captured.error);
    }

    auto result = write_bgra_image(output_path, captured.width, captured.height, captured.bgra);
    if (result.ok) {
        result.coordinates = x11_coordinates(0, 0, captured.width, captured.height, std::nullopt);
    }
    return result;
}
#endif
#endif

}

bool desktop_capture_available() {
#ifdef _WIN32
    return d3d11_desktop_capture_available();
#elif defined(__APPLE__)
    return CGPreflightScreenCaptureAccess();
#else
    bool x11_supported = false;
#ifdef KISEKI_HAS_X11
    const char* display = std::getenv("DISPLAY");
    if (display != nullptr && display[0] != '\0') {
        Display* xdisplay = XOpenDisplay(nullptr);
        if (xdisplay != nullptr) {
            const Window root = RootWindow(xdisplay, DefaultScreen(xdisplay));
            x11_supported = x11_get_image_supported(xdisplay, root);
            XCloseDisplay(xdisplay);
        }
    }
#endif
    if (x11_supported) {
        return true;
    }
#ifdef KISEKI_HAS_XDG_DESKTOP_PORTAL
    return wayland_portal_capture_available();
#else
    return false;
#endif
#endif
}

bool window_capture_available() {
#ifdef _WIN32
    return true;
#elif defined(__APPLE__)
    return CGPreflightScreenCaptureAccess() && kiseki::platform::target::target_window_available();
#else
#ifdef KISEKI_HAS_X11
    return kiseki::platform::target::target_window_available();
#else
    return false;
#endif
#endif
}

CaptureResult capture_desktop_bmp(const std::filesystem::path& output_path) {
    if (const auto error = image_output_path_error(output_path); !error.empty()) {
        return fail_capture(output_path, error);
    }
#ifdef _WIN32
    return capture_desktop_bmp_d3d11(output_path);
#elif defined(__APPLE__)
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        return fail_capture(output_path, "macOS Screen Recording permission is required for desktop screenshots; approve the prompt in System Settings and rerun");
    }
    return capture_desktop_bmp_screencapturekit(output_path);
#else
    std::string x11_error;
#ifdef KISEKI_HAS_X11
    const char* display = std::getenv("DISPLAY");
    if (display != nullptr && display[0] != '\0') {
        const auto x11_capture = capture_x11_root(nullptr, output_path);
        if (x11_capture.ok) {
            return x11_capture;
        }
        x11_error = x11_capture.error;
    }
#endif
#ifdef KISEKI_HAS_XDG_DESKTOP_PORTAL
    if (wayland_portal_capture_available()) {
        const auto portal_capture = capture_desktop_bmp_wayland_portal(output_path);
        if (portal_capture.ok || x11_error.empty()) {
            return portal_capture;
        }
        return fail_capture(output_path, x11_error + "; Wayland portal fallback failed: " + portal_capture.error);
    }
#endif
    if (!x11_error.empty()) {
        return fail_capture(output_path, x11_error);
    }
#if defined(KISEKI_HAS_X11) || defined(KISEKI_HAS_XDG_DESKTOP_PORTAL)
    return fail_capture(output_path, "Linux desktop capture requires a usable X11 DISPLAY or Wayland XDG Desktop Portal session");
#else
    return fail_capture(output_path, "Linux desktop capture support was not compiled in; build with X11 or XDG Desktop Portal support");
#endif
#endif
}

CaptureResult capture_x11_display_image(const std::string& display_name, const std::filesystem::path& output_path) {
    if (const auto error = image_output_path_error(output_path); !error.empty()) {
        return fail_capture(output_path, error);
    }
#if !defined(_WIN32) && defined(KISEKI_HAS_X11)
    return capture_x11_root(display_name.c_str(), output_path);
#else
    (void)display_name;
    return fail_capture(output_path, "X11 display capture support was not compiled in");
#endif
}

CaptureResult capture_window_bmp(const kiseki::platform::target::TargetQuery& target, const std::filesystem::path& output_path) {
    if (const auto error = image_output_path_error(output_path); !error.empty()) {
        return fail_capture(output_path, error);
    }
    const auto resolved = kiseki::platform::target::resolve_window(target);
    if (!resolved.ok) {
        return fail_capture(output_path, resolved.error);
    }

#ifdef _WIN32
    const auto hwnd = hwnd_from_id(resolved.window.id);
    if (!hwnd || !IsWindow(*hwnd)) {
        return fail_capture(output_path, "resolved target window is no longer valid");
    }
    return capture_hwnd_image(*hwnd, resolved.window.id, output_path);
#elif defined(__APPLE__)
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess()) {
        return fail_capture(output_path, "macOS Screen Recording permission is required for window screenshots; approve the prompt in System Settings and rerun");
    }

    const auto window_id = cg_window_id_from_string(resolved.window.id);
    if (!window_id) {
        return fail_capture(output_path, "resolved target window id is invalid");
    }

    return capture_window_bmp_screencapturekit(*window_id, output_path);
#else
#ifdef KISEKI_HAS_X11
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return fail_capture(output_path, x11_open_error(nullptr));
    }

    const Window window = static_cast<Window>(std::stoull(resolved.window.id, nullptr, 0));
    const auto captured = read_x11_window_pixels(
        display, window, "XGetImage failed; target may not support OS window capture in this session");
    int root_x = 0;
    int root_y = 0;
    Window child = None;
    const bool translated = captured.error.empty() &&
        XTranslateCoordinates(display, window, DefaultRootWindow(display), 0, 0, &root_x, &root_y, &child) != 0;
    XCloseDisplay(display);
    if (!captured.error.empty()) {
        return fail_capture(output_path, captured.error);
    }

    auto result = write_bgra_image(output_path, captured.width, captured.height, captured.bgra);
    if (result.ok && translated) {
        result.coordinates = x11_coordinates(root_x, root_y, captured.width, captured.height, resolved.window.id);
    }
    return result;
#else
    return fail_capture(output_path, "Linux X11 window capture support was not compiled in");
#endif
#endif
}

CaptureResult capture_background_window_bmp(const kiseki::platform::target::TargetQuery& target, const std::filesystem::path& output_path) {
    return capture_window_bmp(target, output_path);
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

OperationResult capture_window_burst_bmp(const WindowBurstOptions& options) {
    if (options.frames == 0 || options.fps == 0) {
        return fail_operation("burst frames and fps must be greater than zero");
    }

    const BurstOptions frame_options{
        .output_directory = options.output_directory,
        .prefix = options.prefix,
        .frames = options.frames,
        .fps = options.fps,
    };
    const auto frame_interval = std::chrono::microseconds{1'000'000 / options.fps};
    for (std::uint32_t index = 0; index < options.frames; ++index) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = capture_window_bmp(options.target, frame_path(frame_options, index));
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
        .message = "captured " + std::to_string(options.frames) + " target-window frames to " + options.output_directory.string(),
        .error = "",
    };
}

}
