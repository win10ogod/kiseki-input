#include "platform/input/input.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <limits>
#include <memory>
#include <tuple>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "platform/input/sequence_support.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#else
#ifdef KISEKI_HAS_X11
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <cstdlib>
#endif
#endif

namespace kiseki::platform::input {
#ifdef __APPLE__
double mac_double_click_interval();
#endif

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
    return click == "none" || click == "left" || click == "right" || click == "middle" || click == "left-down" ||
           click == "left-up" || click == "right-down" || click == "right-up" || click == "middle-down" ||
           click == "middle-up" || click == "x1" || click == "x2" || click == "x1-down" || click == "x1-up" ||
           click == "x2-down" || click == "x2-up";
}

OperationResult validate_drag_timing(const std::vector<MousePoint> &points, int step_delay_ms, int start_hold_ms,
                                     int end_hold_ms) {
    if (points.size() < 2)
        return fail("mouse drag requires at least two points");
    if (step_delay_ms < 0 || start_hold_ms < 0 || end_hold_ms < 0)
        return fail("mouse drag delays must be non-negative");
    const bool timed = points.front().time_ms >= 0;
    std::int64_t previous = -1;
    for (const auto &point : points) {
        if (point.time_ms < -1 || (point.time_ms >= 0) != timed || (timed && point.time_ms < previous))
            return fail("drag timestamps must be present for every point and non-decreasing");
        previous = point.time_ms;
    }
    if (timed && points.front().time_ms != 0)
        return fail("first drag timestamp must be 0");
    const auto horizon = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::time_point::max() - std::chrono::steady_clock::now())
                             .count() -
                         static_cast<std::int64_t>(start_hold_ms) - end_hold_ms;
    if ((timed && points.back().time_ms > horizon) ||
        (!timed && step_delay_ms > 0 && points.size() > static_cast<std::uint64_t>(horizon / step_delay_ms)))
        return fail("drag duration exceeds monotonic clock range");
    return ok("");
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
using IbSendMouseWheelFn = bool(__stdcall *)(int);

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
        mouse_wheel_ = reinterpret_cast<IbSendMouseWheelFn>(GetProcAddress(dll_, "IbSendMouseWheel"));
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
    bool mouse_wheel(int delta) const {
        return mouse_wheel_ != nullptr && mouse_wheel_(delta);
    }

private:
    HMODULE dll_ = nullptr;
    IbSendInitFn init_ = nullptr;
    IbSendDestroyFn destroy_ = nullptr;
    IbSendKeybdDownFn key_down_ = nullptr;
    IbSendKeybdUpFn key_up_ = nullptr;
    IbSendMouseMoveFn mouse_move_ = nullptr;
    IbSendMouseClickFn mouse_click_ = nullptr;
    IbSendMouseWheelFn mouse_wheel_ = nullptr;
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
    if (name == "shift" || name == "lshift")
        return VK_LSHIFT;
    if (name == "rshift")
        return VK_RSHIFT;
    if (name == "ctrl" || name == "control" || name == "lctrl")
        return VK_LCONTROL;
    if (name == "rctrl")
        return VK_RCONTROL;
    if (name == "alt" || name == "option" || name == "lalt")
        return VK_LMENU;
    if (name == "ralt" || name == "altgr")
        return VK_RMENU;
    if (name == "win" || name == "super" || name == "meta") return VK_LWIN;
    if (name == "left") return VK_LEFT;
    if (name == "right") return VK_RIGHT;
    if (name == "up") return VK_UP;
    if (name == "down") return VK_DOWN;
    if (name == "rwin" || name == "rmeta")
        return VK_RWIN;
    if (name == "delete" || name == "del" || name == "forward-delete")
        return VK_DELETE;
    if (name == "home")
        return VK_HOME;
    if (name == "end")
        return VK_END;
    if (name == "pageup" || name == "pgup")
        return VK_PRIOR;
    if (name == "pagedown" || name == "pgdn")
        return VK_NEXT;
    if (name == "insert" || name == "ins")
        return VK_INSERT;
    if (name == "capslock")
        return VK_CAPITAL;
    if (name == "numlock")
        return VK_NUMLOCK;
    if (name == "scrolllock")
        return VK_SCROLL;
    if (name == "pause")
        return VK_PAUSE;
    if (name == "printscreen")
        return VK_SNAPSHOT;
    if (name == "menu" || name == "apps")
        return VK_APPS;
    if (name == "minus" || name == "-")
        return VK_OEM_MINUS;
    if (name == "equal" || name == "=")
        return VK_OEM_PLUS;
    if (name == "leftbracket" || name == "[")
        return VK_OEM_4;
    if (name == "rightbracket" || name == "]")
        return VK_OEM_6;
    if (name == "semicolon" || name == ";")
        return VK_OEM_1;
    if (name == "quote" || name == "'")
        return VK_OEM_7;
    if (name == "backslash" || name == "\\")
        return VK_OEM_5;
    if (name == "comma" || name == ",")
        return VK_OEM_COMMA;
    if (name == "period" || name == ".")
        return VK_OEM_PERIOD;
    if (name == "slash" || name == "/")
        return VK_OEM_2;
    if (name == "grave" || name == "`")
        return VK_OEM_3;
    if (name == "numpad-enter")
        return VK_RETURN;
    if (name == "numpad-add")
        return VK_ADD;
    if (name == "numpad-subtract")
        return VK_SUBTRACT;
    if (name == "numpad-multiply")
        return VK_MULTIPLY;
    if (name == "numpad-divide")
        return VK_DIVIDE;
    if (name == "numpad-decimal")
        return VK_DECIMAL;
    if (name.size() == 7 && name.starts_with("numpad") && std::isdigit(static_cast<unsigned char>(name[6])))
        return VK_NUMPAD0 + name[6] - '0';
    if (name.size() >= 2 && name[0] == 'f') {
        if (!std::all_of(name.begin() + 1, name.end(), [](unsigned char c) { return std::isdigit(c); }))
            return 0;
        const int number = std::atoi(name.c_str() + 1);
        if (number >= 1 && number <= 24) {
            return static_cast<unsigned short>(VK_F1 + number - 1);
        }
    }
    return 0;
}

bool extended_virtual_key(unsigned short vk) {
    // Some layouts return the base scan even for MAPVK_VK_TO_VSC_EX. These
    // physical keys retain their E0 identity independently of the active layout.
    switch (vk) {
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_SNAPSHOT:
    case VK_DIVIDE:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
        return true;
    default:
        return false;
    }
}

bool send_key_event(unsigned short vk, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    const DWORD thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, GetKeyboardLayout(thread));
    input.ki.wScan = static_cast<WORD>(scan & 0xff);
    input.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) |
                       (((scan & 0xff00) == 0xe000 || extended_virtual_key(vk)) ? KEYEVENTF_EXTENDEDKEY : 0);
    return SendInput(1, &input, sizeof(INPUT)) == 1;
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
    const UINT sent = SendInput(2, inputs, sizeof(INPUT));
    if (sent == 1) {
        // The key-down was inserted but the pair was only partially accepted.
        if (SendInput(1, &inputs[1], sizeof(INPUT)) != 1)
            return false;
    }
    return sent == 2;
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

