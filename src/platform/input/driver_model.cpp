#include "platform/input/driver_model.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace kiseki::platform::input::driver {
namespace {

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool one_of(std::string_view key, std::initializer_list<std::string_view> names) {
    return std::find(names.begin(), names.end(), key) != names.end();
}

} // namespace

std::optional<Type> parse_type(std::string_view name) {
    const auto value = lower(name);
    if (value == "any" || value == "anydriver")
        return Type::any;
    if (value == "sendinput")
        return Type::sendinput;
    if (value == "logitech")
        return Type::logitech;
    if (value == "logitech-ghub-new" || value == "logitechghubnew")
        return Type::logitech_ghub_new;
    if (value == "razer")
        return Type::razer;
    if (value == "dd")
        return Type::dd;
    if (value == "mouclass" || value == "mouclassinputinjection")
        return Type::mouclass;
    return std::nullopt;
}

std::string type_name(Type type) {
    switch (type) {
    case Type::any:
        return "any";
    case Type::sendinput:
        return "sendinput";
    case Type::logitech:
        return "logitech";
    case Type::logitech_ghub_new:
        return "logitech-ghub-new";
    case Type::razer:
        return "razer";
    case Type::dd:
        return "dd";
    case Type::mouclass:
        return "mouclass";
    }
    return "any";
}

std::string config_type_name(Type type) {
    switch (type) {
    case Type::any:
        return "AnyDriver";
    case Type::sendinput:
        return "SendInput";
    case Type::logitech:
        return "Logitech";
    case Type::logitech_ghub_new:
        return "LogitechGHubNew";
    case Type::razer:
        return "Razer";
    case Type::dd:
        return "DD";
    case Type::mouclass:
        return "MouClassInputInjection";
    }
    return "AnyDriver";
}

std::uint32_t ib_send_type(Type type) {
    // Values of IbInputSimulator's Send::SendType.
    switch (type) {
    case Type::any:
        return 0;
    case Type::sendinput:
        return 1;
    case Type::logitech:
        return 2;
    case Type::razer:
        return 3;
    case Type::dd:
        return 4;
    case Type::mouclass:
        return 5;
    case Type::logitech_ghub_new:
        return 6;
    }
    return 0;
}

std::string ib_error_name(int code) {
    static const char *names[] = {"Success",          "InvalidArgument",    "LibraryNotFound", "LibraryLoadFailed",
                                  "LibraryError",     "DeviceCreateFailed", "DeviceNotFound",  "DeviceOpenFailed"};
    if (code >= 0 && code < static_cast<int>(std::size(names)))
        return names[code];
    return "error " + std::to_string(code);
}

const std::vector<Type> &probe_order() {
    // LogitechGHubNew precedes the legacy Logitech report format because both open the
    // same device; only delivery verification can tell which report the driver accepts.
    static const std::vector<Type> order{Type::logitech_ghub_new, Type::logitech, Type::razer, Type::dd,
                                         Type::mouclass};
    return order;
}

bool supports_keyboard(Type type) {
    return type != Type::mouclass;
}

bool driver_level(Type type) {
    return type != Type::sendinput && type != Type::any;
}

std::optional<std::string> key_limitation(Type type, std::string_view requested) {
    const auto key = lower(requested);
    const auto prefix = type_name(type) + " driver ";
    switch (type) {
    case Type::mouclass:
        return prefix + "has no keyboard injection";
    case Type::razer:
        // Razer translates VKs through a fixed USB/scan table and adds E0 only for
        // navigation keys and Win. Measured: rctrl, ralt, numpad-enter, numpad-divide, and
        // apps arrive as other keys; numlock and pause follow from the same table.
        if (one_of(key, {"rctrl"}))
            return prefix + "delivers left Ctrl for rctrl";
        if (one_of(key, {"ralt", "altgr"}))
            return prefix + "delivers left Alt for ralt";
        if (one_of(key, {"numpad-enter"}))
            return prefix + "delivers main Enter for numpad-enter";
        if (one_of(key, {"numpad-divide"}))
            return prefix + "delivers the main slash key for numpad-divide";
        if (one_of(key, {"menu", "apps"}))
            return prefix + "does not deliver the Menu key identity";
        if (one_of(key, {"numlock"}))
            return prefix + "maps numlock to the non-extended Pause scan code";
        if (one_of(key, {"pause"}))
            return prefix + "maps pause to the left Ctrl scan code";
        return std::nullopt;
    case Type::logitech:
    case Type::logitech_ghub_new:
    case Type::dd:
        // These paths receive only the VK; VK_RETURN cannot name the keypad Enter.
        if (key == "numpad-enter")
            return prefix + "receives only the virtual key and delivers main Enter for numpad-enter";
        return std::nullopt;
    case Type::any:
    case Type::sendinput:
        return std::nullopt;
    }
    return std::nullopt;
}

std::size_t simultaneous_key_limit(Type type) {
    // The Logitech keyboard report has six key slots; a seventh down is silently dropped.
    return type == Type::logitech || type == Type::logitech_ghub_new ? 6 : 0;
}

