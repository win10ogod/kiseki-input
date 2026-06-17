#include "platform/observe/ui_observation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#include <uiautomation.h>
#elif defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace kiseki::platform::observe {

namespace {

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

UiObservationResult fail(std::string error) {
    return UiObservationResult{
        .ok = false,
        .code = 2,
        .error = std::move(error),
    };
}

UiObservationResult from_window_tree(
    const kiseki::platform::target::TargetQuery& query,
    std::string fallback_reason = {}) {
    const auto inspected = kiseki::platform::target::inspect_window(query);
    if (!inspected.ok) {
        return fail(inspected.error);
    }

    UiObservationResult result{
        .ok = true,
        .code = 0,
        .source = "platform-window-tree",
        .visual = false,
        .coordinate_space = "screen",
        .target = inspected.window,
        .fallback_reason = std::move(fallback_reason),
    };

    for (const auto& child : inspected.children) {
        result.elements.push_back(UiElement{
            .kind = "child-window",
            .id = child.id,
            .parent_id = child.parent_id,
            .depth = 1,
            .name = child.title,
            .title = child.title,
            .class_name = child.class_name,
            .has_bounds = true,
            .x = child.x,
            .y = child.y,
            .width = child.width,
            .height = child.height,
        });
    }
    return result;
}

#ifdef _WIN32

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }

    ~ComPtr() {
        reset();
    }

    T* get() const {
        return ptr_;
    }

    T** put() {
        reset();
        return &ptr_;
    }

    T* operator->() const {
        return ptr_;
    }

    explicit operator bool() const {
        return ptr_ != nullptr;
    }

    void reset() {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

class ComApartment {
public:
    ComApartment() {
        hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        initialized_ = SUCCEEDED(hr_);
        if (hr_ == RPC_E_CHANGED_MODE) {
            initialized_ = false;
        }
    }

    ~ComApartment() {
        if (initialized_) {
            CoUninitialize();
        }
    }

    HRESULT status() const {
        return hr_;
    }

private:
    HRESULT hr_ = S_OK;
    bool initialized_ = false;
};

std::string hresult_text(HRESULT hr) {
    std::ostringstream stream;
    stream << "0x" << std::hex << static_cast<unsigned long>(hr);
    return stream.str();
}

std::string utf16_to_utf8(const wchar_t* text, int length) {
    if (text == nullptr || length <= 0) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string output(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, output.data(), size, nullptr, nullptr);
    return output;
}

std::string take_bstr(BSTR value) {
    if (value == nullptr) {
        return {};
    }
    const auto length = static_cast<int>(SysStringLen(value));
    std::string output = utf16_to_utf8(value, length);
    SysFreeString(value);
    return output;
}

std::string element_bstr(IUIAutomationElement* element, HRESULT (__stdcall IUIAutomationElement::*getter)(BSTR*)) {
    BSTR value = nullptr;
    if (element == nullptr || FAILED((element->*getter)(&value))) {
        return {};
    }
    return take_bstr(value);
}

std::string hwnd_id(HWND hwnd) {
    std::ostringstream stream;
    stream << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(hwnd);
    return stream.str();
}

bool parse_hwnd_id(const std::string& id, HWND& hwnd) {
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(id, &consumed, 0);
        if (consumed != id.size()) {
            return false;
        }
        hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value));
        return true;
    } catch (...) {
        return false;
    }
}

UiElement describe_uia_element(
    IUIAutomationElement* element,
    std::string id,
    std::string parent_id,
    int depth) {
    UiElement output{
        .kind = "uia-element",
        .id = std::move(id),
        .parent_id = std::move(parent_id),
        .depth = depth,
    };

    output.name = element_bstr(element, &IUIAutomationElement::get_CurrentName);
    output.title = output.name;
    output.automation_id = element_bstr(element, &IUIAutomationElement::get_CurrentAutomationId);
    output.class_name = element_bstr(element, &IUIAutomationElement::get_CurrentClassName);
    output.localized_control_type = element_bstr(element, &IUIAutomationElement::get_CurrentLocalizedControlType);
    output.framework_id = element_bstr(element, &IUIAutomationElement::get_CurrentFrameworkId);

    CONTROLTYPEID control_type = 0;
    if (SUCCEEDED(element->get_CurrentControlType(&control_type))) {
        output.control_type = control_type;
    }

    int process_id = 0;
    if (SUCCEEDED(element->get_CurrentProcessId(&process_id))) {
        output.process_id = process_id;
    }

    RECT rect{};
    if (SUCCEEDED(element->get_CurrentBoundingRectangle(&rect))) {
        output.has_bounds = true;
        output.x = rect.left;
        output.y = rect.top;
        output.width = rect.right - rect.left;
        output.height = rect.bottom - rect.top;
    }

    BOOL enabled = FALSE;
    if (SUCCEEDED(element->get_CurrentIsEnabled(&enabled))) {
        output.has_enabled = true;
        output.enabled = enabled != FALSE;
    }

    BOOL offscreen = FALSE;
    if (SUCCEEDED(element->get_CurrentIsOffscreen(&offscreen))) {
        output.has_offscreen = true;
        output.offscreen = offscreen != FALSE;
    }

    return output;
}