LPARAM key_lparam(unsigned short vk, bool release, bool numpad_enter = false) {
    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    LPARAM lparam = 1 | (static_cast<LPARAM>(scan & 0xff) << 16);
    if ((scan & 0xff00) == 0xe000 || extended_virtual_key(vk) || numpad_enter)
        lparam |= 1LL << 24;
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

POINT map_parent_point_to_receiver(HWND parent, HWND receiver, const MousePoint& source) {
    POINT point{source.x, source.y};
    if (receiver != parent) {
        MapWindowPoints(parent, receiver, &point, 1);
    }
    return point;
}

#elif defined(__APPLE__)

struct MacKey {
    CGKeyCode code = 0;
    CGEventFlags modifier_flag = 0;
    bool modifier = false;
};

std::optional<MacKey> mac_key_for_name(const std::string& key) {
    const std::string name = lower_copy(key);
    if (name.size() == 1) {
        switch (name[0]) {
            case 'a': return MacKey{0};
            case 's': return MacKey{1};
            case 'd': return MacKey{2};
            case 'f': return MacKey{3};
            case 'h': return MacKey{4};
            case 'g': return MacKey{5};
            case 'z': return MacKey{6};
            case 'x': return MacKey{7};
            case 'c': return MacKey{8};
            case 'v': return MacKey{9};
            case 'b': return MacKey{11};
            case 'q': return MacKey{12};
            case 'w': return MacKey{13};
            case 'e': return MacKey{14};
            case 'r': return MacKey{15};
            case 'y': return MacKey{16};
            case 't': return MacKey{17};
            case '1': return MacKey{18};
            case '2': return MacKey{19};
            case '3': return MacKey{20};
            case '4': return MacKey{21};
            case '6': return MacKey{22};
            case '5': return MacKey{23};
            case '9': return MacKey{25};
            case '7': return MacKey{26};
            case '8': return MacKey{28};
            case '0': return MacKey{29};
            case 'o': return MacKey{31};
            case 'u': return MacKey{32};
            case 'i': return MacKey{34};
            case 'p': return MacKey{35};
            case 'l': return MacKey{37};
            case 'j': return MacKey{38};
            case 'k': return MacKey{40};
            case 'n': return MacKey{45};
            case 'm': return MacKey{46};
            default: break;
        }
    }

    if (name == "enter" || name == "return") return MacKey{36};
    if (name == "esc" || name == "escape") return MacKey{53};
    if (name == "space") return MacKey{49};
    if (name == "tab") return MacKey{48};
    if (name == "backspace" || name == "delete") return MacKey{51};
    if (name == "forward-delete" || name == "del") return MacKey{117};
    if (name == "left") return MacKey{123};
    if (name == "right") return MacKey{124};
    if (name == "down") return MacKey{125};
    if (name == "up") return MacKey{126};
    if (name == "home") return MacKey{115};
    if (name == "end") return MacKey{119};
    if (name == "pageup") return MacKey{116};
    if (name == "pagedown") return MacKey{121};
    if (name == "minus") return MacKey{27};
    if (name == "equal") return MacKey{24};
    if (name == "leftbracket") return MacKey{33};
    if (name == "rightbracket") return MacKey{30};
    if (name == "semicolon") return MacKey{41};
    if (name == "quote") return MacKey{39};
    if (name == "backslash") return MacKey{42};
    if (name == "comma") return MacKey{43};
    if (name == "period") return MacKey{47};
    if (name == "slash") return MacKey{44};
    if (name == "grave") return MacKey{50};
    if (name == "shift" || name == "lshift")
        return MacKey{56, kCGEventFlagMaskShift, true};
    if (name == "rshift")
        return MacKey{60, kCGEventFlagMaskShift, true};
    if (name == "ctrl" || name == "control" || name == "lctrl")
        return MacKey{59, kCGEventFlagMaskControl, true};
    if (name == "rctrl")
        return MacKey{62, kCGEventFlagMaskControl, true};
    if (name == "alt" || name == "option" || name == "lalt")
        return MacKey{58, kCGEventFlagMaskAlternate, true};
    if (name == "ralt" || name == "altgr" || name == "roption")
        return MacKey{61, kCGEventFlagMaskAlternate, true};
    if (name == "rwin" || name == "rmeta" || name == "rcmd")
        return MacKey{54, kCGEventFlagMaskCommand, true};
    if (name == "capslock")
        return MacKey{57, kCGEventFlagMaskAlphaShift, true};
    if (name == "numpad-enter")
        return MacKey{76};
    if (name == "numpad-add")
        return MacKey{69};
    if (name == "numpad-subtract")
        return MacKey{78};
    if (name == "numpad-multiply")
        return MacKey{67};
    if (name == "numpad-divide")
        return MacKey{75};
    if (name == "numpad-decimal")
        return MacKey{65};
    if (name.size() == 7 && name.starts_with("numpad") && std::isdigit(static_cast<unsigned char>(name[6]))) {
        static constexpr CGKeyCode codes[]{82, 83, 84, 85, 86, 87, 88, 89, 91, 92};
        return MacKey{codes[name[6] - '0']};
    }
    if (name == "cmd" || name == "command" || name == "win" || name == "super" || name == "meta") {
        return MacKey{55, kCGEventFlagMaskCommand, true};
    }
    if (name.size() >= 2 && name[0] == 'f') {
        if (!std::all_of(name.begin() + 1, name.end(), [](unsigned char c) { return std::isdigit(c); }))
            return std::nullopt;
        const int number = std::atoi(name.c_str() + 1);
        if (number >= 1 && number <= 20) {
            static constexpr CGKeyCode function_keys[] = {
                122, 120, 99, 118, 96, 97, 98, 100, 101, 109,
                103, 111, 105, 107, 113, 106, 64, 79, 80, 90,
            };
            return MacKey{function_keys[number - 1]};
        }
    }
    return std::nullopt;
}

bool post_mac_key_event(const MacKey& key, bool down, CGEventFlags flags) {
    CGEventRef event = CGEventCreateKeyboardEvent(nullptr, key.code, down);
    if (event == nullptr) {
        return false;
    }
    CGEventSetFlags(event, flags);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return true;
}

OperationResult require_mac_accessibility() {
    if (AXIsProcessTrusted()) {
        return ok("accessibility trusted");
    }

    const void* keys[] = {kAXTrustedCheckOptionPrompt};
    const void* values[] = {kCFBooleanTrue};
    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    const bool trusted = options != nullptr && AXIsProcessTrustedWithOptions(options);
    if (options != nullptr) {
        CFRelease(options);
    }
    return trusted ? ok("accessibility trusted")
                   : fail("macOS Accessibility permission is required for global input; approve the prompt in System Settings and rerun");
}

OperationResult send_text_with_cgevent(const std::string& text) {
    const auto trust = require_mac_accessibility();
    if (!trust.ok) {
        return trust;
    }

    CFStringRef value = CFStringCreateWithCString(kCFAllocatorDefault, text.c_str(), kCFStringEncodingUTF8);
    if (value == nullptr) {
        return fail("failed to decode text");
    }

    const CFIndex length = CFStringGetLength(value);
    std::vector<UniChar> characters(static_cast<std::size_t>(length));
    if (length > 0) {
        CFStringGetCharacters(value, CFRangeMake(0, length), characters.data());
    }
    CFRelease(value);

    constexpr CFIndex chunk_size = 64;
    for (CFIndex offset = 0; offset < length; offset += chunk_size) {
        const auto chunk = static_cast<UniCharCount>(std::min(chunk_size, length - offset));
        CGEventRef down = CGEventCreateKeyboardEvent(nullptr, 0, true);
        CGEventRef up = CGEventCreateKeyboardEvent(nullptr, 0, false);
        if (down == nullptr || up == nullptr) {
            if (down != nullptr) CFRelease(down);
            if (up != nullptr) CFRelease(up);
            return fail("CGEventCreateKeyboardEvent text failed");
        }
        CGEventKeyboardSetUnicodeString(down, chunk, characters.data() + offset);
        CGEventKeyboardSetUnicodeString(up, chunk, characters.data() + offset);
        CGEventPost(kCGHIDEventTap, down);
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(down);
        CFRelease(up);
        if (!wait_input_ms(1)) return fail("input cancelled");
    }
    return ok("text input sent through macOS CGEvent");
}

CGPoint current_mouse_location() {
    CGEventRef event = CGEventCreate(nullptr);
    if (event == nullptr) {
        return CGPointMake(0.0, 0.0);
    }
    const CGPoint point = CGEventGetLocation(event);
    CFRelease(event);
    return point;
}

OperationResult mac_background_input_unavailable() {
    return fail("macOS target-window background input backend is not implemented; use global input or a target-specific automation backend");
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
    static const std::map<std::string, KeySym> extra{
        {"lshift", XK_Shift_L},
        {"rshift", XK_Shift_R},
        {"lctrl", XK_Control_L},
        {"rctrl", XK_Control_R},
        {"lalt", XK_Alt_L},
        {"ralt", XK_Alt_R},
        {"option", XK_Alt_L},
        {"altgr", XK_ISO_Level3_Shift},
        {"rwin", XK_Super_R},
        {"rmeta", XK_Super_R},
        {"delete", XK_Delete},
        {"del", XK_Delete},
        {"forward-delete", XK_Delete},
        {"home", XK_Home},
        {"end", XK_End},
        {"pageup", XK_Page_Up},
        {"pagedown", XK_Page_Down},
        {"pgup", XK_Page_Up},
        {"pgdn", XK_Page_Down},
        {"insert", XK_Insert},
        {"capslock", XK_Caps_Lock},
        {"numlock", XK_Num_Lock},
        {"scrolllock", XK_Scroll_Lock},
        {"pause", XK_Pause},
        {"printscreen", XK_Print},
        {"menu", XK_Menu},
        {"minus", XK_minus},
        {"equal", XK_equal},
        {"leftbracket", XK_bracketleft},
        {"rightbracket", XK_bracketright},
        {"semicolon", XK_semicolon},
        {"quote", XK_apostrophe},
        {"backslash", XK_backslash},
        {"comma", XK_comma},
        {"period", XK_period},
        {"slash", XK_slash},
        {"grave", XK_grave},
        {"numpad-enter", XK_KP_Enter},
        {"numpad-add", XK_KP_Add},
        {"numpad-subtract", XK_KP_Subtract},
        {"numpad-multiply", XK_KP_Multiply},
        {"numpad-divide", XK_KP_Divide},
        {"numpad-decimal", XK_KP_Decimal},
    };
    if (auto it = extra.find(name); it != extra.end())
        return it->second;
    if (name.size() == 7 && name.starts_with("numpad") && std::isdigit(static_cast<unsigned char>(name[6])))
        return XK_KP_0 + name[6] - '0';
    if (name.size() >= 2 && name[0] == 'f') {
        if (!std::all_of(name.begin() + 1, name.end(), [](unsigned char c) { return std::isdigit(c); }))
            return NoSymbol;
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

struct X11TextKey {
    KeySym keysym = NoSymbol;
    bool shift = false;
};

X11TextKey x11_text_key_for_char(unsigned char c) {
    if (c >= 'a' && c <= 'z') return {static_cast<KeySym>(XK_a + (c - 'a')), false};
    if (c >= 'A' && c <= 'Z') return {static_cast<KeySym>(XK_a + (c - 'A')), true};
    if (c >= '0' && c <= '9') return {static_cast<KeySym>(XK_0 + (c - '0')), false};

    switch (c) {
        case '\n':
        case '\r': return {XK_Return, false};
        case '\t': return {XK_Tab, false};
        case ' ': return {XK_space, false};
        case '-': return {XK_minus, false};
        case '_': return {XK_minus, true};
        case '=': return {XK_equal, false};
        case '+': return {XK_equal, true};
        case '[': return {XK_bracketleft, false};
        case '{': return {XK_bracketleft, true};
        case ']': return {XK_bracketright, false};
        case '}': return {XK_bracketright, true};
        case '\\': return {XK_backslash, false};
        case '|': return {XK_backslash, true};
        case ';': return {XK_semicolon, false};
        case ':': return {XK_semicolon, true};
        case '\'': return {XK_apostrophe, false};
        case '"': return {XK_apostrophe, true};
        case ',': return {XK_comma, false};
        case '<': return {XK_comma, true};
        case '.': return {XK_period, false};
        case '>': return {XK_period, true};
        case '/': return {XK_slash, false};
        case '?': return {XK_slash, true};
        case '`': return {XK_grave, false};
        case '~': return {XK_grave, true};
        case '!': return {XK_1, true};
        case '@': return {XK_2, true};
        case '#': return {XK_3, true};
        case '$': return {XK_4, true};
        case '%': return {XK_5, true};
        case '^': return {XK_6, true};
        case '&': return {XK_7, true};
        case '*': return {XK_8, true};
        case '(': return {XK_9, true};
        case ')': return {XK_0, true};
        default: return {};
    }
}

std::string unsupported_x11_text_byte(unsigned char c) {
    std::ostringstream stream;
    stream << "unsupported Linux X11 text byte: 0x" << std::hex << static_cast<int>(c);
    return stream.str();
}

OperationResult send_text_with_xtest(std::string_view text) {
    return with_display([&](Display* display, const XTestApi& xtest) {
        const KeyCode shift = XKeysymToKeycode(display, XK_Shift_L);
        for (const unsigned char c : text) {
            const X11TextKey key = x11_text_key_for_char(c);
            if (key.keysym == NoSymbol) {
                return fail(unsupported_x11_text_byte(c));
            }
            const KeyCode code = XKeysymToKeycode(display, key.keysym);
            if (code == 0 || (key.shift && shift == 0)) {
                return fail(unsupported_x11_text_byte(c));
            }
            if (key.shift) {
                xtest.fake_key(display, shift, True, CurrentTime);
            }
            xtest.fake_key(display, code, True, CurrentTime);
            xtest.fake_key(display, code, False, CurrentTime);
            if (key.shift) {
                xtest.fake_key(display, shift, False, CurrentTime);
            }
            XSync(display, False);
            if (!wait_input_ms(1)) return fail("input cancelled");
        }
        return ok("text input sent");
    });
}

bool send_background_key_event_x11(Display* display, Window window, KeyCode keycode, int type, unsigned int state) {
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
    event.state = state;
    event.type = type;
    const long mask = type == KeyPress ? KeyPressMask : KeyReleaseMask;
    return XSendEvent(display, window, True, mask, reinterpret_cast<XEvent*>(&event)) != 0;
}

OperationResult send_background_text_key_x11(Display* display, Window window, unsigned char c) {
    const X11TextKey key = x11_text_key_for_char(c);
    if (key.keysym == NoSymbol) {
        return fail(unsupported_x11_text_byte(c));
    }
    const KeyCode keycode = XKeysymToKeycode(display, key.keysym);
    const KeyCode shift = XKeysymToKeycode(display, XK_Shift_L);
    if (keycode == 0 || (key.shift && shift == 0)) {
        return fail(unsupported_x11_text_byte(c));
    }

    if (key.shift && !send_background_key_event_x11(display, window, shift, KeyPress, 0)) {
        return fail("XSendEvent shift press failed");
    }
    if (!send_background_key_event_x11(display, window, keycode, KeyPress, key.shift ? ShiftMask : 0)) {
        return fail("XSendEvent key press failed");
    }
    if (!send_background_key_event_x11(display, window, keycode, KeyRelease, key.shift ? ShiftMask : 0)) {
        return fail("XSendEvent key release failed");
    }
    if (key.shift && !send_background_key_event_x11(display, window, shift, KeyRelease, ShiftMask)) {
        return fail("XSendEvent shift release failed");
    }
    return ok("background key input sent");
}
#endif
#endif

// Native resources stay alive for the invocation so a sequence cannot change driver
// or X11 connection between a down and its matching up.
#ifdef _WIN32
IbInputSimulator &driver() {
    static thread_local IbInputSimulator value;
    return value;
}
#elif defined(KISEKI_HAS_X11) && !defined(__APPLE__)
struct NativeX11 {
    Display *display = XOpenDisplay(nullptr);
    XTestApi xtest;
    ~NativeX11() {
        if (display)
            XCloseDisplay(display);
    }
};
NativeX11 &native_x11() {
    static thread_local NativeX11 value;
    return value;
}
#endif
thread_local std::set<std::string> owned_keys;
thread_local std::set<std::string> owned_buttons;
#ifdef _WIN32
struct BackgroundMouseState {
    HWND receiver = nullptr;
    std::set<std::string> buttons;
    POINT last{};
};
thread_local std::map<HWND, BackgroundMouseState> background_mouse_states;
#elif defined(KISEKI_HAS_X11)
struct BackgroundMouseState {
    std::set<std::string> buttons;
    int x = 0;
    int y = 0;
};
thread_local std::map<Window, BackgroundMouseState> background_mouse_states;
#endif

std::string choose_backend(const std::string &requested) {
    const auto name = lower_copy(requested.empty() ? "auto" : requested);
#ifdef _WIN32
    if (name == "auto")
        return driver().available() ? "driver" : "system";
#else
    if (name == "auto")
        return "system";
#endif
    return name;
}
OperationResult check_backend(const std::string &backend) {
    if (!supported_backend(backend))
        return fail("backend must be auto, driver, or system");
#ifdef _WIN32
    if (backend == "driver" && !driver().available())
        return fail("IbInputSimulator is not available");
#elif defined(__APPLE__)
    if (backend == "driver")
        return fail("macOS native input uses the system backend, not driver");
    return require_mac_accessibility();
#elif defined(KISEKI_HAS_X11)
    if (backend == "driver")
        return fail("Linux native input uses the system X11/XTest backend, not driver");
    if (!native_x11().display)
        return fail("XOpenDisplay failed; DISPLAY is not available");
    if (!native_x11().xtest.available())
        return fail("libXtst is not available");
#else
    return fail("Linux X11/XTest input support was not compiled in");
#endif
    return ok("");
}
std::string key_token(const std::string &key, const std::string &backend) {
#ifdef _WIN32
    return backend + ":key:" + std::to_string(virtual_key_for_name(key)) +
           (lower_copy(key) == "numpad-enter" ? ":numpad" : "");
#elif defined(__APPLE__)
    const auto mapped = mac_key_for_name(key);
    return backend + ":key:" + std::to_string(mapped ? mapped->code : 65535);
#elif defined(KISEKI_HAS_X11)
    return backend + ":key:" + std::to_string(keysym_for_name(key));
#else
    return backend + ":key:" + key;
#endif
}
std::string button_token(const std::string &button, const std::string &backend) {
    return backend + ":button:" + button;
}
bool native_key_down(const std::string &key, const std::string &backend) {
    if (owned_keys.contains(key_token(key, backend)))
        return true;
#ifdef _WIN32
    return (GetAsyncKeyState(virtual_key_for_name(key)) & 0x8000) != 0;
#elif defined(__APPLE__)
    const auto mapped = mac_key_for_name(key);
    return mapped && CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, mapped->code);
#elif defined(KISEKI_HAS_X11)
    auto *display = native_x11().display;
    if (!display)
        return false;
    const auto code = XKeysymToKeycode(display, keysym_for_name(key));
    char state[32]{};
    XQueryKeymap(display, state);
    return code != 0 && (state[code / 8] & (1 << (code % 8))) != 0;
#else
    return false;
#endif
}
#ifdef __APPLE__
CGEventFlags mac_device_modifier_flag(CGKeyCode code) {
    switch (code) {
    case 56:
        return 0x2;
    case 60:
        return 0x4;
    case 59:
        return 0x1;
    case 62:
        return 0x2000;
    case 58:
        return 0x20;
    case 61:
        return 0x40;
    case 55:
        return 0x8;
    case 54:
        return 0x10;
    default:
        return 0;
    }
}

CGEventFlags mac_modifier_flags(const std::string &changed = "", bool down = false) {
    CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState);
    flags &= ~(kCGEventFlagMaskShift | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskCommand |
               static_cast<CGEventFlags>(0x207f));
    for (const auto *name : {"lshift", "rshift", "lctrl", "rctrl", "lalt", "ralt", "cmd", "rcmd"}) {
        const auto key = mac_key_for_name(name);
        const bool pressed = !changed.empty() && key_token(name, "system") == key_token(changed, "system")
                                 ? down
                                 : native_key_down(name, "system");
        if (pressed && key)
            flags |= key->modifier_flag | mac_device_modifier_flag(key->code);
    }
    return flags;
}
#endif
OperationResult send_native_key(const std::string &key, bool down, const std::string &backend) {
#ifdef _WIN32
    const auto vk = virtual_key_for_name(key);
    if (backend == "driver") {
        if (lower_copy(key) == "numpad-enter")
            return fail("IbInputSimulator key API cannot distinguish numpad-enter; select --backend system");
        return (down ? driver().key_down(vk) : driver().key_up(vk)) ? ok("")
                                                                    : fail("IbInputSimulator key event failed");
    }
    if (lower_copy(key) != "numpad-enter")
        return send_key_event(vk, down) ? ok("") : fail("SendInput key event failed");
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_RETURN;
    input.ki.wScan = 0x1c;
    input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | (down ? 0 : KEYEVENTF_KEYUP);
    return SendInput(1, &input, sizeof(INPUT)) == 1 ? ok("") : fail("SendInput numpad enter failed");
#elif defined(__APPLE__)
    const auto mapped = mac_key_for_name(key);
    return mapped && post_mac_key_event(*mapped, down, mac_modifier_flags(key, down))
               ? ok("")
               : fail("CGEventCreateKeyboardEvent failed");
#elif defined(KISEKI_HAS_X11)
    auto &x = native_x11();
    const auto code = XKeysymToKeycode(x.display, keysym_for_name(key));
    if (!code)
        return fail("unsupported X11 keycode: " + key);
    const bool sent = x.xtest.fake_key(x.display, code, down, CurrentTime) != 0;
    XFlush(x.display);
    return sent ? ok("") : fail("XTest key event failed");
#else
    return fail("native keyboard input was not compiled");
#endif
}
bool native_button_down(const std::string &button, const std::string &backend) {
    if (owned_buttons.contains(button_token(button, backend)))
        return true;
#ifdef _WIN32
    const int vk = button == "left"     ? VK_LBUTTON
                   : button == "right"  ? VK_RBUTTON
                   : button == "middle" ? VK_MBUTTON
                   : button == "x1"     ? VK_XBUTTON1
                                        : VK_XBUTTON2;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#elif defined(__APPLE__)
    const auto b = button == "left" ? 0 : button == "right" ? 1 : button == "middle" ? 2 : button == "x1" ? 3 : 4;
    return CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, static_cast<CGMouseButton>(b));
#elif defined(KISEKI_HAS_X11)
    auto *d = native_x11().display;
    if (!d)
        return false;
    Window root, child;
    int rx, ry, x, y;
    unsigned mask;
    if (!XQueryPointer(d, DefaultRootWindow(d), &root, &child, &rx, &ry, &x, &y, &mask))
        return false;
    const unsigned m = button == "left"     ? Button1Mask
                       : button == "middle" ? Button2Mask
                       : button == "right"  ? Button3Mask
                                            : 0;
    return (mask & m) != 0;
#else
    return false;
#endif
}
OperationResult move_native_mouse(const MouseOptions &options, const std::string &backend) {
#ifdef _WIN32
    if (backend == "driver") {
        auto position = options.absolute ? normalized_absolute_position(options.x, options.y)
                                         : std::pair<int, int>{options.dx, options.dy};
        return driver().mouse_move(position.first, position.second, options.absolute ? 0 : 1)
                   ? ok("")
                   : fail("IbInputSimulator mouse move failed");
    }
    INPUT input{};
    input.type = INPUT_MOUSE;
    if (options.absolute) {
        const auto [x, y] = normalized_absolute_position(options.x, options.y);
        input.mi.dx = x;
        input.mi.dy = y;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    } else {
        input.mi.dx = options.dx;
        input.mi.dy = options.dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
    }
    return SendInput(1, &input, sizeof(INPUT)) == 1 ? ok("") : fail("SendInput mouse move failed");
#elif defined(__APPLE__)
    CGPoint point = options.absolute ? CGPointMake(options.x, options.y) : current_mouse_location();
    if (!options.absolute) {
        point.x += options.dx;
        point.y += options.dy;
    }
    CGMouseButton button = kCGMouseButtonLeft;
    CGEventType type = kCGEventMouseMoved;
    for (const auto *name : {"left", "right", "middle", "x1", "x2"}) {
        if (native_button_down(name, backend)) {
            button = static_cast<CGMouseButton>(std::string{name} == "left"     ? 0
                                                : std::string{name} == "right"  ? 1
                                                : std::string{name} == "middle" ? 2
                                                : std::string{name} == "x1"     ? 3
                                                                                : 4);
            type = button == 0   ? kCGEventLeftMouseDragged
                   : button == 1 ? kCGEventRightMouseDragged
                                 : kCGEventOtherMouseDragged;
            break;
        }
    }
    CGEventRef event = CGEventCreateMouseEvent(nullptr, type, point, button);
    if (!event)
        return fail("CGEventCreateMouseEvent move failed");
    CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, button);
    CGEventSetFlags(event, mac_modifier_flags());
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return ok("");
#elif defined(KISEKI_HAS_X11)
    auto &x = native_x11();
    bool sent = true;
    if (options.absolute)
        XWarpPointer(x.display, None, DefaultRootWindow(x.display), 0, 0, 0, 0, options.x, options.y);
    else
        sent = x.xtest.fake_motion(x.display, options.dx, options.dy, CurrentTime) != 0;
    XFlush(x.display);
    return sent ? ok("") : fail("XTest mouse move failed");