WheelPlan plan_wheel(Type type, int vertical, int horizontal) {
    WheelPlan plan;
    const auto name = type_name(type);
    switch (type) {
    case Type::sendinput:
    case Type::razer:
    case Type::any:
        if (vertical)
            plan.events.push_back({false, vertical, vertical});
        if (horizontal)
            plan.events.push_back({true, horizontal, horizontal});
        return plan;
    case Type::logitech:
    case Type::logitech_ghub_new:
    case Type::dd:
        if (horizontal) {
            plan.error = name + " driver has no horizontal wheel; use --backend system";
            return plan;
        }
        if (vertical % 120 != 0) {
            plan.error = name + " driver sends whole wheel detents only; use a multiple of 120 or --backend system";
            return plan;
        }
        if (type == Type::dd) {
            // DD repeats one detent per unit of mouseData.
            if (vertical)
                plan.events.push_back({false, vertical / 120, vertical});
        } else {
            // Logitech reports only the sign of each event: one detent per report.
            for (int index = 0; index < std::abs(vertical) / 120; ++index)
                plan.events.push_back({false, vertical > 0 ? 120 : -120, vertical > 0 ? 120 : -120});
        }
        return plan;
    case Type::mouclass:
        if (vertical || horizontal)
            plan.error = "mouclass driver has no wheel injection; use --backend system";
        return plan;
    }
    return plan;
}

std::string type_choices() {
    return "any, logitech-ghub-new, logitech, razer, dd, mouclass, or sendinput";
}

std::string validate_argument(Type type, const std::string &argument) {
    if (argument.empty())
        return "";
    if (type == Type::dd)
        return "";
    if (type == Type::mouclass) {
        if (argument.size() > 10 ||
            !std::all_of(argument.begin(), argument.end(), [](unsigned char c) { return std::isdigit(c); }))
            return "mouclass driver argument must be a decimal process id";
        const auto pid = std::stoull(argument);
        if (pid == 0 || pid > std::numeric_limits<std::uint32_t>::max())
            return "mouclass driver argument must be a nonzero process id";
        return "";
    }
    return "driver argument applies only to dd (DLL path) or mouclass (process id), not " + type_name(type);
}

Settings resolve_settings(const SettingsLayers &layers) {
    Settings settings;
    const auto present = [](const std::optional<std::string> &value) { return value && !value->empty(); };
    int layer = 0;
    std::optional<std::string> name;
    if (layers.cli_type) {
        name = layers.cli_type;
        layer = 3;
        settings.source = "cli";
    } else if (present(layers.env_type)) {
        name = layers.env_type;
        layer = 2;
        settings.source = "env";
    } else if (!layers.config_error.empty()) {
        settings.source = "config";
        settings.error = "config: " + layers.config_error;
        return settings;
    } else if (present(layers.config_type)) {
        name = layers.config_type;
        layer = 1;
        settings.source = layers.config_present ? "config" : "default";
    }
    if (name) {
        const auto parsed = parse_type(*name);
        if (!parsed) {
            settings.error = settings.source + " driver type '" + *name + "' must be " + type_choices();
            return settings;
        }
        settings.type = *parsed;
    }
    if (layers.cli_argument)
        settings.argument = *layers.cli_argument;
    else if (present(layers.env_argument) && layer <= 2)
        settings.argument = *layers.env_argument;
    else if (present(layers.config_argument) && layer <= 1)
        settings.argument = *layers.config_argument;
    settings.error = validate_argument(settings.type, settings.argument);
    return settings;
}

std::optional<AxisMap> fit_axis(const std::vector<std::pair<double, double>> &samples, double tolerance) {
    if (samples.size() < 2)
        return std::nullopt;
    double mean_x = 0, mean_y = 0;
    for (const auto &[x, y] : samples) {
        mean_x += x;
        mean_y += y;
    }
    mean_x /= static_cast<double>(samples.size());
    mean_y /= static_cast<double>(samples.size());
    double sxx = 0, sxy = 0;
    for (const auto &[x, y] : samples) {
        sxx += (x - mean_x) * (x - mean_x);
        sxy += (x - mean_x) * (y - mean_y);
    }
    if (sxx <= 0)
        return std::nullopt;
    AxisMap map;
    map.scale = sxy / sxx;
    map.offset = mean_y - map.scale * mean_x;
    if (!(map.scale > 0.1 && map.scale < 10))
        return std::nullopt;
    for (const auto &[x, y] : samples)
        if (std::abs(map.scale * x + map.offset - y) > tolerance)
            return std::nullopt;
    // Within quantization of the identity mapping, use Kiseki's exact integer normalization.
    map.identity = std::abs(map.scale - 1.0) * 65535.0 <= tolerance && std::abs(map.offset) <= tolerance;
    if (map.identity) {
        map.scale = 1.0;
        map.offset = 0.0;
    }
    return map;
}

int map_request(const AxisMap &map, int normalized_target) {
    if (map.identity)
        return normalized_target;
    const double request = std::round((normalized_target - map.offset) / map.scale);
    return static_cast<int>(std::clamp(request, -65535.0, 131070.0));
}

int normalize_coordinate(int value, int origin, int size) {
    if (size <= 1)
        return 0;
    const int clamped = std::clamp(value, origin, origin + size - 1);
    const long long relative = static_cast<long long>(clamped - origin) * 65535LL;
    return static_cast<int>(relative / static_cast<long long>(size - 1));
}

}
