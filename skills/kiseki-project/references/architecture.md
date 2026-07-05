# Kiseki Architecture Notes

## Source Map

- `src/cli/`: CLI parsing, dependency injection seams, user-facing command behavior.
- `src/core/config/`: config model, JSON conversion, validation, persistence, default paths.
- `src/core/capabilities/`: foundation capability data and JSON conversion.
- `src/platform/capture/`: desktop and burst screenshot backends plus BMP writing.
- `src/platform/input/`: keyboard, text, mouse, and drag backends.
- `src/platform/notification/`: heartbeat notification and daemon loop.
- `src/platform/runtime_capabilities.*`: runtime platform probing and limitation messages.
- `src/platform/session/`: Linux Xvfb background desktop lifecycle and optional CUA Driver provider wrapper.
- `src/platform/target/`: platform target-window listing and resolver for title, PID, and window id selectors.
- `src/webui/`: embedded static assets, config-only API, HTTP server.
- `test/`: Catch2 unit tests.
- `ui/`: source WebUI assets mirrored into embedded static assets.

## Design Boundaries

CLI owns operation execution. WebUI owns configuration only.

Keep these boundaries intact:

- WebUI can read/write config and show capabilities.
- WebUI must not trigger input, screenshots, daemon operations, notifications, shell commands, or process launch.
- Observation commands such as `observe ui` are CLI-only. They should read structured system/app state and must not call screenshot/OCR/vision backends. Windows `uia` uses UI Automation; macOS `ax` uses Accessibility API data equivalent to the Accessibility Inspector element tree. Provider fallback must be explicit in the output; strict provider requests should fail instead of silently downgrading.
- Platform code should expose small C++ functions returning `OperationResult` or `CaptureResult`.
- CLI commands should be testable through injected dependencies in `kiseki::cli::Dependencies`.
- Keep mode semantics visible at the CLI boundary: `input ...` and `screenshot ...` are current-session/non-background families, while `background ...` owns selected-window, isolated-display, and CUA target-routed background families. `kiseki modes --json` is the machine-readable contract for this split.
- Background actions must be verified with the matching background screenshot family, not with `screenshot desktop`.

## Adding CLI Commands

1. Add option structs and dependency callback fields in `src/cli/app.hpp`.
2. Wire parsing and default dependency behavior in `src/cli/app.cpp`.
3. Add focused CLI tests in `test/cli/cli_app_test.cpp`.
4. Keep operation code in the relevant platform/core module, not directly inside parser callbacks.
5. Use exit code `2` for user/config/backend errors.
6. For background operations, prefer the integrated `background ...` command group and route to existing backend slices through command normalization instead of duplicating platform callbacks.

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
- `src/platform/session/macos_cua.*` shells out to `cua-driver`; despite the historical filename, the wrapper is the optional cross-platform CUA Driver bridge. Do not make CUA a hard build dependency.
- `background cua` commands are CLI-only and must not add WebUI operation routes. Older direct CUA/background command families are removed from the public CLI.
- Treat `kiseki input ...` on macOS as global/current-session input. Treat `kiseki background cua ...` as the target-routed CUA background path.
- For drawing workflows, keep foreground `input drag --file` and CUA `background cua draw` separate. The former uses the active pointer path and is appropriate for dense sampled strokes with configurable delay; the latter sends sparse window-local CUA drag segments and must be verified with CUA screenshot/state.
- `background cua feedback ...` is only visual agent-cursor feedback for CUA actions. It must not be described as moving the real system pointer.
- Treat CUA support as live only after verifying `status`, launch/window listing, screenshot/state, and at least one action command on a real logged-in GUI session with required platform permissions granted.

CUA Driver:

- Keep CUA as an optional runtime provider selected by `background cua`, separate from native screenshot/input implementations.
- Detect the binary from `KISEKI_CUA_DRIVER`, `PATH`, and known platform install locations.
- Pass JSON arguments through stdin rather than fragile shell-quoted JSON command-line arguments.
- Do not infer live support from binary presence. `session.cuaBackground` means the binary is discoverable; live support needs CUA status plus target action artifacts.
- Windows CUA runs in the interactive desktop session where the installed driver is available.
- Linux CUA follows upstream pre-release status and requires true graphical Linux validation.

Windows selected-window:

- Resolve a target HWND through `platform/target`.
- Use `target inspect` when a command needs receiver detail; it reports child HWNDs, class names, and bounds.
- Keep direct selected-window behavior message/API based. Do not present it as universal canvas/raw-input operation.

Windows background screenshot:

- Keep Windows background work scoped to observation through `background window screenshot`.
- Do not introduce a separate Windows session backend for the current project direction.
- Selected-window input remains a message/API helper for targets that accept normal Windows messages; do not describe it as universal background operation.
- Background screenshot should use target selectors and the selected-window capture backend without activating the target.

## WebUI Asset Rule

The current build embeds WebUI assets in `src/webui/static_assets.cpp`. If editing `ui/`, update embedded assets too, or tests that inspect WebUI behavior may not reflect the visible source files.

Tests should continue to assert that WebUI assets do not reference operational API routes.

## Safety Language

When touching background input or game-targeted wording, preserve this constraint:

> Game-class programs support targeted high-speed screenshot where available; background keyboard/mouse only works for targets that accept system window messages or public automation interfaces.
