#include "platform/target/target.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#ifdef KISEKI_HAS_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <cstdlib>
#endif
#endif

namespace kiseki::platform::target {

namespace {

ResolveResult fail(std::string error) {
    return ResolveResult{
        .ok = false,
        .code = 2,
        .window = {},
        .error = std::move(error),
    };
}

ListResult fail_list(std::string error) {
    return ListResult{
        .ok = false,
        .code = 2,
        .windows = {},
        .error = std::move(error),
    };
}

InspectResult fail_inspect(std::string error) {
    return InspectResult{
        .ok = false,
        .code = 2,
        .window = {},
        .children = {},
        .error = std::move(error),
    };
}

ResolveResult ok(TargetWindow window) {
    return ResolveResult{
        .ok = true,
        .code = 0,
        .window = std::move(window),
        .error = "",
    };
}

InspectResult ok_inspect(TargetWindow window, std::vector<TargetChildWindow> children) {
    return InspectResult{
        .ok = true,
        .code = 0,
        .window = std::move(window),
        .children = std::move(children),
        .error = "",
    };
}

ListResult ok_list(std::vector<TargetWindow> windows) {
    return ListResult{
        .ok = true,
        .code = 0,
        .windows = std::move(windows),
        .error = "",
    };
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool title_matches(const std::string& title, const std::string& needle) {
    return needle.empty() || lower_copy(title).find(lower_copy(needle)) != std::string::npos;
}

std::optional<unsigned long long> parse_window_id(const std::string& id) {
    if (id.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const unsigned long long value = std::stoull(id, &consumed, 0);
        if (consumed != id.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

bool query_matches(const TargetQuery& query, const TargetWindow& window) {
    if (!title_matches(window.title, query.title)) {
        return false;
    }
    if (query.pid != 0 && query.pid != window.pid) {
        return false;
    }
    if (!query.window_id.empty()) {
        const auto requested = parse_window_id(query.window_id);
        const auto actual = parse_window_id(window.id);
        if (!requested || !actual || *requested != *actual) {
            return false;
        }
    }
    return true;
}

ResolveResult single_match_or_error(const std::vector<TargetWindow>& matches) {
    if (matches.empty()) {
        return fail("target window not found");
    }
    if (matches.size() > 1) {
        return fail("target selector matched " + std::to_string(matches.size()) + " windows; add --target-pid or --target-window-id");
    }
    return ok(matches.front());
}

#ifdef _WIN32

std::string utf16_to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string output(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), size, nullptr, nullptr);
    return output;
}

std::string window_text(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(hwnd, wide.data(), static_cast<int>(wide.size()));
    wide.resize(static_cast<std::size_t>(copied > 0 ? copied : 0));
    return utf16_to_utf8(wide);
}

std::string window_class_name(HWND hwnd) {
    wchar_t buffer[256]{};
    const int copied = GetClassNameW(hwnd, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
    if (copied <= 0) {
        return {};
    }
    return utf16_to_utf8(std::wstring{buffer, buffer + copied});
}

std::string hwnd_id(HWND hwnd) {
    std::ostringstream stream;
    stream << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(hwnd);
    return stream.str();
}

std::optional<TargetWindow> describe_window(HWND hwnd) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return std::nullopt;
    }

    RECT rect{};
    if (GetWindowRect(hwnd, &rect) == 0) {
        return std::nullopt;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    return TargetWindow{
        .id = hwnd_id(hwnd),
        .title = window_text(hwnd),
        .pid = static_cast<std::uint32_t>(pid),
        .x = rect.left,
        .y = rect.top,
        .width = width,
        .height = height,
    };
}

std::optional<TargetChildWindow> describe_child_window(HWND hwnd, HWND parent) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return std::nullopt;
    }

    RECT rect{};
    if (GetWindowRect(hwnd, &rect) == 0) {
        return std::nullopt;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }

    return TargetChildWindow{
        .id = hwnd_id(hwnd),
        .parent_id = hwnd_id(parent),
        .title = window_text(hwnd),
        .class_name = window_class_name(hwnd),
        .x = rect.left,
        .y = rect.top,
        .width = width,
        .height = height,
    };
}

struct EnumContext {
    TargetQuery query;
    std::vector<TargetWindow> matches;
};

BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lparam) {
    auto* context = reinterpret_cast<EnumContext*>(lparam);
    const auto window = describe_window(hwnd);
    if (window && query_matches(context->query, *window)) {
        context->matches.push_back(*window);
    }
    return TRUE;
}

std::vector<TargetWindow> enumerate_windows(const TargetQuery& filter) {
    if (!filter.window_id.empty()) {
        const auto window_id = parse_window_id(filter.window_id);
        if (!window_id) {
            return {};
        }
        HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(*window_id));
        const auto window = describe_window(hwnd);
        if (window && query_matches(filter, *window)) {
            return {*window};
        }
        return {};
    }

