#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "platform/input/driver_model.hpp"

using namespace kiseki::platform::input::driver;

TEST_CASE("driver type names round-trip through both Kiseki and config spellings") {
    REQUIRE(parse_type("any") == Type::any);
    REQUIRE(parse_type("AnyDriver") == Type::any);
    REQUIRE(parse_type("logitech-ghub-new") == Type::logitech_ghub_new);
    REQUIRE(parse_type("LogitechGHubNew") == Type::logitech_ghub_new);
    REQUIRE(parse_type("MouClassInputInjection") == Type::mouclass);
    REQUIRE(parse_type("mouclass") == Type::mouclass);
    REQUIRE(parse_type("RAZER") == Type::razer);
    REQUIRE_FALSE(parse_type("uinput").has_value());

    REQUIRE(type_name(Type::logitech_ghub_new) == "logitech-ghub-new");
    REQUIRE(config_type_name(Type::mouclass) == "MouClassInputInjection");
    REQUIRE(config_type_name(Type::any) == "AnyDriver");
}

TEST_CASE("Send::SendType values match IbInputSimulator") {
    REQUIRE(ib_send_type(Type::any) == 0);
    REQUIRE(ib_send_type(Type::sendinput) == 1);
    REQUIRE(ib_send_type(Type::logitech) == 2);
    REQUIRE(ib_send_type(Type::razer) == 3);
    REQUIRE(ib_send_type(Type::dd) == 4);
    REQUIRE(ib_send_type(Type::mouclass) == 5);
    REQUIRE(ib_send_type(Type::logitech_ghub_new) == 6);
}

TEST_CASE("probe order includes LogitechGHubNew first and omits sendinput") {
    const auto &order = probe_order();
    REQUIRE(order.front() == Type::logitech_ghub_new);
    REQUIRE(std::find(order.begin(), order.end(), Type::logitech) != order.end());
    REQUIRE(std::find(order.begin(), order.end(), Type::sendinput) == order.end());
    REQUIRE(std::find(order.begin(), order.end(), Type::any) == order.end());
}

TEST_CASE("key limitations refuse identities a driver cannot deliver") {
    REQUIRE(key_limitation(Type::mouclass, "a").has_value());
    REQUIRE(key_limitation(Type::razer, "rctrl").has_value());
    REQUIRE(key_limitation(Type::razer, "ralt").has_value());
    REQUIRE(key_limitation(Type::razer, "numpad-enter").has_value());
    REQUIRE(key_limitation(Type::razer, "numpad-divide").has_value());
    REQUIRE_FALSE(key_limitation(Type::razer, "a").has_value());
    REQUIRE_FALSE(key_limitation(Type::razer, "left").has_value());
    REQUIRE(key_limitation(Type::logitech, "numpad-enter").has_value());
    REQUIRE_FALSE(key_limitation(Type::logitech, "rctrl").has_value());
    REQUIRE_FALSE(key_limitation(Type::sendinput, "numpad-enter").has_value());
}

TEST_CASE("wheel plans encode vendor capabilities") {
    const auto razer = plan_wheel(Type::razer, 120, -120);
    REQUIRE(razer.error.empty());
    REQUIRE(razer.events.size() == 2);
    REQUIRE_FALSE(razer.events[0].horizontal);
    REQUIRE(razer.events[1].horizontal);

    const auto logitech_h = plan_wheel(Type::logitech, 0, 120);
    REQUIRE_FALSE(logitech_h.error.empty()); // no horizontal wheel

    const auto logitech_fraction = plan_wheel(Type::logitech, 60, 0);
    REQUIRE_FALSE(logitech_fraction.error.empty()); // whole detents only

    const auto logitech_two = plan_wheel(Type::logitech, 240, 0);
    REQUIRE(logitech_two.error.empty());
    REQUIRE(logitech_two.events.size() == 2); // one report per detent

    const auto dd = plan_wheel(Type::dd, 240, 0);
    REQUIRE(dd.error.empty());
    REQUIRE(dd.events.size() == 1);
    REQUIRE(dd.events[0].data == 2);

    REQUIRE_FALSE(plan_wheel(Type::mouclass, 120, 0).error.empty());
}

