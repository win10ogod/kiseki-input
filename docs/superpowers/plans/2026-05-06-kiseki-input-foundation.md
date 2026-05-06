# Kiseki Input Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first runnable C++ foundation for Kiseki Input: CMake, tests, configuration model, validation, config storage, capabilities output, CLI command skeleton, and a WebUI that can only read/write configuration.

**Architecture:** The project starts with a `kiseki_core` library for configuration and capabilities, a `kiseki_webui` library for config-only HTTP behavior, and a `kiseki_cli` library so command behavior is testable without spawning processes. The executable is a thin `main.cpp` wrapper. Operational input, screenshot, target, notification, and platform backends are intentionally handled by separate follow-on plans so each feature can be optimized independently.

**Tech Stack:** C++20, CMake, CLI11, nlohmann/json, cpp-httplib, Catch2, CTest.

---

## Scope Check

The approved spec covers multiple independent subsystems: configuration, WebUI, target resolution, input simulation, screenshots, notifications, platform capability probing, and daemon behavior. This plan implements the foundation slice only. It produces a buildable CLI with config and capabilities commands plus a config-only WebUI boundary. The following subsystems get separate implementation plans after this one: target resolution, burst screenshot pipeline, Windows/Linux screenshot backends, input event pipeline, Windows/Linux input backends, and notification/daemon behavior.

## File Structure

Create and modify these files:

```text
CMakeLists.txt
src/core/version.hpp
src/core/version.cpp
src/core/config/config_model.hpp
src/core/config/config_model.cpp
src/core/config/config_validation.hpp
src/core/config/config_validation.cpp
src/core/config/config_store.hpp
src/core/config/config_store.cpp
src/core/capabilities/capabilities_model.hpp
src/core/capabilities/capabilities_model.cpp
src/cli/app.hpp
src/cli/app.cpp
src/cli/main.cpp
src/webui/config_api.hpp
src/webui/config_api.cpp
src/webui/static_assets.hpp
src/webui/static_assets.cpp
src/webui/web_server.hpp
src/webui/web_server.cpp
ui/index.html
ui/styles.css
ui/app.js
test/smoke_test.cpp
test/config/config_model_test.cpp
test/config/config_validation_test.cpp
test/config/config_store_test.cpp
test/capabilities/capabilities_model_test.cpp
test/cli/cli_app_test.cpp
test/webui/config_api_test.cpp
test/webui/static_assets_test.cpp
```

The existing empty legacy directories under `src/windows/*` and `src/linux/*` do not need code in this plan.

---

### Task 1: CMake, Dependencies, And Smoke Test

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/core/version.hpp`
- Create: `src/core/version.cpp`
- Create: `src/cli/main.cpp`
- Create: `test/smoke_test.cpp`

- [ ] **Step 1: Write the failing smoke test**

Create `test/smoke_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "core/version.hpp"

TEST_CASE("version is available") {
    REQUIRE(kiseki::core::version() == "0.1.0");
}
```

- [ ] **Step 2: Run configure to verify it fails**

Run:

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
```

Expected: FAIL because `CMakeLists.txt` does not exist.

- [ ] **Step 3: Add the CMake project and minimal implementation**

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.24)

project(KisekiInput VERSION 0.1.0 LANGUAGES CXX)

option(KISEKI_BUILD_TESTING "Build Kiseki tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    CLI11
    URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.4.2.tar.gz
)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
)
FetchContent_Declare(
    cpp_httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.15.3.tar.gz
)
FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz
)

FetchContent_MakeAvailable(CLI11 nlohmann_json cpp_httplib)

set(KISEKI_CORE_SOURCES
    src/core/version.cpp
)

add_library(kiseki_core STATIC ${KISEKI_CORE_SOURCES})
target_include_directories(kiseki_core PUBLIC src)
target_link_libraries(kiseki_core PUBLIC nlohmann_json::nlohmann_json)

add_executable(kiseki src/cli/main.cpp)
target_link_libraries(kiseki PRIVATE kiseki_core CLI11::CLI11)

if(KISEKI_BUILD_TESTING)
    FetchContent_MakeAvailable(Catch2)
    include(CTest)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    set(KISEKI_TEST_SOURCES
        test/smoke_test.cpp
    )

    add_executable(kiseki_tests ${KISEKI_TEST_SOURCES})
    target_link_libraries(kiseki_tests PRIVATE kiseki_core Catch2::Catch2WithMain)
    catch_discover_tests(kiseki_tests)
endif()
```

Create `src/core/version.hpp`:

```cpp
#pragma once

#include <string_view>

namespace kiseki::core {

std::string_view version();

}
```

Create `src/core/version.cpp`:

```cpp
#include "core/version.hpp"

namespace kiseki::core {

std::string_view version() {
    return "0.1.0";
}

}
```

Create `src/cli/main.cpp`:

```cpp
#include <CLI/CLI.hpp>

#include <iostream>

#include "core/version.hpp"

int main(int argc, char** argv) {
    CLI::App app{"Kiseki Input"};
    app.set_version_flag("--version", std::string{kiseki::core::version()});

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    std::cout << "Kiseki Input " << kiseki::core::version() << '\n';
    return 0;
}
```

- [ ] **Step 4: Run smoke verification**

Run:

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS with the `version is available` test passing.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/version.hpp src/core/version.cpp src/cli/main.cpp test/smoke_test.cpp
git commit -m "build: add c++ project scaffold"
```

---

### Task 2: Configuration Model And JSON Round Trip

**Files:**
- Create: `src/core/config/config_model.hpp`
- Create: `src/core/config/config_model.cpp`
- Create: `test/config/config_model_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing configuration model test**

Create `test/config/config_model_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "core/config/config_model.hpp"

using kiseki::core::config::AppConfig;
using kiseki::core::config::config_from_json;
using kiseki::core::config::default_config;
using kiseki::core::config::to_json;

TEST_CASE("default config matches schema version one") {
    const AppConfig config = default_config();

    REQUIRE(config.schema_version == 1);
    REQUIRE(config.webui.host == "127.0.0.1");
    REQUIRE(config.webui.port == 8787);
    REQUIRE(config.heartbeat.enabled);
    REQUIRE(config.heartbeat.interval_seconds == 300);
    REQUIRE(config.heartbeat.notification_enabled);
    REQUIRE(config.heartbeat.message == "Kiseki Input is running");
    REQUIRE(config.input.default_backend == "background-window");
    REQUIRE(config.input.windows_driver == "AnyDriver");
    REQUIRE(config.input.linux_driver == "uinput");
    REQUIRE(config.input.background_input_enabled);
    REQUIRE(config.screenshot.burst_fps == 60);
    REQUIRE(config.screenshot.burst_frames == 8);
    REQUIRE(config.screenshot.format == "png");
    REQUIRE(config.safety.allow_driver_input_without_target);
    REQUIRE(config.safety.allow_background_input_for_games);
}

