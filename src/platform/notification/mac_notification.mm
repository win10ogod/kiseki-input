#include "platform/notification/mac_notification.hpp"

#import <Foundation/Foundation.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiseki::platform::notification {

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

std::string to_utf8(NSString* value) {
    if (value == nil) {
        return {};
    }
    const char* utf8 = value.UTF8String;
    return utf8 == nullptr ? std::string{} : std::string{utf8};
}

}

namespace detail {

std::string_view macos_notification_script() {
    static constexpr std::string_view script =
        "on run argv\n"
        "display notification (item 1 of argv) with title \"Kiseki Input\"\n"
        "end run\n";
    return script;
}

std::vector<std::string> macos_notification_process_arguments(const std::string& message) {
    return {
        "-",
        message,
    };
}

}

OperationResult notify_once_macos(const std::string& message) {
    @autoreleasepool {
        const auto arguments = detail::macos_notification_process_arguments(message);
        NSMutableArray<NSString*>* task_arguments =
            [NSMutableArray arrayWithCapacity:static_cast<NSUInteger>(arguments.size())];

        for (const auto& argument : arguments) {
            NSString* value = [[NSString alloc] initWithBytes:argument.data()
                                                      length:argument.size()
                                                    encoding:NSUTF8StringEncoding];
            if (value == nil) {
                return fail("failed to encode osascript argument");
            }
            [task_arguments addObject:value];
        }

        NSTask* task = [[NSTask alloc] init];
        task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/osascript"];
        task.arguments = task_arguments;

        NSPipe* input_pipe = [NSPipe pipe];
        task.standardInput = input_pipe;

        NSError* launch_error = nil;
        if (![task launchAndReturnError:&launch_error]) {
            std::string detail = to_utf8(launch_error.localizedDescription);
            if (detail.empty()) {
                detail = "unknown launch error";
            }
            return fail("failed to launch osascript: " + detail);
        }

        const auto script = detail::macos_notification_script();
        NSData* script_data = [NSData dataWithBytes:script.data() length:script.size()];
        [[input_pipe fileHandleForWriting] writeData:script_data];
        [[input_pipe fileHandleForWriting] closeFile];

        [task waitUntilExit];
        if (task.terminationStatus != 0) {
            return fail("osascript notification failed with exit code " + std::to_string(task.terminationStatus));
        }

        return ok("notification shown");
    }
}

}