#else
    return fail("native mouse input was not compiled");
#endif
}
OperationResult send_native_button(const std::string &button, bool down, int count, const std::string &backend,
                                   std::optional<MousePoint> position = std::nullopt) {
#ifdef _WIN32
    DWORD flag = sendinput_mouse_flags_for_click(button + (down ? "-down" : "-up"));
    DWORD data = 0;
    if (button == "x1" || button == "x2") {
        flag = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
        data = button == "x1" ? XBUTTON1 : XBUTTON2;
    }
    if (backend == "driver")
        return driver().mouse_click(flag | data) ? ok("") : fail("IbInputSimulator mouse button failed");
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    input.mi.mouseData = data;
    return SendInput(1, &input, sizeof(INPUT)) == 1 ? ok("") : fail("SendInput mouse button failed");
#elif defined(__APPLE__)
    const auto b = button == "left" ? 0 : button == "right" ? 1 : button == "middle" ? 2 : button == "x1" ? 3 : 4;
    const auto type = b == 0   ? (down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp)
                      : b == 1 ? (down ? kCGEventRightMouseDown : kCGEventRightMouseUp)
                               : (down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp);
    const auto point = position ? CGPointMake(position->x, position->y) : current_mouse_location();
    CGEventRef event = CGEventCreateMouseEvent(nullptr, type, point, static_cast<CGMouseButton>(b));
    if (!event)
        return fail("CGEventCreateMouseEvent button failed");
    CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, b);
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, count);
    CGEventSetFlags(event, mac_modifier_flags());
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return ok("");
#elif defined(KISEKI_HAS_X11)
    auto &x = native_x11();
    const unsigned b = button == "left" ? 1 : button == "right" ? 3 : button == "middle" ? 2 : button == "x1" ? 8 : 9;
    const bool sent = x.xtest.fake_button(x.display, b, down, CurrentTime) != 0;
    XFlush(x.display);
    return sent ? ok("") : fail("XTest mouse button failed");
