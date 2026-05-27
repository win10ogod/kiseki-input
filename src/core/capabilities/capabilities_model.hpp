#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace kiseki::core::capabilities {

struct InputCapabilities {
    bool driver = false;
    bool system = false;
    bool background_window = false;
};

struct CaptureCapabilities {
    bool desktop = false;
    bool window = false;
    bool region = false;
    bool burst = false;
};

struct SessionCapabilities {
    bool background_desktop = false;
    bool macos_cua_background = false;
};

struct CapabilityMatrix {
    InputCapabilities input;
    CaptureCapabilities capture;
    SessionCapabilities session;
    std::vector<std::string> limitations;
};

CapabilityMatrix foundation_capabilities();
nlohmann::json to_json(const CapabilityMatrix& capabilities);

}
