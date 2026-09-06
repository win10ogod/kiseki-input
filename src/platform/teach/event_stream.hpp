#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace kiseki::platform::teach {

// Native callbacks enqueue raw events on a dedicated thread. Screenshot latency
// cannot delay event reception; JSON conversion and disk I/O happen when drained.
// X11 embedders must call XInitThreads before their first Xlib call.
class NativeEventStream {
  public:
    explicit NativeEventStream(std::chrono::steady_clock::time_point start);
    ~NativeEventStream();
    NativeEventStream(const NativeEventStream &) = delete;
    NativeEventStream &operator=(const NativeEventStream &) = delete;
    std::optional<std::string> initialize();
    void stop();
    std::vector<nlohmann::json> drain();
    std::optional<std::pair<int, int>> last_mouse_position() const;
    std::string source() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
