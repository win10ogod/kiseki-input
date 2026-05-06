#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/config/config_model.hpp"

namespace kiseki::core::config {

struct ValidationIssue {
    std::string path;
    std::string message;
};

struct ValidationResult {
    std::vector<ValidationIssue> issues;

    bool valid() const;
    bool has_issue(std::string_view path) const;
};

ValidationResult validate_config(const AppConfig& config);

}