TEST_CASE("config converts to and from json") {
    AppConfig config = default_config();
    config.webui.port = 9000;
    config.heartbeat.message = "running";
    config.screenshot.default_output_directory = "frames";

    const auto json = to_json(config);
    const AppConfig parsed = config_from_json(json);

    REQUIRE(parsed.webui.port == 9000);
    REQUIRE(parsed.heartbeat.message == "running");
    REQUIRE(parsed.screenshot.default_output_directory == "frames");
    REQUIRE(to_json(parsed) == json);
}
```

- [ ] **Step 2: Register the test and verify it fails**

Modify the source lists in `CMakeLists.txt`:

```cmake
set(KISEKI_CORE_SOURCES
    src/core/version.cpp
    src/core/config/config_model.cpp
)
```

```cmake
set(KISEKI_TEST_SOURCES
    test/smoke_test.cpp
    test/config/config_model_test.cpp
)
```

Run:

```bash
cmake --build build
```

Expected: FAIL because `core/config/config_model.hpp` does not exist.

- [ ] **Step 3: Implement the model**

Create `src/core/config/config_model.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace kiseki::core::config {

struct WebUiConfig {
    std::string host;
    std::uint16_t port;
};

struct HeartbeatConfig {
    bool enabled;
    std::uint32_t interval_seconds;
    bool notification_enabled;
    std::string message;
};

struct InputConfig {
    std::string default_backend;
    std::string windows_driver;
    std::string linux_driver;
    bool background_input_enabled;
};

struct ScreenshotConfig {
    std::string default_output_directory;
    std::uint32_t burst_fps;
    std::uint32_t burst_frames;
    std::string format;
};

struct SafetyConfig {
    bool allow_driver_input_without_target;
    bool allow_background_input_for_games;
};

struct AppConfig {
    std::uint32_t schema_version;
    WebUiConfig webui;
    HeartbeatConfig heartbeat;
    InputConfig input;
    ScreenshotConfig screenshot;
    SafetyConfig safety;
};

AppConfig default_config();
nlohmann::json to_json(const AppConfig& config);
AppConfig config_from_json(const nlohmann::json& json);

}
```

Create `src/core/config/config_model.cpp`:

```cpp
#include "core/config/config_model.hpp"

namespace kiseki::core::config {

AppConfig default_config() {
    return AppConfig{
        .schema_version = 1,
        .webui = WebUiConfig{
            .host = "127.0.0.1",
            .port = 8787,
        },
        .heartbeat = HeartbeatConfig{
            .enabled = true,
            .interval_seconds = 300,
            .notification_enabled = true,
            .message = "Kiseki Input is running",
        },
        .input = InputConfig{
            .default_backend = "background-window",
            .windows_driver = "AnyDriver",
            .linux_driver = "uinput",
            .background_input_enabled = true,
        },
        .screenshot = ScreenshotConfig{
            .default_output_directory = "",
            .burst_fps = 60,
            .burst_frames = 8,
            .format = "png",
        },
        .safety = SafetyConfig{
            .allow_driver_input_without_target = true,
            .allow_background_input_for_games = true,
        },
    };
}

nlohmann::json to_json(const AppConfig& config) {
    return nlohmann::json{
        {"schemaVersion", config.schema_version},
        {"webui", {
            {"host", config.webui.host},
            {"port", config.webui.port},
        }},
        {"heartbeat", {
            {"enabled", config.heartbeat.enabled},
            {"intervalSeconds", config.heartbeat.interval_seconds},
            {"notificationEnabled", config.heartbeat.notification_enabled},
            {"message", config.heartbeat.message},
        }},
        {"input", {
            {"defaultBackend", config.input.default_backend},
            {"windowsDriver", config.input.windows_driver},
            {"linuxDriver", config.input.linux_driver},
            {"backgroundInputEnabled", config.input.background_input_enabled},
        }},
        {"screenshot", {
            {"defaultOutputDirectory", config.screenshot.default_output_directory},
            {"burstFps", config.screenshot.burst_fps},
            {"burstFrames", config.screenshot.burst_frames},
            {"format", config.screenshot.format},
        }},
        {"safety", {
            {"allowDriverInputWithoutTarget", config.safety.allow_driver_input_without_target},
            {"allowBackgroundInputForGames", config.safety.allow_background_input_for_games},
        }},
    };
}

AppConfig config_from_json(const nlohmann::json& json) {
    AppConfig config = default_config();

    config.schema_version = json.value("schemaVersion", config.schema_version);

    const auto webui = json.value("webui", nlohmann::json::object());
    config.webui.host = webui.value("host", config.webui.host);
    config.webui.port = webui.value("port", config.webui.port);

    const auto heartbeat = json.value("heartbeat", nlohmann::json::object());
    config.heartbeat.enabled = heartbeat.value("enabled", config.heartbeat.enabled);
    config.heartbeat.interval_seconds = heartbeat.value("intervalSeconds", config.heartbeat.interval_seconds);
    config.heartbeat.notification_enabled = heartbeat.value("notificationEnabled", config.heartbeat.notification_enabled);
    config.heartbeat.message = heartbeat.value("message", config.heartbeat.message);

    const auto input = json.value("input", nlohmann::json::object());
    config.input.default_backend = input.value("defaultBackend", config.input.default_backend);
    config.input.windows_driver = input.value("windowsDriver", config.input.windows_driver);
    config.input.linux_driver = input.value("linuxDriver", config.input.linux_driver);
    config.input.background_input_enabled = input.value("backgroundInputEnabled", config.input.background_input_enabled);

    const auto screenshot = json.value("screenshot", nlohmann::json::object());
    config.screenshot.default_output_directory = screenshot.value("defaultOutputDirectory", config.screenshot.default_output_directory);
    config.screenshot.burst_fps = screenshot.value("burstFps", config.screenshot.burst_fps);
    config.screenshot.burst_frames = screenshot.value("burstFrames", config.screenshot.burst_frames);
    config.screenshot.format = screenshot.value("format", config.screenshot.format);

    const auto safety = json.value("safety", nlohmann::json::object());
    config.safety.allow_driver_input_without_target =
        safety.value("allowDriverInputWithoutTarget", config.safety.allow_driver_input_without_target);
    config.safety.allow_background_input_for_games =
        safety.value("allowBackgroundInputForGames", config.safety.allow_background_input_for_games);

    return config;
}

}
```

- [ ] **Step 4: Run the tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS for smoke and config model tests.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/config/config_model.hpp src/core/config/config_model.cpp test/config/config_model_test.cpp
git commit -m "feat: add configuration model"
```

---

### Task 3: Configuration Validation

**Files:**
- Create: `src/core/config/config_validation.hpp`
- Create: `src/core/config/config_validation.cpp`
- Create: `test/config/config_validation_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing validation tests**

Create `test/config/config_validation_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "core/config/config_model.hpp"
#include "core/config/config_validation.hpp"

using kiseki::core::config::default_config;
using kiseki::core::config::validate_config;

TEST_CASE("default config is valid") {
    const auto result = validate_config(default_config());
    REQUIRE(result.valid());
    REQUIRE(result.issues.empty());
}

TEST_CASE("invalid webui port is rejected") {
    auto config = default_config();
    config.webui.port = 0;

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.issues.at(0).path == "webui.port");
}

TEST_CASE("invalid backend and screenshot settings are rejected") {
    auto config = default_config();
    config.input.default_backend = "raw";
    config.input.windows_driver = "Unknown";
    config.screenshot.burst_fps = 0;
    config.screenshot.burst_frames = 0;
    config.screenshot.format = "gif";

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("input.defaultBackend"));
    REQUIRE(result.has_issue("input.windowsDriver"));
    REQUIRE(result.has_issue("screenshot.burstFps"));
    REQUIRE(result.has_issue("screenshot.burstFrames"));
    REQUIRE(result.has_issue("screenshot.format"));
}

TEST_CASE("empty heartbeat message is rejected when notifications are enabled") {
    auto config = default_config();
    config.heartbeat.notification_enabled = true;
    config.heartbeat.message = "";

    const auto result = validate_config(config);

    REQUIRE_FALSE(result.valid());
    REQUIRE(result.has_issue("heartbeat.message"));
}
```

- [ ] **Step 2: Register and verify failure**

Modify `CMakeLists.txt`:

```cmake
set(KISEKI_CORE_SOURCES
    src/core/version.cpp
    src/core/config/config_model.cpp
    src/core/config/config_validation.cpp
)
```

```cmake
set(KISEKI_TEST_SOURCES
    test/smoke_test.cpp
    test/config/config_model_test.cpp
    test/config/config_validation_test.cpp
)
```

Run:

```bash
cmake --build build
```

Expected: FAIL because `core/config/config_validation.hpp` does not exist.

- [ ] **Step 3: Implement validation**

Create `src/core/config/config_validation.hpp`:

```cpp
#pragma once

