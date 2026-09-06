#import <AppKit/AppKit.h>

namespace kiseki::platform::input {
double mac_double_click_interval() {
    return [NSEvent doubleClickInterval];
}
}
