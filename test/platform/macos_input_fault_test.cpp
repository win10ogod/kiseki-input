#include <ApplicationServices/ApplicationServices.h>
#include <iostream>
#include <set>
#include <vector>

namespace {
struct SentEvent {
    CGEventType type;
    CGKeyCode key;
    CGEventFlags flags;
    CGPoint position;
    int click_count;
};
std::vector<SentEvent> sent;
std::uint32_t posted = 0, delivered = 0;
std::set<CGKeyCode> stale_keys, physical_keys;
std::set<CGMouseButton> stale_buttons;
CGPoint physical_pointer{100, 200};
int creation_attempts = 0, fail_creation_at = -1;
bool fake_trusted() { return true; }
uint32_t fake_counter(CGEventSourceStateID, CGEventType) { return delivered; }
bool fake_key_state(CGEventSourceStateID, CGKeyCode key) {
    return stale_keys.contains(key) || physical_keys.contains(key);
}
bool fake_button_state(CGEventSourceStateID, CGMouseButton button) { return stale_buttons.contains(button); }
CGEventRef fake_snapshot(CGEventSourceRef source) {
    auto event = CGEventCreate(source);
    if (event) CGEventSetLocation(event, physical_pointer);
    return event;
}
CGEventRef fake_create_key(CGEventSourceRef source, CGKeyCode key, bool down) {
    if (++creation_attempts == fail_creation_at) return nullptr;
    return CGEventCreateKeyboardEvent(source, key, down);
}
void fake_post(CGEventTapLocation, CGEventRef event) {
    const auto type = CGEventGetType(event);
    const auto key = static_cast<CGKeyCode>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
    const auto flags = CGEventGetFlags(event);
    sent.push_back({type, key, flags, CGEventGetLocation(event),
                    static_cast<int>(CGEventGetIntegerValueField(event, kCGMouseEventClickState))});
    ++posted;
    // Leave the old system down state visible after an up was posted.
    if (type == kCGEventKeyDown ||
        (type == kCGEventFlagsChanged && flags & (kCGEventFlagMaskShift | kCGEventFlagMaskControl)))
        stale_keys.insert(key);
    if (type == kCGEventLeftMouseDown) stale_buttons.insert(kCGMouseButtonLeft);
}
void deliver() {
    delivered = posted;
    stale_keys.clear();
    stale_buttons.clear();
}
}

// Compile the actual implementation against a lagging WindowServer boundary.
// Events are allocated normally but never posted to the desktop.
#define AXIsProcessTrusted fake_trusted
#define CGEventSourceCounterForEventType fake_counter
#define CGEventSourceKeyState fake_key_state
#define CGEventSourceButtonState fake_button_state
#define CGEventCreate fake_snapshot
#define CGEventCreateKeyboardEvent fake_create_key
#define CGEventPost fake_post
#include "../../src/platform/input/input.cpp"
#undef CGEventPost
#undef CGEventCreateKeyboardEvent
#undef CGEventCreate
#undef CGEventSourceButtonState
#undef CGEventSourceKeyState
#undef CGEventSourceCounterForEventType
#undef AXIsProcessTrusted

int main() {
    using namespace kiseki::platform::input;
    InputCancellationScope cancellation;
    int failures = 0;
    const auto require = [&](bool condition, const char *message) {
        if (!condition) { ++failures; std::cerr << message << '\n'; }
    };
    const auto finish_phase = [&] { deliver(); synchronize_input(); sent.clear(); };
    for (int i = 0; i < 100; ++i)
        require(tap_key("a", "system").ok, "rapid tap must succeed");
    require(sent.size() == 200, "stale system state must not suppress a requested tap");
    for (std::size_t i = 0; i < sent.size(); ++i)
        require(sent[i].type == (i % 2 ? kCGEventKeyUp : kCGEventKeyDown), "tap pairs must remain ordered");
    finish_phase();

    for (int i = 0; i < 20; ++i) {
        require(key_action("shift", true, "system").ok, "Shift down must succeed");
        require(tap_key("a", "system").ok, "modified A must succeed");
        require(key_action("shift", false, "system").ok, "Shift up must succeed");
        require(tap_key("b", "system").ok, "unmodified B must succeed");
    }
    int b_events = 0;
    for (const auto &event : sent)
        if (event.key == 11 && (event.type == kCGEventKeyDown || event.type == kCGEventKeyUp)) {
            ++b_events;
            require(!(event.flags & kCGEventFlagMaskShift), "released Shift must not leak into B");
        }
    require(b_events == 40, "all unmodified B pairs must be posted");
    finish_phase();

    physical_keys.insert(59);
    require(key_combo("ctrl+a", "system").ok, "borrowed physical Ctrl chord must succeed");
    require(sent.size() == 2, "borrowed Ctrl must not be injected or released");
    for (const auto &event : sent)
        require(event.key == 0 && (event.flags & kCGEventFlagMaskControl), "borrowed Ctrl must remain on A");
    require(physical_keys.contains(59), "physical Ctrl must remain held");
    physical_keys.clear();
    finish_phase();

    for (int i = 0; i < 100; ++i)
        require(mouse_action({1, 0, 0, 0, false, "system", "none"}).ok, "relative move must succeed");
    require(sent.size() == 100, "all relative moves must be posted");
    for (std::size_t i = 0; i < sent.size(); ++i)
        require(sent[i].position.x == 101 + i && sent[i].position.y == 200, "relative moves must accumulate");
    finish_phase();
    physical_pointer = {500, 300};
    require(mouse_action({1, 0, 0, 0, false, "system", "none"}).ok, "move after external cursor change must succeed");
    require(sent.size() == 1 && sent[0].position.x == 501, "acknowledged state must reconcile external cursor changes");
    finish_phase();

    require(mouse_action({0, 0, 100, 200, true, "system", "left-down"}).ok, "split down must succeed");
    require(mouse_action({0, 0, 280, 200, true, "system", "none"}).ok, "split move must succeed");
    require(mouse_action({0, 0, 0, 0, false, "system", "left-up"}).ok, "split up must succeed");
    require(!sent.empty() && sent.back().type == kCGEventLeftMouseUp && sent.back().position.x == 280,
            "split release must use the last submitted position");
    finish_phase();

    require(mouse_action({0, 0, 0, 0, false, "system", "left", 2, 0}).ok,
            "stale button state must not suppress the second click");
    require(sent.size() == 4, "double click must contain two complete pairs");
    for (std::size_t i = 0; i < sent.size(); ++i)
        require(sent[i].click_count == static_cast<int>(i / 2 + 1), "both click halves must have matching counts");
    finish_phase();

    creation_attempts = 0;
    fail_creation_at = 2;
    require(!key_combo("ctrl+a", "system").ok, "event allocation failure must be reported");
    require(sent.size() == 2 && sent.back().type == kCGEventFlagsChanged &&
                !(sent.back().flags & kCGEventFlagMaskControl), "partial chord must post Ctrl cleanup");
    finish_phase();
    std::cout << "macOS delayed-boundary failures=" << failures << '\n';
    return failures ? 2 : 0;
}
