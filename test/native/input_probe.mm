#import <Cocoa/Cocoa.h>
#include "scenarios.hpp"
#include "platform/capture/screenshot.hpp"
#include <array>
#include <cmath>
#include <cstring>

using namespace native_probe;
static std::ofstream received;
static void record(NSEvent *event, const char *kind, int button = -1, bool down = false) {
    const auto flags = event.modifierFlags;
    nlohmann::json value{{"phase", phase.load()},
                         {"kind", kind},
                         {"eventType", static_cast<int>(event.type)},
                         {"nativeTimeMs", event.timestamp * 1000},
                         {"x", event.locationInWindow.x},
                         {"y", event.locationInWindow.y},
                         {"down", down},
                         {"button", button},
                         {"shift", (flags & NSEventModifierFlagShift) != 0},
                         {"ctrl", (flags & NSEventModifierFlagControl) != 0},
                         {"space", CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, 49)}};
    if (std::string{kind} == "button")
        value["clickCount"] = event.clickCount;
    if (std::string{kind} == "key") {
        value["code"] = event.keyCode;
        value["repeat"] = event.type == NSEventTypeKeyDown ? static_cast<bool>(event.isARepeat) : false;
    }
    if (std::string{kind} == "wheel") {
        value["deltaY"] = event.scrollingDeltaY;
        value["deltaX"] = event.scrollingDeltaX;
    }
    received << value.dump() << std::endl;
}
@interface ProbeView : NSView
@end
@implementation ProbeView
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)isFlipped {
    return YES;
}
- (void)mouseDown:(NSEvent *)e {
    record(e, "button", 0, true);
}
- (void)mouseUp:(NSEvent *)e {
    record(e, "button", 0, false);
}
- (void)rightMouseDown:(NSEvent *)e {
    record(e, "button", 1, true);
}
- (void)rightMouseUp:(NSEvent *)e {
    record(e, "button", 1, false);
}
- (void)otherMouseDown:(NSEvent *)e {
    record(e, "button", static_cast<int>(e.buttonNumber), true);
}
- (void)otherMouseUp:(NSEvent *)e {
    record(e, "button", static_cast<int>(e.buttonNumber), false);
}
- (void)mouseMoved:(NSEvent *)e {
    record(e, "move");
}
- (void)mouseDragged:(NSEvent *)e {
    record(e, "drag", 0);
}
- (void)rightMouseDragged:(NSEvent *)e {
    record(e, "drag", 1);
}
- (void)otherMouseDragged:(NSEvent *)e {
    record(e, "drag", static_cast<int>(e.buttonNumber));
}
- (void)scrollWheel:(NSEvent *)e {
    record(e, "wheel");
}
- (void)keyDown:(NSEvent *)e {
    record(e, "key", -1, true);
}
- (void)keyUp:(NSEvent *)e {
    record(e, "key", -1, false);
}
- (void)flagsChanged:(NSEvent *)e {
    record(e, "flags");
}
- (void)drawRect:(NSRect)rect {
    [[NSColor whiteColor] setFill];
    NSRectFill(self.bounds);
    const CGFloat width = self.bounds.size.width, height = self.bounds.size.height;
    NSArray<NSColor *> *colors =
        @[ [NSColor redColor], [NSColor greenColor], [NSColor blueColor], [NSColor magentaColor] ];
    const NSRect blocks[] = {NSMakeRect(20, 20, 60, 60), NSMakeRect(width - 80, 20, 60, 60),
                             NSMakeRect(20, height - 80, 60, 60), NSMakeRect(width - 80, height - 80, 60, 60)};
    for (int i = 0; i < 4; ++i) {
        [colors[i] setFill];
        NSRectFill(blocks[i]);
    }
    [@"Kiseki native input regression\nTOP: red / green\nBOTTOM: blue / magenta"
            drawInRect:NSMakeRect(140, 120, 430, 180)
        withAttributes:@{
            NSFontAttributeName : [NSFont systemFontOfSize:20],
            NSForegroundColorAttributeName : [NSColor blackColor]
        }];
}
@end