#else
    return fail("native mouse input was not compiled");
#endif
}
OperationResult send_native_wheel(int vertical, int horizontal, const std::string &backend) {
#ifdef _WIN32
    if (backend == "driver") {
        if (horizontal != 0)
            return fail("IbInputSimulator has no horizontal wheel API; select --backend system");
        return driver().mouse_wheel(vertical) ? ok("") : fail("IbInputSimulator wheel event failed");
    }
    for (const auto [delta, flag] :
         {std::pair<int, DWORD>{vertical, MOUSEEVENTF_WHEEL}, {horizontal, MOUSEEVENTF_HWHEEL}}) {
        if (!delta)
            continue;
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = flag;
        input.mi.mouseData = static_cast<DWORD>(delta);
        if (SendInput(1, &input, sizeof(INPUT)) != 1)
            return fail("SendInput wheel failed");
    }
    return ok("");
#elif defined(__APPLE__)
    CGEventRef event = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitPixel, 2, vertical, -horizontal);
    if (!event)
        return fail("CGEventCreateScrollWheelEvent failed");
    CGEventSetFlags(event, mac_modifier_flags());
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return ok("");
#elif defined(KISEKI_HAS_X11)
    auto &x = native_x11();
    static thread_local long long remainder_y = 0, remainder_x = 0;
    remainder_y += vertical;
    remainder_x += horizontal;
    for (auto [remainder, positive, negative] :
         {std::tuple<long long *, unsigned, unsigned>{&remainder_y, 4, 5}, {&remainder_x, 7, 6}}) {
        while (*remainder >= 120 || *remainder <= -120) {
            const unsigned b = *remainder > 0 ? positive : negative;
            if (!x.xtest.fake_button(x.display, b, True, CurrentTime))
                return fail("XTest wheel down failed");
            if (!x.xtest.fake_button(x.display, b, False, CurrentTime))
                return fail("XTest wheel up failed");
            *remainder += *remainder > 0 ? -120 : 120;
            XFlush(x.display);
        }
    }
    return ok("");