#include <string>
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
```

Create `src/core/config/config_validation.cpp`:

```cpp
#include "core/config/config_validation.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace kiseki::core::config {

namespace {

bool one_of(std::string_view value, const auto& allowed) {
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

void add_issue(ValidationResult& result, std::string path, std::string message) {
    result.issues.push_back(ValidationIssue{
        .path = std::move(path),
        .message = std::move(message),
    });
}

}

bool ValidationResult::valid() const {
    return issues.empty();
}

bool ValidationResult::has_issue(std::string_view path) const {
    return std::any_of(issues.begin(), issues.end(), [path](const ValidationIssue& issue) {
        return issue.path == path;
    });
}

ValidationResult validate_config(const AppConfig& config) {
    ValidationResult result;

    if (config.schema_version != 1) {
        add_issue(result, "schemaVersion", "schemaVersion must be 1");
    }

    if (config.webui.host.empty()) {
        add_issue(result, "webui.host", "host must not be empty");
    }

    if (config.webui.port == 0) {
        add_issue(result, "webui.port", "port must be between 1 and 65535");
    }

    if (config.heartbeat.enabled && config.heartbeat.interval_seconds < 1) {
        add_issue(result, "heartbeat.intervalSeconds", "intervalSeconds must be at least 1 when heartbeat is enabled");
    }

    if (config.heartbeat.notification_enabled && config.heartbeat.message.empty()) {
        add_issue(result, "heartbeat.message", "message must not be empty when notifications are enabled");
    }

    constexpr std::array<std::string_view, 2> input_backends{
        "driver",
        "background-window",
    };
    if (!one_of(config.input.default_backend, input_backends)) {
        add_issue(result, "input.defaultBackend", "defaultBackend must be driver or background-window");
    }

    constexpr std::array<std::string_view, 7> windows_drivers{
        "AnyDriver",
        "SendInput",
        "Logitech",
        "LogitechGHubNew",
        "Razer",
        "DD",
        "MouClassInputInjection",
    };
    if (!one_of(config.input.windows_driver, windows_drivers)) {
        add_issue(result, "input.windowsDriver", "windowsDriver is not supported");
    }

    if (config.input.linux_driver != "uinput") {
        add_issue(result, "input.linuxDriver", "linuxDriver must be uinput");
    }

    if (config.screenshot.burst_fps < 1 || config.screenshot.burst_fps > 240) {
        add_issue(result, "screenshot.burstFps", "burstFps must be between 1 and 240");
    }

    if (config.screenshot.burst_frames < 1 || config.screenshot.burst_frames > 240) {
        add_issue(result, "screenshot.burstFrames", "burstFrames must be between 1 and 240");
    }

    constexpr std::array<std::string_view, 1> formats{"png"};
    if (!one_of(config.screenshot.format, formats)) {
        add_issue(result, "screenshot.format", "format must be png");
    }

    return result;
}

}
```

- [ ] **Step 4: Run validation tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS for all registered tests.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/config/config_validation.hpp src/core/config/config_validation.cpp test/config/config_validation_test.cpp
git commit -m "feat: validate configuration"
```

---

### Task 4: Configuration Store And Platform Paths

**Files:**
- Create: `src/core/config/config_store.hpp`
- Create: `src/core/config/config_store.cpp`
- Create: `test/config/config_store_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing config store tests**

Create `test/config/config_store_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

#include "core/config/config_store.hpp"

using kiseki::core::config::ConfigStore;
using kiseki::core::config::EnvironmentSnapshot;
using kiseki::core::config::PlatformKind;
using kiseki::core::config::default_config;
using kiseki::core::config::default_config_path;

namespace {

void write_text(const std::filesystem::path& path, std::string_view content) {
    std::ofstream file{path};
    file << content;
}

}

TEST_CASE("default path uses appdata on windows") {
    EnvironmentSnapshot env;
    env.appdata = "C:/Users/A/AppData/Roaming";

    const auto path = default_config_path(env, PlatformKind::Windows);

    REQUIRE(path.generic_string() == "C:/Users/A/AppData/Roaming/KisekiInput/config.json");
}

TEST_CASE("default path uses xdg config home on linux") {
    EnvironmentSnapshot env;
    env.xdg_config_home = "/home/a/.config-custom";
    env.home = "/home/a";

    const auto path = default_config_path(env, PlatformKind::Linux);

    REQUIRE(path.generic_string() == "/home/a/.config-custom/kiseki-input/config.json");
}

TEST_CASE("default path falls back to home on linux") {
    EnvironmentSnapshot env;
    env.home = "/home/a";

    const auto path = default_config_path(env, PlatformKind::Linux);

    REQUIRE(path.generic_string() == "/home/a/.config/kiseki-input/config.json");
}

TEST_CASE("store saves and loads config") {
    const auto temp = std::filesystem::temp_directory_path() / "kiseki-config-store-test.json";
    std::filesystem::remove(temp);

    ConfigStore store{temp};
    auto config = default_config();
    config.webui.port = 9999;

    const auto save_result = store.save(config);
    REQUIRE(save_result.ok);

    const auto load_result = store.load_or_default();
    REQUIRE(load_result.ok);
    REQUIRE(load_result.config.webui.port == 9999);

    std::filesystem::remove(temp);
}

TEST_CASE("invalid saved config returns validation error") {
    const auto temp = std::filesystem::temp_directory_path() / "kiseki-invalid-config-store-test.json";
    write_text(temp, "{\"webui\":{\"port\":0}}");

    ConfigStore store{temp};
    const auto load_result = store.load_or_default();

    REQUIRE_FALSE(load_result.ok);
    REQUIRE(load_result.error.find("webui.port") != std::string::npos);

    std::filesystem::remove(temp);
}
```

- [ ] **Step 2: Register and verify failure**

Modify `CMakeLists.txt`:

```cmake
set(KISEKI_CORE_SOURCES
    src/core/version.cpp
    src/core/config/config_model.cpp
    src/core/config/config_validation.cpp
    src/core/config/config_store.cpp
)
```

```cmake
set(KISEKI_TEST_SOURCES
    test/smoke_test.cpp
    test/config/config_model_test.cpp
    test/config/config_validation_test.cpp
    test/config/config_store_test.cpp
)
```

Run:

```bash
cmake --build build
```

Expected: FAIL because `core/config/config_store.hpp` does not exist.

- [ ] **Step 3: Implement config store**

Create `src/core/config/config_store.hpp`:

```cpp
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
```

Create `src/core/config/config_store.cpp`:

```cpp
#include "core/config/config_store.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
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
#else
    return PlatformKind::Linux;
#endif
}

