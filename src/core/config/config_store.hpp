#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/config/config_model.hpp"

namespace kiseki::core::config {

enum class PlatformKind {
    Windows,
    Linux,
};

struct EnvironmentSnapshot {
    std::optional<std::string> appdata;
    std::optional<std::string> xdg_config_home;
    std::optional<std::string> home;
};

struct StoreResult {
    bool ok;
    AppConfig config;
    std::string error;
};

struct SaveResult {
    bool ok;
    std::string error;
};

EnvironmentSnapshot current_environment();
PlatformKind current_platform();
std::filesystem::path default_config_path(const EnvironmentSnapshot& env, PlatformKind platform);

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path path);

    const std::filesystem::path& path() const;
    StoreResult load_or_default() const;
    SaveResult save(const AppConfig& config) const;

private:
    std::filesystem::path path_;
};

}
