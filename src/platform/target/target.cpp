#include "platform/target/target.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#else
#ifdef KISEKI_HAS_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <cstdlib>

#include "platform/target/x11_client_windows.hpp"
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

// "0x..." is hexadecimal; anything else is decimal (CUA and other providers print decimal ids).
std::optional<unsigned long long> parse_window_id(const std::string& id) {
    if (id.empty()) {
        return std::nullopt;
    }
    const bool hex = id.size() > 2 && id[0] == '0' && (id[1] == 'x' || id[1] == 'X');
    const std::string digits = hex ? id.substr(2) : id;
    if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [hex](unsigned char c) {
            return hex ? std::isxdigit(c) != 0 : std::isdigit(c) != 0;
        })) {
        return std::nullopt;
    }
    try {
        return std::stoull(digits, nullptr, hex ? 16 : 10);
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

#elif defined(__APPLE__)

std::string cf_string_to_utf8(CFStringRef value) {
    if (value == nullptr) {
        return {};
    }

    const CFIndex length = CFStringGetLength(value);
    const CFIndex max_size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    if (max_size <= 1) {
        return {};
    }

    std::string output(static_cast<std::size_t>(max_size), '\0');
    if (!CFStringGetCString(value, output.data(), max_size, kCFStringEncodingUTF8)) {
        return {};
    }
    output.resize(std::strlen(output.c_str()));
    return output;
}

std::optional<long long> cf_number_to_i64(CFDictionaryRef dictionary, CFStringRef key) {
    CFNumberRef value = static_cast<CFNumberRef>(CFDictionaryGetValue(dictionary, key));
    if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID()) {
        return std::nullopt;
    }

    long long result = 0;
    if (!CFNumberGetValue(value, kCFNumberLongLongType, &result)) {
        return std::nullopt;
    }
    return result;
}

std::optional<double> cf_number_to_double(CFDictionaryRef dictionary, CFStringRef key) {
    CFNumberRef value = static_cast<CFNumberRef>(CFDictionaryGetValue(dictionary, key));
    if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID()) {
        return std::nullopt;
    }

    double result = 0.0;
    if (!CFNumberGetValue(value, kCFNumberDoubleType, &result)) {
        return std::nullopt;
    }
    return result;
}

std::string cf_dictionary_string(CFDictionaryRef dictionary, CFStringRef key) {
    CFStringRef value = static_cast<CFStringRef>(CFDictionaryGetValue(dictionary, key));
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) {
        return {};
    }
    return cf_string_to_utf8(value);
}

std::string cg_window_id(CGWindowID window) {
    std::ostringstream stream;
    stream << "0x" << std::hex << static_cast<unsigned long>(window);
    return stream.str();
}

std::optional<CGWindowID> parse_cg_window_id(const std::string& id) {
    const auto value = parse_window_id(id);
    if (!value || *value > static_cast<unsigned long long>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<CGWindowID>(*value);
}

std::optional<TargetWindow> describe_cg_window(CFDictionaryRef dictionary) {
    const auto window_number = cf_number_to_i64(dictionary, kCGWindowNumber);
    if (!window_number ||
        *window_number <= 0 ||
        *window_number > static_cast<long long>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }

    const auto layer = cf_number_to_i64(dictionary, kCGWindowLayer).value_or(0);
    if (layer != 0) {
        return std::nullopt;
    }

    const auto alpha = cf_number_to_double(dictionary, kCGWindowAlpha).value_or(1.0);
    if (alpha <= 0.0) {
        return std::nullopt;
    }

    CFDictionaryRef bounds_value = static_cast<CFDictionaryRef>(CFDictionaryGetValue(dictionary, kCGWindowBounds));
    if (bounds_value == nullptr || CFGetTypeID(bounds_value) != CFDictionaryGetTypeID()) {
        return std::nullopt;
    }

    CGRect bounds{};
    if (!CGRectMakeWithDictionaryRepresentation(bounds_value, &bounds)) {
        return std::nullopt;
    }
    if (bounds.size.width <= 0.0 || bounds.size.height <= 0.0) {
        return std::nullopt;
    }

    const std::string owner = cf_dictionary_string(dictionary, kCGWindowOwnerName);
    const std::string name = cf_dictionary_string(dictionary, kCGWindowName);
    std::string title = name;
    if (!owner.empty() && !name.empty()) {
        title = owner + " - " + name;
    } else if (title.empty()) {
        title = owner;
    }

    const auto pid = cf_number_to_i64(dictionary, kCGWindowOwnerPID).value_or(0);
    return TargetWindow{
        .id = cg_window_id(static_cast<CGWindowID>(*window_number)),
        .title = title,
        .pid = pid > 0 && pid <= static_cast<long long>(std::numeric_limits<std::uint32_t>::max()) ? static_cast<std::uint32_t>(pid) : 0U,
        .x = static_cast<int>(std::llround(bounds.origin.x)),
        .y = static_cast<int>(std::llround(bounds.origin.y)),
        .width = static_cast<int>(std::llround(bounds.size.width)),
        .height = static_cast<int>(std::llround(bounds.size.height)),
    };
}

std::vector<TargetWindow> enumerate_windows(const TargetQuery& filter) {
    CGWindowListOption options = kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements;
    CGWindowID relative_window = kCGNullWindowID;
    if (!filter.window_id.empty()) {
        const auto window_id = parse_cg_window_id(filter.window_id);
        if (!window_id) {
            return {};
        }
        options = kCGWindowListOptionIncludingWindow;
        relative_window = *window_id;
    }

    CFArrayRef windows = CGWindowListCopyWindowInfo(options, relative_window);
    if (windows == nullptr) {
        return {};
    }

    std::vector<TargetWindow> matches;
    const CFIndex count = CFArrayGetCount(windows);
    for (CFIndex index = 0; index < count; ++index) {
        CFDictionaryRef dictionary = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, index));
        if (dictionary == nullptr || CFGetTypeID(dictionary) != CFDictionaryGetTypeID()) {
            continue;
        }
        const auto window = describe_cg_window(dictionary);
        if (window && query_matches(filter, *window)) {
            matches.push_back(*window);
        }
    }
    CFRelease(windows);
    return matches;
}