std::filesystem::path default_config_path(const EnvironmentSnapshot& env, PlatformKind platform) {
    if (platform == PlatformKind::Windows) {
        const std::filesystem::path base = env.appdata.value_or(".");
        return base / "KisekiInput" / "config.json";
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
    if (!std::filesystem::exists(path_)) {
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
            .error = "failed to open config file: " + path_.string(),
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
            .error = error.what(),
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
    std::filesystem::create_directories(path_.parent_path(), error_code);
    if (error_code) {
        return SaveResult{
            .ok = false,
            .error = error_code.message(),
        };
    }

    std::ofstream file{path_};
    if (!file) {
        return SaveResult{
            .ok = false,
            .error = "failed to write config file: " + path_.string(),
        };
    }

    file << to_json(config).dump(2) << '\n';
    return SaveResult{
        .ok = true,
        .error = "",
    };
}

}
```

- [ ] **Step 4: Run config store tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS for all registered tests.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/config/config_store.hpp src/core/config/config_store.cpp test/config/config_store_test.cpp
git commit -m "feat: persist configuration"
```

---

### Task 5: Capability Matrix Model

**Files:**
- Create: `src/core/capabilities/capabilities_model.hpp`
- Create: `src/core/capabilities/capabilities_model.cpp`
- Create: `test/capabilities/capabilities_model_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing capability tests**

Create `test/capabilities/capabilities_model_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "core/capabilities/capabilities_model.hpp"

using kiseki::core::capabilities::foundation_capabilities;
using kiseki::core::capabilities::to_json;

TEST_CASE("foundation capabilities include explicit limitations") {
    const auto capabilities = foundation_capabilities();
    const auto json = to_json(capabilities);

    REQUIRE(json["input"]["driver"].get<bool>() == false);
    REQUIRE(json["input"]["backgroundWindow"].get<bool>() == false);
    REQUIRE(json["capture"]["desktop"].get<bool>() == false);
    REQUIRE(json["capture"]["window"].get<bool>() == false);
    REQUIRE(json["capture"]["region"].get<bool>() == false);
    REQUIRE(json["capture"]["burst"].get<bool>() == false);
    REQUIRE_FALSE(json["limitations"].empty());
}
```

- [ ] **Step 2: Register and verify failure**

Modify `CMakeLists.txt`:

```cmake
set(KISEKI_CORE_SOURCES
    src/core/version.cpp
    src/core/config/config_model.cpp
    src/core/config/config_validation.cpp
    src/core/config/config_store.cpp
    src/core/capabilities/capabilities_model.cpp
)
```

```cmake
set(KISEKI_TEST_SOURCES
    test/smoke_test.cpp
    test/config/config_model_test.cpp
    test/config/config_validation_test.cpp
    test/config/config_store_test.cpp
    test/capabilities/capabilities_model_test.cpp
)
```

Run:

```bash
cmake --build build
```

Expected: FAIL because `core/capabilities/capabilities_model.hpp` does not exist.

- [ ] **Step 3: Implement capability model**

Create `src/core/capabilities/capabilities_model.hpp`:

```cpp
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
```

Create `src/core/capabilities/capabilities_model.cpp`:

```cpp
#include "core/capabilities/capabilities_model.hpp"

namespace kiseki::core::capabilities {

CapabilityMatrix foundation_capabilities() {
    return CapabilityMatrix{
        .input = InputCapabilities{
            .driver = false,
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
            "background-window input is not guaranteed for Raw Input, DirectInput, protected fullscreen, or anti-cheat protected games",
        },
    };
}

nlohmann::json to_json(const CapabilityMatrix& capabilities) {
    return nlohmann::json{
        {"input", {
            {"driver", capabilities.input.driver},
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
```

- [ ] **Step 4: Run capability tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS for all registered tests.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/core/capabilities/capabilities_model.hpp src/core/capabilities/capabilities_model.cpp test/capabilities/capabilities_model_test.cpp
git commit -m "feat: add capability matrix"
```

---

### Task 6: Testable CLI App For Config And Doctor Commands

**Files:**
- Create: `src/cli/app.hpp`
- Create: `src/cli/app.cpp`
- Create: `test/cli/cli_app_test.cpp`
- Modify: `src/cli/main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing CLI tests**

Create `test/cli/cli_app_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>
#include <vector>

#include "cli/app.hpp"

using kiseki::cli::Io;
using kiseki::cli::run;

namespace {

std::filesystem::path temp_config_path(std::string_view name) {
    return std::filesystem::temp_directory_path() / std::string{name};
}

}

TEST_CASE("config path prints the active config path") {
    std::ostringstream out;
    std::ostringstream err;
    const auto path = temp_config_path("kiseki-cli-path-test.json");

    const int code = run({"kiseki", "config", "path"}, path, Io{out, err});

    REQUIRE(code == 0);
    REQUIRE(out.str().find(path.string()) != std::string::npos);
    REQUIRE(err.str().empty());
}

TEST_CASE("config show prints default json when file is absent") {
    std::ostringstream out;
    std::ostringstream err;
    const auto path = temp_config_path("kiseki-cli-show-test.json");
    std::filesystem::remove(path);

    const int code = run({"kiseki", "config", "show"}, path, Io{out, err});

    REQUIRE(code == 0);
    REQUIRE(out.str().find("\"schemaVersion\": 1") != std::string::npos);
    REQUIRE(err.str().empty());
}

TEST_CASE("config validate reports valid config") {
    std::ostringstream out;
    std::ostringstream err;
    const auto path = temp_config_path("kiseki-cli-validate-test.json");
    std::filesystem::remove(path);

    const int code = run({"kiseki", "config", "validate"}, path, Io{out, err});

    REQUIRE(code == 0);
    REQUIRE(out.str() == "configuration is valid\n");
    REQUIRE(err.str().empty());
}

TEST_CASE("capabilities prints machine readable json") {
    std::ostringstream out;
    std::ostringstream err;
    const auto path = temp_config_path("kiseki-cli-capabilities-test.json");

    const int code = run({"kiseki", "capabilities"}, path, Io{out, err});

    REQUIRE(code == 0);
    REQUIRE(out.str().find("\"backgroundWindow\"") != std::string::npos);
    REQUIRE(out.str().find("\"limitations\"") != std::string::npos);
    REQUIRE(err.str().empty());
}

TEST_CASE("doctor prints human readable diagnostics") {
    std::ostringstream out;
    std::ostringstream err;
    const auto path = temp_config_path("kiseki-cli-doctor-test.json");

    const int code = run({"kiseki", "doctor"}, path, Io{out, err});

    REQUIRE(code == 0);
    REQUIRE(out.str().find("Kiseki Input doctor") != std::string::npos);
    REQUIRE(out.str().find("Config path:") != std::string::npos);
    REQUIRE(err.str().empty());
}
```

- [ ] **Step 2: Register and verify failure**

Modify `CMakeLists.txt`:

```cmake
add_library(kiseki_cli STATIC
    src/cli/app.cpp
)
target_include_directories(kiseki_cli PUBLIC src)
target_link_libraries(kiseki_cli PUBLIC kiseki_core CLI11::CLI11)

add_executable(kiseki src/cli/main.cpp)
target_link_libraries(kiseki PRIVATE kiseki_cli)
```

Replace the existing `add_executable(kiseki ...)` block with the block above.

Modify test sources:

```cmake
set(KISEKI_TEST_SOURCES
    test/smoke_test.cpp
    test/config/config_model_test.cpp
    test/config/config_validation_test.cpp
    test/config/config_store_test.cpp
    test/capabilities/capabilities_model_test.cpp
    test/cli/cli_app_test.cpp
)
```

Modify the test link line:

```cmake
target_link_libraries(kiseki_tests PRIVATE kiseki_core kiseki_cli Catch2::Catch2WithMain)
```

Run:

```bash
cmake --build build
```

Expected: FAIL because `cli/app.hpp` does not exist.

- [ ] **Step 3: Implement CLI app**

Create `src/cli/app.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace kiseki::cli {

struct Io {
    std::ostream& out;
    std::ostream& err;
};

int run(const std::vector<std::string>& args, std::filesystem::path config_path, Io io);
std::filesystem::path resolve_config_path(std::filesystem::path override_path);

}
```

Create `src/cli/app.cpp`:

```cpp
#include "cli/app.hpp"

#include <CLI/CLI.hpp>

#include <iostream>
#include <sstream>
#include <utility>

#include "core/capabilities/capabilities_model.hpp"
#include "core/config/config_model.hpp"
#include "core/config/config_store.hpp"
#include "core/version.hpp"

namespace kiseki::cli {

namespace {

int print_config_path(const core::config::ConfigStore& store, Io io) {
    io.out << store.path().string() << '\n';
    return 0;
}

int print_config(const core::config::ConfigStore& store, Io io) {
    const auto result = store.load_or_default();
    if (!result.ok) {
        io.err << "config error: " << result.error << '\n';
        return 2;
    }

    io.out << core::config::to_json(result.config).dump(2) << '\n';
    return 0;
}

int validate_config(const core::config::ConfigStore& store, Io io) {
    const auto result = store.load_or_default();
    if (!result.ok) {
        io.err << "configuration is invalid: " << result.error << '\n';
        return 2;
    }

    io.out << "configuration is valid\n";
    return 0;
}

int print_capabilities(Io io) {
    io.out << core::capabilities::to_json(core::capabilities::foundation_capabilities()).dump(2) << '\n';
    return 0;
}

int print_doctor(const core::config::ConfigStore& store, Io io) {
    const auto capabilities = core::capabilities::foundation_capabilities();
    io.out << "Kiseki Input doctor\n";
    io.out << "Version: " << core::version() << '\n';
    io.out << "Config path: " << store.path().string() << '\n';
    io.out << "Input driver backend: " << (capabilities.input.driver ? "available" : "not available in foundation build") << '\n';
    io.out << "Background-window input: " << (capabilities.input.background_window ? "available" : "not available in foundation build") << '\n';
    io.out << "Screenshot burst: " << (capabilities.capture.burst ? "available" : "not available in foundation build") << '\n';
    return 0;
}

}

std::filesystem::path resolve_config_path(std::filesystem::path override_path) {
    if (!override_path.empty()) {
        return override_path;
    }

    return core::config::default_config_path(
        core::config::current_environment(),
        core::config::current_platform());
}

int run(const std::vector<std::string>& args, std::filesystem::path config_path, Io io) {
    CLI::App app{"Kiseki Input"};
    app.set_version_flag("--version", std::string{core::version()});
    app.require_subcommand(1);

    int exit_code = 0;
    core::config::ConfigStore store{resolve_config_path(std::move(config_path))};

    auto* config = app.add_subcommand("config", "Inspect and validate configuration");
    config->require_subcommand(1);

    auto* config_path_command = config->add_subcommand("path", "Print configuration path");
    config_path_command->callback([&] {
        exit_code = print_config_path(store, io);
    });

    auto* config_show_command = config->add_subcommand("show", "Print active configuration");
    config_show_command->callback([&] {
        exit_code = print_config(store, io);
    });

    auto* config_validate_command = config->add_subcommand("validate", "Validate active configuration");
    config_validate_command->callback([&] {
        exit_code = validate_config(store, io);
    });

    auto* capabilities = app.add_subcommand("capabilities", "Print machine-readable capability matrix");
    capabilities->callback([&] {
        exit_code = print_capabilities(io);
    });

    auto* doctor = app.add_subcommand("doctor", "Print human-readable diagnostics");
    doctor->callback([&] {
        exit_code = print_doctor(store, io);
    });

    try {
        std::vector<std::string> parse_args = args;
        app.parse(parse_args);
    } catch (const CLI::ParseError& error) {
        return app.exit(error, io.out, io.err);
    }

    return exit_code;
}

}
```

Replace `src/cli/main.cpp`:

```cpp
#include <iostream>
#include <string>
#include <vector>

#include "cli/app.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }

    return kiseki::cli::run(args, {}, kiseki::cli::Io{std::cout, std::cerr});
}
```

- [ ] **Step 4: Run CLI tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS for all registered tests.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/cli/app.hpp src/cli/app.cpp src/cli/main.cpp test/cli/cli_app_test.cpp
git commit -m "feat: add config cli commands"
```

---

### Task 7: Config-Only WebUI API

**Files:**
- Create: `src/webui/config_api.hpp`
- Create: `src/webui/config_api.cpp`
- Create: `test/webui/config_api_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing WebUI API tests**

Create `test/webui/config_api_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include <nlohmann/json.hpp>

#include "webui/config_api.hpp"

using kiseki::webui::ConfigApi;

namespace {

std::filesystem::path temp_config_path(std::string_view name) {
    return std::filesystem::temp_directory_path() / std::string{name};
}

}

TEST_CASE("config api returns default config") {
    const auto path = temp_config_path("kiseki-webui-api-get-test.json");
    std::filesystem::remove(path);
    ConfigApi api{path};

    const auto response = api.get_config();
    const auto json = nlohmann::json::parse(response.body);

    REQUIRE(response.status == 200);
    REQUIRE(response.content_type == "application/json");
    REQUIRE(json["schemaVersion"].get<int>() == 1);
}

TEST_CASE("config api rejects invalid saved config") {
    const auto path = temp_config_path("kiseki-webui-api-put-test.json");
    std::filesystem::remove(path);
    ConfigApi api{path};

    const auto response = api.put_config(R"({"webui":{"port":0}})");

    REQUIRE(response.status == 400);
    REQUIRE(response.body.find("webui.port") != std::string::npos);
}

TEST_CASE("config api exposes only configuration routes") {
    const auto routes = ConfigApi::routes();

    REQUIRE(routes == std::vector<std::string>{
        "GET /api/config",
        "PUT /api/config",
        "GET /api/capabilities",
    });
}
```

- [ ] **Step 2: Register and verify failure**

Modify `CMakeLists.txt`:

```cmake
add_library(kiseki_webui STATIC
    src/webui/config_api.cpp
)
target_include_directories(kiseki_webui PUBLIC src)
target_link_libraries(kiseki_webui PUBLIC kiseki_core)
```

Add the webui library before `add_library(kiseki_cli ...)`.

Modify test sources:

```cmake
set(KISEKI_TEST_SOURCES
    test/smoke_test.cpp
    test/config/config_model_test.cpp
    test/config/config_validation_test.cpp
    test/config/config_store_test.cpp
    test/capabilities/capabilities_model_test.cpp
    test/cli/cli_app_test.cpp
    test/webui/config_api_test.cpp
)
```

Modify the test link line:

```cmake
target_link_libraries(kiseki_tests PRIVATE kiseki_core kiseki_cli kiseki_webui Catch2::Catch2WithMain)
```

Run:

```bash
cmake --build build
```

Expected: FAIL because `webui/config_api.hpp` does not exist.

- [ ] **Step 3: Implement config API**

Create `src/webui/config_api.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace kiseki::webui {

struct ApiResponse {
    int status;
    std::string body;
    std::string content_type;
};

class ConfigApi {
public:
    explicit ConfigApi(std::filesystem::path config_path);

    ApiResponse get_config() const;
    ApiResponse put_config(std::string_view body) const;
    ApiResponse get_capabilities() const;

    static std::vector<std::string> routes();

private:
    std::filesystem::path config_path_;
};

}
```

Create `src/webui/config_api.cpp`:

```cpp
#include "webui/config_api.hpp"

#include <nlohmann/json.hpp>

#include <utility>

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

}

ConfigApi::ConfigApi(std::filesystem::path config_path)
    : config_path_{std::move(config_path)} {}

ApiResponse ConfigApi::get_config() const {
    core::config::ConfigStore store{config_path_};
    const auto result = store.load_or_default();
    if (!result.ok) {
        return json_response(500, {{"error", result.error}});
    }

    return json_response(200, core::config::to_json(result.config));
}

ApiResponse ConfigApi::put_config(std::string_view body) const {
    try {
        const auto json = nlohmann::json::parse(body);
        const auto config = core::config::config_from_json(json);
        const auto validation = core::config::validate_config(config);
        if (!validation.valid()) {
            nlohmann::json issues = nlohmann::json::array();
            for (const auto& issue : validation.issues) {
                issues.push_back({{"path", issue.path}, {"message", issue.message}});
            }
            return json_response(400, {{"error", "invalid configuration"}, {"issues", issues}});
        }

        core::config::ConfigStore store{config_path_};
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
    return json_response(
        200,
        core::capabilities::to_json(core::capabilities::foundation_capabilities()));
}

std::vector<std::string> ConfigApi::routes() {
    return {
        "GET /api/config",
        "PUT /api/config",
        "GET /api/capabilities",
    };
}

}
```

- [ ] **Step 4: Run WebUI API tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS for all registered tests.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/webui/config_api.hpp src/webui/config_api.cpp test/webui/config_api_test.cpp
git commit -m "feat: add config-only web api"
```

---

### Task 8: Embedded Static Assets And Web Server

**Files:**
- Create: `src/webui/static_assets.hpp`
- Create: `src/webui/static_assets.cpp`
- Create: `src/webui/web_server.hpp`
- Create: `src/webui/web_server.cpp`
- Create: `ui/index.html`
- Create: `ui/styles.css`
- Create: `ui/app.js`
- Create: `test/webui/static_assets_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing static asset boundary test**

Create `test/webui/static_assets_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "webui/static_assets.hpp"

using kiseki::webui::app_js;
using kiseki::webui::index_html;

TEST_CASE("webui assets do not reference operational api routes") {
    const std::string html = index_html();
    const std::string js = app_js();
    const std::string combined = html + "\n" + js;

    REQUIRE(combined.find("/api/input") == std::string::npos);
    REQUIRE(combined.find("/api/screenshot") == std::string::npos);
    REQUIRE(combined.find("/api/execute") == std::string::npos);
    REQUIRE(combined.find("/api/notify") == std::string::npos);
    REQUIRE(combined.find("/api/daemon") == std::string::npos);
}
```

- [ ] **Step 2: Register and verify failure**

Modify the webui library in `CMakeLists.txt`:

```cmake
add_library(kiseki_webui STATIC
    src/webui/config_api.cpp
    src/webui/static_assets.cpp
    src/webui/web_server.cpp
)
target_include_directories(kiseki_webui PUBLIC src)
target_link_libraries(kiseki_webui PUBLIC kiseki_core httplib::httplib)
```

Modify test sources:

```cmake
set(KISEKI_TEST_SOURCES
    test/smoke_test.cpp
    test/config/config_model_test.cpp
    test/config/config_validation_test.cpp
    test/config/config_store_test.cpp
    test/capabilities/capabilities_model_test.cpp
    test/cli/cli_app_test.cpp
    test/webui/config_api_test.cpp
    test/webui/static_assets_test.cpp
)
```

Run:

```bash
cmake --build build
```

Expected: FAIL because `webui/static_assets.hpp` does not exist.

- [ ] **Step 3: Add editable UI source files**

Create `ui/index.html`:

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Kiseki Input Configuration</title>
    <link rel="stylesheet" href="/styles.css">
  </head>
  <body>
    <main class="shell">
      <header class="topbar">
        <div>
          <h1>Kiseki Input</h1>
          <p>Configuration</p>
        </div>
        <button id="save" type="button">Save</button>
      </header>

      <form id="config-form" class="grid">
        <section>
          <h2>WebUI</h2>
          <label>Host <input name="webui.host" autocomplete="off"></label>
          <label>Port <input name="webui.port" type="number" min="1" max="65535"></label>
        </section>

        <section>
          <h2>Heartbeat</h2>
          <label><input name="heartbeat.enabled" type="checkbox"> Enabled</label>
          <label>Interval seconds <input name="heartbeat.intervalSeconds" type="number" min="1"></label>
          <label><input name="heartbeat.notificationEnabled" type="checkbox"> Notifications</label>
          <label>Message <input name="heartbeat.message" autocomplete="off"></label>
        </section>

        <section>
          <h2>Input Defaults</h2>
          <label>Default backend
            <select name="input.defaultBackend">
              <option value="background-window">background-window</option>
              <option value="driver">driver</option>
            </select>
          </label>
          <label>Windows driver
            <select name="input.windowsDriver">
              <option>AnyDriver</option>
              <option>SendInput</option>
              <option>Logitech</option>
              <option>LogitechGHubNew</option>
              <option>Razer</option>
              <option>DD</option>
              <option>MouClassInputInjection</option>
            </select>
          </label>
          <label>Linux driver
            <select name="input.linuxDriver">
              <option>uinput</option>
            </select>
          </label>
          <label><input name="input.backgroundInputEnabled" type="checkbox"> Background input enabled</label>
        </section>

        <section>
          <h2>Screenshot Defaults</h2>
          <label>Output directory <input name="screenshot.defaultOutputDirectory" autocomplete="off"></label>
          <label>Burst FPS <input name="screenshot.burstFps" type="number" min="1" max="240"></label>
          <label>Burst frames <input name="screenshot.burstFrames" type="number" min="1" max="240"></label>
          <label>Format
            <select name="screenshot.format">
              <option>png</option>
            </select>
          </label>
        </section>
      </form>

      <pre id="status" role="status"></pre>
    </main>
    <script src="/app.js"></script>
  </body>
</html>
```

Create `ui/styles.css`:

```css
:root {
  color-scheme: light dark;
  font-family: "Segoe UI", system-ui, sans-serif;
  background: #f5f7fb;
  color: #172033;
}

body {
  margin: 0;
}

.shell {
  max-width: 1120px;
  margin: 0 auto;
  padding: 24px;
}

.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 20px;
}

h1,
h2,
p {
  margin: 0;
}

h1 {
  font-size: 28px;
}

h2 {
  font-size: 16px;
  margin-bottom: 14px;
}

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: 16px;
}

section {
  background: #ffffff;
  border: 1px solid #d8deea;
  border-radius: 8px;
  padding: 16px;
}

label {
  display: grid;
  gap: 6px;
  margin-bottom: 12px;
  font-size: 14px;
}

input,
select,
button {
  font: inherit;
}

input,
select {
  min-height: 36px;
  border: 1px solid #bcc5d6;
  border-radius: 6px;
  padding: 6px 8px;
}

button {
  min-height: 38px;
  border: 0;
  border-radius: 6px;
  padding: 0 16px;
  background: #1b5fc9;
  color: white;
}

#status {
  min-height: 20px;
  margin-top: 16px;
  white-space: pre-wrap;
}
```

Create `ui/app.js`:

```javascript
const form = document.querySelector("#config-form");
const save = document.querySelector("#save");
const status = document.querySelector("#status");

let config = null;

function field(name) {
  return form.elements[name];
}

function setField(name, value) {
  const control = field(name);
  if (control.type === "checkbox") {
    control.checked = Boolean(value);
  } else {
    control.value = value;
  }
}

function readField(name) {
  const control = field(name);
  if (control.type === "checkbox") return control.checked;
  if (control.type === "number") return Number(control.value);
  return control.value;
}

function populate(nextConfig) {
  config = nextConfig;
  setField("webui.host", config.webui.host);
  setField("webui.port", config.webui.port);
  setField("heartbeat.enabled", config.heartbeat.enabled);
  setField("heartbeat.intervalSeconds", config.heartbeat.intervalSeconds);
  setField("heartbeat.notificationEnabled", config.heartbeat.notificationEnabled);
  setField("heartbeat.message", config.heartbeat.message);
  setField("input.defaultBackend", config.input.defaultBackend);
  setField("input.windowsDriver", config.input.windowsDriver);
  setField("input.linuxDriver", config.input.linuxDriver);
  setField("input.backgroundInputEnabled", config.input.backgroundInputEnabled);
  setField("screenshot.defaultOutputDirectory", config.screenshot.defaultOutputDirectory);
  setField("screenshot.burstFps", config.screenshot.burstFps);
  setField("screenshot.burstFrames", config.screenshot.burstFrames);
  setField("screenshot.format", config.screenshot.format);
}

function collect() {
  return {
    ...config,
    webui: {
      host: readField("webui.host"),
      port: readField("webui.port")
    },
    heartbeat: {
      enabled: readField("heartbeat.enabled"),
      intervalSeconds: readField("heartbeat.intervalSeconds"),
      notificationEnabled: readField("heartbeat.notificationEnabled"),
      message: readField("heartbeat.message")
    },
    input: {
      defaultBackend: readField("input.defaultBackend"),
      windowsDriver: readField("input.windowsDriver"),
      linuxDriver: readField("input.linuxDriver"),
      backgroundInputEnabled: readField("input.backgroundInputEnabled")
    },
    screenshot: {
      defaultOutputDirectory: readField("screenshot.defaultOutputDirectory"),
      burstFps: readField("screenshot.burstFps"),
      burstFrames: readField("screenshot.burstFrames"),
      format: readField("screenshot.format")
    }
  };
}

async function loadConfig() {
  const response = await fetch("/api/config");
  const body = await response.json();
  if (!response.ok) throw new Error(body.error || "Failed to load configuration");
  populate(body);
  status.textContent = "";
}

async function saveConfig() {
  const response = await fetch("/api/config", {
    method: "PUT",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(collect())
  });
  const body = await response.json();
  if (!response.ok) throw new Error(body.error || "Failed to save configuration");
  populate(body);
  status.textContent = "Saved";
}

save.addEventListener("click", () => {
  saveConfig().catch((error) => {
    status.textContent = error.message;
  });
});

loadConfig().catch((error) => {
  status.textContent = error.message;
});
```

- [ ] **Step 4: Embed assets and add web server**

Create `src/webui/static_assets.hpp`:

```cpp
#pragma once

#include <string_view>

namespace kiseki::webui {

std::string_view index_html();
std::string_view styles_css();
std::string_view app_js();

}
```

Create `src/webui/static_assets.cpp` by copying the exact contents from `ui/index.html`, `ui/styles.css`, and `ui/app.js` into raw string literals:

```cpp
#include "webui/static_assets.hpp"

namespace kiseki::webui {

std::string_view index_html() {
    return R"HTML(<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Kiseki Input Configuration</title>
    <link rel="stylesheet" href="/styles.css">
  </head>
  <body>
    <main class="shell">
      <header class="topbar">
        <div>
          <h1>Kiseki Input</h1>
          <p>Configuration</p>
        </div>
        <button id="save" type="button">Save</button>
      </header>
      <form id="config-form" class="grid">
        <section><h2>WebUI</h2><label>Host <input name="webui.host" autocomplete="off"></label><label>Port <input name="webui.port" type="number" min="1" max="65535"></label></section>
        <section><h2>Heartbeat</h2><label><input name="heartbeat.enabled" type="checkbox"> Enabled</label><label>Interval seconds <input name="heartbeat.intervalSeconds" type="number" min="1"></label><label><input name="heartbeat.notificationEnabled" type="checkbox"> Notifications</label><label>Message <input name="heartbeat.message" autocomplete="off"></label></section>
        <section><h2>Input Defaults</h2><label>Default backend <select name="input.defaultBackend"><option value="background-window">background-window</option><option value="driver">driver</option></select></label><label>Windows driver <select name="input.windowsDriver"><option>AnyDriver</option><option>SendInput</option><option>Logitech</option><option>LogitechGHubNew</option><option>Razer</option><option>DD</option><option>MouClassInputInjection</option></select></label><label>Linux driver <select name="input.linuxDriver"><option>uinput</option></select></label><label><input name="input.backgroundInputEnabled" type="checkbox"> Background input enabled</label></section>
        <section><h2>Screenshot Defaults</h2><label>Output directory <input name="screenshot.defaultOutputDirectory" autocomplete="off"></label><label>Burst FPS <input name="screenshot.burstFps" type="number" min="1" max="240"></label><label>Burst frames <input name="screenshot.burstFrames" type="number" min="1" max="240"></label><label>Format <select name="screenshot.format"><option>png</option></select></label></section>
      </form>
      <pre id="status" role="status"></pre>
    </main>
    <script src="/app.js"></script>
  </body>
</html>)HTML";
}

std::string_view styles_css() {
    return R"CSS(:root{color-scheme:light dark;font-family:"Segoe UI",system-ui,sans-serif;background:#f5f7fb;color:#172033}body{margin:0}.shell{max-width:1120px;margin:0 auto;padding:24px}.topbar{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:20px}h1,h2,p{margin:0}h1{font-size:28px}h2{font-size:16px;margin-bottom:14px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:16px}section{background:#fff;border:1px solid #d8deea;border-radius:8px;padding:16px}label{display:grid;gap:6px;margin-bottom:12px;font-size:14px}input,select,button{font:inherit}input,select{min-height:36px;border:1px solid #bcc5d6;border-radius:6px;padding:6px 8px}button{min-height:38px;border:0;border-radius:6px;padding:0 16px;background:#1b5fc9;color:white}#status{min-height:20px;margin-top:16px;white-space:pre-wrap})CSS";
}

std::string_view app_js() {
    return R"JS(const form=document.querySelector("#config-form");const save=document.querySelector("#save");const status=document.querySelector("#status");let config=null;function field(name){return form.elements[name]}function setField(name,value){const control=field(name);if(control.type==="checkbox"){control.checked=Boolean(value)}else{control.value=value}}function readField(name){const control=field(name);if(control.type==="checkbox")return control.checked;if(control.type==="number")return Number(control.value);return control.value}function populate(nextConfig){config=nextConfig;setField("webui.host",config.webui.host);setField("webui.port",config.webui.port);setField("heartbeat.enabled",config.heartbeat.enabled);setField("heartbeat.intervalSeconds",config.heartbeat.intervalSeconds);setField("heartbeat.notificationEnabled",config.heartbeat.notificationEnabled);setField("heartbeat.message",config.heartbeat.message);setField("input.defaultBackend",config.input.defaultBackend);setField("input.windowsDriver",config.input.windowsDriver);setField("input.linuxDriver",config.input.linuxDriver);setField("input.backgroundInputEnabled",config.input.backgroundInputEnabled);setField("screenshot.defaultOutputDirectory",config.screenshot.defaultOutputDirectory);setField("screenshot.burstFps",config.screenshot.burstFps);setField("screenshot.burstFrames",config.screenshot.burstFrames);setField("screenshot.format",config.screenshot.format)}function collect(){return{...config,webui:{host:readField("webui.host"),port:readField("webui.port")},heartbeat:{enabled:readField("heartbeat.enabled"),intervalSeconds:readField("heartbeat.intervalSeconds"),notificationEnabled:readField("heartbeat.notificationEnabled"),message:readField("heartbeat.message")},input:{defaultBackend:readField("input.defaultBackend"),windowsDriver:readField("input.windowsDriver"),linuxDriver:readField("input.linuxDriver"),backgroundInputEnabled:readField("input.backgroundInputEnabled")},screenshot:{defaultOutputDirectory:readField("screenshot.defaultOutputDirectory"),burstFps:readField("screenshot.burstFps"),burstFrames:readField("screenshot.burstFrames"),format:readField("screenshot.format")}}}async function loadConfig(){const response=await fetch("/api/config");const body=await response.json();if(!response.ok)throw new Error(body.error||"Failed to load configuration");populate(body);status.textContent=""}async function saveConfig(){const response=await fetch("/api/config",{method:"PUT",headers:{"Content-Type":"application/json"},body:JSON.stringify(collect())});const body=await response.json();if(!response.ok)throw new Error(body.error||"Failed to save configuration");populate(body);status.textContent="Saved"}save.addEventListener("click",()=>{saveConfig().catch((error)=>{status.textContent=error.message})});loadConfig().catch((error)=>{status.textContent=error.message});)JS";
}

}
```

Create `src/webui/web_server.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace kiseki::webui {

class WebServer {
public:
    explicit WebServer(std::filesystem::path config_path);

    int listen(const std::string& host, std::uint16_t port);

private:
    std::filesystem::path config_path_;
};

std::string build_listen_url(const std::string& host, std::uint16_t port);

}
```

Create `src/webui/web_server.cpp`:

```cpp
#include "webui/web_server.hpp"

#include <httplib.h>

#include "webui/config_api.hpp"
#include "webui/static_assets.hpp"

#include <utility>

namespace kiseki::webui {

WebServer::WebServer(std::filesystem::path config_path)
    : config_path_{std::move(config_path)} {}

int WebServer::listen(const std::string& host, std::uint16_t port) {
    ConfigApi api{config_path_};
    httplib::Server server;

    server.Get("/", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(std::string{index_html()}, "text/html; charset=utf-8");
    });
    server.Get("/styles.css", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(std::string{styles_css()}, "text/css; charset=utf-8");
    });
    server.Get("/app.js", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(std::string{app_js()}, "application/javascript; charset=utf-8");
    });
    server.Get("/api/config", [&api](const httplib::Request&, httplib::Response& response) {
        const auto result = api.get_config();
        response.status = result.status;
        response.set_content(result.body, result.content_type);
    });
    server.Put("/api/config", [&api](const httplib::Request& request, httplib::Response& response) {
        const auto result = api.put_config(request.body);
        response.status = result.status;
        response.set_content(result.body, result.content_type);
    });
    server.Get("/api/capabilities", [&api](const httplib::Request&, httplib::Response& response) {
        const auto result = api.get_capabilities();
        response.status = result.status;
        response.set_content(result.body, result.content_type);
    });

    return server.listen(host, port) ? 0 : 2;
}

std::string build_listen_url(const std::string& host, std::uint16_t port) {
    return "http://" + host + ":" + std::to_string(port);
}

}
```

- [ ] **Step 5: Run static asset tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS for all registered tests.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/webui/static_assets.hpp src/webui/static_assets.cpp src/webui/web_server.hpp src/webui/web_server.cpp ui/index.html ui/styles.css ui/app.js test/webui/static_assets_test.cpp
git commit -m "feat: embed config webui assets"
```

---

### Task 9: Config-UI CLI Command With Injectable Launcher

**Files:**
- Modify: `src/cli/app.hpp`
- Modify: `src/cli/app.cpp`
- Modify: `test/cli/cli_app_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing config-ui CLI test**

Append to `test/cli/cli_app_test.cpp`:

```cpp
TEST_CASE("config-ui launches configured local web server") {
    std::ostringstream out;
    std::ostringstream err;
    const auto path = temp_config_path("kiseki-cli-config-ui-test.json");

    kiseki::cli::Dependencies dependencies;
    dependencies.launch_config_ui = [&](const kiseki::cli::WebUiLaunchOptions& options,
                                        const std::filesystem::path& config_path,
                                        Io io) {
        REQUIRE(options.host == "127.0.0.1");
        REQUIRE(options.port == 8787);
        REQUIRE(config_path == path);
        io.out << "fake launch\n";
        return 0;
    };

    const int code = run({"kiseki", "config-ui"}, path, Io{out, err}, dependencies);

    REQUIRE(code == 0);
    REQUIRE(out.str() == "fake launch\n");
    REQUIRE(err.str().empty());
}
```

Run:

```bash
cmake --build build
```

Expected: FAIL because `Dependencies` and `WebUiLaunchOptions` do not exist.

- [ ] **Step 2: Add injectable CLI dependencies**

Replace `src/cli/app.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace kiseki::cli {

struct Io {
    std::ostream& out;
    std::ostream& err;
};

struct WebUiLaunchOptions {
    std::string host;
    std::uint16_t port;
};

struct Dependencies {
    std::function<int(const WebUiLaunchOptions&, const std::filesystem::path&, Io)> launch_config_ui;
};

Dependencies default_dependencies();

int run(
    const std::vector<std::string>& args,
    std::filesystem::path config_path,
    Io io,
    Dependencies dependencies = default_dependencies());

std::filesystem::path resolve_config_path(std::filesystem::path override_path);

}
```

Modify `CMakeLists.txt` so `kiseki_cli` links `kiseki_webui`:

```cmake
target_link_libraries(kiseki_cli PUBLIC kiseki_core kiseki_webui CLI11::CLI11)
```

- [ ] **Step 3: Implement config-ui command**

Add includes to `src/cli/app.cpp`:

```cpp
#include "webui/web_server.hpp"
```

Add this function before `resolve_config_path`:

```cpp
Dependencies default_dependencies() {
    return Dependencies{
        .launch_config_ui = [](const WebUiLaunchOptions& options, const std::filesystem::path& config_path, Io io) {
            io.out << "Serving configuration UI at "
                   << kiseki::webui::build_listen_url(options.host, options.port)
                   << '\n';
            kiseki::webui::WebServer server{config_path};
            return server.listen(options.host, options.port);
        },
    };
}
```

Change the `run` signature in `src/cli/app.cpp`:

```cpp
int run(
    const std::vector<std::string>& args,
    std::filesystem::path config_path,
    Io io,
    Dependencies dependencies) {
```

Inside `run`, after creating `ConfigStore store`, add:

```cpp
WebUiLaunchOptions webui_options{
    .host = "",
    .port = 0,
};
```

Add this subcommand before parsing:

```cpp
auto* config_ui = app.add_subcommand("config-ui", "Launch local configuration WebUI");
config_ui->add_option("--host", webui_options.host, "Listen host");
config_ui->add_option("--port", webui_options.port, "Listen port");
config_ui->callback([&] {
    const auto loaded = store.load_or_default();
    if (!loaded.ok) {
        io.err << "config error: " << loaded.error << '\n';
        exit_code = 2;
        return;
    }

    if (webui_options.host.empty()) {
        webui_options.host = loaded.config.webui.host;
    }
    if (webui_options.port == 0) {
        webui_options.port = loaded.config.webui.port;
    }

    exit_code = dependencies.launch_config_ui(webui_options, store.path(), io);
});
```

- [ ] **Step 4: Run CLI and WebUI tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS for all registered tests.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/cli/app.hpp src/cli/app.cpp test/cli/cli_app_test.cpp
git commit -m "feat: add config ui command"
```

---

### Task 10: Foundation Verification And Documentation

**Files:**
- Create: `README.md`
- Modify: no source files unless verification reveals a defect

- [ ] **Step 1: Write README with supported foundation commands**

Create `README.md`:

```markdown
# Kiseki Input

Kiseki Input is a cross-platform C++ CLI tool for configuration-first input and screenshot automation workflows.

## Foundation Build

This build includes:

- C++ CLI executable: `kiseki`
- JSON configuration defaults, validation, and persistence
- Config-only local WebUI: `kiseki config-ui`
- Machine-readable capabilities: `kiseki capabilities`
- Human-readable diagnostics: `kiseki doctor`

Operational input simulation, screenshot capture, target resolution, notifications, and daemon behavior are implemented in separate feature slices after this foundation.

## Commands

```text
kiseki config path
kiseki config show
kiseki config validate
kiseki config-ui
kiseki capabilities
kiseki doctor
```

The WebUI exposes only:

```text
GET /api/config
PUT /api/config
GET /api/capabilities
```

It does not expose input, screenshot, notification, daemon, shell, or execution routes.

## Build

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
```

- [ ] **Step 2: Run full verification**

Run:

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/kiseki config validate
./build/kiseki capabilities
./build/kiseki doctor
```

Expected:

```text
configuration is valid
```

Expected for `capabilities`: JSON containing `input`, `capture`, and `limitations`.

Expected for `doctor`: text containing `Kiseki Input doctor` and `Config path:`.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: document foundation commands"
```

---

## Self-Review Checklist

- Spec coverage: This plan covers CMake/test harness, configuration model/store/validation, CLI skeleton, WebUI config-only API, static config UI, capabilities output, and doctor output.
- Intentional exclusions: platform input backends, screenshot backends, target matching, notifications, and daemon behavior are not part of this foundation plan because the approved spec needs independent optimization boundaries.
- WebUI safety: Tests assert that embedded assets do not reference operational API routes, and the API route list contains only configuration and capabilities endpoints.
- Type consistency: `AppConfig`, `ConfigStore`, `CapabilityMatrix`, `ConfigApi`, `Io`, `Dependencies`, and `WebUiLaunchOptions` names match across tasks.
- Verification: Every implementation task includes a failing test step, a passing test command, and a commit command.
