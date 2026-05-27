# Kiseki Architecture Notes

## Source Map

- `src/cli/`: CLI parsing, dependency injection seams, user-facing command behavior.
- `src/core/config/`: config model, JSON conversion, validation, persistence, default paths.
- `src/core/capabilities/`: foundation capability data and JSON conversion.
- `src/platform/capture/`: desktop and burst screenshot backends plus BMP writing.
- `src/platform/input/`: keyboard, text, mouse, and drag backends.
- `src/platform/notification/`: heartbeat notification and daemon loop.
- `src/platform/runtime_capabilities.*`: runtime platform probing and limitation messages.
- `src/platform/session/`: Linux Xvfb background desktop lifecycle and optional macOS CUA provider wrapper.
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
- Linux background desktop support is Xvfb-based and should stay separate from the current physical `DISPLAY`. Use scoped environment changes when routing screenshot or input commands to an isolated `DISPLAY`.

macOS:

- Keep native ScreenCaptureKit/Quartz support separate from the optional CUA provider.
- `src/platform/session/macos_cua.*` shells out to `cua-driver`; do not make CUA a hard build dependency.
- `mac-background` commands are CLI-only and must not add WebUI operation routes.
- Treat CUA support as live only after verifying `status`, launch/window listing, screenshot/state, and at least one action command on a real logged-in macOS GUI session with permissions granted.

Windows selected-window:

- Resolve a target HWND through `platform/target`.
- Use `target inspect` when a command needs receiver detail; it reports child HWNDs, class names, and bounds.
- Keep direct selected-window behavior message/API based. Do not present it as universal canvas/raw-input operation.

Windows background screenshot:

- Keep Windows background work scoped to observation through `screenshot background-window`.
- Do not introduce a separate Windows session backend for the current project direction.
- Selected-window input remains a compatibility helper for targets that accept normal Windows messages; do not describe it as universal background operation.
- Background screenshot should use target selectors and the selected-window capture backend without activating the target.

## WebUI Asset Rule

The current build embeds WebUI assets in `src/webui/static_assets.cpp`. If editing `ui/`, update embedded assets too, or tests that inspect WebUI behavior may not reflect the visible source files.

Tests should continue to assert that WebUI assets do not reference operational API routes.

## Safety Language

When touching background input or game-targeted wording, preserve this constraint:

> Game-class programs support targeted high-speed screenshot where available; background keyboard/mouse only works for targets that accept system window messages or public automation interfaces.
