#include "platform/input/input.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
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

    bool mouse_move(int dx, int dy) const {
        return mouse_move_ != nullptr &&
               mouse_move_(static_cast<unsigned int>(dx), static_cast<unsigned int>(dy), 1);
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
    XFlush(display);
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

OperationResult tap_key(const std::string& key) {
    return key_combo(key);
}

OperationResult key_combo(const std::string& keys) {
    const auto key_list = split_keys(keys);
    if (key_list.empty()) {
        return fail("no key specified");
    }

#ifdef _WIN32
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
    return send_combo_with_sendinput(key_list);
#else
#ifdef KISEKI_HAS_X11
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
    std::vector<INPUT> inputs;
    inputs.reserve(text.size() * 2U);
    for (const unsigned char c : text) {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = c;
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(down);
        inputs.push_back(up);
    }
    if (SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) != inputs.size()) {
        return fail("SendInput text failed");
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

OperationResult mouse_action(const MouseOptions& options) {
    const std::string click = lower_copy(options.click);
    if (!(click == "none" || click == "left" || click == "right" || click == "middle")) {
        return fail("click must be none, left, right, or middle");
    }

#ifdef _WIN32
    IbInputSimulator simulator;
    if (simulator.available()) {
        if ((options.dx != 0 || options.dy != 0) && !simulator.mouse_move(options.dx, options.dy)) {
            return fail("IbInputSimulator mouse move failed");
        }
        if (click != "none") {
            unsigned int button = 0x06;
            if (click == "right") button = 0x18;
            if (click == "middle") button = 0x60;
            if (!simulator.mouse_click(button)) {
                return fail("IbInputSimulator mouse click failed");
            }
        }
        return ok("mouse input sent through IbInputSimulator");
    }

    std::vector<INPUT> inputs;
    if (options.dx != 0 || options.dy != 0) {
        INPUT move{};
        move.type = INPUT_MOUSE;
        move.mi.dx = options.dx;
        move.mi.dy = options.dy;
        move.mi.dwFlags = MOUSEEVENTF_MOVE;
        inputs.push_back(move);
    }
    if (click != "none") {
        DWORD down = MOUSEEVENTF_LEFTDOWN;
        DWORD up = MOUSEEVENTF_LEFTUP;
        if (click == "right") {
            down = MOUSEEVENTF_RIGHTDOWN;
            up = MOUSEEVENTF_RIGHTUP;
        } else if (click == "middle") {
            down = MOUSEEVENTF_MIDDLEDOWN;
            up = MOUSEEVENTF_MIDDLEUP;
        }
        INPUT down_input{};
        down_input.type = INPUT_MOUSE;
        down_input.mi.dwFlags = down;
        INPUT up_input{};
        up_input.type = INPUT_MOUSE;
        up_input.mi.dwFlags = up;
        inputs.push_back(down_input);
        inputs.push_back(up_input);
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
    return with_display([&](Display* display, const XTestApi& xtest) {
        if (options.dx != 0 || options.dy != 0) {
            xtest.fake_motion(display, options.dx, options.dy, CurrentTime);
        }
        if (click != "none") {
            unsigned int button = 1;
            if (click == "right") button = 3;
            if (click == "middle") button = 2;
            xtest.fake_button(display, button, True, CurrentTime);
            xtest.fake_button(display, button, False, CurrentTime);
        }
        return ok("mouse input sent");
    });
#else
    return fail("Linux X11/XTest input support was not compiled in");
#endif
#endif
}

}
