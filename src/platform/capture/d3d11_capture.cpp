#include "platform/capture/d3d11_capture.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "platform/capture/bitmap_writer.hpp"

namespace kiseki::platform::capture {
namespace {

using Microsoft::WRL::ComPtr;

template <typename Fn>
class ScopeExit {
public:
    explicit ScopeExit(Fn fn) noexcept : fn_(std::move(fn)), active_(true) {}
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit(ScopeExit&& other) noexcept : fn_(std::move(other.fn_)), active_(other.active_) {
        other.active_ = false;
    }
    ~ScopeExit() noexcept {
        if (active_) {
            fn_();
        }
    }

    void dismiss() noexcept {
        active_ = false;
    }

private:
    Fn fn_;
    bool active_;
};

template <typename Fn>
ScopeExit<Fn> make_scope_exit(Fn fn) noexcept {
    return ScopeExit<Fn>(std::move(fn));
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

std::string hresult_hex(HRESULT hr) {
    std::ostringstream out;
    out << "HRESULT 0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
        << static_cast<std::uint32_t>(hr);
    return out.str();
}

std::string hresult_error(const std::string& operation, HRESULT hr) {
    return operation + " failed (" + hresult_hex(hr) + ")";
}

bool is_reset_hresult(HRESULT hr) noexcept {
    return hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
}

const char* rotation_name(DXGI_MODE_ROTATION rotation) noexcept {
    switch (rotation) {
    case DXGI_MODE_ROTATION_UNSPECIFIED:
        return "UNSPECIFIED";
    case DXGI_MODE_ROTATION_IDENTITY:
        return "IDENTITY";
    case DXGI_MODE_ROTATION_ROTATE90:
        return "ROTATE90";
    case DXGI_MODE_ROTATION_ROTATE180:
        return "ROTATE180";
    case DXGI_MODE_ROTATION_ROTATE270:
        return "ROTATE270";
    default:
        return "UNKNOWN";
    }
}

std::size_t bgra_size(UINT width, UINT height) noexcept {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
}

bool contains_visible_bgr(const std::vector<std::uint8_t>& bgra) noexcept {
    for (std::size_t offset = 0; offset + 2U < bgra.size(); offset += 4U) {
        if (bgra[offset] != 0 || bgra[offset + 1U] != 0 || bgra[offset + 2U] != 0) {
            return true;
        }
    }
    return false;
}

CaptureResult capture_desktop_bmp_gdi_fallback(const std::filesystem::path& output_path) {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        return fail_capture(output_path, "failed to read virtual screen dimensions for GDI fallback");
    }

    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        return fail_capture(output_path, "GetDC failed in GDI fallback");
    }
    auto release_screen_dc = make_scope_exit([screen_dc]() noexcept {
        ReleaseDC(nullptr, screen_dc);
    });

    HDC memory_dc = CreateCompatibleDC(screen_dc);
    if (memory_dc == nullptr) {
        return fail_capture(output_path, "CreateCompatibleDC failed in GDI fallback");
    }
    auto delete_memory_dc = make_scope_exit([memory_dc]() noexcept {
        DeleteDC(memory_dc);
    });

    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, width, height);
    if (bitmap == nullptr) {
        return fail_capture(output_path, "CreateCompatibleBitmap failed in GDI fallback");
    }
    auto delete_bitmap = make_scope_exit([bitmap]() noexcept {
        DeleteObject(bitmap);
    });

    const HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
    auto restore_bitmap = make_scope_exit([memory_dc, old_bitmap]() noexcept {
        if (old_bitmap != nullptr) {
            SelectObject(memory_dc, old_bitmap);
        }
    });

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

    if (!copied) {
        return fail_capture(output_path, "BitBlt failed in GDI fallback");
    }
    if (lines != height) {
        return fail_capture(output_path, "GetDIBits failed in GDI fallback");
    }

    return write_bgra_bmp(output_path, width, height, pixels);
}

