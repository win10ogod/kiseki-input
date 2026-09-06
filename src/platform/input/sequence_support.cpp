#include "platform/input/sequence_support.hpp"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <exception>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace kiseki::platform::input {
namespace {
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> cancelled{false};
void handle_cancel(int) {
    cancelled.store(true, std::memory_order_relaxed);
}
#ifdef _WIN32
BOOL WINAPI handle_console_cancel(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
        return FALSE;
    cancelled.store(true, std::memory_order_relaxed);
    return TRUE;
}
#endif
OperationResult error(std::string message) {
    return {false, 2, "", std::move(message)};
}
}

ReleaseStack::~ReleaseStack() {
    if (!entries_.empty())
        finish({true, 0, "", ""});
}
void ReleaseStack::add(std::string name, std::function<OperationResult()> release) {
    entries_.push_back({std::move(name), std::move(release)});
}
OperationResult ReleaseStack::finish(OperationResult result) {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        OperationResult released;
        try {
            released = it->release();
        } catch (const std::exception &e) {
            released = error(e.what());
        } catch (...) {
            released = error("unknown release error");
        }
        if (!released.ok) {
            if (!result.error.empty())
                result.error += "; ";
            result.error += "cleanup " + it->name + " failed: " + released.error;
            result.ok = false;
            if (result.code == 0)
                result.code = released.code == 0 ? 2 : released.code;
        }
    }
    entries_.clear();
    return result;
}

OperationResult run_key_chord(const std::vector<std::string> &keys,
                              const std::function<bool(const std::string &)> &is_down,
                              const std::function<OperationResult(const std::string &, bool)> &send, int hold_ms) {
    if (hold_ms < 0)
        return error("key hold duration must be non-negative");
    ReleaseStack releases;
    for (const auto &key : keys) {
        if (input_cancelled())
            return releases.finish(error("input cancelled"));
        // Repeated aliases or modifiers already held by the operator are borrowed.
        if (is_down(key))
            continue;
        const auto result = send(key, true);
        if (!result.ok)
            return releases.finish(result);
        releases.add(key, [&, key] { return send(key, false); });
    }
    return releases.finish(wait_input_ms(hold_ms) ? OperationResult{true, 0, "input sent", ""}
                                                  : error("input cancelled"));
}

bool input_cancelled() {
    return cancelled.load(std::memory_order_relaxed);
}
void request_input_cancel() {
    cancelled.store(true, std::memory_order_relaxed);
}
bool wait_until_input(std::chrono::steady_clock::time_point deadline) {
    while (!input_cancelled()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return true;
        std::this_thread::sleep_until(std::min(deadline, now + std::chrono::milliseconds(10)));
    }
    return false;
}
bool wait_input_ms(int milliseconds) {
    return wait_until_input(std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds));
}
InputCancellationScope::InputCancellationScope() {
    cancelled.store(false, std::memory_order_relaxed);
    old_int_ = std::signal(SIGINT, handle_cancel);
    old_term_ = std::signal(SIGTERM, handle_cancel);
#ifdef _WIN32
    SetConsoleCtrlHandler(handle_console_cancel, TRUE);
#endif
}
InputCancellationScope::~InputCancellationScope() {
#ifdef _WIN32
    SetConsoleCtrlHandler(handle_console_cancel, FALSE);
#endif
    if (old_int_ != SIG_ERR)
        std::signal(SIGINT, old_int_);
    if (old_term_ != SIG_ERR)
        std::signal(SIGTERM, old_term_);
}
}