static void capture_and_click(const std::string &window_id) {
    using namespace kiseki::platform;
    phase = 16;
    const auto path = output / "four-corners.bmp";
    const auto capture = capture::capture_window_bmp({.window_id = window_id}, path);
    if (!capture.ok || !capture.coordinates) {
        ++failures;
        std::cerr << "capture metadata failed: " << capture.error << '\n';
        return;
    }
    std::ifstream bitmap{path, std::ios::binary};
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>{bitmap}, {}};
    if (bytes.size() < 54) {
        ++failures;
        return;
    }
    std::uint32_t offset;
    std::memcpy(&offset, bytes.data() + 10, 4);
    struct Color {
        double x = 0, y = 0;
        int count = 0;
    };
    std::array<Color, 4> colors;
    for (int y = 0; y < capture.height; ++y)
        for (int x = 0; x < capture.width; ++x) {
            const auto index = offset + (static_cast<std::size_t>(y) * capture.width + x) * 4;
            if (index + 2 >= bytes.size())
                continue;
            const int b = bytes[index], g = bytes[index + 1], r = bytes[index + 2];
            const int color = r > 180 && g < 80 && b < 80    ? 0
                              : g > 180 && r < 80 && b < 80  ? 1
                              : b > 180 && r < 80 && g < 80  ? 2
                              : r > 180 && b > 180 && g < 80 ? 3
                                                             : -1;
            if (color >= 0) {
                colors[color].x += x;
                colors[color].y += y;
                ++colors[color].count;
            }
        }
    for (auto &color : colors) {
        if (color.count == 0) {
            ++failures;
            std::cerr << "corner color absent\n";
            return;
        }
        color.x /= color.count;
        color.y /= color.count;
    }
    const bool upright = colors[0].y < colors[2].y && colors[1].y < colors[3].y && colors[0].x < colors[1].x;
    if (!upright)
        ++failures;
    const auto &c = *capture.coordinates;
    nlohmann::json result{{"upright", upright},
                          {"width", capture.width},
                          {"height", capture.height},
                          {"originX", c.origin_x},
                          {"originY", c.origin_y},
                          {"pointWidth", c.width},
                          {"pointHeight", c.height},
                          {"pixelsPerUnitX", c.pixels_per_unit_x},
                          {"pixelsPerUnitY", c.pixels_per_unit_y},
                          {"clicks", nlohmann::json::array()}};
    for (const auto &color : colors) {
        const int x = static_cast<int>(std::lround(c.origin_x + color.x / c.pixels_per_unit_x));
        const int y = static_cast<int>(std::lround(c.origin_y + color.y / c.pixels_per_unit_y));
        result["clicks"].push_back({{"pixelX", color.x}, {"pixelY", color.y}, {"screenX", x}, {"screenY", y}});
        check(input::mouse_action({0, 0, x, y, true, "system", "left"}));
        wait(100);
    }
    std::ofstream report{output / "capture-mapping.json"};
    report << result.dump(2);
    wait(200);
}
int main(int argc, char **argv) {
    output = argc > 1 ? argv[1] : "native-probe";
    const bool skip_capture = argc > 2 && std::string{argv[2]} == "--skip-capture";
    std::filesystem::create_directories(output);
    received.open(output / "receiver-events.jsonl");
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        auto previous = [[NSWorkspace sharedWorkspace] frontmostApplication];
        auto cursor = CGEventCreate(nullptr);
        const auto previous_cursor = CGEventGetLocation(cursor);
        CFRelease(cursor);
        if (!AXIsProcessTrusted()) {
            std::cerr << "Accessibility permission is required\n";
            return 2;
        }
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(140, 160, 720, 460)
                                                       styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        window.title = @"Kiseki native input regression";
        window.acceptsMouseMovedEvents = YES;
        window.releasedWhenClosed = NO;
        ProbeView *view = [[ProbeView alloc] initWithFrame:NSMakeRect(0, 0, 720, 460)];
        window.contentView = view;
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [window makeFirstResponder:view];
        const CGFloat height = CGDisplayBounds(CGMainDisplayID()).size.height;
        const NSPoint screen = [window convertPointToScreen:NSMakePoint(160, 300)];
        const int x = static_cast<int>(screen.x), y = static_cast<int>(height - screen.y);
        const auto window_id = std::to_string(window.windowNumber);
        std::cout << "pid=" << getpid() << " window_id=" << window_id << " point=" << x << "," << y << std::endl;
        std::thread worker([=] {
            wait(700);
            if ([[NSWorkspace sharedWorkspace] frontmostApplication].processIdentifier != getpid()) {
                ++failures;
                std::cerr << "test window did not acquire foreground\n";
            } else {
                if (!skip_capture)
                    capture_and_click(window_id);
                run(x, y, window_id, false);
            }
            CGWarpMouseCursorPosition(previous_cursor);
            dispatch_async(dispatch_get_main_queue(), ^{
              [previous activateWithOptions:NSApplicationActivateIgnoringOtherApps];
              [window orderOut:nil];
              [NSApp stop:nil];
              [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                                  location:NSZeroPoint
                                             modifierFlags:0
                                                 timestamp:0
                                              windowNumber:0
                                                   context:nil
                                                   subtype:0
                                                     data1:0
                                                     data2:0]
                       atStart:NO];
            });
        });
        [NSApp run];
        worker.join();
    }
    return failures ? 2 : 0;
}