bool rotated_dimensions(UINT src_width, UINT src_height, DXGI_MODE_ROTATION rotation, UINT& dst_width, UINT& dst_height, std::string& error) {
    switch (rotation) {
    case DXGI_MODE_ROTATION_IDENTITY:
    case DXGI_MODE_ROTATION_ROTATE180:
        dst_width = src_width;
        dst_height = src_height;
        return true;
    case DXGI_MODE_ROTATION_ROTATE90:
    case DXGI_MODE_ROTATION_ROTATE270:
        dst_width = src_height;
        dst_height = src_width;
        return true;
    default:
        error = std::string("unsupported DXGI output rotation: ") + rotation_name(rotation);
        return false;
    }
}

bool rotate_bgra(
    const std::vector<std::uint8_t>& src,
    UINT src_width,
    UINT src_height,
    DXGI_MODE_ROTATION rotation,
    UINT expected_width,
    UINT expected_height,
    std::vector<std::uint8_t>& dst,
    std::string& error) {
    UINT dst_width = 0;
    UINT dst_height = 0;
    if (!rotated_dimensions(src_width, src_height, rotation, dst_width, dst_height, error)) {
        return false;
    }

    if (dst_width != expected_width || dst_height != expected_height) {
        std::ostringstream out;
        out << "rotated D3D11 frame dimensions " << dst_width << 'x' << dst_height
            << " do not match DXGI desktop coordinates " << expected_width << 'x' << expected_height
            << " for rotation " << rotation_name(rotation);
        error = out.str();
        return false;
    }

    dst.assign(bgra_size(dst_width, dst_height), 0);

    switch (rotation) {
    case DXGI_MODE_ROTATION_IDENTITY:
        dst = src;
        return true;
    case DXGI_MODE_ROTATION_ROTATE180:
        for (UINT y = 0; y < src_height; ++y) {
            for (UINT x = 0; x < src_width; ++x) {
                const std::size_t src_index = (static_cast<std::size_t>(y) * src_width + x) * 4U;
                const UINT dst_x = src_width - 1U - x;
                const UINT dst_y = src_height - 1U - y;
                const std::size_t dst_index = (static_cast<std::size_t>(dst_y) * dst_width + dst_x) * 4U;
                std::memcpy(dst.data() + dst_index, src.data() + src_index, 4U);
            }
        }
        return true;
    case DXGI_MODE_ROTATION_ROTATE90:
        for (UINT y = 0; y < src_height; ++y) {
            for (UINT x = 0; x < src_width; ++x) {
                const std::size_t src_index = (static_cast<std::size_t>(y) * src_width + x) * 4U;
                const UINT dst_x = src_height - 1U - y;
                const UINT dst_y = x;
                const std::size_t dst_index = (static_cast<std::size_t>(dst_y) * dst_width + dst_x) * 4U;
                std::memcpy(dst.data() + dst_index, src.data() + src_index, 4U);
            }
        }
        return true;
    case DXGI_MODE_ROTATION_ROTATE270:
        for (UINT y = 0; y < src_height; ++y) {
            for (UINT x = 0; x < src_width; ++x) {
                const std::size_t src_index = (static_cast<std::size_t>(y) * src_width + x) * 4U;
                const UINT dst_x = y;
                const UINT dst_y = src_width - 1U - x;
                const std::size_t dst_index = (static_cast<std::size_t>(dst_y) * dst_width + dst_x) * 4U;
                std::memcpy(dst.data() + dst_index, src.data() + src_index, 4U);
            }
        }
        return true;
    default:
        error = std::string("unsupported DXGI output rotation: ") + rotation_name(rotation);
        return false;
    }
}

class D3D11CaptureContext {
public:
    struct OutputContext {
        ComPtr<IDXGIOutput> output;
        ComPtr<IDXGIOutput1> output1;
        ComPtr<IDXGIOutputDuplication> duplication;
        ComPtr<ID3D11Texture2D> staging;
        RECT desktop_rect{};
        UINT width = 0;
        UINT height = 0;
        DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_UNSPECIFIED;
        bool has_cached_frame = false;
        std::vector<std::uint8_t> cached_bgra;
    };

    struct AdapterContext {
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        std::vector<OutputContext> outputs;
    };

    struct DesktopFrame {
        int left = 0;
        int top = 0;
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> bgra;
    };

    static D3D11CaptureContext& instance() {
        static D3D11CaptureContext context;
        return context;
    }