struct WalkContext {
    IUIAutomationTreeWalker* walker = nullptr;
    int max_depth = 4;
    int max_elements = 256;
    int next_id = 1;
    bool truncated = false;
    std::vector<UiElement> elements;
};

void walk_uia_children(WalkContext& context, IUIAutomationElement* parent, const std::string& parent_id, int depth) {
    if (context.truncated || depth > context.max_depth) {
        return;
    }

    ComPtr<IUIAutomationElement> child;
    HRESULT hr = context.walker->GetFirstChildElement(parent, child.put());
    while (SUCCEEDED(hr) && child.get() != nullptr) {
        if (static_cast<int>(context.elements.size()) >= context.max_elements) {
            context.truncated = true;
            return;
        }

        const std::string id = "uia:" + std::to_string(context.next_id++);
        context.elements.push_back(describe_uia_element(child.get(), id, parent_id, depth));
        walk_uia_children(context, child.get(), id, depth + 1);
        if (context.truncated) {
            return;
        }

        ComPtr<IUIAutomationElement> next;
        hr = context.walker->GetNextSiblingElement(child.get(), next.put());
        child = std::move(next);
    }
}

UiObservationResult observe_windows_uia(const UiObservationOptions& options) {
    const auto resolved = kiseki::platform::target::resolve_window(options.target);
    if (!resolved.ok) {
        return fail(resolved.error);
    }

    HWND hwnd = nullptr;
    if (!parse_hwnd_id(resolved.window.id, hwnd) || !IsWindow(hwnd)) {
        return fail("resolved target window id is not a live HWND");
    }

    ComApartment apartment;
    if (FAILED(apartment.status()) && apartment.status() != RPC_E_CHANGED_MODE) {
        return fail("CoInitializeEx failed for Windows UI Automation: " + hresult_text(apartment.status()));
    }

    ComPtr<IUIAutomation> automation;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(automation.put()));
    if (FAILED(hr) || !automation) {
        return fail("CoCreateInstance(CUIAutomation) failed: " + hresult_text(hr));
    }

    ComPtr<IUIAutomationElement> root;
    hr = automation->ElementFromHandle(hwnd, root.put());
    if (FAILED(hr) || !root) {
        return fail("UIAutomation ElementFromHandle failed: " + hresult_text(hr));
    }

    ComPtr<IUIAutomationTreeWalker> walker;
    hr = automation->get_ControlViewWalker(walker.put());
    if (FAILED(hr) || !walker) {
        return fail("UIAutomation get_ControlViewWalker failed: " + hresult_text(hr));
    }

    WalkContext context{
        .walker = walker.get(),
        .max_depth = options.max_depth,
        .max_elements = options.max_elements,
    };

    const std::string root_id = "uia:0";
    if (context.max_elements > 0) {
        context.elements.push_back(describe_uia_element(root.get(), root_id, hwnd_id(hwnd), 0));
    }
    walk_uia_children(context, root.get(), root_id, 1);

    return UiObservationResult{
        .ok = true,
        .code = 0,
        .source = "windows-uia",
        .visual = false,
        .coordinate_space = "screen",
        .target = resolved.window,
        .elements = std::move(context.elements),
        .truncated = context.truncated,
    };
}

#elif defined(__APPLE__)

template <typename T>
class CFPtr {
public:
    CFPtr() = default;
    explicit CFPtr(T ref) : ref_(ref) {}
    CFPtr(const CFPtr&) = delete;
    CFPtr& operator=(const CFPtr&) = delete;

