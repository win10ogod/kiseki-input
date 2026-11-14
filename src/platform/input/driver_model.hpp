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
    int probe_timeout_ms = 300;
    int verify_timeout_ms = 1000;
    bool operator==(const Settings &) const = default;
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
