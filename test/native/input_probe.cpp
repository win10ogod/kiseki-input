#include "scenarios.hpp"
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <X11/Xlib.h>
#endif

using namespace native_probe;
std::ofstream received;
#ifdef _WIN32
void binding_cases(HWND window, HWND spare) {
    using namespace kiseki::platform::input;
    for (int scenario = 0; scenario < 3; ++scenario) {
        phase = 23 + scenario;
        SetWindowTextW(window, L"Kiseki binding target");
        SetWindowTextW(spare, L"Kiseki spare target");
        auto dependencies = kiseki::cli::default_dependencies();
        const auto original = dependencies.input_background_mouse;
        dependencies.input_background_mouse = [=](const kiseki::cli::BackgroundMouseOptions &options,
                                                  kiseki::cli::Io io) {
            const int code = original(options, io);
            if (code == 0 && options.click == "left-down") {
                if (scenario == 0)
                    SetWindowTextW(window, L"Kiseki renamed target");
                if (scenario < 2)
                    SetWindowTextW(spare, L"Kiseki binding target");
                else
                    ShowWindow(window, SW_HIDE);
            }
            return code;
        };
        nlohmann::json sequence{{"steps", nlohmann::json::array({
            {{"type", "background-mouse"}, {"targetTitle", "Kiseki binding target"},
             {"x", 40}, {"y", 210}, {"click", "left-down"}},
            {{"type", "background-mouse"}, {"targetTitle", "Kiseki binding target"},
             {"x", 240}, {"y", 210}}
        })}};
        const auto path = output / ("binding-" + std::to_string(scenario) + ".json");
        { std::ofstream file{path}; file << sequence.dump(2); }
        const int code = kiseki::cli::run({"input", "sequence", "--file", path.string()},
                                          output / "config.json", {std::cout, std::cerr}, dependencies);
        if (code) ++failures;
        wait(150);
        ShowWindow(window, SW_SHOW);
        SetWindowTextW(window, L"Kiseki native input regression");
        SetWindowTextW(spare, L"Kiseki spare target");
    }
    phase = 26;
    auto dependencies = kiseki::cli::default_dependencies();
    const auto original = dependencies.input_background_mouse;
    dependencies.input_background_mouse = [=](const kiseki::cli::BackgroundMouseOptions &options,
                                              kiseki::cli::Io io) {
        const int code = original(options, io);
        if (code == 0 && options.click == "left-down") {
            wait(100);
            SendMessageW(spare, WM_CLOSE, 0, 0);
        }
        return code;
    };
    nlohmann::json sequence{{"steps", nlohmann::json::array({
        {{"type", "background-mouse"}, {"targetWindowId", std::to_string(reinterpret_cast<std::uintptr_t>(spare))},
         {"x", 40}, {"y", 100}, {"click", "left-down"}}
    })}};
    const auto path = output / "binding-destroyed.json";
    { std::ofstream file{path}; file << sequence.dump(2); }
    std::ostringstream out, err;
    const int code = kiseki::cli::run({"input", "sequence", "--file", path.string()},
                                      output / "config.json", {out, err}, dependencies);
    if (code == 0 || err.str().find("cleanup") == std::string::npos) {
        ++failures;
        std::cerr << "destroyed recipient cleanup failure was not reported\n";
    }
    std::ofstream{output / "destroyed-recipient.json"} << nlohmann::json{
        {"exitCode", code}, {"error", err.str()}, {"destroyed", !IsWindow(spare)}}.dump(2);
    SetForegroundWindow(window);
    SetFocus(window);
    wait(150);
}
LRESULT CALLBACK receiver(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    std::string kind;
    int button = -1;
    bool down = false;
    switch (message) {
    case WM_MOUSEMOVE:
        kind = "move";
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
        kind = "button";
        button = 0;
        down = message != WM_LBUTTONUP;
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONUP:
        kind = "button";
        button = 1;
        down = message != WM_RBUTTONUP;
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONUP:
        kind = "button";
        button = 2;
        down = message != WM_MBUTTONUP;
        break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONUP:
        kind = "button";
        button = HIWORD(wp) == XBUTTON1 ? 3 : 4;
        down = message != WM_XBUTTONUP;
        break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        kind = "key";
        down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        kind = "wheel";
        break;
    }
    if (!kind.empty()) {
        nlohmann::json event{{"phase", phase.load()},
                             {"kind", kind},
                             {"message", message},
                             {"wParam", wp},
                             {"lParam", lp},
                             {"receiver", reinterpret_cast<std::uintptr_t>(window)},
                             {"nativeTimeMs", GetMessageTime()},
                             {"x", static_cast<SHORT>(LOWORD(lp))},
                             {"y", static_cast<SHORT>(HIWORD(lp))},
                             {"down", down},
                             {"button", button},
                             {"shift", (GetKeyState(VK_SHIFT) & 0x8000) != 0},
                             {"ctrl", (GetKeyState(VK_CONTROL) & 0x8000) != 0},
                             {"space", (GetKeyState(VK_SPACE) & 0x8000) != 0}};
        if (kind == "move")
            event["buttons"] = wp;
        if (kind == "key") {
            event["code"] = wp;
            event["scan"] = (lp >> 16) & 255;
            event["extended"] = (lp >> 24) & 1;
        }
        if (kind == "wheel") {
            event["delta"] = static_cast<SHORT>(HIWORD(wp));
            event["horizontal"] = message == WM_MOUSEHWHEEL;
        }
        received << event.dump() << std::endl;
        if (phase == 22) {
            if (message == WM_LBUTTONUP)
                dense_drag_released = true;
            if (message == WM_MOUSEMOVE)
                Sleep(20); // A busy application must still receive the requested path.
        }
        // Keep the owned test window stable when exercising side buttons/keys.
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        auto dc = BeginPaint(window, &paint);
        RECT area;
        GetClientRect(window, &area);
        DrawTextW(dc, L"Kiseki native input regression probe\nEvents are saved in receiver-events.jsonl", -1, &area,
                  DT_CENTER | DT_WORDBREAK);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
void pump() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
int main(int argc, char **argv) {
    output = argc > 1 ? argv[1] : "native-probe";
    std::filesystem::create_directories(output);
    received.open(output / "receiver-events.jsonl");
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT previous_cursor{};
    GetCursorPos(&previous_cursor);
    HWND previous = GetForegroundWindow();
    auto old_layout = GetKeyboardLayout(0);
    auto english = LoadKeyboardLayoutW(L"00000409", 0);
    if (english)
        ActivateKeyboardLayout(english, 0);
    WNDCLASSW type{};
    type.lpfnWndProc = receiver;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"KisekiNativeInputProbe";
    type.style = CS_DBLCLKS;
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&type);
    HWND window = CreateWindowW(type.lpszClassName, L"Kiseki native input regression", WS_OVERLAPPEDWINDOW, 120, 100,
                                740, 460, nullptr, nullptr, type.hInstance, nullptr);
    HWND spare = CreateWindowW(type.lpszClassName, L"Kiseki spare target", WS_OVERLAPPEDWINDOW,
                                900, 100, 240, 200, nullptr, nullptr, type.hInstance, nullptr);
    ShowWindow(spare, SW_SHOWNOACTIVATE);
    // Adjacent child receivers expose accidental re-targeting during a split drag.
    CreateWindowW(type.lpszClassName, L"left receiver", WS_CHILD | WS_VISIBLE, 0, 180, 200, 100, window, nullptr,
                  type.hInstance, nullptr);
    CreateWindowW(type.lpszClassName, L"right receiver", WS_CHILD | WS_VISIBLE, 220, 180, 300, 100, window, nullptr,
                  type.hInstance, nullptr);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(window);
    UpdateWindow(window);
    for (int i = 0; i < 150; ++i) {
        pump();
        Sleep(1);
    }
    if (GetForegroundWindow() != window) {
        std::cerr << "test window did not acquire foreground\n";
        DestroyWindow(window);
        return 2;
    }
    POINT point{100, 100};
    ClientToScreen(window, &point);
    std::thread sender([&] {
        binding_cases(window, spare);
        run(point.x, point.y, std::to_string(reinterpret_cast<std::uintptr_t>(window)), true);
    });
    while (!finished) {
        pump();
        Sleep(1);
    }
    sender.join();
    pump();
    SetCursorPos(previous_cursor.x, previous_cursor.y);
    if (previous && IsWindow(previous))
        SetForegroundWindow(previous);
    ActivateKeyboardLayout(old_layout, 0);
    DestroyWindow(spare);
    DestroyWindow(window);
    return failures ? 2 : 0;
}
#else
int main(int argc, char **argv) {
    XInitThreads();
    output = argc > 1 ? argv[1] : "native-probe";
    std::filesystem::create_directories(output);
    received.open(output / "receiver-events.jsonl");
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "XOpenDisplay failed\n";
        return 2;
    }
    Window root = DefaultRootWindow(display), rr, child, previous;
    int ox, oy, cx, cy, revert;
    unsigned mask;
    XQueryPointer(display, root, &rr, &child, &ox, &oy, &cx, &cy, &mask);
    XGetInputFocus(display, &previous, &revert);
    XSetWindowAttributes attributes{};
    attributes.override_redirect = True;
    attributes.background_pixel = WhitePixel(display, DefaultScreen(display));
    Window window = XCreateWindow(display, root, 100, 100, 740, 460, 0, CopyFromParent, InputOutput, CopyFromParent,
                                  CWOverrideRedirect | CWBackPixel, &attributes);
    XStoreName(display, window, "Kiseki native input regression");
    XSelectInput(display, window,
                 PointerMotionMask | ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask |
                     ExposureMask);
    XMapRaised(display, window);
    XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
    XSync(display, False);
    std::thread sender([&] { run(200, 200, std::to_string(window), true); });
    while (!finished || XPending(display)) {
        while (XPending(display)) {
            XEvent e;
            XNextEvent(display, &e);
            if (e.type == KeyPress || e.type == KeyRelease || e.type == MotionNotify || e.type == ButtonPress ||
                e.type == ButtonRelease) {
                const bool key = e.type == KeyPress || e.type == KeyRelease;
                const unsigned code = key ? e.xkey.keycode : e.xbutton.button;
                const bool wheel = !key && e.type != MotionNotify && code >= 4 && code <= 7;
                nlohmann::json event{{"phase", phase.load()},
                                     {"kind", key                      ? "key"
                                              : e.type == MotionNotify ? "move"
                                              : wheel                  ? "wheel"
                                                                       : "button"},
                                     {"nativeTimeMs", e.xmotion.time},
                                     {"x", e.xmotion.x},
                                     {"y", e.xmotion.y},
                                     {"buttons", e.xmotion.state},
                                     {"shift", (e.xmotion.state & ShiftMask) != 0},
                                     {"ctrl", (e.xmotion.state & ControlMask) != 0},
                                     {"code", code},
                                     {"down", e.type == KeyPress || e.type == ButtonPress},
                                     {"button", code == 1   ? 0
                                                : code == 3 ? 1
                                                : code == 2 ? 2
                                                : code == 8 ? 3
                                                : code == 9 ? 4
                                                            : static_cast<int>(code)}};
                if (wheel) {
                    event["delta"] = code == 4 || code == 7 ? 120 : -120;
                    event["horizontal"] = code >= 6;
                }
                received << event.dump() << std::endl;
            }
        }
        wait(1);
    }
    sender.join();
    XWarpPointer(display, None, root, 0, 0, 0, 0, ox, oy);
    XSetInputFocus(display, previous, revert, CurrentTime);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return failures ? 2 : 0;
}
#endif
