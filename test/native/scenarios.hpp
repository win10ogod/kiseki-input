#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include "cli/app.hpp"
#include "platform/input/input.hpp"
#include "platform/input/sequence_support.hpp"
#include "platform/teach/event_stream.hpp"

namespace native_probe {
inline std::atomic<int> phase{0};
inline std::atomic<bool> finished{false};
inline std::atomic<int> failures{0};
inline std::atomic<bool> dense_drag_released{false};
inline std::filesystem::path output;
inline void wait(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
inline void check(const kiseki::platform::OperationResult &result) {
    if (!result.ok) {
        ++failures;
        std::cerr << "phase=" << phase << " error=" << result.error << '\n';
    }
}
inline void run(int x, int y, const std::string &window_id, bool background) {
    using namespace kiseki::platform::input;
    InputCancellationScope cancellation;
    kiseki::platform::teach::NativeEventStream stream{std::chrono::steady_clock::now()};
    if (const auto error = stream.initialize()) {
        ++failures;
        std::cerr << *error << '\n';
    }
    const auto mouse = [&](int px, int py, const std::string &click) {
        check(mouse_action({0, 0, px, py, true, "system", click}));
        wait(80);
    };
    wait(400);
    for (int button = 0; button < 3; ++button) {
        phase = button + 1;
        const std::string name = button == 0 ? "left" : button == 1 ? "right" : "middle";
        mouse(x, y + button * 25, name + "-down");
        mouse(x + 40, y + button * 25, "none");
        mouse(x + 80, y + button * 25, "none");
        mouse(x + 120, y + button * 25, name + "-up");
        wait(100);
    }
    phase = 4;
    check(mouse_drag_absolute({{x, y}, {x + 40, y}, {x + 80, y}, {x + 120, y}}, "system", 100, 200, 200, "left",
                              "shift"));
    wait(120);
    phase = 5;
    check(mouse_drag_absolute({{x, y + 25, 0}, {x + 40, y + 25, 75}, {x + 80, y + 25, 225}, {x + 120, y + 25, 300}},
                              "system", 2, 100, 100, "right", "ctrl"));
    wait(120);
    phase = 6;
    mouse(x, y, "none");
    check(mouse_action({.dx = 0,
                        .dy = 0,
                        .x = 0,
                        .y = 0,
                        .absolute = false,
                        .backend = "system",
                        .click = "left",
                        .click_count = 2,
                        .click_interval_ms = 80,
                        .hold_ms = 20}));
    wait(120);
    phase = 7;
    check(mouse_action({.dx = 0,
                        .dy = 0,
                        .x = 0,
                        .y = 0,
                        .absolute = false,
                        .backend = "system",
                        .click = "none",
                        .wheel = 120,
                        .hwheel = -120}));
    wait(120);
    phase = 8;
    for (int i = 0; i < 100; ++i) {
        check(tap_key("a", "system"));
    }
    check(synchronize_input());
    wait(120);
    phase = 9;
    check(tap_key("left", "system", 180));
    check(tap_key("numpad-enter", "system"));
    check(tap_key("rctrl", "system"));
    wait(120);
    phase = 10;
    mouse(x, y, "x1");
    mouse(x, y, "x2");
    wait(120);
    phase = 11;
    // Exercise the actual CLI sequence path with mixed input and absolute deadlines.
    nlohmann::json sequence{
        {"steps",
         nlohmann::json::array({{{"type", "key"}, {"key", "space"}, {"action", "down"}, {"backend", "system"}},
                                {{"type", "mouse"}, {"x", x}, {"y", y}, {"click", "left-down"}, {"backend", "system"}},
                                {{"type", "mouse"}, {"x", x + 60}, {"y", y}, {"atMs", 150}, {"backend", "system"}},
                                {{"type", "mouse"}, {"x", x + 120}, {"y", y}, {"atMs", 300}, {"backend", "system"}},
                                {{"type", "mouse"}, {"click", "left-up"}, {"backend", "system"}},
                                {{"type", "key"}, {"key", "space"}, {"action", "up"}, {"backend", "system"}}})}};
    const auto path = output / "sequence.json";
    {
        std::ofstream file{path};
        file << sequence.dump(2);
    }
    const int code = kiseki::cli::run({"input", "sequence", "--file", path.string()}, output / "config.json",
                                      {std::cout, std::cerr});
    if (code)
        ++failures;
    wait(120);
    if (background) {
        kiseki::platform::target::TargetQuery query{.window_id = window_id};
        for (int button = 0; button < 3; ++button) {
            phase = 12 + button;
            const std::string name = button == 0 ? "left" : button == 1 ? "right" : "middle";
            for (const auto &a : std::vector<BackgroundMouseOptions>{
                     {query, 40, 210, name + "-down"}, {query, 240, 210, "none"}, {query, 440, 210, name + "-up"}}) {
                check(background_mouse_action(a));
                wait(80);
            }
            wait(120);
        }
    }
    phase = 18;
    mouse(x, y, "none");
    for (int i = 0; i < 100; ++i)
        check(mouse_action({1, 0, 0, 0, false, "system", "none"}));
    check(synchronize_input());
    check(mouse_action({0, 0, 0, 0, false, "system", "left"}));
    wait(150);
    phase = 19;
    check(mouse_action({0, 0, x, y, true, "system", "left-down"}));
    check(mouse_action({0, 0, x + 180, y, true, "system", "none"}));
    check(mouse_action({0, 0, 0, 0, false, "system", "left-up"}));
    check(synchronize_input());
    wait(150);
    phase = 20;
    for (int i = 0; i < 20; ++i) {
        check(key_action("shift", true, "system"));
        check(tap_key("b", "system"));
        check(key_action("shift", false, "system"));
        check(tap_key("c", "system"));
    }
    check(synchronize_input());
    wait(150);
    phase = 21;
    for (const auto &held : {"enter", "numpad-enter"}) {
        check(key_action(held, true, "system"));
        check(tap_key(std::string{held} == "enter" ? "numpad-enter" : "enter", "system"));
        check(key_action(held, false, "system"));
    }
    check(synchronize_input());
    wait(150);
#ifdef _WIN32
    phase = 22;
    std::vector<MousePoint> dense;
    for (int i = 0; i < 100; ++i)
        dense.push_back({x + i, y});
    check(mouse_drag_absolute(dense, "system", 2, 0, 0));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!dense_drag_released && std::chrono::steady_clock::now() < deadline)
        wait(10);
    if (!dense_drag_released) {
        ++failures;
        std::cerr << "dense drag release did not reach receiver\n";
    }
    wait(150);
#endif
    phase = 15;
    stream.stop();
    std::ofstream file{output / "stream-events.jsonl"};
    for (const auto &e : stream.drain())
        file << e.dump() << '\n';
    std::cout << "event_source=" << stream.source() << " operation_failures=" << failures << std::endl;
    finished = true;
}
}