#else
    return fail("native wheel input was not compiled");
#endif
}
OperationResult button_action(const std::string &button, bool down, int count, const std::string &backend,
                              bool cleanup_only = false, std::optional<MousePoint> position = std::nullopt) {
    const auto token = button_token(button, backend);
    if (cleanup_only && !owned_buttons.contains(token))
        return ok("");
    const bool was_down = native_button_down(button, backend);
    if (down && was_down)
        return ok("");
    const auto result = send_native_button(button, down, count, backend, position);
    if (result.ok) {
        if (down && !was_down)
            owned_buttons.insert(token);
        if (!down)
            owned_buttons.erase(token);
    }
    return result;
}
}

bool system_input_available() {
#ifdef _WIN32
    return true;
#elif defined(__APPLE__)
    return AXIsProcessTrusted();
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
    return driver().available();
#else
    return false;
#endif
}

bool background_window_input_available() {
#ifdef _WIN32
    return true;
#elif defined(__APPLE__)
    return false;
#else
#ifdef KISEKI_HAS_X11
    return kiseki::platform::target::target_window_available();
#else
    return false;
#endif
#endif
}

bool key_supported(const std::string &key) {
#ifdef _WIN32
    return virtual_key_for_name(key) != 0;
#elif defined(__APPLE__)
    return mac_key_for_name(key).has_value();
#elif defined(KISEKI_HAS_X11)
    return keysym_for_name(key) != NoSymbol;
#else
    return false;
#endif
}
OperationResult key_action(const std::string &key, bool down, const std::string &requested, bool cleanup_only) {
    if (!key_supported(key))
        return fail("unsupported key: " + key);
    if (down && input_cancelled())
        return fail("input cancelled");
    const auto backend = choose_backend(requested);
    const auto ready = check_backend(backend);
    if (!ready.ok)
        return ready;
    const auto token = key_token(key, backend);
    if (cleanup_only && !owned_keys.contains(token))
        return ok("");
    const bool was_down = native_key_down(key, backend);
    const auto result = send_native_key(key, down, backend);
    if (result.ok) {
        if (down && !was_down)
            owned_keys.insert(token);
        if (!down)
            owned_keys.erase(token);
    }
    return result.ok ? ok("key " + key + (down ? " down" : " up")) : result;
}
OperationResult tap_key(const std::string &key, const std::string &backend, int hold_ms) {
    return key_combo(key, backend, hold_ms);
}

