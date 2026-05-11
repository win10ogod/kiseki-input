#include "core/capabilities/capabilities_model.hpp"

namespace kiseki::core::capabilities {

CapabilityMatrix foundation_capabilities() {
    return CapabilityMatrix{
        .input = InputCapabilities{
            .driver = false,
            .system = false,
            .background_window = false,
        },
        .capture = CaptureCapabilities{
            .desktop = false,
            .window = false,
            .region = false,
            .burst = false,
        },
        .limitations = {
            "foundation build exposes configuration and WebUI only",
            "input, screenshot, target, notification, and daemon backends are separate implementation slices",
            "background-window input depends on whether the target accepts system window messages or public automation events",
        },
    };
}

nlohmann::json to_json(const CapabilityMatrix& capabilities) {
    return nlohmann::json{
        {"input", {
            {"driver", capabilities.input.driver},
            {"system", capabilities.input.system},
            {"backgroundWindow", capabilities.input.background_window},
        }},
        {"capture", {
            {"desktop", capabilities.capture.desktop},
            {"window", capabilities.capture.window},
            {"region", capabilities.capture.region},
            {"burst", capabilities.capture.burst},
        }},
        {"limitations", capabilities.limitations},
    };
}

}