#else
#ifdef KISEKI_HAS_X11

std::string x11_open_error() {
    const char* name = std::getenv("DISPLAY");
    if (name == nullptr || name[0] == '\0') {
        return "XOpenDisplay failed: DISPLAY is not set";
    }
    return std::string{"XOpenDisplay failed for DISPLAY="} + name + "; no X server accepted the connection";
}

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

int ignore_x11_error(Display*, XErrorEvent*) {
    return 0;
}

// Windows can disappear between XQueryTree and later requests; Xlib's default
// handler would terminate the process on the resulting BadWindow error.
class ScopedX11ErrorTrap {
public:
    explicit ScopedX11ErrorTrap(Display* display)
        : display_{display}, previous_{XSetErrorHandler(ignore_x11_error)} {
    }
    ScopedX11ErrorTrap(const ScopedX11ErrorTrap&) = delete;
    ScopedX11ErrorTrap& operator=(const ScopedX11ErrorTrap&) = delete;
    ~ScopedX11ErrorTrap() {
        XSync(display_, False);
        XSetErrorHandler(previous_);
    }

private:
    Display* display_;
    XErrorHandler previous_;
};

std::optional<std::vector<unsigned long>> read_cardinal_property(Display* display, Window window, Atom property, Atom type) {
    if (property == None) {
        return std::nullopt;
    }
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long items = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, window, property, 0, 1L << 20, False, type, &actual_type, &actual_format, &items,
                           &bytes_after, &data) != Success) {
        return std::nullopt;
    }
    std::optional<std::vector<unsigned long>> values;
    if (actual_type == type && actual_format == 32) {
        // Xlib returns 32-bit properties as an array of long.
        const auto* longs = reinterpret_cast<const unsigned long*>(data);
        values.emplace(longs, longs + items);
    }
    if (data != nullptr) {
        XFree(data);
    }
    return values;
}

// libXRes is loaded at runtime so builds do not need its headers; these mirror
// the stable XResClientIdSpec/XResClientIdValue ABI from <X11/extensions/XRes.h>.
struct XResClientIdSpecAbi {
    XID client;
    unsigned int mask;
};

struct XResClientIdValueAbi {
    XResClientIdSpecAbi spec;
    long length;
    void* value;
};

struct XResLibrary {
    using QueryExtension = Bool (*)(Display*, int*, int*);
    using QueryVersion = Status (*)(Display*, int*, int*);
    using QueryClientIds = Status (*)(Display*, long, XResClientIdSpecAbi*, long*, XResClientIdValueAbi**);
    using GetClientPid = pid_t (*)(XResClientIdValueAbi*);
    using ClientIdsDestroy = void (*)(long, XResClientIdValueAbi*);

