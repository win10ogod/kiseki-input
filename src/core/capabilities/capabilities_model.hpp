#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace kiseki::core::capabilities {

struct InputCapabilities {
    bool driver = false;
    bool background_window = false;
};

struct CaptureCapabilities {
    bool desktop = false;
    bool window = false;
    bool region = false;
    bool burst = false;
};

struct CapabilityMatrix {
    InputCapabilities input;
    CaptureCapabilities capture;
    std::vector<std::string> limitations;
};

CapabilityMatrix foundation_capabilities();
nlohmann::json to_json(const CapabilityMatrix& capabilities);

}