    bool available() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string error;
        return ensure_initialized_locked(error);
    }

    CaptureResult capture(const std::filesystem::path& output_path) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string error;
        if (!ensure_initialized_locked(error)) {
            const auto fallback = capture_desktop_bmp_gdi_fallback(output_path);
            if (fallback.ok) {
                return fallback;
            }
            return fail_capture(output_path, error + "; GDI fallback failed: " + fallback.error);
        }

        for (int attempt = 0; attempt < 2; ++attempt) {
            DesktopFrame frame;
            bool reset_needed = false;
            error.clear();
            if (capture_frame_locked(frame, error, reset_needed)) {
                if (contains_visible_bgr(frame.bgra)) {
                    return write_bgra_bmp(output_path, frame.width, frame.height, frame.bgra);
                }

                // Some desktop-duplication sessions can successfully return a frame whose
                // copied texture is entirely zeroed. Treat that as unusable instead of
                // reporting a successful black screenshot.
                const auto fallback = capture_desktop_bmp_gdi_fallback(output_path);
                if (fallback.ok) {
                    return fallback;
                }
                return fail_capture(output_path, "D3D11 desktop duplication returned an all-black frame and GDI fallback failed: " + fallback.error);
            }

            if (reset_needed && attempt == 0) {
                reset_locked();
                error.clear();
                if (!initialize_locked(error)) {
                    const auto fallback = capture_desktop_bmp_gdi_fallback(output_path);
                    if (fallback.ok) {
                        return fallback;
                    }
                    return fail_capture(output_path, error + "; GDI fallback failed: " + fallback.error);
                }
                continue;
            }

            const auto fallback = capture_desktop_bmp_gdi_fallback(output_path);
            if (fallback.ok) {
                return fallback;
            }
            return fail_capture(output_path, error + "; GDI fallback failed: " + fallback.error);
        }

        const auto fallback = capture_desktop_bmp_gdi_fallback(output_path);
        if (fallback.ok) {
            return fallback;
        }
        return fail_capture(output_path, "D3D11 desktop duplication failed after reset retry; GDI fallback failed: " + fallback.error);
    }