    EnumContext context{
        .query = filter,
        .matches = {},
    };
    EnumWindows(enum_windows_proc, reinterpret_cast<LPARAM>(&context));
    return std::move(context.matches);
}

struct ChildEnumContext {
    HWND parent = nullptr;
    std::vector<TargetChildWindow> children;
};

BOOL CALLBACK enum_child_windows_proc(HWND hwnd, LPARAM lparam) {
    auto* context = reinterpret_cast<ChildEnumContext*>(lparam);
    const auto child = describe_child_window(hwnd, context->parent);
    if (child) {
        context->children.push_back(*child);
    }
    return TRUE;
}

std::vector<TargetChildWindow> enumerate_child_windows(HWND parent) {
    ChildEnumContext context{
        .parent = parent,
        .children = {},
    };
    EnumChildWindows(parent, enum_child_windows_proc, reinterpret_cast<LPARAM>(&context));
    return std::move(context.children);
}

#else
#ifdef KISEKI_HAS_X11

std::optional<unsigned long> parse_x11_window_id(const std::string& id) {
    const auto value = parse_window_id(id);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<unsigned long>(*value);
}

std::string get_window_title(Display* display, Window window) {
    std::string title;
    Atom utf8 = XInternAtom(display, "UTF8_STRING", True);
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", True);
    if (utf8 != None && net_wm_name != None) {
        Atom actual_type = None;
        int actual_format = 0;
        unsigned long items = 0;
        unsigned long bytes_after = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(display, window, net_wm_name, 0, 1024, False, utf8, &actual_type, &actual_format, &items, &bytes_after, &data) == Success && data != nullptr) {
            title.assign(reinterpret_cast<char*>(data), static_cast<std::size_t>(items));
            XFree(data);
        }
    }

    if (title.empty()) {
        char* name = nullptr;
        if (XFetchName(display, window, &name) != 0 && name != nullptr) {
            title = name;
            XFree(name);
        }
    }
    return title;
}

std::uint32_t get_window_pid(Display* display, Window window) {
    Atom pid_atom = XInternAtom(display, "_NET_WM_PID", True);
    if (pid_atom == None) {
        return 0;
    }

    Atom actual_type = None;
    int actual_format = 0;
    unsigned long items = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    std::uint32_t pid = 0;
    if (XGetWindowProperty(display, window, pid_atom, 0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &items, &bytes_after, &data) == Success && data != nullptr) {
        if (items > 0 && actual_format == 32) {
            pid = static_cast<std::uint32_t>(*reinterpret_cast<unsigned long*>(data));
        }
        XFree(data);
    }
    return pid;
}

std::string get_window_class(Display* display, Window window) {
    Atom wm_class = XInternAtom(display, "WM_CLASS", True);
    if (wm_class == None) {
        return {};
    }

    Atom actual_type = None;
    int actual_format = 0;
    unsigned long items = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    std::string result;
    if (XGetWindowProperty(display, window, wm_class, 0, 1024, False, XA_STRING, &actual_type, &actual_format, &items, &bytes_after, &data) == Success && data != nullptr) {
        const char* text = reinterpret_cast<const char*>(data);
        const char* end = text + items;
        while (text < end && *text != '\0') {
            ++text;
        }
        if (text + 1 < end) {
            result.assign(text + 1, end);
        }
        XFree(data);
    }
    return result;
}

std::string x11_window_id(Window window) {
    std::ostringstream stream;
    stream << "0x" << std::hex << static_cast<unsigned long>(window);
    return stream.str();
}

std::optional<TargetChildWindow> describe_child_window(Display* display, Window window, Window parent) {
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display, window, &attributes) == 0) {
        return std::nullopt;
    }
    if (attributes.width <= 0 || attributes.height <= 0) {
        return std::nullopt;
    }

    Window root = DefaultRootWindow(display);
    Window child = None;
    int root_x = 0;
    int root_y = 0;
    XTranslateCoordinates(display, window, root, 0, 0, &root_x, &root_y, &child);

    return TargetChildWindow{
        .id = x11_window_id(window),
        .parent_id = x11_window_id(parent),
        .title = get_window_title(display, window),
        .class_name = get_window_class(display, window),
        .x = root_x,
        .y = root_y,
        .width = attributes.width,
        .height = attributes.height,
    };
}

