#include "core/config/config_store.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/config/config_validation.hpp"

namespace kiseki::core::config {

namespace {

std::optional<std::string> getenv_string(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string{value};
}

std::string validation_error(const ValidationResult& validation) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < validation.issues.size(); ++index) {
        const auto& issue = validation.issues[index];
        if (index > 0) {
            stream << "; ";
        }
        stream << issue.path << ": " << issue.message;
    }
    return stream.str();
}

std::string file_error(std::string_view operation, const std::filesystem::path& path) {
    return std::string{operation} + ": " + path.string();
}

std::string file_error(
    std::string_view operation,
    const std::filesystem::path& path,
    const std::error_code& error_code) {
    return std::string{operation} + ": " + path.string() + ": " + error_code.message();
}

}

EnvironmentSnapshot current_environment() {
    return EnvironmentSnapshot{
        .appdata = getenv_string("APPDATA"),
        .xdg_config_home = getenv_string("XDG_CONFIG_HOME"),
        .home = getenv_string("HOME"),
    };
}

PlatformKind current_platform() {
#ifdef _WIN32
    return PlatformKind::Windows;
#elif defined(__APPLE__)
    return PlatformKind::MacOS;
#else
    return PlatformKind::Linux;
#endif
}

std::filesystem::path default_config_path(const EnvironmentSnapshot& env, PlatformKind platform) {
    if (platform == PlatformKind::Windows) {
        const std::filesystem::path base = env.appdata.value_or(".");
        return base / "KisekiInput" / "config.json";
    }

    if (platform == PlatformKind::MacOS) {
        const std::filesystem::path home = env.home.value_or(".");
        return home / "Library" / "Application Support" / "KisekiInput" / "config.json";
    }

    if (env.xdg_config_home.has_value()) {
        return std::filesystem::path{*env.xdg_config_home} / "kiseki-input" / "config.json";
    }

    const std::filesystem::path home = env.home.value_or(".");
    return home / ".config" / "kiseki-input" / "config.json";
}

ConfigStore::ConfigStore(std::filesystem::path path)
    : path_{std::move(path)} {}

const std::filesystem::path& ConfigStore::path() const {
    return path_;
}

StoreResult ConfigStore::load_or_default() const {
    std::error_code error_code;
    if (!std::filesystem::exists(path_, error_code)) {
        if (error_code) {
            return StoreResult{
                .ok = false,
                .config = default_config(),
                .error = file_error("failed to inspect config file", path_, error_code),
            };
        }
        return StoreResult{
            .ok = true,
            .config = default_config(),
            .error = "",
        };
    }

    std::ifstream file{path_};
    if (!file) {
        return StoreResult{
            .ok = false,
            .config = default_config(),
            .error = file_error("failed to open config file", path_),
        };
    }

    try {
        nlohmann::json json;
        file >> json;
        AppConfig config = config_from_json(json);
        const ValidationResult validation = validate_config(config);
        if (!validation.valid()) {
            return StoreResult{
                .ok = false,
                .config = config,
                .error = validation_error(validation),
            };
        }

        return StoreResult{
            .ok = true,
            .config = config,
            .error = "",
        };
    } catch (const std::exception& error) {
        return StoreResult{
            .ok = false,
            .config = default_config(),
            .error = file_error("failed to parse config file", path_) + ": " + error.what(),
        };
    }
}

SaveResult ConfigStore::save(const AppConfig& config) const {
    const ValidationResult validation = validate_config(config);
    if (!validation.valid()) {
        return SaveResult{
            .ok = false,
            .error = validation_error(validation),
        };
    }

    std::error_code error_code;
    const auto parent = path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error_code);
    }
    if (error_code) {
        return SaveResult{
            .ok = false,
            .error = file_error("failed to create config directory", parent, error_code),
        };
    }

    std::ofstream file{path_};
    if (!file) {
        return SaveResult{
            .ok = false,
            .error = file_error("failed to open config file for writing", path_),
        };
    }

    file << to_json(config).dump(2) << '\n';
    const bool write_ok = static_cast<bool>(file);
    file.close();
    if (!write_ok) {
        return SaveResult{
            .ok = false,
            .error = file_error("failed to write config file", path_),
        };
    }

    if (!file) {
        return SaveResult{
            .ok = false,
            .error = file_error("failed to close config file", path_),
        };
    }

    return SaveResult{
        .ok = true,
        .error = "",
    };
}

}
