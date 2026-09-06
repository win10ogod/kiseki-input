#include "platform/teach/event_stream.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#elif defined(KISEKI_HAS_XRECORD)
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/record.h>
#include <poll.h>
#endif

namespace kiseki::platform::teach {
using Clock = std::chrono::steady_clock;
namespace {
enum class Kind { key, move, button, wheel, status };
struct RawEvent {
    Kind kind = Kind::move;
    std::uint64_t timestamp_us = 0;
    std::uint64_t source_time = 0;
    int code = 0;
    int scan = 0;
    std::uint64_t flags = 0;
    double x = 0;
    double y = 0;
    double wheel = 0;
    double hwheel = 0;
    int click_count = 0;
    bool down = false;
    bool repeat = false;
    bool injected = false;
    std::string message;
};
const char *button_name(int code) {
    switch (code) {
    case 0:
        return "left";
    case 1:
        return "right";
    case 2:
        return "middle";
    case 3:
        return "x1";
    case 4:
        return "x2";
    default:
        return "other";
    }
}
}

struct NativeEventStream::Impl {
    Clock::time_point start;
    std::thread worker;
    std::atomic<bool> stopping{false};
    mutable std::mutex mutex;
    std::vector<RawEvent> queue;
    std::optional<std::pair<int, int>> mouse;
    std::array<bool, 256> keys{};
    int last_released_key = -1;
    std::uint64_t last_release_time = 0;
    std::promise<std::optional<std::string>> ready;
    explicit Impl(Clock::time_point epoch) : start(epoch) {}
    void push(RawEvent event) {
        event.timestamp_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
        if (event.kind == Kind::key && event.code >= 0 && event.code < 256) {
            event.repeat = event.repeat || (event.down && keys[event.code]);
#ifdef KISEKI_HAS_XRECORD
            // Core X11 auto-repeat is a release/press pair with the same server time.
            if (event.down && event.code == last_released_key && event.source_time == last_release_time)
                event.repeat = true;
            if (!event.down) {
                last_released_key = event.code;
                last_release_time = event.source_time;
            }
#endif
            keys[event.code] = event.down;
        }
        std::lock_guard lock{mutex};
        if (event.kind == Kind::move || event.kind == Kind::button || event.kind == Kind::wheel)
            mouse = {static_cast<int>(event.x), static_cast<int>(event.y)};
        queue.push_back(std::move(event));
    }
#ifdef _WIN32
    static thread_local Impl *active;
    static LRESULT CALLBACK keyboard(int code, WPARAM message, LPARAM data) {
        if (code == HC_ACTION && active) {
            const auto &k = *reinterpret_cast<KBDLLHOOKSTRUCT *>(data);
            RawEvent e;
            e.kind = Kind::key;
            e.code = static_cast<int>(k.vkCode);
            e.scan = static_cast<int>(k.scanCode);
            e.flags = k.flags;
            e.source_time = k.time;
            e.injected = (k.flags & LLKHF_INJECTED) != 0;
            e.down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            active->push(std::move(e));
        }
        return CallNextHookEx(nullptr, code, message, data);
    }
    static LRESULT CALLBACK pointer(int code, WPARAM message, LPARAM data) {
        if (code == HC_ACTION && active) {
            const auto &m = *reinterpret_cast<MSLLHOOKSTRUCT *>(data);
            RawEvent e;
            e.x = m.pt.x;
            e.y = m.pt.y;
            e.flags = m.flags;
            e.source_time = m.time;
            e.injected = (m.flags & LLMHF_INJECTED) != 0;
            switch (message) {
            case WM_MOUSEMOVE:
                e.kind = Kind::move;
                break;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                e.kind = Kind::button;
                e.code = 0;
                e.down = message == WM_LBUTTONDOWN;
                break;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                e.kind = Kind::button;
                e.code = 1;
                e.down = message == WM_RBUTTONDOWN;
                break;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                e.kind = Kind::button;
                e.code = 2;
                e.down = message == WM_MBUTTONDOWN;
                break;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
                e.kind = Kind::button;
                e.code = HIWORD(m.mouseData) == XBUTTON1 ? 3 : 4;
                e.down = message == WM_XBUTTONDOWN;
                break;
            case WM_MOUSEWHEEL:
                e.kind = Kind::wheel;
                e.wheel = static_cast<SHORT>(HIWORD(m.mouseData));
                break;
            case WM_MOUSEHWHEEL:
                e.kind = Kind::wheel;
                e.hwheel = static_cast<SHORT>(HIWORD(m.mouseData));
                break;
            default:
                return CallNextHookEx(nullptr, code, message, data);
            }
            active->push(std::move(e));
        }
        return CallNextHookEx(nullptr, code, message, data);
    }
    void run() {
        active = this;
        MSG message{};
        PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        const auto keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard, GetModuleHandleW(nullptr), 0);
        const auto mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, pointer, GetModuleHandleW(nullptr), 0);
        if (!keyboard_hook || !mouse_hook) {
            const auto error = GetLastError();
            if (keyboard_hook)
                UnhookWindowsHookEx(keyboard_hook);
            if (mouse_hook)
                UnhookWindowsHookEx(mouse_hook);
            active = nullptr;
            ready.set_value("Windows input hooks failed, error " + std::to_string(error));
            return;
        }
        POINT point{};
        if (GetCursorPos(&point)) {
            std::lock_guard lock{mutex};
            mouse = {point.x, point.y};
        }
        ready.set_value(std::nullopt);
        while (!stopping) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 25, QS_ALLINPUT);
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        UnhookWindowsHookEx(mouse_hook);
        UnhookWindowsHookEx(keyboard_hook);
        active = nullptr;
    }
