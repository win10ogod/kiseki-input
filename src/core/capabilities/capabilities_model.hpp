#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace kiseki::core::capabilities {

struct InputCapabilities {
    bool driver;
    bool background_window;
};

struct CaptureCapabilities {
    bool desktop;
    bool window;
    bool region;
    bool burst;
};

struct CapabilityMatrix {
    InputCapabilities input;
    CaptureCapabilities capture;
    std::vector<std::string> limitations;
};

CapabilityMatrix foundation_capabilities();
nlohmann::json to_json(const CapabilityMatrix& capabilities);

}