TEST_CASE("settings resolution prefers cli over env over config") {
    SettingsLayers layers;
    layers.config_type = "AnyDriver";
    layers.config_present = true;
    layers.env_type = "razer";
    layers.cli_type = "logitech";
    const auto settings = resolve_settings(layers);
    REQUIRE(settings.type == Type::logitech);
    REQUIRE(settings.source == "cli");
    REQUIRE(settings.error.empty());
}

TEST_CASE("settings resolution falls back to env then config") {
    SettingsLayers env_layers;
    env_layers.config_type = "razer";
    env_layers.config_present = true;
    env_layers.env_type = "dd";
    REQUIRE(resolve_settings(env_layers).type == Type::dd);
    REQUIRE(resolve_settings(env_layers).source == "env");

    SettingsLayers config_layers;
    config_layers.config_type = "Razer";
    config_layers.config_present = true;
    const auto settings = resolve_settings(config_layers);
    REQUIRE(settings.type == Type::razer);
    REQUIRE(settings.source == "config");
}

TEST_CASE("an argument only applies from the layer that selected the type or higher") {
    // CLI type with a config argument: the config argument must not leak in.
    SettingsLayers layers;
    layers.cli_type = "dd";
    layers.config_argument = "C:/old/DD64.dll";
    layers.config_present = true;
    REQUIRE(resolve_settings(layers).argument.empty());

    // CLI type and CLI argument travel together.
    layers.cli_argument = "C:/new/DD64.dll";
    REQUIRE(resolve_settings(layers).argument == "C:/new/DD64.dll");
}

TEST_CASE("mouclass argument must be a process id and bad input is rejected") {
    SettingsLayers layers;
    layers.cli_type = "mouclass";
    layers.cli_argument = "4321";
    REQUIRE(resolve_settings(layers).error.empty());

    layers.cli_argument = "not-a-pid";
    REQUIRE_FALSE(resolve_settings(layers).error.empty());

    SettingsLayers bad_type;
    bad_type.cli_type = "razer";
    bad_type.cli_argument = "4321";
    REQUIRE_FALSE(resolve_settings(bad_type).error.empty()); // razer takes no argument
}

TEST_CASE("unknown driver type names produce a clear error") {
    SettingsLayers layers;
    layers.cli_type = "turbo";
    const auto settings = resolve_settings(layers);
    REQUIRE_FALSE(settings.error.empty());
    REQUIRE(settings.error.find("turbo") != std::string::npos);
}

TEST_CASE("axis fit detects an identity mapping and a scaled mapping") {
    const auto identity = fit_axis({{0, 0}, {16384, 16384}, {32768, 32768}, {65535, 65535}}, 4.0);
    REQUIRE(identity.has_value());
    REQUIRE(identity->identity);
    REQUIRE(map_request(*identity, 20000) == 20000);

    // observed = 0.5 * requested (a driver that halves the commanded distance).
    const auto scaled = fit_axis({{0, 0}, {20000, 10000}, {40000, 20000}, {60000, 30000}}, 4.0);
    REQUIRE(scaled.has_value());
    REQUIRE_FALSE(scaled->identity);
    REQUIRE(map_request(*scaled, 10000) == 20000);

    const auto nonlinear = fit_axis({{0, 0}, {10000, 2000}, {20000, 15000}, {30000, 9000}}, 4.0);
    REQUIRE_FALSE(nonlinear.has_value());
}

namespace {
// Models a vendor's response to a relative command. Returns observed (dx, dy). Used to build
// probe samples the way the live self-test would, so the classifier is proven against good,
// silent, scaled, dead-axis, erratic, and fling behaviours without any live input.
std::vector<ProbeSample> probe_with(const std::function<std::pair<int, int>(int, int)> &vendor) {
    std::vector<ProbeSample> samples;
    for (const auto &[cdx, cdy] : standard_probes()) {
        const auto [odx, ody] = vendor(cdx, cdy);
        samples.push_back({cdx, cdy, odx, ody, true});
    }
    return samples;
}
int round_half(double v) { return static_cast<int>(v < 0 ? v - 0.5 : v + 0.5); }
}