#elif defined(__APPLE__)
    CFMachPortRef tap = nullptr;
    static CGEventRef callback(CGEventTapProxy, CGEventType type, CGEventRef event, void *context) {
        auto &self = *static_cast<Impl *>(context);
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            RawEvent status;
            status.kind = Kind::status;
            status.message = "macOS event tap was disabled; event gap possible; tap re-enabled";
            self.push(std::move(status));
            CGEventTapEnable(self.tap, true);
            return event;
        }
        RawEvent e;
        e.source_time = CGEventGetTimestamp(event);
        e.flags = CGEventGetFlags(event);
        const auto point = CGEventGetLocation(event);
        e.x = point.x;
        e.y = point.y;
        if (type == kCGEventKeyDown || type == kCGEventKeyUp || type == kCGEventFlagsChanged) {
            e.kind = Kind::key;
            e.code = static_cast<int>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
            e.scan = e.code;
            e.down = type == kCGEventKeyDown;
            e.repeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0;
            if (type == kCGEventFlagsChanged) {
                std::uint64_t mask = 0;
                switch (e.code) {
                case 56:
                    mask = 0x2;
                    break;
                case 60:
                    mask = 0x4;
                    break;
                case 59:
                    mask = 0x1;
                    break;
                case 62:
                    mask = 0x2000;
                    break;
                case 58:
                    mask = 0x20;
                    break;
                case 61:
                    mask = 0x40;
                    break;
                case 55:
                    mask = 0x8;
                    break;
                case 54:
                    mask = 0x10;
                    break;
                case 57:
                    mask = kCGEventFlagMaskAlphaShift;
                    break;
                case 63:
                    mask = kCGEventFlagMaskSecondaryFn;
                    break;
                default:
                    break;
                }
                std::uint64_t group = 0, devices = 0;
                switch (e.code) {
                case 56:
                case 60:
                    group = kCGEventFlagMaskShift;
                    devices = 0x6;
                    break;
                case 59:
                case 62:
                    group = kCGEventFlagMaskControl;
                    devices = 0x2001;
                    break;
                case 58:
                case 61:
                    group = kCGEventFlagMaskAlternate;
                    devices = 0x60;
                    break;
                case 55:
                case 54:
                    group = kCGEventFlagMaskCommand;
                    devices = 0x18;
                    break;
                default:
                    break;
                }
                // CGEvent injectors may provide only the aggregate modifier flag.
                // In that case, flagsChanged still identifies which key changed.
                e.down = group && (e.flags & group) && !(e.flags & devices) ? !self.keys[e.code & 255]
                         : mask                                             ? (e.flags & mask) != 0
                                                                            : !self.keys[e.code & 255];
            }
        } else if (type == kCGEventScrollWheel) {
            e.kind = Kind::wheel;
            const bool continuous = CGEventGetIntegerValueField(event, kCGScrollWheelEventIsContinuous) != 0;
            e.code = continuous ? 1 : 0;
            e.wheel = CGEventGetDoubleValueField(event, continuous ? kCGScrollWheelEventPointDeltaAxis1
                                                                   : kCGScrollWheelEventFixedPtDeltaAxis1);
            e.hwheel = -CGEventGetDoubleValueField(event, continuous ? kCGScrollWheelEventPointDeltaAxis2
                                                                     : kCGScrollWheelEventFixedPtDeltaAxis2);
        } else if (type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged ||
                   type == kCGEventRightMouseDragged || type == kCGEventOtherMouseDragged) {
            e.kind = Kind::move;
        } else {
            e.kind = Kind::button;
            e.code = static_cast<int>(CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber));
            e.down = type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown || type == kCGEventOtherMouseDown;
            e.click_count = static_cast<int>(CGEventGetIntegerValueField(event, kCGMouseEventClickState));
        }
        self.push(std::move(e));
        return event;
    }
    void run() {
        CGEventMask mask = 0;
        for (auto type : {kCGEventKeyDown, kCGEventKeyUp, kCGEventFlagsChanged, kCGEventMouseMoved,
                          kCGEventLeftMouseDragged, kCGEventRightMouseDragged, kCGEventOtherMouseDragged,
                          kCGEventLeftMouseDown, kCGEventLeftMouseUp, kCGEventRightMouseDown, kCGEventRightMouseUp,
                          kCGEventOtherMouseDown, kCGEventOtherMouseUp, kCGEventScrollWheel})
            mask |= CGEventMaskBit(type);
        tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, mask, callback,
                               this);
        if (!tap) {
            ready.set_value("macOS event tap creation failed; grant Input Monitoring permission to the recording "
                            "process or its terminal host");
            return;
        }
        const auto source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0);
        if (!source) {
            CFRelease(tap);
            tap = nullptr;
            ready.set_value("macOS event tap run-loop creation failed");
            return;
        }
        CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
        if (auto event = CGEventCreate(nullptr)) {
            const auto p = CGEventGetLocation(event);
            std::lock_guard lock{mutex};
            mouse = {static_cast<int>(p.x), static_cast<int>(p.y)};
            CFRelease(event);
        }
        for (std::size_t code = 0; code < 128; ++code)
            keys[code] = CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, static_cast<CGKeyCode>(code));
        CGEventTapEnable(tap, true);
        ready.set_value(std::nullopt);
        while (!stopping)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.025, false);
        CGEventTapEnable(tap, false);
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
        CFRelease(source);
        CFRelease(tap);
        tap = nullptr;
    }