std::optional<TargetWindow> describe_window(Display* display, Window window) {
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display, window, &attributes) == 0) {
        return std::nullopt;
    }
    if (attributes.width <= 0 || attributes.height <= 0) {
        return std::nullopt;
    }

    Window root = DefaultRootWindow(display);
    Window child = None;
    int root_x = 0;
    int root_y = 0;
    XTranslateCoordinates(display, window, root, 0, 0, &root_x, &root_y, &child);

    return TargetWindow{
        .id = x11_window_id(window),
        .title = get_window_title(display, window),
        .pid = get_window_pid(display, window),
        .x = root_x,
        .y = root_y,
        .width = attributes.width,
        .height = attributes.height,
    };
}

void collect_windows(Display* display, Window root, std::vector<Window>& windows) {
    Window parent = None;
    Window* children = nullptr;
    unsigned int child_count = 0;
    if (XQueryTree(display, root, &root, &parent, &children, &child_count) == 0) {
        return;
    }

    for (unsigned int index = 0; index < child_count; ++index) {
        windows.push_back(children[index]);
        collect_windows(display, children[index], windows);
    }

    if (children != nullptr) {
        XFree(children);
    }
}

std::vector<TargetChildWindow> enumerate_child_windows(Display* display, Window parent) {
    std::vector<Window> windows;
    collect_windows(display, parent, windows);

    std::vector<TargetChildWindow> children;
    for (const Window window : windows) {
        const auto child = describe_child_window(display, window, parent);
        if (child) {
            children.push_back(*child);
        }
    }
    return children;
}

std::vector<TargetWindow> enumerate_windows(Display* display, const TargetQuery& filter) {
    std::vector<TargetWindow> matches;
    if (!filter.window_id.empty()) {
        const auto window_id = parse_x11_window_id(filter.window_id);
        if (!window_id) {
            return matches;
        }
        const auto window = describe_window(display, static_cast<Window>(*window_id));
        if (window && query_matches(filter, *window)) {
            matches.push_back(*window);
        }
        return matches;
    }

    std::vector<Window> windows;
    collect_windows(display, DefaultRootWindow(display), windows);
    for (const Window window : windows) {
        const auto description = describe_window(display, window);
        if (description && query_matches(filter, *description)) {
            matches.push_back(*description);
        }
    }
    return matches;
}

#endif
#endif

}

bool has_target_selector(const TargetQuery& query) {
    return !query.title.empty() || query.pid != 0 || !query.window_id.empty();
}

bool target_window_available() {
#ifdef _WIN32
    return true;
#else
#ifdef KISEKI_HAS_X11
    const char* display_name = std::getenv("DISPLAY");
    if (display_name == nullptr || display_name[0] == '\0') {
        return false;
    }
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return false;
    }
    XCloseDisplay(display);
    return true;
#else
    return false;
#endif
#endif
}

ListResult list_windows(const TargetQuery& filter) {
#ifdef _WIN32
    return ok_list(enumerate_windows(filter));
#else
#ifdef KISEKI_HAS_X11
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return fail_list("XOpenDisplay failed; DISPLAY is not available");
    }

    if (!filter.window_id.empty() && !parse_x11_window_id(filter.window_id)) {
        XCloseDisplay(display);
        return fail_list("invalid target window id");
    }

    auto matches = enumerate_windows(display, filter);
    XCloseDisplay(display);
    return ok_list(std::move(matches));
#else
    return fail_list("Linux X11 target window support was not compiled in");
#endif
#endif
}

ResolveResult resolve_window(const TargetQuery& query) {
    if (!has_target_selector(query)) {
        return fail("target selector required: use --target-title, --target-pid, or --target-window-id");
    }

    auto result = list_windows(query);
    if (!result.ok) {
        return fail(result.error);
    }
    return single_match_or_error(result.windows);
}

InspectResult inspect_window(const TargetQuery& query) {
    const auto resolved = resolve_window(query);
    if (!resolved.ok) {
        return fail_inspect(resolved.error);
    }

#ifdef _WIN32
    const auto value = parse_window_id(resolved.window.id);
    if (!value) {
        return fail_inspect("resolved target window id is invalid");
    }
    HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(*value));
    if (!IsWindow(hwnd)) {
        return fail_inspect("resolved target window is no longer valid");
    }
    return ok_inspect(resolved.window, enumerate_child_windows(hwnd));
#else
#ifdef KISEKI_HAS_X11
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return fail_inspect("XOpenDisplay failed; DISPLAY is not available");
    }
    const auto value = parse_x11_window_id(resolved.window.id);
    if (!value) {
        XCloseDisplay(display);
        return fail_inspect("resolved target window id is invalid");
    }
    auto children = enumerate_child_windows(display, static_cast<Window>(*value));
    XCloseDisplay(display);
    return ok_inspect(resolved.window, std::move(children));
#else
    return fail_inspect("Linux X11 target inspection support was not compiled in");
#endif
#endif
}

}