TEST_CASE("classify_probes accepts a faithful driver") {
    const auto good = probe_with([](int dx, int dy) { return std::pair{dx, dy}; });
    REQUIRE(classify_probes(good).result == Delivery::verified);
}

TEST_CASE("classify_probes accepts a scaled driver (Razer-like y/1.2 plus mild acceleration)") {
    // x roughly 1:1, y scaled by ~0.83 and rounded; always nonzero for a 6px command.
    const auto scaled = probe_with([](int dx, int dy) {
        return std::pair{dx, round_half(dy / 1.2)};
    });
    REQUIRE(classify_probes(scaled).result == Delivery::verified);
}

TEST_CASE("classify_probes rejects a silent driver") {
    const auto silent = probe_with([](int, int) { return std::pair{0, 0}; });
    REQUIRE(classify_probes(silent).result == Delivery::silent);
}

TEST_CASE("classify_probes rejects an unfaithful driver that flings (absolute-as-relative)") {
    // A driver that amplifies a small relative command into a huge jump.
    const auto fling = probe_with([](int dx, int dy) { return std::pair{dx * 20, dy * 20}; });
    REQUIRE(classify_probes(fling).result == Delivery::unfaithful);
}

TEST_CASE("classify_probes rejects reversed direction and axis coupling") {
    const auto reversed = probe_with([](int dx, int dy) { return std::pair{-dx, -dy}; });
    REQUIRE(classify_probes(reversed).result == Delivery::unfaithful);

    const auto coupled = probe_with([](int dx, int dy) { return std::pair{dx ? dx : 9, dy ? dy : 9}; });
    REQUIRE(classify_probes(coupled).result == Delivery::unfaithful);
}

TEST_CASE("classify_probes rejects a dead axis") {
    // x works, y never moves: cannot be positioned vertically.
    const auto dead_y = probe_with([](int dx, int dy) { return std::pair{dx, 0}; });
    REQUIRE(classify_probes(dead_y).result == Delivery::unfaithful);
}

TEST_CASE("classify_probes rejects non-repeatable motion") {
    int calls = 0;
    std::vector<ProbeSample> samples;
    for (const auto &[cdx, cdy] : standard_probes()) {
        // The second +x probe jumps far more than the first.
        const int scale = (cdx > 0 && ++calls == 2) ? 5 : 1;
        samples.push_back({cdx, cdy, cdx * scale, cdy, true});
    }
    REQUIRE(classify_probes(samples).result == Delivery::unfaithful);
}

TEST_CASE("classify_probes defers when too few probes are valid (edges/held input)") {
    auto samples = probe_with([](int dx, int dy) { return std::pair{dx, dy}; });
    for (std::size_t i = 2; i < samples.size(); ++i)
        samples[i].valid = false; // only two usable probes remain
    REQUIRE(classify_probes(samples).result == Delivery::deferred);
}

TEST_CASE("delivery names round-trip") {
    for (auto d : {Delivery::untested, Delivery::verified, Delivery::silent, Delivery::unfaithful, Delivery::deferred})
        REQUIRE(parse_delivery(delivery_name(d)) == d);
    REQUIRE(usable(Delivery::verified));
    REQUIRE_FALSE(usable(Delivery::silent));
    REQUIRE_FALSE(usable(Delivery::unfaithful));
    REQUIRE_FALSE(usable(Delivery::deferred));
}

TEST_CASE("normalize_coordinate matches the virtual-desktop 0..65535 mapping") {
    REQUIRE(normalize_coordinate(0, 0, 1920) == 0);
    REQUIRE(normalize_coordinate(1919, 0, 1920) == 65535);
    REQUIRE(normalize_coordinate(960, 0, 1920) > 32000);
    REQUIRE(normalize_coordinate(960, 0, 1920) < 33000);
}
