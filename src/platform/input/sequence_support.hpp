#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "platform/result.hpp"

namespace kiseki::platform::input {

// Cleanup only resources acquired by this operation; borrowed user state is never owned.
class ReleaseStack {
  public:
    ReleaseStack() = default;
    ReleaseStack(const ReleaseStack &) = delete;
    ReleaseStack &operator=(const ReleaseStack &) = delete;
    ~ReleaseStack();
    void add(std::string name, std::function<OperationResult()> release);
    OperationResult finish(OperationResult result);

  private:
    struct Entry {
        std::string name;
        std::function<OperationResult()> release;
    };
    std::vector<Entry> entries_;
};

OperationResult run_key_chord(const std::vector<std::string> &keys,
                              const std::function<bool(const std::string &)> &is_down,
                              const std::function<OperationResult(const std::string &, bool)> &send, int hold_ms = 0);

bool input_cancelled();
void request_input_cancel();
bool wait_until_input(std::chrono::steady_clock::time_point deadline);
bool wait_input_ms(int milliseconds);

// Installs handlers only while a CLI invocation is running; normal Ctrl+C can unwind.
class InputCancellationScope {
  public:
    InputCancellationScope();
    ~InputCancellationScope();
    InputCancellationScope(const InputCancellationScope &) = delete;
    InputCancellationScope &operator=(const InputCancellationScope &) = delete;

  private:
    using Handler = void (*)(int);
    Handler old_int_ = nullptr;
    Handler old_term_ = nullptr;
};

}
