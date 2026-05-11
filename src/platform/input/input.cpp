#include "platform/input/input.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#ifdef KISEKI_HAS_X11
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <cstdlib>
#endif
#endif

namespace kiseki::platform::input {

namespace {

OperationResult ok(std::string message) {
    return OperationResult{
        .ok = true,
        .code = 0,
        .message = std::move(message),
        .error = "",
    };
}

OperationResult fail(std::string error) {
    return OperationResult{
        .ok = false,
        .code = 2,
        .message = "",
        .error = std::move(error),
    };
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::string> split_keys(std::string_view keys) {
    std::vector<std::string> result;
    std::string current;
    for (char c : keys) {
        if (c == '+') {
            if (!current.empty()) {
                result.push_back(lower_copy(current));
                current.clear();
            }
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        result.push_back(lower_copy(current));
    }
    return result;
}

bool supported_mouse_click(const std::string& click) {
    return click == "none" ||
           click == "left" ||
           click == "right" ||
           click == "middle" ||
           click == "left-down" ||
           click == "left-up" ||
           click == "right-down" ||
           click == "right-up" ||
           click == "middle-down" ||
           click == "middle-up";
}

bool supported_backend(const std::string& backend) {
    return backend == "auto" || backend == "driver" || backend == "system";
}

bool combo_contains_system_hotkey(const std::vector<std::string>& keys) {
    return std::find(keys.begin(), keys.end(), "win") != keys.end() ||
           std::find(keys.begin(), keys.end(), "super") != keys.end() ||
           std::find(keys.begin(), keys.end(), "meta") != keys.end();
}

#ifdef _WIN32

using IbSendInitFn = unsigned long(__stdcall*)(unsigned long, unsigned long, void*);
using IbSendDestroyFn = void(__stdcall*)();
using IbSendKeybdDownFn = bool(__stdcall*)(unsigned short);
using IbSendKeybdUpFn = bool(__stdcall*)(unsigned short);
using IbSendMouseMoveFn = bool(__stdcall*)(unsigned int, unsigned int, unsigned int);
using IbSendMouseClickFn = bool(__stdcall*)(unsigned int);

class IbInputSimulator {
public:
    IbInputSimulator() {
        dll_ = LoadLibraryW(L"IbInputSimulator.dll");
        if (dll_ == nullptr) {
            return;
        }

        init_ = reinterpret_cast<IbSendInitFn>(GetProcAddress(dll_, "IbSendInit"));
        destroy_ = reinterpret_cast<IbSendDestroyFn>(GetProcAddress(dll_, "IbSendDestroy"));
        key_down_ = reinterpret_cast<IbSendKeybdDownFn>(GetProcAddress(dll_, "IbSendKeybdDown"));
        key_up_ = reinterpret_cast<IbSendKeybdUpFn>(GetProcAddress(dll_, "IbSendKeybdUp"));
        mouse_move_ = reinterpret_cast<IbSendMouseMoveFn>(GetProcAddress(dll_, "IbSendMouseMove"));
        mouse_click_ = reinterpret_cast<IbSendMouseClickFn>(GetProcAddress(dll_, "IbSendMouseClick"));
        if (!(init_ && destroy_ && key_down_ && key_up_ && mouse_move_ && mouse_click_)) {
            FreeLibrary(dll_);
            dll_ = nullptr;
            return;
        }

        initialized_ = init_(0, 1, nullptr) == 0;
    }

    ~IbInputSimulator() {
        if (initialized_ && destroy_ != nullptr) {
            destroy_();
        }
        if (dll_ != nullptr) {
            FreeLibrary(dll_);
        }
    }

    bool available() const {
        return initialized_;
    }

    bool key_down(unsigned short vk) const {
        return key_down_ != nullptr && key_down_(vk);
    }

    bool key_up(unsigned short vk) const {
        return key_up_ != nullptr && key_up_(vk);
    }

    bool mouse_move(int x, int y, unsigned int mode) const {
        return mouse_move_ != nullptr &&
               mouse_move_(static_cast<unsigned int>(x), static_cast<unsigned int>(y), mode);
    }

    bool mouse_click(unsigned int button) const {
        return mouse_click_ != nullptr && mouse_click_(button);
    }

private:
    HMODULE dll_ = nullptr;
    IbSendInitFn init_ = nullptr;
    IbSendDestroyFn destroy_ = nullptr;
    IbSendKeybdDownFn key_down_ = nullptr;
    IbSendKeybdUpFn key_up_ = nullptr;
    IbSendMouseMoveFn mouse_move_ = nullptr;
    IbSendMouseClickFn mouse_click_ = nullptr;
    bool initialized_ = false;
};

unsigned short virtual_key_for_name(const std::string& key) {
    const std::string name = lower_copy(key);
    if (name.size() == 1) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return static_cast<unsigned short>(c);
        }
    }
    if (name == "enter" || name == "return") return VK_RETURN;
    if (name == "esc" || name == "escape") return VK_ESCAPE;
    if (name == "space") return VK_SPACE;
    if (name == "tab") return VK_TAB;
    if (name == "backspace") return VK_BACK;
    if (name == "shift") return VK_SHIFT;
    if (name == "ctrl" || name == "control") return VK_CONTROL;
    if (name == "alt") return VK_MENU;
    if (name == "win" || name == "super" || name == "meta") return VK_LWIN;
    if (name == "left") return VK_LEFT;
    if (name == "right") return VK_RIGHT;
    if (name == "up") return VK_UP;
    if (name == "down") return VK_DOWN;
    if (name.size() >= 2 && name[0] == 'f') {
        const int number = std::atoi(name.c_str() + 1);
        if (number >= 1 && number <= 24) {
            return static_cast<unsigned short>(VK_F1 + number - 1);
        }
    }
    return 0;
}

bool send_key_event(unsigned short vk, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    return SendInput(1, &input, sizeof(INPUT)) == 1;
}

OperationResult send_combo_with_sendinput(const std::vector<std::string>& keys) {
    std::vector<unsigned short> vks;
    for (const auto& key : keys) {
        const auto vk = virtual_key_for_name(key);
        if (vk == 0) {
            return fail("unsupported key: " + key);
        }
        vks.push_back(vk);
    }

    for (const auto vk : vks) {
        if (!send_key_event(vk, true)) {
            return fail("SendInput key down failed");
        }
    }
    for (auto iter = vks.rbegin(); iter != vks.rend(); ++iter) {
        if (!send_key_event(*iter, false)) {
            return fail("SendInput key up failed");
        }
    }
    return ok("input sent");
}

std::wstring utf8_to_utf16(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    unsigned int code_page = CP_UTF8;
    unsigned long flags = MB_ERR_INVALID_CHARS;
    if (size == 0) {
        code_page = CP_ACP;
        flags = 0;
        size = MultiByteToWideChar(code_page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    }
    if (size == 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(code_page, flags, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

bool send_unicode_code_unit(wchar_t c) {
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wScan = static_cast<WORD>(c);
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

int normalized_absolute_coordinate(int value, int origin, int size) {
    if (size <= 1) {
        return 0;
    }
    const int clamped = std::clamp(value, origin, origin + size - 1);
    const long long relative = static_cast<long long>(clamped - origin) * 65535LL;
    return static_cast<int>(relative / static_cast<long long>(size - 1));
}

std::pair<int, int> normalized_absolute_position(int x, int y) {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return {
        normalized_absolute_coordinate(x, left, width),
        normalized_absolute_coordinate(y, top, height),
    };
}

unsigned int ib_mouse_button_for_click(const std::string& click) {
    if (click == "left") return 0x06;
    if (click == "right") return 0x18;
    if (click == "middle") return 0x60;
    if (click == "left-down") return 0x02;
    if (click == "left-up") return 0x04;
    if (click == "right-down") return 0x08;
    if (click == "right-up") return 0x10;
    if (click == "middle-down") return 0x20;
    if (click == "middle-up") return 0x40;
    return 0;
}

DWORD sendinput_mouse_flags_for_click(const std::string& click) {
    if (click == "left") return MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP;
    if (click == "right") return MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP;
    if (click == "middle") return MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP;
    if (click == "left-down") return MOUSEEVENTF_LEFTDOWN;
    if (click == "left-up") return MOUSEEVENTF_LEFTUP;
    if (click == "right-down") return MOUSEEVENTF_RIGHTDOWN;
    if (click == "right-up") return MOUSEEVENTF_RIGHTUP;
    if (click == "middle-down") return MOUSEEVENTF_MIDDLEDOWN;
    if (click == "middle-up") return MOUSEEVENTF_MIDDLEUP;
    return 0;
}

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

std::string class_name_lower(HWND hwnd) {
    wchar_t buffer[256]{};
    const int copied = GetClassNameW(hwnd, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
    std::string name;
    name.reserve(static_cast<std::size_t>(copied > 0 ? copied : 0));
    for (int index = 0; index < copied; ++index) {
        const wchar_t c = buffer[index];
        name.push_back(c < 128 ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '?');
    }
    return name;
}

bool is_text_input_class(HWND hwnd) {
    const std::string name = class_name_lower(hwnd);
    return name.find("edit") != std::string::npos ||
           name.find("richedit") != std::string::npos ||
           name.find("textbox") != std::string::npos;
}

struct TextChildSearch {
    HWND result = nullptr;
};

BOOL CALLBACK enum_text_child_proc(HWND hwnd, LPARAM lparam) {
    auto* search = reinterpret_cast<TextChildSearch*>(lparam);
    if (IsWindowVisible(hwnd) && is_text_input_class(hwnd)) {
        search->result = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND first_text_child(HWND hwnd) {
    TextChildSearch search;
    EnumChildWindows(hwnd, enum_text_child_proc, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

HWND keyboard_message_target(HWND hwnd) {
    DWORD pid = 0;
    const DWORD thread_id = GetWindowThreadProcessId(hwnd, &pid);
    if (thread_id != 0) {
        GUITHREADINFO info{};
        info.cbSize = sizeof(info);
        if (GetGUIThreadInfo(thread_id, &info) != 0 &&
            info.hwndFocus != nullptr &&
            (info.hwndFocus == hwnd || IsChild(hwnd, info.hwndFocus))) {
            return info.hwndFocus;
        }
    }

    if (HWND child = first_text_child(hwnd); child != nullptr) {
        return child;
    }
    return hwnd;
}

LPARAM key_lparam(unsigned short vk, bool release) {
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LPARAM lparam = 1 | (static_cast<LPARAM>(scan) << 16);
    if (release) {
        lparam |= (1LL << 30) | (1LL << 31);
    }
    return lparam;
}

OperationResult with_resolved_hwnd(const kiseki::platform::target::TargetQuery& query, const auto& callback) {
    const auto resolved = kiseki::platform::target::resolve_window(query);
    if (!resolved.ok) {
        return fail(resolved.error);
    }

    const auto hwnd = hwnd_from_id(resolved.window.id);
    if (!hwnd || !IsWindow(*hwnd)) {
        return fail("resolved target window is no longer valid");
    }
    return callback(*hwnd);
}

HWND mouse_message_target(HWND hwnd, POINT& point) {
    HWND child = ChildWindowFromPointEx(hwnd, point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
    if (child == nullptr || child == hwnd) {
        return hwnd;
    }

    MapWindowPoints(hwnd, child, &point, 1);
    return child;
}

LPARAM mouse_lparam(const POINT& point) {
    return MAKELPARAM(static_cast<WORD>(point.x), static_cast<WORD>(point.y));
}

OperationResult post_mouse_button(HWND hwnd, UINT down_message, UINT up_message, WPARAM state, const POINT& point, const std::string& click) {
    if (click.ends_with("-down")) {
        return PostMessageW(hwnd, down_message, state, mouse_lparam(point)) != 0 ? ok("background mouse input sent") : fail("PostMessage mouse down failed");
    }
    if (click.ends_with("-up")) {
        return PostMessageW(hwnd, up_message, 0, mouse_lparam(point)) != 0 ? ok("background mouse input sent") : fail("PostMessage mouse up failed");
    }
    if (PostMessageW(hwnd, down_message, state, mouse_lparam(point)) == 0) {
        return fail("PostMessage mouse down failed");
    }
    if (PostMessageW(hwnd, up_message, 0, mouse_lparam(point)) == 0) {
        return fail("PostMessage mouse up failed");
    }
    return ok("background mouse input sent");
}

#else
#ifdef KISEKI_HAS_X11

using XTestFakeKeyEventFn = int (*)(Display*, unsigned int, Bool, unsigned long);
using XTestFakeRelativeMotionEventFn = int (*)(Display*, int, int, unsigned long);
using XTestFakeButtonEventFn = int (*)(Display*, unsigned int, Bool, unsigned long);

struct XTestApi {
    void* library = nullptr;
    XTestFakeKeyEventFn fake_key = nullptr;
    XTestFakeRelativeMotionEventFn fake_motion = nullptr;
    XTestFakeButtonEventFn fake_button = nullptr;

    XTestApi() {
        library = dlopen("libXtst.so.6", RTLD_LAZY);
        if (library == nullptr) {
            return;
        }
        fake_key = reinterpret_cast<XTestFakeKeyEventFn>(dlsym(library, "XTestFakeKeyEvent"));
        fake_motion = reinterpret_cast<XTestFakeRelativeMotionEventFn>(dlsym(library, "XTestFakeRelativeMotionEvent"));
        fake_button = reinterpret_cast<XTestFakeButtonEventFn>(dlsym(library, "XTestFakeButtonEvent"));
    }

    ~XTestApi() {
        if (library != nullptr) {
            dlclose(library);
        }
    }

    bool available() const {
        return fake_key != nullptr && fake_motion != nullptr && fake_button != nullptr;
    }
};

KeySym keysym_for_name(const std::string& key) {
    const std::string name = lower_copy(key);
    if (name.size() == 1) {
        return XStringToKeysym(name.c_str());
    }
    if (name == "enter" || name == "return") return XK_Return;
    if (name == "esc" || name == "escape") return XK_Escape;
    if (name == "space") return XK_space;
    if (name == "tab") return XK_Tab;
    if (name == "backspace") return XK_BackSpace;
    if (name == "shift") return XK_Shift_L;
    if (name == "ctrl" || name == "control") return XK_Control_L;
    if (name == "alt") return XK_Alt_L;
    if (name == "win" || name == "super" || name == "meta") return XK_Super_L;
    if (name == "left") return XK_Left;
    if (name == "right") return XK_Right;
    if (name == "up") return XK_Up;
    if (name == "down") return XK_Down;
    if (name.size() >= 2 && name[0] == 'f') {
        const int number = std::atoi(name.c_str() + 1);
        if (number >= 1 && number <= 35) {
            return static_cast<KeySym>(XK_F1 + number - 1);
        }
    }
    return NoSymbol;
}

OperationResult with_display(const auto& callback) {
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return fail("XOpenDisplay failed; DISPLAY is not available");
    }
    XTestApi xtest;
    if (!xtest.available()) {
        XCloseDisplay(display);
        return fail("libXtst is not available");
    }
    const auto result = callback(display, xtest);
    XSync(display, False);
    XCloseDisplay(display);
    return result;
}

OperationResult send_combo_with_xtest(const std::vector<std::string>& keys) {
    return with_display([&](Display* display, const XTestApi& xtest) {
        std::vector<KeyCode> keycodes;
        for (const auto& key : keys) {
            const KeySym sym = keysym_for_name(key);
            if (sym == NoSymbol) {
                return fail("unsupported key: " + key);
            }
            const KeyCode code = XKeysymToKeycode(display, sym);
            if (code == 0) {
                return fail("unsupported keycode: " + key);
            }
            keycodes.push_back(code);
        }
        for (const auto code : keycodes) {
            xtest.fake_key(display, code, True, CurrentTime);
        }
        for (auto iter = keycodes.rbegin(); iter != keycodes.rend(); ++iter) {
            xtest.fake_key(display, *iter, False, CurrentTime);
        }
        return ok("input sent");
    });
}

OperationResult with_target_window(const kiseki::platform::target::TargetQuery& query, const auto& callback) {
    const auto resolved = kiseki::platform::target::resolve_window(query);
    if (!resolved.ok) {
        return fail(resolved.error);
    }

    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return fail("XOpenDisplay failed; DISPLAY is not available");
    }

    const Window window = static_cast<Window>(std::stoull(resolved.window.id, nullptr, 0));
    const auto result = callback(display, window);
    XFlush(display);
    XCloseDisplay(display);
    return result;
}

OperationResult send_background_key_x11(Display* display, Window window, const std::string& key) {
    const KeySym sym = keysym_for_name(key);
    if (sym == NoSymbol) {
        return fail("unsupported key: " + key);
    }
    const KeyCode keycode = XKeysymToKeycode(display, sym);
    if (keycode == 0) {
        return fail("unsupported keycode: " + key);
    }

    XKeyEvent event{};
    event.display = display;
    event.window = window;
    event.root = DefaultRootWindow(display);
    event.subwindow = None;
    event.time = CurrentTime;
    event.x = 1;
    event.y = 1;
    event.x_root = 1;
    event.y_root = 1;
    event.same_screen = True;
    event.keycode = keycode;
    event.state = 0;

    event.type = KeyPress;
    if (XSendEvent(display, window, True, KeyPressMask, reinterpret_cast<XEvent*>(&event)) == 0) {
        return fail("XSendEvent key press failed");
    }
    event.type = KeyRelease;
    if (XSendEvent(display, window, True, KeyReleaseMask, reinterpret_cast<XEvent*>(&event)) == 0) {
        return fail("XSendEvent key release failed");
    }
    return ok("background key input sent");
}

std::string x11_key_name_for_char(char c) {
    if (c == '\n' || c == '\r') return "Return";
    if (c == '\t') return "Tab";
    if (c == ' ') return "space";
    return std::string{c};
}
#endif
#endif

}

bool system_input_available() {
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
    XCloseDisplay(xdisplay);
    XTestApi xtest;
    return xtest.available();
#else
    return false;
#endif
#endif
}

bool driver_input_available() {
#ifdef _WIN32
    IbInputSimulator simulator;
    return simulator.available();
#else
    return false;
#endif
}

bool background_window_input_available() {
#ifdef _WIN32
    return true;
#else
#ifdef KISEKI_HAS_X11
    return kiseki::platform::target::target_window_available();
#else
    return false;
#endif
#endif
}

OperationResult tap_key(const std::string& key, const std::string& backend) {
    return key_combo(key, backend);
}

OperationResult key_combo(const std::string& keys, const std::string& backend) {
    const auto key_list = split_keys(keys);
    if (key_list.empty()) {
        return fail("no key specified");
    }
    const std::string selected_backend = lower_copy(backend.empty() ? "auto" : backend);
    if (!supported_backend(selected_backend)) {
        return fail("backend must be auto, driver, or system");
    }

#ifdef _WIN32
    if (selected_backend == "system" || (selected_backend == "auto" && combo_contains_system_hotkey(key_list))) {
        return send_combo_with_sendinput(key_list);
    }
    IbInputSimulator simulator;
    if (simulator.available()) {
        std::vector<unsigned short> vks;
        for (const auto& key : key_list) {
            const auto vk = virtual_key_for_name(key);
            if (vk == 0) {
                return fail("unsupported key: " + key);
            }
            vks.push_back(vk);
        }
        for (const auto vk : vks) {
            if (!simulator.key_down(vk)) {
                return fail("IbInputSimulator key down failed");
            }
        }
        for (auto iter = vks.rbegin(); iter != vks.rend(); ++iter) {
            if (!simulator.key_up(*iter)) {
                return fail("IbInputSimulator key up failed");
            }
        }
        return ok("input sent through IbInputSimulator");
    }
    if (selected_backend == "driver") {
        return fail("IbInputSimulator is not available");
    }
    return send_combo_with_sendinput(key_list);
#else
#ifdef KISEKI_HAS_X11
    if (selected_backend == "driver") {
        return fail("Linux driver backend is not available; system X11/XTest input is available when DISPLAY permits it");
    }
    return send_combo_with_xtest(key_list);
#else
    return fail("Linux X11/XTest input support was not compiled in");
#endif
#endif
}

OperationResult type_text(const std::string& text) {
    if (text.empty()) {
        return ok("input text empty");
    }
#ifdef _WIN32
    const std::wstring wide = utf8_to_utf16(text);
    if (wide.empty()) {
        return fail("failed to decode text");
    }

    for (const wchar_t c : wide) {
        if (!send_unicode_code_unit(c)) {
            return fail("SendInput text failed");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ok("text input sent");
#else
    for (const char c : text) {
        const auto result = key_combo(std::string{c});
        if (!result.ok) {
            return result;
        }
    }
    return ok("text input sent");
#endif
}

OperationResult mouse_drag_absolute(const std::vector<MousePoint>& points, const std::string& backend) {
    if (points.size() < 2) {
        return fail("mouse drag requires at least two points");
    }
    const std::string selected_backend = lower_copy(backend.empty() ? "auto" : backend);
    if (!supported_backend(selected_backend)) {
        return fail("backend must be auto, driver, or system");
    }

#ifdef _WIN32
    if (selected_backend != "system") {
        IbInputSimulator simulator;
        if (!simulator.available() && selected_backend == "driver") {
            return fail("IbInputSimulator is not available");
        }
        if (simulator.available()) {
            const auto first = normalized_absolute_position(points.front().x, points.front().y);
            if (!simulator.mouse_move(first.first, first.second, 0) || !simulator.mouse_click(0x02)) {
                return fail("IbInputSimulator drag start failed");
            }
            for (const auto& point : points) {
                const auto [x, y] = normalized_absolute_position(point.x, point.y);
                if (!simulator.mouse_move(x, y, 0)) {
                    simulator.mouse_click(0x04);
                    return fail("IbInputSimulator drag move failed");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (!simulator.mouse_click(0x04)) {
                return fail("IbInputSimulator drag release failed");
            }
            return ok("mouse drag sent through IbInputSimulator");
        }
    }

    const auto send_absolute_move = [](int x, int y) {
        const auto [nx, ny] = normalized_absolute_position(x, y);
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = nx;
        input.mi.dy = ny;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        return SendInput(1, &input, sizeof(INPUT)) == 1;
    };
    const auto send_button = [](DWORD flags) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = flags;
        return SendInput(1, &input, sizeof(INPUT)) == 1;
    };

    if (!send_absolute_move(points.front().x, points.front().y) || !send_button(MOUSEEVENTF_LEFTDOWN)) {
        return fail("SendInput drag start failed");
    }
    for (const auto& point : points) {
        if (!send_absolute_move(point.x, point.y)) {
            send_button(MOUSEEVENTF_LEFTUP);
            return fail("SendInput drag move failed");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!send_button(MOUSEEVENTF_LEFTUP)) {
        return fail("SendInput drag release failed");
    }
    return ok("mouse drag sent");
#else
#ifdef KISEKI_HAS_X11
    if (selected_backend == "driver") {
        return fail("Linux driver backend is not available; system X11/XTest input is available when DISPLAY permits it");
    }
    return with_display([&](Display* display, const XTestApi& xtest) {
        XWarpPointer(display, None, DefaultRootWindow(display), 0, 0, 0, 0, points.front().x, points.front().y);
        xtest.fake_button(display, 1, True, CurrentTime);
        for (const auto& point : points) {
            XWarpPointer(display, None, DefaultRootWindow(display), 0, 0, 0, 0, point.x, point.y);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        xtest.fake_button(display, 1, False, CurrentTime);
        return ok("mouse drag sent");
    });
#else
    return fail("Linux X11/XTest input support was not compiled in");
#endif
#endif
}

OperationResult mouse_action(const MouseOptions& options) {
    const std::string click = lower_copy(options.click);
    if (!supported_mouse_click(click)) {
        return fail("click must be none, left, right, middle, left-down, left-up, right-down, right-up, middle-down, or middle-up");
    }
    const std::string selected_backend = lower_copy(options.backend.empty() ? "auto" : options.backend);
    if (!supported_backend(selected_backend)) {
        return fail("backend must be auto, driver, or system");
    }

#ifdef _WIN32
    if (selected_backend != "system") {
        IbInputSimulator simulator;
        if (!simulator.available() && selected_backend == "driver") {
            return fail("IbInputSimulator is not available");
        }
        if (simulator.available()) {
            if (options.absolute) {
                const auto [x, y] = normalized_absolute_position(options.x, options.y);
                if (!simulator.mouse_move(x, y, 0)) {
                    return fail("IbInputSimulator mouse absolute move failed");
                }
            } else if ((options.dx != 0 || options.dy != 0) && !simulator.mouse_move(options.dx, options.dy, 1)) {
                return fail("IbInputSimulator mouse relative move failed");
            }
            if (click != "none") {
                if (!simulator.mouse_click(ib_mouse_button_for_click(click))) {
                    return fail("IbInputSimulator mouse click failed");
                }
            }
            return ok("mouse input sent through IbInputSimulator");
        }
    }

    std::vector<INPUT> inputs;
    if (options.absolute) {
        const auto [x, y] = normalized_absolute_position(options.x, options.y);
        INPUT move{};
        move.type = INPUT_MOUSE;
        move.mi.dx = x;
        move.mi.dy = y;
        move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        inputs.push_back(move);
    } else if (options.dx != 0 || options.dy != 0) {
        INPUT move{};
        move.type = INPUT_MOUSE;
        move.mi.dx = options.dx;
        move.mi.dy = options.dy;
        move.mi.dwFlags = MOUSEEVENTF_MOVE;
        inputs.push_back(move);
    }
    if (click != "none") {
        const DWORD flags = sendinput_mouse_flags_for_click(click);
        if ((flags & MOUSEEVENTF_LEFTDOWN) != 0 || (flags & MOUSEEVENTF_RIGHTDOWN) != 0 || (flags & MOUSEEVENTF_MIDDLEDOWN) != 0) {
            INPUT down_input{};
            down_input.type = INPUT_MOUSE;
            down_input.mi.dwFlags = flags & (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_MIDDLEDOWN);
            inputs.push_back(down_input);
        }
        if ((flags & MOUSEEVENTF_LEFTUP) != 0 || (flags & MOUSEEVENTF_RIGHTUP) != 0 || (flags & MOUSEEVENTF_MIDDLEUP) != 0) {
            INPUT up_input{};
            up_input.type = INPUT_MOUSE;
            up_input.mi.dwFlags = flags & (MOUSEEVENTF_LEFTUP | MOUSEEVENTF_RIGHTUP | MOUSEEVENTF_MIDDLEUP);
            inputs.push_back(up_input);
        }
    }
    if (inputs.empty()) {
        return ok("mouse input empty");
    }
    if (SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) != inputs.size()) {
        return fail("SendInput mouse failed");
    }
    return ok("mouse input sent");
#else
#ifdef KISEKI_HAS_X11
    if (selected_backend == "driver") {
        return fail("Linux driver backend is not available; system X11/XTest input is available when DISPLAY permits it");
    }
    return with_display([&](Display* display, const XTestApi& xtest) {
        if (options.absolute) {
            XWarpPointer(display, None, DefaultRootWindow(display), 0, 0, 0, 0, options.x, options.y);
        } else if (options.dx != 0 || options.dy != 0) {
            xtest.fake_motion(display, options.dx, options.dy, CurrentTime);
        }
        if (click != "none") {
            unsigned int button = 1;
            if (click.find("right") == 0) button = 3;
            if (click.find("middle") == 0) button = 2;
            if (click == "left" || click == "right" || click == "middle" || click.ends_with("-down")) {
                xtest.fake_button(display, button, True, CurrentTime);
            }
            if (click == "left" || click == "right" || click == "middle" || click.ends_with("-up")) {
                xtest.fake_button(display, button, False, CurrentTime);
            }
        }
        return ok("mouse input sent");
    });
#else
    return fail("Linux X11/XTest input support was not compiled in");
#endif
#endif
}

OperationResult background_type_text(const kiseki::platform::target::TargetQuery& target, const std::string& text) {
    if (text.empty()) {
        return ok("background text input empty");
    }

#ifdef _WIN32
    const std::wstring wide = utf8_to_utf16(text);
    if (wide.empty()) {
        return fail("failed to decode text");
    }

    return with_resolved_hwnd(target, [&](HWND hwnd) {
        HWND receiver = keyboard_message_target(hwnd);
        for (const wchar_t c : wide) {
            if (PostMessageW(receiver, WM_CHAR, static_cast<WPARAM>(c), 1) == 0) {
                return fail("PostMessage text failed");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return ok("background text input sent");
    });
#else
#ifdef KISEKI_HAS_X11
    return with_target_window(target, [&](Display* display, Window window) {
        for (const unsigned char c : text) {
            if (c > 0x7f) {
                return fail("Linux X11 background text currently supports ASCII text");
            }
            const auto result = send_background_key_x11(display, window, x11_key_name_for_char(static_cast<char>(c)));
            if (!result.ok) {
                return result;
            }
        }
        return ok("background text input sent");
    });
#else
    return fail("Linux X11 background input support was not compiled in");
#endif
#endif
}

OperationResult background_tap_key(const kiseki::platform::target::TargetQuery& target, const std::string& key) {
    if (key.empty()) {
        return fail("no key specified");
    }

#ifdef _WIN32
    const auto vk = virtual_key_for_name(key);
    if (vk == 0) {
        return fail("unsupported key: " + key);
    }

    return with_resolved_hwnd(target, [&](HWND hwnd) {
        HWND receiver = keyboard_message_target(hwnd);
        if (PostMessageW(receiver, WM_KEYDOWN, vk, key_lparam(vk, false)) == 0) {
            return fail("PostMessage key down failed");
        }
        if (PostMessageW(receiver, WM_KEYUP, vk, key_lparam(vk, true)) == 0) {
            return fail("PostMessage key up failed");
        }
        return ok("background key input sent");
    });
#else
#ifdef KISEKI_HAS_X11
    return with_target_window(target, [&](Display* display, Window window) {
        return send_background_key_x11(display, window, key);
    });
#else
    return fail("Linux X11 background input support was not compiled in");
#endif
#endif
}

OperationResult background_mouse_action(const BackgroundMouseOptions& options) {
    const std::string click = lower_copy(options.click);
    if (!supported_mouse_click(click)) {
        return fail("click must be none, left, right, middle, left-down, left-up, right-down, right-up, middle-down, or middle-up");
    }
    if (options.x < 0 || options.y < 0) {
        return fail("background mouse coordinates must be non-negative client coordinates");
    }

#ifdef _WIN32
    return with_resolved_hwnd(options.target, [&](HWND hwnd) {
        POINT point{options.x, options.y};
        HWND receiver = mouse_message_target(hwnd, point);
        if (PostMessageW(receiver, WM_MOUSEMOVE, 0, mouse_lparam(point)) == 0) {
            return fail("PostMessage mouse move failed");
        }
        if (click == "none") {
            return ok("background mouse input sent");
        }
        if (click.find("left") == 0) {
            return post_mouse_button(receiver, WM_LBUTTONDOWN, WM_LBUTTONUP, MK_LBUTTON, point, click);
        }
        if (click.find("right") == 0) {
            return post_mouse_button(receiver, WM_RBUTTONDOWN, WM_RBUTTONUP, MK_RBUTTON, point, click);
        }
        return post_mouse_button(receiver, WM_MBUTTONDOWN, WM_MBUTTONUP, MK_MBUTTON, point, click);
    });
#else
#ifdef KISEKI_HAS_X11
    return with_target_window(options.target, [&](Display* display, Window window) {
        XMotionEvent motion{};
        motion.type = MotionNotify;
        motion.display = display;
        motion.window = window;
        motion.root = DefaultRootWindow(display);
        motion.time = CurrentTime;
        motion.x = options.x;
        motion.y = options.y;
        motion.x_root = options.x;
        motion.y_root = options.y;
        motion.same_screen = True;
        if (XSendEvent(display, window, True, PointerMotionMask, reinterpret_cast<XEvent*>(&motion)) == 0) {
            return fail("XSendEvent mouse move failed");
        }
        if (click == "none") {
            return ok("background mouse input sent");
        }

        unsigned int button = 1;
        if (click.find("right") == 0) button = 3;
        if (click.find("middle") == 0) button = 2;

        XButtonEvent button_event{};
        button_event.display = display;
        button_event.window = window;
        button_event.root = DefaultRootWindow(display);
        button_event.time = CurrentTime;
        button_event.x = options.x;
        button_event.y = options.y;
        button_event.x_root = options.x;
        button_event.y_root = options.y;
        button_event.same_screen = True;
        button_event.button = button;

        if (click == "left" || click == "right" || click == "middle" || click.ends_with("-down")) {
            button_event.type = ButtonPress;
            if (XSendEvent(display, window, True, ButtonPressMask, reinterpret_cast<XEvent*>(&button_event)) == 0) {
                return fail("XSendEvent mouse down failed");
            }
        }
        if (click == "left" || click == "right" || click == "middle" || click.ends_with("-up")) {
            button_event.type = ButtonRelease;
            if (XSendEvent(display, window, True, ButtonReleaseMask, reinterpret_cast<XEvent*>(&button_event)) == 0) {
                return fail("XSendEvent mouse up failed");
            }
        }
        return ok("background mouse input sent");
    });
#else
    return fail("Linux X11 background input support was not compiled in");
#endif
#endif
}

}
