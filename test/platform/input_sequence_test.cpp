#include <catch2/catch_test_macros.hpp>
#include "platform/input/sequence_support.hpp"
#include <set>
#include <csignal>

using namespace kiseki::platform;
using namespace kiseki::platform::input;

TEST_CASE("key chord cleans every acquired key after a partial failure") {
    InputCancellationScope cancellation;
    for (int failure = 1; failure <= 3; ++failure) {
        std::set<std::string> held;
        std::vector<std::string> calls;
        int downs = 0;
        const auto result = run_key_chord(
            {"ctrl", "shift", "a"}, [&](const auto &key) { return held.contains(key); },
            [&](const auto &key, bool down) {
                calls.push_back(key + (down ? "+" : "-"));
                if (down && ++downs == failure)
                    return OperationResult{false, 7, "", "injected down failure"};
                if (down)
                    held.insert(key);
                else
                    held.erase(key);
                return OperationResult{true, 0, "", ""};
            });
        REQUIRE_FALSE(result.ok);
        REQUIRE(result.code == 7);
        REQUIRE(held.empty());
        REQUIRE(calls.size() == static_cast<std::size_t>(failure * 2 - 1));
        if (failure == 3)
            REQUIRE(calls == std::vector<std::string>{"ctrl+", "shift+", "a+", "shift-", "ctrl-"});
    }
}

TEST_CASE("key chord borrows existing state and reports all release failures") {
    InputCancellationScope cancellation;
    std::set<std::string> held{"ctrl"};
    std::vector<std::string> releases;
    const auto result = run_key_chord(
        {"ctrl", "shift", "a"}, [&](const auto &key) { return held.contains(key); },
        [&](const auto &key, bool down) {
            if (down) {
                held.insert(key);
                return OperationResult{true, 0, "", ""};
            }
            releases.push_back(key);
            return OperationResult{false, 8, "", "release rejected"};
        });
    REQUIRE_FALSE(result.ok);
    REQUIRE(releases == std::vector<std::string>{"a", "shift"});
    REQUIRE(result.error.find("cleanup a failed") != std::string::npos);
    REQUIRE(result.error.find("cleanup shift failed") != std::string::npos);
    REQUIRE(held.contains("ctrl"));
}

TEST_CASE("key chord cancellation releases acquired state without waiting for hold duration") {
    InputCancellationScope cancellation;
    bool held = false;
    const auto result = run_key_chord(
        {"a"}, [&](const auto &) { return held; },
        [&](const auto &, bool down) {
            held = down;
            if (down)
                request_input_cancel();
            return OperationResult{true, 0, "", ""};
        },
        100000);
    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(held);
    REQUIRE(result.error.find("cancelled") != std::string::npos);
}

TEST_CASE("SIGINT unwinds a key hold and restores the previous signal handler") {
    bool released = false;
    {
        InputCancellationScope cancellation;
        const auto result = run_key_chord(
            {"a"}, [](const auto &) { return false; },
            [&](const auto &, bool down) {
                if (down)
                    std::raise(SIGINT);
                else
                    released = true;
                return OperationResult{true, 0, "", ""};
            },
            100000);
        REQUIRE_FALSE(result.ok);
    }
    REQUIRE(released);
    InputCancellationScope next;
    REQUIRE_FALSE(input_cancelled());
}