#elif defined(KISEKI_HAS_XRECORD)
    static void callback(XPointer context, XRecordInterceptData *data) {
        auto &self = *reinterpret_cast<Impl *>(context);
        if (data->category == XRecordFromServer) {
            for (unsigned long offset = 0; offset + 32 <= data->data_len * 4; offset += 32) {
                xEvent event{};
                std::memcpy(&event, data->data + offset, 32);
                auto time = event.u.keyButtonPointer.time;
                auto x = event.u.keyButtonPointer.rootX;
                auto y = event.u.keyButtonPointer.rootY;
                auto state = event.u.keyButtonPointer.state;
                if (data->client_swapped) {
                    time = __builtin_bswap32(time);
                    x = static_cast<INT16>(__builtin_bswap16(static_cast<CARD16>(x)));
                    y = static_cast<INT16>(__builtin_bswap16(static_cast<CARD16>(y)));
                    state = __builtin_bswap16(state);
                }
                RawEvent e;
                e.source_time = time;
                e.x = x;
                e.y = y;
                e.flags = state;
                const auto type = event.u.u.type & 0x7f;
                const auto detail = event.u.u.detail;
                if (type == KeyPress || type == KeyRelease) {
                    e.kind = Kind::key;
                    e.code = detail;
                    e.scan = detail;
                    e.down = type == KeyPress;
                } else if (type == MotionNotify)
                    e.kind = Kind::move;
                else if (type == ButtonPress || type == ButtonRelease) {
                    if (detail >= 4 && detail <= 7) {
                        if (type == ButtonRelease)
                            continue;
                        e.kind = Kind::wheel;
                        e.wheel = detail == 4 ? 120 : detail == 5 ? -120 : 0;
                        e.hwheel = detail == 7 ? 120 : detail == 6 ? -120 : 0;
                    } else {
                        e.kind = Kind::button;
                        e.down = type == ButtonPress;
                        e.code = detail == 1   ? 0
                                 : detail == 3 ? 1
                                 : detail == 2 ? 2
                                 : detail == 8 ? 3
                                 : detail == 9 ? 4
                                               : detail;
                    }
                } else
                    continue;
                self.push(std::move(e));
            }
        }
        XRecordFreeData(data);
    }
    void run() {
        // Both connections are owned by this thread; the caller initializes Xlib threading before its first Xlib call.
        Display *control = XOpenDisplay(nullptr);
        Display *data = XOpenDisplay(nullptr);
        if (!control || !data) {
            if (control)
                XCloseDisplay(control);
            if (data)
                XCloseDisplay(data);
            ready.set_value("XRecord could not open DISPLAY");
            return;
        }
        int major = 0, minor = 0;
        if (!XRecordQueryVersion(control, &major, &minor)) {
            XCloseDisplay(data);
            XCloseDisplay(control);
            ready.set_value("X server does not expose the RECORD extension");
            return;
        }
        auto *range = XRecordAllocRange();
        if (!range) {
            XCloseDisplay(data);
            XCloseDisplay(control);
            ready.set_value("XRecordAllocRange failed");
            return;
        }
        range->device_events.first = KeyPress;
        range->device_events.last = MotionNotify;
        XRecordClientSpec clients = XRecordAllClients;
        const auto context = XRecordCreateContext(control, 0, &clients, 1, &range, 1);
        XFree(range);
        XSync(control, False);
        if (!context || !XRecordEnableContextAsync(data, context, callback, reinterpret_cast<XPointer>(this))) {
            if (context)
                XRecordFreeContext(control, context);
            XCloseDisplay(data);
            XCloseDisplay(control);
            ready.set_value("XRecordEnableContextAsync failed");
            return;
        }
        ready.set_value(std::nullopt);
        pollfd fd{ConnectionNumber(data), POLLIN, 0};
        while (!stopping) {
            XRecordProcessReplies(data);
            poll(&fd, 1, 25);
        }
        XRecordDisableContext(control, context);
        XSync(control, False);
        XRecordProcessReplies(data);
        XRecordFreeContext(control, context);
        XCloseDisplay(data);
        XCloseDisplay(control);
    }