OperationResult key_combo(const std::string &keys, const std::string &requested, int hold_ms) {
    const auto list = split_keys(keys);
    if (list.empty())
        return fail("no key specified");
    for (const auto &key : list)
        if (!key_supported(key))
            return fail("unsupported key: " + key);
    auto backend = choose_backend(requested);
#ifdef _WIN32
    if ((requested.empty() || lower_copy(requested) == "auto") && combo_contains_system_hotkey(list))
        backend = "system";
#endif
    const auto ready = check_backend(backend);
    if (!ready.ok)
        return ready;
    return run_key_chord(
        list, [&](const std::string &key) { return native_key_down(key, backend); },
        [&](const std::string &key, bool down) { return key_action(key, down, backend); }, hold_ms);
}

OperationResult type_text(const std::string& text) {
    if (input_cancelled()) return fail("input cancelled");
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
        if (!wait_input_ms(1)) return fail("input cancelled");
    }
    return ok("text input sent");
#elif defined(__APPLE__)
    return send_text_with_cgevent(text);
#else
#ifdef KISEKI_HAS_X11
    return send_text_with_xtest(text);
#else
    return fail("Linux X11/XTest input support was not compiled in");
#endif
#endif
}

OperationResult mouse_drag_absolute(const std::vector<MousePoint> &points, const std::string &requested,
                                    int step_delay_ms, int start_hold_ms, int end_hold_ms,
                                    const std::string &requested_button, const std::string &modifiers) {
    const auto timing = validate_drag_timing(points, step_delay_ms, start_hold_ms, end_hold_ms);
    if (!timing.ok)
        return timing;
    const auto button = lower_copy(requested_button);
    if (button != "left" && button != "right" && button != "middle" && button != "x1" && button != "x2")
        return fail("unsupported drag button: " + button);
    const auto keys = split_keys(modifiers);
    for (const auto &key : keys)
        if (!key_supported(key))
            return fail("unsupported modifier key: " + key);
    const bool timed = points.front().time_ms >= 0;
    const auto backend = choose_backend(requested);
    const auto ready = check_backend(backend);
    if (!ready.ok)
        return ready;
    ReleaseStack releases;
    for (const auto &key : keys) {
        if (native_key_down(key, backend))
            continue;
        const auto result = key_action(key, true, backend);
        if (!result.ok)
            return releases.finish(result);
        releases.add(key, [key, backend] { return key_action(key, false, backend, true); });
    }
    MousePoint last_position = points.front();
    auto move = [&](const MousePoint &point) {
        const auto result = move_native_mouse(MouseOptions{0, 0, point.x, point.y, true, backend, "none"}, backend);
        if (result.ok)
            last_position = point;
        return result;
    };
    auto result = move(points.front());
    if (!result.ok)
        return releases.finish(result);
    const bool borrowed = native_button_down(button, backend);
    result = button_action(button, true, 1, backend, false, last_position);
    if (!result.ok)
        return releases.finish(result);
    if (!borrowed)
        releases.add(button,
                     [&, button, backend] { return button_action(button, false, 1, backend, true, last_position); });
    if (!wait_input_ms(start_hold_ms))
        return releases.finish(fail("input cancelled"));
    const auto start = std::chrono::steady_clock::now();
    const auto duration = timed ? points.back().time_ms
                                : static_cast<std::int64_t>(step_delay_ms) * static_cast<std::int64_t>(points.size());
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto offset =
            timed ? points[index].time_ms : static_cast<std::int64_t>(step_delay_ms) * static_cast<std::int64_t>(index);
        if (!wait_until_input(start + std::chrono::milliseconds(offset)))
            return releases.finish(fail("input cancelled"));
        result = move(points[index]);
        if (!result.ok)
            return releases.finish(result);
    }
    if (!timed && !wait_until_input(start + std::chrono::milliseconds(duration)))
        return releases.finish(fail("input cancelled"));
    if (!wait_input_ms(end_hold_ms))
        return releases.finish(fail("input cancelled"));
    return releases.finish(ok("mouse drag sent"));
}

OperationResult mouse_action(const MouseOptions& options) {
    const auto click = lower_copy(options.click);
    if (!supported_mouse_click(click))
        return fail("unsupported mouse click: " + click);
    if (options.click_count < 1 || options.click_interval_ms < 0 || options.hold_ms < 0)
        return fail("click count must be positive and delays non-negative");
    if ((click == "none" || click.find('-') != std::string::npos) && (options.click_count != 1 || options.hold_ms != 0))
        return fail("click count and hold duration require a complete click");
    const auto backend = choose_backend(options.backend);
    const auto ready = check_backend(backend);
    if (!ready.ok)
        return ready;
    if (backend == "driver" && options.hwheel)
        return fail("IbInputSimulator has no horizontal wheel API; select --backend system");
    const auto dash = click.rfind('-');
    const auto button = dash == std::string::npos ? click : click.substr(0, dash);
    if (options.cleanup_only) {
        if (!click.ends_with("-up"))
            return fail("cleanup requires a button up");
        return button_action(button, false, 1, backend, true);
    }
    if (input_cancelled() && !click.ends_with("-up"))
        return fail("input cancelled");
    std::optional<MousePoint> click_position;
#ifdef __APPLE__
    // CGEventPost is asynchronous: reading the cursor immediately after posting
    // a move can return its old location. The button belongs at the requested point.
    const auto before = current_mouse_location();
    click_position = options.absolute
                         ? MousePoint{options.x, options.y}
                         : MousePoint{static_cast<int>(before.x) + options.dx, static_cast<int>(before.y) + options.dy};
#endif
    if (options.absolute || options.dx || options.dy) {
        const auto result = move_native_mouse(options, backend);
        if (!result.ok)
            return result;
    }
    if (options.wheel || options.hwheel) {
        const auto result = send_native_wheel(options.wheel, options.hwheel, backend);
        if (!result.ok)
            return result;
    }
    if (click == "none")
        return ok("mouse input sent");
    if (click.ends_with("-down") || click.ends_with("-up"))
        return button_action(button, click.ends_with("-down"), 1, backend, false, click_position);
    if (native_button_down(button, backend))
        return fail("mouse button is already held; send its explicit up before clicking");
#ifdef __APPLE__
    // CGEvent doesn't infer click state across CLI processes. Use an explicit count
    // for a double/triple click; within a process also preserve rapid legacy clicks.
    static thread_local std::map<std::string, std::pair<CGPoint, std::chrono::steady_clock::time_point>> last_click;
    static thread_local std::map<std::string, int> last_count;
    const CGPoint position = CGPointMake(click_position->x, click_position->y);
    auto found = last_click.find(button);
    int base = 0;
    if (options.click_count == 1 && found != last_click.end() &&
        std::chrono::steady_clock::now() - found->second.second <
            std::chrono::duration<double>(mac_double_click_interval()) &&
        std::abs(position.x - found->second.first.x) <= 4 && std::abs(position.y - found->second.first.y) <= 4)
        base = last_count[button];
#else
    const int base = 0;
#endif
    for (int index = 1; index <= options.click_count; ++index) {
        ReleaseStack release;
        auto result = button_action(button, true, base + index, backend, false, click_position);
        if (!result.ok)
            return result;
        release.add(button,
                    [&, index] { return button_action(button, false, base + index, backend, true, click_position); });
        result = release.finish(wait_input_ms(options.hold_ms) ? ok("") : fail("input cancelled"));
        if (!result.ok)
            return result;
        if (index < options.click_count && !wait_input_ms(options.click_interval_ms))
            return fail("input cancelled");
    }
#ifdef __APPLE__
    last_click[button] = {position, std::chrono::steady_clock::now()};
    last_count[button] = base + options.click_count;
#endif
    return ok("mouse input sent");
}