    CFPtr(CFPtr&& other) noexcept : ref_(std::exchange(other.ref_, nullptr)) {}

    CFPtr& operator=(CFPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ref_ = std::exchange(other.ref_, nullptr);
        }
        return *this;
    }

    ~CFPtr() {
        reset();
    }

    T get() const {
        return ref_;
    }

    explicit operator bool() const {
        return ref_ != nullptr;
    }

    void reset(T ref = nullptr) {
        if (ref_ != nullptr) {
            CFRelease(ref_);
        }
        ref_ = ref;
    }

private:
    T ref_ = nullptr;
};

std::string ax_error_text(AXError error) {
    switch (error) {
    case kAXErrorSuccess: return "success";
    case kAXErrorFailure: return "failure";
    case kAXErrorIllegalArgument: return "illegal argument";
    case kAXErrorInvalidUIElement: return "invalid UI element";
    case kAXErrorInvalidUIElementObserver: return "invalid UI element observer";
    case kAXErrorCannotComplete: return "cannot complete";
    case kAXErrorAttributeUnsupported: return "attribute unsupported";
    case kAXErrorActionUnsupported: return "action unsupported";
    case kAXErrorNotificationUnsupported: return "notification unsupported";
    case kAXErrorNotImplemented: return "not implemented";
    case kAXErrorNotificationAlreadyRegistered: return "notification already registered";
    case kAXErrorNotificationNotRegistered: return "notification not registered";
    case kAXErrorAPIDisabled: return "API disabled";
    case kAXErrorNoValue: return "no value";
    case kAXErrorParameterizedAttributeUnsupported: return "parameterized attribute unsupported";
    case kAXErrorNotEnoughPrecision: return "not enough precision";
    default: return "AXError " + std::to_string(static_cast<int>(error));
    }
}

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
    output.resize(std::char_traits<char>::length(output.c_str()));
    return output;
}

std::string cf_type_to_string(CFTypeRef value) {
    if (value == nullptr) {
        return {};
    }
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        return cf_string_to_utf8(static_cast<CFStringRef>(value));
    }
    if (CFGetTypeID(value) == CFBooleanGetTypeID()) {
        return CFBooleanGetValue(static_cast<CFBooleanRef>(value)) ? "true" : "false";
    }
    if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        double number = 0.0;
        if (CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &number)) {
            std::ostringstream stream;
            stream << number;
            return stream.str();
        }
    }
    return {};
}

CFPtr<CFTypeRef> copy_ax_attribute(AXUIElementRef element, CFStringRef attribute) {
    CFTypeRef value = nullptr;
    if (element == nullptr || AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess) {
        return {};
    }
    return CFPtr<CFTypeRef>{value};
}

std::string ax_string_attribute(AXUIElementRef element, CFStringRef attribute) {
    auto value = copy_ax_attribute(element, attribute);
    if (!value) {
        return {};
    }
    return cf_type_to_string(value.get());
}

std::optional<bool> ax_bool_attribute(AXUIElementRef element, CFStringRef attribute) {
    auto value = copy_ax_attribute(element, attribute);
    if (!value || CFGetTypeID(value.get()) != CFBooleanGetTypeID()) {
        return std::nullopt;
    }
    return CFBooleanGetValue(static_cast<CFBooleanRef>(value.get())) != false;
}

std::optional<CGPoint> ax_point_attribute(AXUIElementRef element, CFStringRef attribute) {
    auto value = copy_ax_attribute(element, attribute);
    if (!value || CFGetTypeID(value.get()) != AXValueGetTypeID()) {
        return std::nullopt;
    }
    auto ax_value = static_cast<AXValueRef>(value.get());
    if (AXValueGetType(ax_value) != kAXValueCGPointType) {
        return std::nullopt;
    }
    CGPoint point{};
    if (!AXValueGetValue(ax_value, static_cast<AXValueType>(kAXValueCGPointType), &point)) {
        return std::nullopt;
    }
    return point;
}

std::optional<CGSize> ax_size_attribute(AXUIElementRef element, CFStringRef attribute) {
    auto value = copy_ax_attribute(element, attribute);
    if (!value || CFGetTypeID(value.get()) != AXValueGetTypeID()) {
        return std::nullopt;
    }
    auto ax_value = static_cast<AXValueRef>(value.get());
    if (AXValueGetType(ax_value) != kAXValueCGSizeType) {
        return std::nullopt;
    }
    CGSize size{};
    if (!AXValueGetValue(ax_value, static_cast<AXValueType>(kAXValueCGSizeType), &size)) {
        return std::nullopt;
    }
    return size;
}

