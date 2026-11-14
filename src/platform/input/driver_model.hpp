#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Portable rules for the IbInputSimulator driver backend. The Windows runtime uses
// them; tests exercise them on every platform.
namespace kiseki::platform::input::driver {

enum class Type { any, sendinput, logitech, logitech_ghub_new, razer, dd, mouclass };

// Accepts Kiseki names (any, logitech-ghub-new, mouclass, ...) and the IbInputSimulator
// names used by the config file (AnyDriver, LogitechGHubNew, MouClassInputInjection, ...).
std::optional<Type> parse_type(std::string_view name);
std::string type_name(Type type);
std::string config_type_name(Type type);
std::uint32_t ib_send_type(Type type);
std::string ib_error_name(int code);
// Candidates tried by `any`, in order. SendInput is diagnostic and never selected automatically.
const std::vector<Type> &probe_order();
bool supports_keyboard(Type type);
bool driver_level(Type type);

// Static limits from each IbInputSimulator translation path; checked before sending so a
// known-wrong identity never reaches an application.
std::optional<std::string> key_limitation(Type type, std::string_view key);
std::size_t simultaneous_key_limit(Type type); // 0 means unlimited

// Delivery verdict for a driver type. A driver must prove it moves the pointer by a bounded,
// direction-correct, repeatable amount (scaling and acceleration are fine); otherwise it is
// unfaithful and must not be used, even though its device opened.
enum class Delivery { untested, verified, silent, unfaithful, deferred };
std::string delivery_name(Delivery delivery);
std::optional<Delivery> parse_delivery(std::string_view name);
bool usable(Delivery delivery); // only `verified` may be used

// One bounded relative self-test probe: what was commanded, what the cursor actually moved.
struct ProbeSample {
    int cmd_dx = 0;
    int cmd_dy = 0;
    int obs_dx = 0;
    int obs_dy = 0;
    bool valid = true; // false when a screen edge clamped the move, so it proves nothing
};
struct ProbeLimits {
    int fling_bound = 40;     // a <=~10px command moving more than this is a fling
    int cross_bound = 6;      // movement on the uncommanded axis must stay small
    int repeat_tolerance = 8; // repeats of one command must agree within this
    int min_valid = 4;        // fewer valid probes than this is inconclusive
};
struct ProbeVerdict {
    Delivery result = Delivery::untested;
    std::string detail;
};
// Classifies self-test probes into a delivery verdict. Pure and platform-independent so the
// good/silent/scaled/unfaithful vendor behaviours are covered by unit tests, not live runs.
ProbeVerdict classify_probes(const std::vector<ProbeSample> &samples, const ProbeLimits &limits = {});
// The small, bounded, duplicated relative commands the self-test sends (duplicates let the
// classifier check repeatability). Magnitudes stay above sub-pixel scaling and never fling.
const std::vector<std::pair<int, int>> &standard_probes();

struct WheelEvent {
    bool horizontal = false;
    int data = 0;  // MOUSEINPUT mouseData passed to IbSendInput
    int delta = 0; // delta expected in the low-level event stream
};
struct WheelPlan {
    std::string error;
    std::vector<WheelEvent> events;
};
WheelPlan plan_wheel(Type type, int vertical, int horizontal);

struct Settings {
    Type type = Type::any;
    std::string argument;
    std::string source = "default";
    std::string error;
    std::string cache_path; // where to persist verification across invocations (empty: none)
    bool reverify = false;   // force a fresh self-test and overwrite any cache entry
    int probe_timeout_ms = 300;
    int verify_timeout_ms = 1000;
    bool operator==(const Settings &) const = default;
    // Two settings open the same device iff type and argument match; cache/reverify do not
    // change the opened device, so they must not force a re-open.
    bool same_target(const Settings &other) const {
        return type == other.type && argument == other.argument && error == other.error;
    }
};

// Each layer may name a type and an argument. An argument applies only from the layer that
// selected the type or a higher one (cli > env > config).
struct SettingsLayers {
    std::optional<std::string> cli_type;
    std::optional<std::string> cli_argument;
    std::optional<std::string> env_type;
    std::optional<std::string> env_argument;
    std::optional<std::string> config_type;
    std::optional<std::string> config_argument;
    bool config_present = false;
    std::string config_error;
};
Settings resolve_settings(const SettingsLayers &layers);
std::string validate_argument(Type type, const std::string &argument);
std::string type_choices();

// Absolute moves are calibrated in normalized 0..65535 virtual-desktop units:
// observed = scale * requested + offset.
struct AxisMap {
    double scale = 1.0;
    double offset = 0.0;
    bool identity = true;
};
// Returns a map when the samples are linear within `tolerance` normalized units.
std::optional<AxisMap> fit_axis(const std::vector<std::pair<double, double>> &samples, double tolerance);
int map_request(const AxisMap &map, int normalized_target);
int normalize_coordinate(int value, int origin, int size);

}
