#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/target/target.hpp"

namespace kiseki::platform::observe {

struct UiObservationOptions {
    kiseki::platform::target::TargetQuery target;
    std::string provider = "auto";
    int max_depth = 4;
    int max_elements = 256;
};

struct UiElement {
    std::string kind;
    std::string id;
    std::string parent_id;
    int depth = 0;
    std::string name;
    std::string title;
    std::string automation_id;
    std::string class_name;
    std::string localized_control_type;
    std::string framework_id;
    int control_type = 0;
    int process_id = 0;
    bool has_bounds = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool has_enabled = false;
    bool enabled = false;
    bool has_offscreen = false;
    bool offscreen = false;
    std::string role;
    std::string subrole;
    std::string description;
    std::string value;
};

struct UiObservationResult {
    bool ok = false;
    int code = 2;
    std::string source;
    bool visual = false;
    std::string coordinate_space = "screen";
    kiseki::platform::target::TargetWindow target;
    std::vector<UiElement> elements;
    bool truncated = false;
    std::string fallback_reason;
    std::string error;
};

UiObservationResult observe_ui(const UiObservationOptions& options);

}