    QueryExtension query_extension = nullptr;
    QueryVersion query_version = nullptr;
    QueryClientIds query_client_ids = nullptr;
    GetClientPid get_client_pid = nullptr;
    ClientIdsDestroy client_ids_destroy = nullptr;

    bool loaded() const {
        return query_extension != nullptr && query_version != nullptr && query_client_ids != nullptr &&
               get_client_pid != nullptr && client_ids_destroy != nullptr;
    }
};

const XResLibrary& xres_library() {
    static const XResLibrary library = [] {
        XResLibrary result;
        void* handle = dlopen("libXRes.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (handle == nullptr) {
            handle = dlopen("libXRes.so", RTLD_LAZY | RTLD_LOCAL);
        }
        if (handle == nullptr) {
            return result;
        }
        result.query_extension = reinterpret_cast<XResLibrary::QueryExtension>(dlsym(handle, "XResQueryExtension"));
        result.query_version = reinterpret_cast<XResLibrary::QueryVersion>(dlsym(handle, "XResQueryVersion"));
        result.query_client_ids = reinterpret_cast<XResLibrary::QueryClientIds>(dlsym(handle, "XResQueryClientIds"));
        result.get_client_pid = reinterpret_cast<XResLibrary::GetClientPid>(dlsym(handle, "XResGetClientPid"));
        result.client_ids_destroy = reinterpret_cast<XResLibrary::ClientIdsDestroy>(dlsym(handle, "XResClientIdsDestroy"));
        return result;
    }();
    return library;
}

// Resolves window owners: _NET_WM_PID when the client sets it, otherwise the
// X server's own record of the owning client's pid (X-Resource 1.2), else unknown.
class X11PidResolver {
public:
    explicit X11PidResolver(Display* display)
        : display_{display}, pid_atom_{XInternAtom(display, "_NET_WM_PID", True)} {
    }

    std::pair<std::uint32_t, std::string> resolve(Window window) {
        if (const auto values = read_cardinal_property(display_, window, pid_atom_, XA_CARDINAL);
            values && !values->empty() && values->front() > 0 &&
            values->front() <= std::numeric_limits<std::uint32_t>::max()) {
            return {static_cast<std::uint32_t>(values->front()), "_NET_WM_PID"};
        }
        if (const auto pid = query_xres(window)) {
            return {*pid, "x-resource"};
        }
        return {0U, "unknown"};
    }

private:
    bool xres_usable() {
        if (!checked_) {
            checked_ = true;
            const auto& library = xres_library();
            int event_base = 0;
            int error_base = 0;
            int major = 0;
            int minor = 0;
            usable_ = library.loaded() && library.query_extension(display_, &event_base, &error_base) &&
                      library.query_version(display_, &major, &minor) != 0 && (major > 1 || (major == 1 && minor >= 2));
        }
        return usable_;
    }

    std::optional<std::uint32_t> query_xres(Window window) {
        if (!xres_usable()) {
            return std::nullopt;
        }
        const auto& library = xres_library();
        constexpr unsigned int pid_mask = 1U << 1; // XRES_CLIENT_ID_PID_MASK
        XResClientIdSpecAbi spec{window, pid_mask};
        long count = 0;
        XResClientIdValueAbi* values = nullptr;
        if (library.query_client_ids(display_, 1, &spec, &count, &values) != Success || values == nullptr) {
            return std::nullopt;
        }
        std::optional<std::uint32_t> pid;
        for (long index = 0; index < count && !pid; ++index) {
            const pid_t value = library.get_client_pid(&values[index]);
            if (value > 0) {
                pid = static_cast<std::uint32_t>(value);
            }
        }
        library.client_ids_destroy(count, values);
        return pid;
    }

    Display* display_;
    Atom pid_atom_;
    bool checked_ = false;
    bool usable_ = false;
};

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

std::optional<TargetWindow> describe_window(Display* display, Window window, X11PidResolver& pids) {
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

    auto [pid, pid_source] = pids.resolve(window);
    return TargetWindow{
        .id = x11_window_id(window),
        .title = get_window_title(display, window),
        .pid = pid,
        .x = root_x,
        .y = root_y,
        .width = attributes.width,
        .height = attributes.height,
        .pid_source = std::move(pid_source),
    };
}

std::vector<Window> query_children(Display* display, Window window) {
    Window root = None;
    Window parent = None;
    Window* children = nullptr;
    unsigned int child_count = 0;
    std::vector<Window> result;
    if (XQueryTree(display, window, &root, &parent, &children, &child_count) == 0) {
        return result;
    }
    result.assign(children, children + child_count);
    if (children != nullptr) {
        XFree(children);
    }
    return result;
}