std::optional<CGRect> ax_bounds(AXUIElementRef element) {
    const auto point = ax_point_attribute(element, kAXPositionAttribute);
    const auto size = ax_size_attribute(element, kAXSizeAttribute);
    if (!point || !size) {
        return std::nullopt;
    }
    return CGRect{*point, *size};
}

UiElement describe_ax_element(
    AXUIElementRef element,
    std::string id,
    std::string parent_id,
    int depth) {
    UiElement output{
        .kind = "ax-element",
        .id = std::move(id),
        .parent_id = std::move(parent_id),
        .depth = depth,
    };

    output.role = ax_string_attribute(element, kAXRoleAttribute);
    output.subrole = ax_string_attribute(element, kAXSubroleAttribute);
    output.name = ax_string_attribute(element, kAXTitleAttribute);
    output.title = output.name;
    output.description = ax_string_attribute(element, kAXDescriptionAttribute);
    output.value = ax_string_attribute(element, kAXValueAttribute);
    output.automation_id = ax_string_attribute(element, kAXIdentifierAttribute);
    output.localized_control_type = output.role;
    output.framework_id = "AX";

    const auto enabled = ax_bool_attribute(element, kAXEnabledAttribute);
    if (enabled) {
        output.has_enabled = true;
        output.enabled = *enabled;
    }

    const auto bounds = ax_bounds(element);
    if (bounds) {
        output.has_bounds = true;
        output.x = static_cast<int>(std::lround(bounds->origin.x));
        output.y = static_cast<int>(std::lround(bounds->origin.y));
        output.width = static_cast<int>(std::lround(bounds->size.width));
        output.height = static_cast<int>(std::lround(bounds->size.height));
    }

    return output;
}

CFPtr<CFArrayRef> ax_array_attribute(AXUIElementRef element, CFStringRef attribute) {
    auto value = copy_ax_attribute(element, attribute);
    if (!value || CFGetTypeID(value.get()) != CFArrayGetTypeID()) {
        return {};
    }
    auto array = static_cast<CFArrayRef>(value.get());
    CFRetain(array);
    return CFPtr<CFArrayRef>{array};
}

struct AxWalkContext {
    int max_depth = 4;
    int max_elements = 256;
    int next_id = 1;
    bool truncated = false;
    std::vector<UiElement> elements;
};

void walk_ax_children(AxWalkContext& context, AXUIElementRef parent, const std::string& parent_id, int depth) {
    if (context.truncated || depth > context.max_depth) {
        return;
    }

    auto children = ax_array_attribute(parent, kAXChildrenAttribute);
    if (!children) {
        return;
    }

    const CFIndex count = CFArrayGetCount(children.get());
    for (CFIndex index = 0; index < count; ++index) {
        if (static_cast<int>(context.elements.size()) >= context.max_elements) {
            context.truncated = true;
            return;
        }
        auto child = static_cast<AXUIElementRef>(const_cast<void*>(CFArrayGetValueAtIndex(children.get(), index)));
        if (child == nullptr || CFGetTypeID(child) != AXUIElementGetTypeID()) {
            continue;
        }

        const std::string id = "ax:" + std::to_string(context.next_id++);
        context.elements.push_back(describe_ax_element(child, id, parent_id, depth));
        walk_ax_children(context, child, id, depth + 1);
        if (context.truncated) {
            return;
        }
    }
}

double bounds_distance(const kiseki::platform::target::TargetWindow& target, const CGRect& bounds) {
    return std::abs(bounds.origin.x - target.x) +
           std::abs(bounds.origin.y - target.y) +
           std::abs(bounds.size.width - target.width) +
           std::abs(bounds.size.height - target.height);
}