OperationResult background_type_text(const kiseki::platform::target::TargetQuery& target, const std::string& text) {
    if (input_cancelled()) return fail("input cancelled");
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
            if (!wait_input_ms(1)) return fail("input cancelled");
        }
        return ok("background text input sent");
    });
#elif defined(__APPLE__)
    static_cast<void>(target);
    return mac_background_input_unavailable();
#else
#ifdef KISEKI_HAS_X11
    return with_target_window(target, [&](Display* display, Window window) {
        for (const unsigned char c : text) {
            const auto result = send_background_text_key_x11(display, window, c);
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
        if (PostMessageW(receiver, WM_KEYDOWN, vk, key_lparam(vk, false, lower_copy(key) == "numpad-enter")) == 0) {
            return fail("PostMessage key down failed");
        }
        if (PostMessageW(receiver, WM_KEYUP, vk, key_lparam(vk, true, lower_copy(key) == "numpad-enter")) == 0) {
            return fail("PostMessage key up failed");
        }
        return ok("background key input sent");
    });
#elif defined(__APPLE__)
    static_cast<void>(target);
    return mac_background_input_unavailable();
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
    const auto click = lower_copy(options.click);
    if (!supported_mouse_click(click))
        return fail("unsupported mouse click: " + click);
    if (options.click_count < 1 || options.click_interval_ms < 0 || options.hold_ms < 0)
        return fail("click count must be positive and delays non-negative");
    if ((click == "none" || click.find('-') != std::string::npos) && (options.click_count != 1 || options.hold_ms != 0))
        return fail("click count and hold duration require a complete click");
    if (input_cancelled() && !click.ends_with("-up"))
        return fail("input cancelled");
    const auto dash = click.rfind('-');
    const auto button = dash == std::string::npos ? click : click.substr(0, dash);
    std::string explicit_buttons = options.held_buttons;
    std::replace(explicit_buttons.begin(), explicit_buttons.end(), ',', '+');
    const auto explicit_list = split_keys(explicit_buttons);
    for (const auto &b : explicit_list)
        if (b != "left" && b != "right" && b != "middle" && b != "x1" && b != "x2")
            return fail("unsupported held button: " + b);
#ifdef _WIN32
    return with_resolved_hwnd(options.target, [&](HWND hwnd) {
        auto &state = background_mouse_states[hwnd];
        if (state.receiver && !IsWindow(state.receiver))
            state = {};
        POINT point{options.x, options.y};
        if (!options.receiver_window_id.empty()) {
            const auto receiver = hwnd_from_id(options.receiver_window_id);
            if (!receiver || !IsWindow(*receiver) || (*receiver != hwnd && !IsChild(hwnd, *receiver)))
                return fail("receiver must be the target window or its child");
            if (!state.buttons.empty() && state.receiver != *receiver)
                return fail("cannot change receiver while a button is held");
            state.receiver = *receiver;
        } else if (state.buttons.empty()) {
            // Descend to the actual receiver, retaining it from button down to up.
            POINT local = point;
            HWND receiver = hwnd;
            while (true) {
                const HWND next = mouse_message_target(receiver, local);
                if (next == receiver)
                    break;
                receiver = next;
            }
            state.receiver = receiver;
        }
        if (!state.receiver)
            state.receiver = hwnd;
        if (options.cleanup_only) {
            if (!state.buttons.contains(button))
                return ok("");
            point = state.last;
        } else
            point = map_parent_point_to_receiver(hwnd, state.receiver, {options.x, options.y});
        state.last = point;
        auto mask = [&](const std::string &exclude = "") {
            WPARAM result = 0;
            for (const auto *b : {"left", "right", "middle", "x1", "x2"}) {
                if (b == exclude)
                    continue;
                if (state.buttons.contains(b) ||
                    std::find(explicit_list.begin(), explicit_list.end(), b) != explicit_list.end())
                    result |= std::string{b} == "left"     ? MK_LBUTTON
                              : std::string{b} == "right"  ? MK_RBUTTON
                              : std::string{b} == "middle" ? MK_MBUTTON
                              : std::string{b} == "x1"     ? MK_XBUTTON1
                                                           : MK_XBUTTON2;
            }
            if (native_key_down("shift", "system") || native_key_down("rshift", "system"))
                result |= MK_SHIFT;
            if (native_key_down("ctrl", "system") || native_key_down("rctrl", "system"))
                result |= MK_CONTROL;
            return result;
        };
        auto send_button = [&](bool down, int count) {
            const UINT normal = button == "left"     ? WM_LBUTTONDOWN
                                : button == "right"  ? WM_RBUTTONDOWN
                                : button == "middle" ? WM_MBUTTONDOWN
                                                     : WM_XBUTTONDOWN;
            const UINT up = button == "left"     ? WM_LBUTTONUP
                            : button == "right"  ? WM_RBUTTONUP
                            : button == "middle" ? WM_MBUTTONUP
                                                 : WM_XBUTTONUP;
            const UINT dbl = button == "left"     ? WM_LBUTTONDBLCLK
                             : button == "right"  ? WM_RBUTTONDBLCLK
                             : button == "middle" ? WM_MBUTTONDBLCLK
                                                  : WM_XBUTTONDBLCLK;
            if (down && state.buttons.contains(button))
                return ok("");
            const auto previous = state.buttons;
            if (down)
                state.buttons.insert(button);
            WPARAM flags = mask(down ? "" : button);
            if (button == "x1" || button == "x2")
                flags |= static_cast<WPARAM>(button == "x1" ? XBUTTON1 : XBUTTON2) << 16;
            if (!PostMessageW(state.receiver, down ? (count % 2 == 0 ? dbl : normal) : up, flags,
                              mouse_lparam(point))) {
                state.buttons = previous;
                return fail("PostMessage mouse button failed");
            }
            if (!down)
                state.buttons.erase(button);
            return ok("");
        };
        if (!options.cleanup_only && !PostMessageW(state.receiver, WM_MOUSEMOVE, mask(), mouse_lparam(point)))
            return fail("PostMessage mouse move failed");
        if (click == "none")
            return ok("background mouse input sent");
        if (click.ends_with("-down") || click.ends_with("-up"))
            return send_button(click.ends_with("-down"), 1);
        if (state.buttons.contains(button))
            return fail("background button is already held; send explicit up before clicking");
        for (int count = 1; count <= options.click_count; ++count) {
            const auto result = send_button(true, count);
            if (!result.ok)
                return result;
            ReleaseStack release;
            release.add(button, [&, count] { return send_button(false, count); });
            const auto finished = release.finish(wait_input_ms(options.hold_ms) ? ok("") : fail("input cancelled"));
            if (!finished.ok)
                return finished;
            if (count < options.click_count && !wait_input_ms(options.click_interval_ms))
                return fail("input cancelled");
        }
        return ok("background mouse input sent");
    });
#elif defined(__APPLE__)
    return mac_background_input_unavailable();
#elif defined(KISEKI_HAS_X11)
    return with_target_window(options.target, [&](Display *display, Window window) {
        if (!options.receiver_window_id.empty()) {
            try {
                std::size_t consumed = 0;
                const auto receiver = std::stoull(options.receiver_window_id, &consumed, 0);
                if (consumed != options.receiver_window_id.size() || receiver != window)
                    return fail("X11 receiver must equal the target window; select the intended child with target-window-id");
            } catch (const std::exception&) {
                return fail("invalid X11 receiver window id");
            }
        }
        auto &state = background_mouse_states[window];
        auto &buttons = state.buttons;
        if (!options.cleanup_only) {
            state.x = options.x;
            state.y = options.y;
        }
        const int x = state.x, y = state.y;
        if (options.cleanup_only && !buttons.contains(button))
            return ok("");
        auto mask = [&] {
            unsigned result = 0;
            for (const auto *b : {"left", "middle", "right"})
                if (buttons.contains(b) ||
                    std::find(explicit_list.begin(), explicit_list.end(), b) != explicit_list.end())
                    result |= std::string{b} == "left"     ? Button1Mask
                              : std::string{b} == "middle" ? Button2Mask
                                                           : Button3Mask;
            return result;
        };
        int rx = x, ry = y;
        Window child;
        XTranslateCoordinates(display, window, DefaultRootWindow(display), x, y, &rx, &ry, &child);
        XMotionEvent motion{};
        motion.type = MotionNotify;
        motion.display = display;
        motion.window = window;
        motion.root = DefaultRootWindow(display);
        motion.x = x;
        motion.y = y;
        motion.x_root = rx;
        motion.y_root = ry;
        motion.same_screen = True;
        motion.state = mask();
        if (!options.cleanup_only && !XSendEvent(display, window, True, PointerMotionMask | ButtonMotionMask,
                                                 reinterpret_cast<XEvent *>(&motion)))
            return fail("XSendEvent mouse move failed");
        XFlush(display);
        if (click == "none")
            return ok("background mouse input sent");
        const unsigned b = button == "left"     ? 1
                           : button == "right"  ? 3
                           : button == "middle" ? 2
                           : button == "x1"     ? 8
                                                : 9;
        auto send_button = [&](bool down) {
            if (down && buttons.contains(button))
                return ok("");
            XButtonEvent e{};
            e.type = down ? ButtonPress : ButtonRelease;
            e.display = display;
            e.window = window;
            e.root = DefaultRootWindow(display);
            e.x = x;
            e.y = y;
            e.x_root = rx;
            e.y_root = ry;
            e.same_screen = True;
            e.button = b;
            e.state = mask();
            if (!XSendEvent(display, window, True, down ? ButtonPressMask : ButtonReleaseMask,
                            reinterpret_cast<XEvent *>(&e)))
                return fail("XSendEvent mouse button failed");
            if (down)
                buttons.insert(button);
            else
                buttons.erase(button);
            XFlush(display);
            return ok("");
        };
        if (click.ends_with("-down") || click.ends_with("-up"))
            return send_button(click.ends_with("-down"));
        if (buttons.contains(button))
            return fail("background button is already held; send explicit up before clicking");
        for (int count = 0; count < options.click_count; ++count) {
            auto result = send_button(true);
            if (!result.ok)
                return result;
            ReleaseStack release;
            release.add(button, [&] { return send_button(false); });
            result = release.finish(wait_input_ms(options.hold_ms) ? ok("") : fail("input cancelled"));
            if (!result.ok)
                return result;
            if (count + 1 < options.click_count && !wait_input_ms(options.click_interval_ms))
                return fail("input cancelled");
        }
        return ok("background mouse input sent");
    });
#else
    return fail("Linux X11 background input support was not compiled in");
#endif
}
OperationResult background_mouse_drag(const BackgroundDragOptions& options) {
    const auto timing =
        validate_drag_timing(options.points, options.step_delay_ms, options.start_hold_ms, options.end_hold_ms);
    if (!timing.ok)
        return timing;
    if (options.button != "left" && options.button != "right" && options.button != "middle" && options.button != "x1" &&
        options.button != "x2")
        return fail("unsupported drag button: " + options.button);
    const auto resolved = kiseki::platform::target::resolve_window(options.target);
    if (!resolved.ok)
        return fail(resolved.error);
    const kiseki::platform::target::TargetQuery target{.window_id = resolved.window.id};
    bool borrowed = false;
#ifdef _WIN32
    if (const auto hwnd = hwnd_from_id(resolved.window.id)) {
        const auto found = background_mouse_states.find(*hwnd);
        borrowed = found != background_mouse_states.end() && found->second.buttons.contains(options.button);
    }
#elif defined(KISEKI_HAS_X11)
    const auto found = background_mouse_states.find(static_cast<Window>(std::stoull(resolved.window.id, nullptr, 0)));
    borrowed = found != background_mouse_states.end() && found->second.buttons.contains(options.button);
#endif
    BackgroundMouseOptions mouse{target, options.points.front().x, options.points.front().y, options.button + "-down"};
    auto result = background_mouse_action(mouse);
    if (!result.ok)
        return result;
    ReleaseStack release;
    if (!borrowed)
        release.add(options.button, [&] {
            mouse.click = options.button + "-up";
            mouse.cleanup_only = true;
            return background_mouse_action(mouse);
        });
    if (!wait_input_ms(options.start_hold_ms))
        return release.finish(fail("input cancelled"));
    const auto start = std::chrono::steady_clock::now();
    const bool timed = options.points.front().time_ms >= 0;
    for (std::size_t index = 1; index < options.points.size(); ++index) {
        const auto offset =
            timed ? options.points[index].time_ms : static_cast<std::int64_t>(options.step_delay_ms) * (index - 1);
        if (!wait_until_input(start + std::chrono::milliseconds(offset)))
            return release.finish(fail("input cancelled"));
        mouse.x = options.points[index].x;
        mouse.y = options.points[index].y;
        mouse.click = "none";
        result = background_mouse_action(mouse);
        if (!result.ok)
            return release.finish(result);
    }
    if (!timed && !wait_until_input(start + std::chrono::milliseconds(static_cast<std::int64_t>(options.step_delay_ms) *
                                                                      (options.points.size() - 1))))
        return release.finish(fail("input cancelled"));
    if (!wait_input_ms(options.end_hold_ms))
        return release.finish(fail("input cancelled"));
    return release.finish(ok("background mouse drag sent"));
}
        }