// _NET_CLIENT_LIST is only trusted while the EWMH check window that the window
// manager advertises still points to itself; a stale list survives a WM exit.
std::optional<std::vector<detail::X11WindowId>> verified_net_client_list(Display* display, Window root) {
    const Atom check = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", True);
    const Atom client_list = XInternAtom(display, "_NET_CLIENT_LIST", True);
    if (check == None || client_list == None) {
        return std::nullopt;
    }
    const auto root_check = read_cardinal_property(display, root, check, XA_WINDOW);
    if (!root_check || root_check->empty()) {
        return std::nullopt;
    }
    const auto self_check = read_cardinal_property(display, static_cast<Window>(root_check->front()), check, XA_WINDOW);
    if (!self_check || self_check->empty() || self_check->front() != root_check->front()) {
        return std::nullopt;
    }
    const auto clients = read_cardinal_property(display, root, client_list, XA_WINDOW);
    if (!clients) {
        return std::nullopt;
    }
    return std::vector<detail::X11WindowId>(clients->begin(), clients->end());
}

std::vector<Window> list_target_windows(Display* display) {
    const Window root = DefaultRootWindow(display);
    const Atom wm_state = XInternAtom(display, "WM_STATE", True);
    const detail::X11TreeAccess access{
        .children = [display](detail::X11WindowId window) {
            const auto children = query_children(display, static_cast<Window>(window));
            return std::vector<detail::X11WindowId>(children.begin(), children.end());
        },
        .has_wm_state = [display, wm_state](detail::X11WindowId window) {
            if (wm_state == None) {
                return false;
            }
            Atom actual_type = None;
            int actual_format = 0;
            unsigned long items = 0;
            unsigned long bytes_after = 0;
            unsigned char* data = nullptr;
            const bool found = XGetWindowProperty(display, static_cast<Window>(window), wm_state, 0, 0, False,
                                                  AnyPropertyType, &actual_type, &actual_format, &items, &bytes_after,
                                                  &data) == Success &&
                               actual_type != None;
            if (data != nullptr) {
                XFree(data);
            }
            return found;
        },
        .is_unmanaged_top_level = [display](detail::X11WindowId window) {
            XWindowAttributes attributes{};
            return XGetWindowAttributes(display, static_cast<Window>(window), &attributes) != 0 &&
                   attributes.map_state == IsViewable && attributes.c_class == InputOutput &&
                   !attributes.override_redirect;
        },
    };
    const auto selection = detail::select_x11_target_windows(root, verified_net_client_list(display, root), access);
    return std::vector<Window>(selection.windows.begin(), selection.windows.end());
}

void collect_windows(Display* display, Window root, std::vector<Window>& windows) {
    for (const Window child : query_children(display, root)) {
        windows.push_back(child);
        collect_windows(display, child, windows);
    }
}

std::vector<TargetChildWindow> enumerate_child_windows(Display* display, Window parent) {
    const ScopedX11ErrorTrap trap{display};
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
    const ScopedX11ErrorTrap trap{display};
    X11PidResolver pids{display};
    std::vector<TargetWindow> matches;
    if (!filter.window_id.empty()) {
        // An explicit id may name any window, including a child receiver.
        const auto window_id = parse_x11_window_id(filter.window_id);
        if (!window_id) {
            return matches;
        }
        const auto window = describe_window(display, static_cast<Window>(*window_id), pids);
        if (window && query_matches(filter, *window)) {
            matches.push_back(*window);
        }
        return matches;
    }

    for (const Window window : list_target_windows(display)) {
        const auto description = describe_window(display, window, pids);
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

std::optional<std::uint64_t> window_id_value(const std::string& id) {
    const auto value = parse_window_id(id);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(*value);
}

bool target_window_available() {
#ifdef _WIN32
    return true;
#elif defined(__APPLE__)
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
    if (windows == nullptr) {
        return false;
    }
    CFRelease(windows);
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
#elif defined(__APPLE__)
    return ok_list(enumerate_windows(filter));
#else
#ifdef KISEKI_HAS_X11
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return fail_list(x11_open_error());
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
#elif defined(__APPLE__)
    const auto value = parse_cg_window_id(resolved.window.id);
    if (!value) {
        return fail_inspect("resolved target window id is invalid");
    }
    return ok_inspect(resolved.window, {});
#else
#ifdef KISEKI_HAS_X11
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return fail_inspect(x11_open_error());
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
