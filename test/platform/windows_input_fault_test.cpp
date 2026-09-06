#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <set>
#include <vector>

namespace {
int attempts = 0;
int fail_at = -1;
std::set<WORD> held;
std::vector<INPUT> sent;
UINT WINAPI fake_send_input(UINT count, LPINPUT inputs, int) {
    for (UINT i = 0; i < count; ++i) {
        if (++attempts == fail_at)
            return i;
        sent.push_back(inputs[i]);
        if (inputs[i].type == INPUT_KEYBOARD) {
            if (inputs[i].ki.dwFlags & KEYEVENTF_KEYUP)
                held.erase(inputs[i].ki.wVk);
            else
                held.insert(inputs[i].ki.wVk);
        }
    }
    return count;
}
SHORT WINAPI fake_key_state(int key) {
    return held.contains(static_cast<WORD>(key)) ? static_cast<SHORT>(0x8000) : 0;
}
}
// Compile the actual implementation against deterministic Win32 boundary faults.
// This executable never sends real keyboard or mouse input.
#define SendInput fake_send_input
#define GetAsyncKeyState fake_key_state
#include "../../src/platform/input/input.cpp"
#undef GetAsyncKeyState
#undef SendInput

int main() {
    using namespace kiseki::platform::input;
    InputCancellationScope cancellation;
    int failures = 0;
    const auto require = [&](bool condition, const char *message) {
        if (!condition) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    for (int failure = 1; failure <= 6; ++failure) {
        attempts = 0;
        fail_at = failure;
        sent.clear();
        held.clear();
        const auto result = key_combo("ctrl+shift+a", "system");
        require(!result.ok, "injected chord failure must be reported");
        if (failure <= 3) {
            require(held.empty(), "successful downs must unwind after a later down fails");
            require(attempts == failure * 2 - 1, "partial chord must release every acquired key");
        } else {
            require(attempts == 6, "release failure must not skip remaining releases");
            require(result.error.find("cleanup") != std::string::npos, "release failure must be observable");
        }
        fail_at = -1;
        for (const auto *key : {"a", "shift", "ctrl"})
            key_action(key, false, "system", true);
    }
    held = {VK_LCONTROL};
    sent.clear();
    attempts = 0;
    const auto borrowed = key_combo("ctrl+a", "system");
    require(borrowed.ok && held == std::set<WORD>{VK_LCONTROL}, "borrowed physical Ctrl must remain held");
    require(sent.size() == 2, "borrowed modifier must not be injected or released");
    held.clear();
    for (const auto *key :
         {"left", "right", "home", "end", "pageup", "delete", "rctrl", "ralt", "numpad-enter", "numpad-divide"}) {
        sent.clear();
        require(tap_key(key, "system").ok, "extended key name must be accepted");
        require(sent.size() == 2, "tap must send a matching pair");
        for (const auto &event : sent) {
            require(event.ki.wScan != 0, "physical key scan must be populated");
            require((event.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) != 0,
                    "extended identity must survive keyboard layout mapping");
        }
    }
    for (const auto *key : {"a", "lctrl", "lshift", "minus", "leftbracket", "numpad0", "numpad-add"}) {
        sent.clear();
        require(tap_key(key, "system").ok, "ordinary physical key name must be accepted");
        for (const auto &event : sent)
            require(!(event.ki.dwFlags & KEYEVENTF_EXTENDEDKEY), "ordinary key must not gain an extended flag");
    }
    for (const bool numpad_first : {false, true}) {
        const std::string first = numpad_first ? "numpad-enter" : "enter";
        const std::string second = numpad_first ? "enter" : "numpad-enter";
        require(key_action(first, true, "system").ok, "first Enter must go down");
        sent.clear();
        require(tap_key(second, "system").ok, "second physical Enter must remain usable");
        require(sent.size() == 2, "a shared VK must not suppress the other physical Enter");
        for (const auto &event : sent)
            require(bool(event.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) == !numpad_first,
                    "second Enter must preserve its own physical identity");
        require(native_key_down(first, "system"), "first Enter acquisition must survive the second tap");
        require(key_action(first, false, "system", true).ok, "first Enter must retain its own cleanup");
    }
    for (const bool absolute : {false, true}) {
        sent.clear();
        require(mouse_action({1, 1, 200, 200, absolute, "system", "none"}).ok, "mouse move must be accepted");
        require(sent.size() == 1, "mouse move must reach SendInput");
        require(sent.size() == 1 && (sent.front().mi.dwFlags & MOUSEEVENTF_MOVE_NOCOALESCE),
                "explicit path points must not use default WM_MOUSEMOVE coalescing");
    }
    std::cout << "Win32 boundary failures=" << failures << '\n';
    return failures ? 2 : 0;
}
