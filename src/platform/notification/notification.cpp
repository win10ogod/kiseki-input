#include "platform/notification/notification.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "core/config/config_store.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

#ifndef _WIN32
std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(c);
        }
    }
    quoted.push_back('\'');
    return quoted;
}
#else
std::wstring utf8_to_utf16(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    unsigned int code_page = CP_UTF8;
    unsigned long flags = MB_ERR_INVALID_CHARS;
    if (size == 0) {
        code_page = CP_ACP;
        flags = 0;
        size = MultiByteToWideChar(code_page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    }
    if (size == 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(code_page, flags, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}
#endif

}

OperationResult notify_once(const std::string& message) {
    if (message.empty()) {
        return fail("notification message is empty");
    }

#ifdef _WIN32
    const std::wstring wide_message = utf8_to_utf16(message);
    if (wide_message.empty()) {
        return fail("failed to decode notification message");
    }
    MessageBoxW(
        nullptr,
        wide_message.c_str(),
        L"Kiseki Input",
        MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    return ok("notification shown");
#else
    const std::string command = "notify-send " + shell_quote("Kiseki Input") + " " + shell_quote(message);
    const int code = std::system(command.c_str());
    if (code != 0) {
        return fail("notify-send failed or is not installed");
    }
    return ok("notification shown");
#endif
}

int run_heartbeat_daemon(const std::filesystem::path& config_path, bool once, std::ostream& out, std::ostream& err) {
    const kiseki::core::config::ConfigStore store{config_path};

    while (true) {
        const auto loaded = store.load_or_default();
        if (!loaded.ok) {
            err << "config error: " << loaded.error << '\n';
            return 2;
        }

        const auto& heartbeat = loaded.config.heartbeat;
        if (!heartbeat.enabled) {
            out << "heartbeat disabled\n";
            return 0;
        }

        if (heartbeat.notification_enabled) {
            const auto result = notify_once(heartbeat.message);
            if (!result.ok) {
                err << result.error << '\n';
                return result.code;
            }
            out << result.message << '\n';
        } else {
            out << "heartbeat notification disabled\n";
        }

        if (once) {
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::seconds{heartbeat.interval_seconds});
    }
}

}