CFPtr<AXUIElementRef> matching_ax_window(AXUIElementRef app, const kiseki::platform::target::TargetWindow& target) {
    auto windows = ax_array_attribute(app, kAXWindowsAttribute);
    if (!windows) {
        return {};
    }

    AXUIElementRef best = nullptr;
    double best_score = 1.0e12;
    const std::string target_title = lower_copy(target.title);
    const CFIndex count = CFArrayGetCount(windows.get());
    for (CFIndex index = 0; index < count; ++index) {
        auto window = static_cast<AXUIElementRef>(const_cast<void*>(CFArrayGetValueAtIndex(windows.get(), index)));
        if (window == nullptr || CFGetTypeID(window) != AXUIElementGetTypeID()) {
            continue;
        }
        double score = 100000.0;
        if (const auto bounds = ax_bounds(window)) {
            score = bounds_distance(target, *bounds);
        }
        const auto title = lower_copy(ax_string_attribute(window, kAXTitleAttribute));
        if (!target_title.empty() && !title.empty() && (title.find(target_title) != std::string::npos || target_title.find(title) != std::string::npos)) {
            score -= 1000.0;
        }
        if (score < best_score) {
            best = window;
            best_score = score;
        }
    }

    if (best == nullptr) {
        return {};
    }
    CFRetain(best);
    return CFPtr<AXUIElementRef>{best};
}

UiObservationResult observe_macos_ax(const UiObservationOptions& options) {
    const auto resolved = kiseki::platform::target::resolve_window(options.target);
    if (!resolved.ok) {
        return fail(resolved.error);
    }

    if (!AXIsProcessTrusted()) {
        return fail("macOS Accessibility permission is required for observe ui provider 'ax'; grant the terminal/Codex host Accessibility permission and rerun");
    }

    CFPtr<AXUIElementRef> app{AXUIElementCreateApplication(static_cast<pid_t>(resolved.window.pid))};
    if (!app) {
        return fail("AXUIElementCreateApplication failed for target pid " + std::to_string(resolved.window.pid));
    }

    auto root = matching_ax_window(app.get(), resolved.window);
    std::string root_parent_id = resolved.window.id;
    std::string fallback_reason;
    if (!root) {
        fallback_reason = "matching AX window was not found; returned target application AX tree";
        CFRetain(app.get());
        root.reset(app.get());
        root_parent_id = "pid:" + std::to_string(resolved.window.pid);
    }

    AxWalkContext context{
        .max_depth = options.max_depth,
        .max_elements = options.max_elements,
    };

    const std::string root_id = "ax:0";
    context.elements.push_back(describe_ax_element(root.get(), root_id, root_parent_id, 0));
    walk_ax_children(context, root.get(), root_id, 1);

    return UiObservationResult{
        .ok = true,
        .code = 0,
        .source = "macos-ax",
        .visual = false,
        .coordinate_space = "screen",
        .target = resolved.window,
        .elements = std::move(context.elements),
        .truncated = context.truncated,
        .fallback_reason = std::move(fallback_reason),
    };
}

#endif

} // namespace

UiObservationResult observe_ui(const UiObservationOptions& options) {
    const std::string provider = lower_copy(options.provider.empty() ? "auto" : options.provider);
    if (options.max_depth < 0) {
        return fail("observe ui --max-depth must be non-negative");
    }
    if (options.max_elements < 1) {
        return fail("observe ui --max-elements must be at least 1");
    }

    if (provider == "window-tree" || provider == "platform-window-tree") {
        return from_window_tree(options.target);
    }

#ifdef _WIN32
    if (provider == "uia" || provider == "windows-uia") {
        return observe_windows_uia(options);
    }
    if (provider == "auto") {
        auto uia = observe_windows_uia(options);
        if (uia.ok) {
            return uia;
        }
        return from_window_tree(options.target, uia.error);
    }
#elif defined(__APPLE__)
    if (provider == "ax" || provider == "macos-ax") {
        return observe_macos_ax(options);
    }
    if (provider == "uia" || provider == "windows-uia") {
        return fail("observe ui provider 'uia' is only available on Windows builds");
    }
    if (provider == "auto") {
        auto ax = observe_macos_ax(options);
        if (ax.ok) {
            return ax;
        }
        return from_window_tree(options.target, ax.error);
    }
#else
    if (provider == "uia" || provider == "windows-uia") {
        return fail("observe ui provider 'uia' is only available on Windows builds");
    }
    if (provider == "ax" || provider == "macos-ax") {
        return fail("observe ui provider 'ax' is only available on macOS builds");
    }
    if (provider == "auto") {
        return from_window_tree(options.target);
    }
#endif

    return fail("observe ui --provider must be auto, window-tree, uia, or ax");
}

}
