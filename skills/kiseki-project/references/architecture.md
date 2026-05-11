# Kiseki Architecture Notes

## Source Map

- `src/cli/`: CLI parsing, dependency injection seams, user-facing command behavior.
- `src/core/config/`: config model, JSON conversion, validation, persistence, default paths.
- `src/core/capabilities/`: foundation capability data and JSON conversion.
- `src/platform/capture/`: desktop and burst screenshot backends plus BMP writing.
- `src/platform/input/`: keyboard, text, mouse, and drag backends.
- `src/platform/notification/`: heartbeat notification and daemon loop.
- `src/platform/runtime_capabilities.*`: runtime platform probing and limitation messages.
- `src/platform/target/`: platform target-window listing and resolver for title, PID, and window id selectors.
- `src/webui/`: embedded static assets, config-only API, HTTP server.
- `test/`: Catch2 unit tests.
- `ui/`: source WebUI assets mirrored into embedded static assets.

## Design Boundaries

CLI owns operation execution. WebUI owns configuration only.

Keep these boundaries intact:

- WebUI can read/write config and show capabilities.
- WebUI must not trigger input, screenshots, daemon operations, notifications, shell commands, or process launch.
- Platform code should expose small C++ functions returning `OperationResult` or `CaptureResult`.
- CLI commands should be testable through injected dependencies in `kiseki::cli::Dependencies`.

## Adding CLI Commands

1. Add option structs and dependency callback fields in `src/cli/app.hpp`.
2. Wire parsing and default dependency behavior in `src/cli/app.cpp`.
3. Add focused CLI tests in `test/cli/cli_app_test.cpp`.
4. Keep operation code in the relevant platform/core module, not directly inside parser callbacks.
5. Use exit code `2` for user/config/backend errors.

## Adding Config Fields

1. Update `AppConfig` in `config_model.hpp`.
2. Add defaults in `default_config()`.
3. Update `to_json()` and `config_from_json()`.
4. Add validation when the field affects behavior or safety.
5. Update WebUI assets only if the field should be user-configurable.
6. Add or update config model, validation, config API, and WebUI tests.

## Adding Platform Backends

Keep platform-specific code inside `#ifdef _WIN32` or `#ifdef KISEKI_HAS_X11` blocks in the relevant platform module.

Windows:

- Link new Win32 dependencies in `CMakeLists.txt`.
- Keep IbInputSimulator optional and runtime-loaded.
- Preserve `SendInput` fallback when possible.
- Do not assume elevated privileges or foreground focus unless the command explicitly requires it.

Linux:

- Prefer system-native APIs available to C++.
- Detect session support before claiming capabilities.
- For WSL, report unsupported compositor/session behavior clearly.
- True Linux behavior needs a real graphical Linux test environment.

## WebUI Asset Rule

The current build embeds WebUI assets in `src/webui/static_assets.cpp`. If editing `ui/`, update embedded assets too, or tests that inspect WebUI behavior may not reflect the visible source files.

Tests should continue to assert that WebUI assets do not reference operational API routes.

## Safety Language

When touching background input or game-targeted wording, preserve this constraint:

> Game-class programs support targeted high-speed screenshot where available; background keyboard/mouse only works for targets that accept system window messages or public automation interfaces.