private:
    D3D11CaptureContext() = default;

    bool ensure_initialized_locked(std::string& error) {
        if (initialized_) {
            return true;
        }
        return initialize_locked(error);
    }

    void reset_locked() noexcept {
        adapters_.clear();
        initialized_ = false;
        virtual_left_ = 0;
        virtual_top_ = 0;
        virtual_width_ = 0;
        virtual_height_ = 0;
    }

    bool initialize_locked(std::string& error) {
        reset_locked();

        ComPtr<IDXGIFactory1> factory;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr)) {
            error = hresult_error("CreateDXGIFactory1", hr);
            return false;
        }

        LONG min_left = std::numeric_limits<LONG>::max();
        LONG min_top = std::numeric_limits<LONG>::max();
        LONG max_right = std::numeric_limits<LONG>::min();
        LONG max_bottom = std::numeric_limits<LONG>::min();
        bool have_output = false;

        const D3D_FEATURE_LEVEL feature_levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        for (UINT adapter_index = 0;; ++adapter_index) {
            ComPtr<IDXGIAdapter1> adapter;
            hr = factory->EnumAdapters1(adapter_index, adapter.GetAddressOf());
            if (hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(hr)) {
                error = hresult_error("IDXGIFactory1::EnumAdapters1", hr);
                return false;
            }

            DXGI_ADAPTER_DESC1 adapter_desc{};
            hr = adapter->GetDesc1(&adapter_desc);
            if (FAILED(hr)) {
                error = hresult_error("IDXGIAdapter1::GetDesc1", hr);
                return false;
            }
            if ((adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                continue;
            }

            AdapterContext adapter_context;
            adapter_context.adapter = adapter;
            D3D_FEATURE_LEVEL selected_feature_level{};
            hr = D3D11CreateDevice(
                adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                feature_levels,
                static_cast<UINT>(sizeof(feature_levels) / sizeof(feature_levels[0])),
                D3D11_SDK_VERSION,
                adapter_context.device.GetAddressOf(),
                &selected_feature_level,
                adapter_context.context.GetAddressOf());
            if (FAILED(hr)) {
                error = hresult_error("D3D11CreateDevice", hr);
                return false;
            }

            for (UINT output_index = 0;; ++output_index) {
                ComPtr<IDXGIOutput> output;
                hr = adapter->EnumOutputs(output_index, output.GetAddressOf());
                if (hr == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                if (FAILED(hr)) {
                    error = hresult_error("IDXGIAdapter1::EnumOutputs", hr);
                    return false;
                }

                DXGI_OUTPUT_DESC output_desc{};
                hr = output->GetDesc(&output_desc);
                if (FAILED(hr)) {
                    error = hresult_error("IDXGIOutput::GetDesc", hr);
                    return false;
                }
                if (!output_desc.AttachedToDesktop) {
                    continue;
                }

                const LONG output_width = output_desc.DesktopCoordinates.right - output_desc.DesktopCoordinates.left;
                const LONG output_height = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
                if (output_width <= 0 || output_height <= 0) {
                    continue;
                }

                OutputContext output_context;
                output_context.output = output;
                hr = output.As(&output_context.output1);
                if (FAILED(hr)) {
                    error = hresult_error("IDXGIOutput::QueryInterface(IDXGIOutput1)", hr);
                    return false;
                }

                hr = output_context.output1->DuplicateOutput(adapter_context.device.Get(), output_context.duplication.GetAddressOf());
                if (FAILED(hr)) {
                    error = hresult_error("IDXGIOutput1::DuplicateOutput", hr);
                    return false;
                }

                output_context.desktop_rect = output_desc.DesktopCoordinates;
                output_context.width = static_cast<UINT>(output_width);
                output_context.height = static_cast<UINT>(output_height);
                output_context.rotation = output_desc.Rotation;
                adapter_context.outputs.push_back(std::move(output_context));

                min_left = std::min(min_left, output_desc.DesktopCoordinates.left);
                min_top = std::min(min_top, output_desc.DesktopCoordinates.top);
                max_right = std::max(max_right, output_desc.DesktopCoordinates.right);
                max_bottom = std::max(max_bottom, output_desc.DesktopCoordinates.bottom);
                have_output = true;
            }

            if (!adapter_context.outputs.empty()) {
                adapters_.push_back(std::move(adapter_context));
            }
        }

        if (!have_output) {
            error = "no attached DXGI desktop outputs are available for D3D11 capture";
            return false;
        }
        if (max_right <= min_left || max_bottom <= min_top) {
            error = "invalid DXGI virtual desktop bounds";
            return false;
        }

        const long long virtual_width = static_cast<long long>(max_right) - static_cast<long long>(min_left);
        const long long virtual_height = static_cast<long long>(max_bottom) - static_cast<long long>(min_top);
        if (virtual_width > std::numeric_limits<int>::max() || virtual_height > std::numeric_limits<int>::max()) {
            error = "DXGI virtual desktop bounds exceed supported bitmap dimensions";
            return false;
        }

        virtual_left_ = static_cast<int>(min_left);
        virtual_top_ = static_cast<int>(min_top);
        virtual_width_ = static_cast<int>(virtual_width);
        virtual_height_ = static_cast<int>(virtual_height);
        initialized_ = true;
        return true;
    }

    bool capture_frame_locked(DesktopFrame& frame, std::string& error, bool& reset_needed) {
        if (virtual_width_ <= 0 || virtual_height_ <= 0) {
            error = "D3D11 capture context has invalid virtual desktop dimensions";
            return false;
        }

        frame.left = virtual_left_;
        frame.top = virtual_top_;
        frame.width = virtual_width_;
        frame.height = virtual_height_;
        frame.bgra.assign(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4U, 0);

        for (auto& adapter : adapters_) {
            for (auto& output : adapter.outputs) {
                std::vector<std::uint8_t> output_bgra;
                if (!capture_output_locked(adapter, output, output_bgra, error, reset_needed)) {
                    return false;
                }
                if (!stitch_output_locked(frame, output, output_bgra, error)) {
                    return false;
                }
            }
        }

        return true;
    }

    bool capture_output_locked(
        AdapterContext& adapter,
        OutputContext& output,
        std::vector<std::uint8_t>& output_bgra,
        std::string& error,
        bool& reset_needed) {
        constexpr int max_acquire_attempts = 4;
        constexpr UINT acquire_timeout_ms = 500;

        for (int acquire_attempt = 0; acquire_attempt < max_acquire_attempts; ++acquire_attempt) {
            DXGI_OUTDUPL_FRAME_INFO frame_info{};
            ComPtr<IDXGIResource> resource;
            HRESULT hr = output.duplication->AcquireNextFrame(acquire_timeout_ms, &frame_info, resource.GetAddressOf());
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                if (output.has_cached_frame) {
                    output_bgra = output.cached_bgra;
                    return true;
                }
                if (acquire_attempt + 1 < max_acquire_attempts) {
                    continue;
                }
                error = hresult_error("IDXGIOutputDuplication::AcquireNextFrame timed out and no cached frame is available", hr);
                return false;
            }
            if (FAILED(hr)) {
                error = hresult_error("IDXGIOutputDuplication::AcquireNextFrame", hr);
                if (is_reset_hresult(hr)) {
                    reset_needed = true;
                }
                return false;
            }

            auto release_frame = make_scope_exit([duplication = output.duplication.Get()]() noexcept {
                if (duplication != nullptr) {
                    duplication->ReleaseFrame();
                }
            });

            // DXGI can wake AcquireNextFrame for pointer-only updates. In that case
            // LastPresentTime is 0 and the acquired surface is not a reliable desktop
            // image; accepting it on first capture is what produced 0,0,0,0 frames.
            if (frame_info.LastPresentTime.QuadPart == 0) {
                if (output.has_cached_frame) {
                    output_bgra = output.cached_bgra;
                    return true;
                }
                if (acquire_attempt + 1 < max_acquire_attempts) {
                    continue;
                }
                error = "IDXGIOutputDuplication::AcquireNextFrame did not return a desktop-present frame";
                return false;
            }

            ComPtr<ID3D11Texture2D> texture;
            hr = resource.As(&texture);
            if (FAILED(hr)) {
                error = hresult_error("IDXGIResource::QueryInterface(ID3D11Texture2D)", hr);
                if (is_reset_hresult(hr)) {
                    reset_needed = true;
                }
                return false;
            }

            D3D11_TEXTURE2D_DESC texture_desc{};
            texture->GetDesc(&texture_desc);
            if (texture_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
                std::ostringstream out;
                out << "D3D11 desktop duplication returned unsupported pixel format " << static_cast<unsigned int>(texture_desc.Format)
                    << "; expected DXGI_FORMAT_B8G8R8A8_UNORM";
                error = out.str();
                return false;
            }
            if (texture_desc.Width == 0 || texture_desc.Height == 0) {
                error = "D3D11 desktop duplication returned an empty texture";
                return false;
            }

            if (!ensure_staging_texture_locked(adapter, output, texture_desc, error, reset_needed)) {
                return false;
            }

            adapter.context->CopyResource(output.staging.Get(), texture.Get());

            D3D11_MAPPED_SUBRESOURCE mapped{};
            hr = adapter.context->Map(output.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) {
                error = hresult_error("ID3D11DeviceContext::Map", hr);
                if (is_reset_hresult(hr)) {
                    reset_needed = true;
                }
                return false;
            }
            auto unmap = make_scope_exit([context = adapter.context.Get(), staging = output.staging.Get()]() noexcept {
                if (context != nullptr && staging != nullptr) {
                    context->Unmap(staging, 0);
                }
            });

            const std::size_t row_bytes = static_cast<std::size_t>(texture_desc.Width) * 4U;
            if (mapped.RowPitch < row_bytes) {
                std::ostringstream out;
                out << "D3D11 mapped row pitch " << mapped.RowPitch << " is smaller than required row bytes " << row_bytes;
                error = out.str();
                return false;
            }

            std::vector<std::uint8_t> raw_bgra(bgra_size(texture_desc.Width, texture_desc.Height));
            const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
            for (UINT y = 0; y < texture_desc.Height; ++y) {
                std::memcpy(
                    raw_bgra.data() + static_cast<std::size_t>(y) * row_bytes,
                    src + static_cast<std::size_t>(y) * mapped.RowPitch,
                    row_bytes);
            }

            unmap.dismiss();
            adapter.context->Unmap(output.staging.Get(), 0);

            if (!rotate_bgra(raw_bgra, texture_desc.Width, texture_desc.Height, output.rotation, output.width, output.height, output_bgra, error)) {
                return false;
            }

            output.cached_bgra = output_bgra;
            output.has_cached_frame = true;
            return true;
        }

        error = "IDXGIOutputDuplication::AcquireNextFrame failed to return a usable frame";
        return false;
    }

    bool ensure_staging_texture_locked(
        AdapterContext& adapter,
        OutputContext& output,
        const D3D11_TEXTURE2D_DESC& texture_desc,
        std::string& error,
        bool& reset_needed) {
        bool recreate = output.staging == nullptr;
        if (!recreate) {
            D3D11_TEXTURE2D_DESC staging_desc{};
            output.staging->GetDesc(&staging_desc);
            recreate = staging_desc.Width != texture_desc.Width || staging_desc.Height != texture_desc.Height || staging_desc.Format != texture_desc.Format;
        }
        if (!recreate) {
            return true;
        }

        D3D11_TEXTURE2D_DESC staging_desc = texture_desc;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;

        output.staging.Reset();
        const HRESULT hr = adapter.device->CreateTexture2D(&staging_desc, nullptr, output.staging.GetAddressOf());
        if (FAILED(hr)) {
            error = hresult_error("ID3D11Device::CreateTexture2D(staging)", hr);
            if (is_reset_hresult(hr)) {
                reset_needed = true;
            }
            return false;
        }
        return true;
    }

    bool stitch_output_locked(DesktopFrame& frame, const OutputContext& output, const std::vector<std::uint8_t>& output_bgra, std::string& error) const {
        const std::size_t expected_size = bgra_size(output.width, output.height);
        if (output_bgra.size() != expected_size) {
            std::ostringstream out;
            out << "D3D11 output frame cache has invalid size " << output_bgra.size() << "; expected " << expected_size;
            error = out.str();
            return false;
        }

        const int dst_x = static_cast<int>(output.desktop_rect.left) - frame.left;
        const int dst_y = static_cast<int>(output.desktop_rect.top) - frame.top;
        if (dst_x < 0 || dst_y < 0 || dst_x + static_cast<int>(output.width) > frame.width || dst_y + static_cast<int>(output.height) > frame.height) {
            error = "D3D11 output desktop coordinates fall outside the virtual desktop frame";
            return false;
        }

        const std::size_t output_row_bytes = static_cast<std::size_t>(output.width) * 4U;
        const std::size_t frame_row_bytes = static_cast<std::size_t>(frame.width) * 4U;
        for (UINT y = 0; y < output.height; ++y) {
            const std::size_t src_offset = static_cast<std::size_t>(y) * output_row_bytes;
            const std::size_t dst_offset = (static_cast<std::size_t>(dst_y) + y) * frame_row_bytes + static_cast<std::size_t>(dst_x) * 4U;
            std::memcpy(frame.bgra.data() + dst_offset, output_bgra.data() + src_offset, output_row_bytes);
        }
        return true;
    }

    std::mutex mutex_;
    bool initialized_ = false;
    std::vector<AdapterContext> adapters_;
    int virtual_left_ = 0;
    int virtual_top_ = 0;
    int virtual_width_ = 0;
    int virtual_height_ = 0;
};

} // namespace

bool d3d11_desktop_capture_available() {
    try {
        return D3D11CaptureContext::instance().available();
    } catch (...) {
        return false;
    }
}

CaptureResult capture_desktop_bmp_d3d11(const std::filesystem::path& output_path) {
    try {
        return D3D11CaptureContext::instance().capture(output_path);
    } catch (const std::exception& exception) {
        return fail_capture(output_path, std::string("D3D11 desktop capture failed: ") + exception.what());
    } catch (...) {
        return fail_capture(output_path, "D3D11 desktop capture failed with an unknown exception");
    }
}

} // namespace kiseki::platform::capture
#endif
