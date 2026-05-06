#include "webui/config_api.hpp"

#include <exception>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/capabilities/capabilities_model.hpp"
#include "core/config/config_model.hpp"
#include "core/config/config_store.hpp"
#include "core/config/config_validation.hpp"

namespace kiseki::webui {

namespace {

ApiResponse json_response(int status, const nlohmann::json& body) {
    return ApiResponse{
        .status = status,
        .body = body.dump(2),
        .content_type = "application/json",
    };
}

nlohmann::json validation_issues_json(const core::config::ValidationResult& validation) {
    nlohmann::json issues = nlohmann::json::array();
    for (const auto& issue : validation.issues) {
        issues.push_back({
            {"path", issue.path},
            {"message", issue.message},
        });
    }
    return issues;
}

}

ConfigApi::ConfigApi(std::filesystem::path config_path)
    : config_path_{std::move(config_path)} {}

ApiResponse ConfigApi::get_config() const {
    const core::config::ConfigStore store{config_path_};
    const auto result = store.load_or_default();
    if (!result.ok) {
        return json_response(500, {{"error", result.error}});
    }

    return json_response(200, core::config::to_json(result.config));
}

ApiResponse ConfigApi::put_config(std::string_view body) const {
    try {
        const auto json = nlohmann::json::parse(body.begin(), body.end());
        const auto config = core::config::config_from_json(json);
        const auto validation = core::config::validate_config(config);
        if (!validation.valid()) {
            return json_response(
                400,
                {
                    {"error", "invalid configuration"},
                    {"issues", validation_issues_json(validation)},
                });
        }

        const core::config::ConfigStore store{config_path_};
        const auto save = store.save(config);
        if (!save.ok) {
            return json_response(500, {{"error", save.error}});
        }

        return json_response(200, core::config::to_json(config));
    } catch (const std::exception& error) {
        return json_response(400, {{"error", error.what()}});
    }
}

ApiResponse ConfigApi::get_capabilities() const {
    return json_response(200, core::capabilities::to_json(core::capabilities::foundation_capabilities()));
}

std::vector<std::string> ConfigApi::routes() {
    return {
        "GET /api/config",
        "PUT /api/config",
        "GET /api/capabilities",
    };
}

}