#else
    void run() {
        ready.set_value("native event stream was not compiled for this platform (Linux X11 requires libXtst RECORD "
                        "development files)");
    }
#endif
};
#ifdef _WIN32
thread_local NativeEventStream::Impl *NativeEventStream::Impl::active = nullptr;
#endif
NativeEventStream::NativeEventStream(Clock::time_point start) : impl_(std::make_unique<Impl>(start)) {}
NativeEventStream::~NativeEventStream() {
    stop();
}
std::optional<std::string> NativeEventStream::initialize() {
    auto future = impl_->ready.get_future();
    impl_->worker = std::thread([this] { impl_->run(); });
    return future.get();
}
void NativeEventStream::stop() {
    impl_->stopping = true;
    if (impl_->worker.joinable())
        impl_->worker.join();
}
std::string NativeEventStream::source() const {
#ifdef _WIN32
    return "windows-low-level-hooks";
#elif defined(__APPLE__)
    return "macos-event-tap";
#elif defined(KISEKI_HAS_XRECORD)
    return "x11-record";
#else
    return "unavailable";
#endif
}
std::optional<std::pair<int, int>> NativeEventStream::last_mouse_position() const {
    std::lock_guard lock{impl_->mutex};
    return impl_->mouse;
}
std::vector<nlohmann::json> NativeEventStream::drain() {
    std::vector<RawEvent> queue;
    {
        std::lock_guard lock{impl_->mutex};
        queue.swap(impl_->queue);
    }
    std::vector<nlohmann::json> events;
    events.reserve(queue.size());
    for (const auto &raw : queue) {
        nlohmann::json e{{"timestampMs", raw.timestamp_us / 1000},
                         {"timestampUs", raw.timestamp_us},
                         {"sourceTimestamp", raw.source_time},
                         {"source", source()},
                         {"flags", raw.flags}};
#ifdef __APPLE__
        e["sourceTimestampUnit"] = "ns";
#else
        e["sourceTimestampUnit"] = "ms";
#endif
        if (raw.kind == Kind::key) {
            e["type"] = "key";
            e["keyCode"] = raw.code;
            e["scanCode"] = raw.scan;
            e["state"] = raw.down ? "down" : "up";
            e["repeat"] = raw.repeat;
#ifdef _WIN32
            e["extended"] = (raw.flags & LLKHF_EXTENDED) != 0;
            wchar_t name[128]{};
            const auto length = GetKeyNameTextW(
                static_cast<LONG>((raw.scan | ((raw.flags & LLKHF_EXTENDED) ? 0x100 : 0)) << 16), name, 128);
            if (length > 0) {
                const int count = WideCharToMultiByte(CP_UTF8, 0, name, length, nullptr, 0, nullptr, nullptr);
                std::string value(static_cast<std::size_t>(count), '\0');
                WideCharToMultiByte(CP_UTF8, 0, name, length, value.data(), count, nullptr, nullptr);
                e["key"] = value;
            }
#endif
        } else if (raw.kind == Kind::status) {
            e["type"] = "recorder_status";
            e["level"] = "warning";
            e["message"] = raw.message;
        } else {
            e["x"] = raw.x;
            e["y"] = raw.y;
            if (raw.kind == Kind::move)
                e["type"] = "mouse_move";
            else if (raw.kind == Kind::button) {
                e["type"] = "mouse_button";
                e["button"] = button_name(raw.code);
                e["buttonCode"] = raw.code;
                e["state"] = raw.down ? "down" : "up";
                e["clickCount"] = raw.click_count;
            } else {
                e["type"] = "mouse_wheel";
                e["deltaY"] = raw.wheel;
                e["deltaX"] = raw.hwheel;
#ifdef __APPLE__
                e["deltaUnit"] = raw.code == 1 ? "pixel" : "line";
#else
                e["deltaUnit"] = "wheel-120";
#endif
            }
        }
#ifdef _WIN32
        e["injected"] = raw.injected;
#endif
        events.push_back(std::move(e));
    }
    return events;
}
}
