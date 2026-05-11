# Kiseki Input Design

## Goal

Build a new cross-platform C++ CLI tool with an embedded local WebUI for visual configuration only, driver-level input simulation, target-aware background input where the platform allows it, system-level screenshots, high-speed burst capture, configurable heartbeat, and closable system notifications.

## Approved Scope

The first version produces one CLI executable. It supports Windows and Linux, uses C++ as the primary language, and uses an embedded local web server for configuration UI. The WebUI cannot trigger screenshots, input simulation, notifications, or any operational command. All operational behavior is exposed only through CLI subcommands.

Windows driver-level keyboard and mouse simulation uses the existing MIT-licensed reference at `F:\輝色臻至\原始參考\IbInputSimulator`. Linux driver-level keyboard and mouse simulation uses native system interfaces. Windows and Linux screenshots use system-level capture APIs rather than browser or application-level capture.

## Explicit Non-Goals

The project does not provide a remote control API, tray application, desktop GUI, browser-triggered action endpoint, protected-input bypass, target process injection, or hidden WebUI execution route.

Game support is limited by operating system and target application behavior. Targeted high-speed screenshots are supported for games when the platform capture backend can access the target surface. Background keyboard and mouse input for games is best-effort and only applies to games that accept system window messages or public automation interfaces. Raw Input, DirectInput, protected full-screen, and targets that do not accept background window messages are not promised to accept true background input.

## Top-Level Commands

```text
kiseki config-ui [--host 127.0.0.1] [--port 8787]
kiseki config path
kiseki config show
kiseki config validate
kiseki input --backend driver key tap --vk <n>
kiseki input --backend driver key down --vk <n>
kiseki input --backend driver key up --vk <n>
kiseki input --backend driver mouse move --x <n> --y <n> [--absolute|--relative]
kiseki input --backend driver mouse click --button left|right|middle|x1|x2
kiseki input --backend driver mouse wheel --delta <n>
kiseki input --backend background-window --target-title <text> key tap --vk <n>
kiseki input --backend background-window --target-pid <pid> mouse click --x <n> --y <n>
kiseki screenshot desktop --output <path>
kiseki screenshot window --target-title <text> --output <path>
kiseki screenshot region --target-title <text> --rect x,y,w,h --output <path>
kiseki screenshot burst --target-title <text> --fps 60 --frames 8 --output-dir <dir>
kiseki daemon
kiseki notify test
kiseki capabilities
kiseki doctor
```

Driver input is a global input stream and does not accept target selectors. Target selectors are valid for background-window input and target-aware screenshot commands.

## Target Selectors

Operational commands that support targets accept these selectors:

```text
--target-pid <pid>
--target-process <name>
--target-title <substring>
--target-class <class>
--target-window-id <id>
```

The target resolver returns a concrete platform window handle or a clear error when no target or multiple ambiguous targets are found. The first version errors on ambiguity.

## Configuration

The configuration file uses JSON and schema versioning.

Windows path:

```text
%APPDATA%\KisekiInput\config.json
```

Linux path:

```text
$XDG_CONFIG_HOME/kiseki-input/config.json
```

When `XDG_CONFIG_HOME` is unset, Linux uses:

```text
~/.config/kiseki-input/config.json
```

Default configuration:

```json
{
  "schemaVersion": 1,
  "webui": {
    "host": "127.0.0.1",
    "port": 8787
  },
  "heartbeat": {
    "enabled": true,
    "intervalSeconds": 300,
    "notificationEnabled": true,
    "message": "Kiseki Input is running"
  },
  "input": {
    "defaultBackend": "background-window",
    "windowsDriver": "AnyDriver",
    "linuxDriver": "uinput",
    "backgroundInputEnabled": true
  },
  "screenshot": {
    "defaultOutputDirectory": "",
    "burstFps": 60,
    "burstFrames": 8,
    "format": "png"
  },
  "safety": {
    "allowDriverInputWithoutTarget": true,
    "allowBackgroundInputForGames": true
  }
}
```

## WebUI Boundary

The embedded WebUI is a local configuration editor. It serves static HTML, CSS, and JavaScript plus configuration-only endpoints:

```text
GET /api/config
PUT /api/config
GET /api/capabilities
```

The WebUI must not expose endpoints or buttons for input simulation, screenshot capture, notification test, daemon control, shell execution, or arbitrary file read/write. WebUI validation mirrors CLI configuration validation so users can see invalid values before saving.

## Runtime Architecture

The tool is organized around small services and backend interfaces:

```text
CLI command
  -> configuration store
  -> target resolver, when the command accepts targets
  -> service layer
  -> platform backend selector
  -> platform backend
  -> encoder, manifest writer, or output formatter
```

The service layer owns cross-platform behavior and validation. Platform modules implement concrete OS-specific work. Backend selectors choose the best available backend and produce explicit limitations when a requested mode is not available.

## Module Layout

```text
src/core/
  config/
    config_model.hpp/.cpp
    config_store.hpp/.cpp
    config_validation.hpp/.cpp

  target/
    target_query.hpp/.cpp
    target_result.hpp
    target_matcher.hpp/.cpp

  input/
    input_event.hpp
    input_backend.hpp
    input_service.hpp/.cpp
    coordinate_mapper.hpp/.cpp

  capture/
    capture_request.hpp
    capture_frame.hpp
    capture_backend.hpp
    capture_service.hpp/.cpp
    burst_scheduler.hpp/.cpp
    burst_manifest.hpp/.cpp
    image_encoder.hpp/.cpp

  notify/
    notify_backend.hpp
    notify_service.hpp/.cpp
    heartbeat.hpp/.cpp

  capabilities/
    capabilities_model.hpp
    capabilities_service.hpp/.cpp

src/cli/
  main.cpp
  command_config.hpp/.cpp
  command_config_ui.hpp/.cpp
  command_input.hpp/.cpp
  command_screenshot.hpp/.cpp
  command_daemon.hpp/.cpp
  command_doctor.hpp/.cpp

src/webui/
  web_server.hpp/.cpp
  config_api.hpp/.cpp
  static_assets.hpp/.cpp

src/platform/windows/
  target/
    win_window_enumerator.hpp/.cpp
    win_process_lookup.hpp/.cpp

  input/
    ib_input_driver.hpp/.cpp
    win_background_input.hpp/.cpp
    win_keymap.hpp/.cpp

  capture/
    win_capture_backend_selector.hpp/.cpp
    win_graphics_capture.hpp/.cpp
    win_dxgi_capture.hpp/.cpp
    win_print_window_capture.hpp/.cpp
    win_desktop_crop_capture.hpp/.cpp
    win_capture_capabilities.hpp/.cpp

  notify/
    win_toast_notify.hpp/.cpp
    win_message_notify.hpp/.cpp

src/platform/linux/
  target/
    x11_window_enumerator.hpp/.cpp
    linux_process_lookup.hpp/.cpp

  input/
    linux_uinput_driver.hpp/.cpp
    x11_background_input.hpp/.cpp
    linux_keymap.hpp/.cpp

  capture/
    linux_capture_backend_selector.hpp/.cpp
    x11_window_capture.hpp/.cpp
    x11_region_capture.hpp/.cpp
    wayland_portal_capture.hpp/.cpp
    linux_capture_capabilities.hpp/.cpp

  notify/
    libnotify_backend.hpp/.cpp
    notify_send_backend.hpp/.cpp
```

The existing user-created folders under `src/windows/keyboard`, `src/windows/mouse`, `src/windows/screenshot`, `src/windows/system`, `src/linux/keyboard`, `src/linux/mouse`, `src/linux/screenshot`, and `src/linux/system` will be replaced by the finer platform layout above during implementation. Empty legacy directories do not need compatibility wrappers because no source files exist in them.

## Input Backends

Driver input:

- Windows uses `IbInputSimulator` for keyboard and mouse events.
- Linux uses native input simulation, with `uinput` as the first supported driver backend.
- Driver input does not target a background window. It sends global input to the system focus.

Background-window input:

- Windows uses platform window messaging and public automation mechanisms where applicable.
- Linux X11 uses X11 window messaging or XTest-style mechanisms where applicable.
- Wayland background input support is limited by compositor policy.
- Background-window input returns a structured unsupported result when the platform or target does not permit the requested event.

Input coordinate mapping is isolated so target-window-relative coordinates, screen coordinates, and region coordinates can be optimized independently.

## Screenshot Backends

Windows capture selection:

1. Windows Graphics Capture for target windows when available.
2. DXGI-based capture for desktop or game-compatible surfaces where available.
3. PrintWindow for ordinary windows that support it.
4. Desktop crop fallback for visible regions.

Linux capture selection:

1. X11 target window capture for X11 sessions.
2. X11 region capture for target-relative or screen-relative regions.
3. Wayland portal capture when the compositor exposes it.

Each backend reports whether it supports desktop capture, target window capture, region capture, occluded window capture, and burst capture. The selector does not silently downgrade in ways that change semantics. For example, if a hidden target window cannot be captured and only visible desktop crop is available, the command returns a limitation unless the user explicitly allows fallback.

## Burst Capture

Burst capture is a first-class screenshot mode:

```text
kiseki screenshot burst --target-title "Game" --fps 60 --frames 8 --output-dir frames
```

The burst scheduler uses a monotonic clock. At 60 FPS it schedules frames approximately every 16.67 ms. The first version supports 8 frames by default and accepts explicit `--fps` and `--frames` values validated against reasonable limits.

Output files use stable sequence numbers and timestamps:

```text
frame_000_20260506T120000.123.png
frame_001_20260506T120000.140.png
```

Each burst writes `manifest.json` containing requested FPS, requested frame count, actual timestamps, per-frame capture duration, encode duration, output path, dropped frame status, selected backend, and backend limitations. If the platform cannot maintain the requested cadence, the CLI exits with success only when frames were captured, prints a warning, and records the actual timing in the manifest.

## Notifications And Heartbeat

The daemon command runs in the foreground by default and can be launched by the user as a background process using the operating system shell or service manager. It reads the JSON configuration and executes heartbeat behavior.

Heartbeat configuration controls:

- enable or disable heartbeat
- interval seconds
- enable or disable notification
- notification message

System reminders are closable pop-up notifications where the platform supports it. Windows uses toast notifications when possible and a simple message notification fallback otherwise. Linux uses libnotify when available and `notify-send` fallback otherwise.

## Capabilities And Doctor

`kiseki capabilities` prints a machine-readable capability matrix. `kiseki doctor` prints human-readable diagnostics, missing dependencies, permission issues, and platform limitations.

Example capability shape:

```json
{
  "input": {
    "driver": true,
    "backgroundWindow": true
  },
  "capture": {
    "desktop": true,
    "window": true,
    "region": true,
    "burst": true
  },
  "limitations": [
    "background-window input depends on whether the target accepts system window messages or public automation events"
  ]
}
```

## Dependencies

Core dependencies:

- CMake
- CLI11 for command parsing
- nlohmann/json for configuration and manifest JSON
- cpp-httplib for local WebUI serving
- stb_image_write for image encoding

Windows dependencies:

- `IbInputSimulator` from `F:\輝色臻至\原始參考\IbInputSimulator`
- Win32 API
- Windows Graphics Capture and DXGI where available

Linux dependencies:

- `/dev/uinput`
- X11, Xfixes, Xcomposite, and XShm for X11 capture and target lookup
- Wayland portal support where available
- libnotify or `notify-send` for notifications

## Testing Strategy

Automated tests avoid moving the real mouse or requiring real screenshots in CI.

Unit tests cover:

- configuration defaults, parsing, validation, and persistence paths
- CLI argument validation and command routing
- target selector parsing and ambiguity handling
- input event models and coordinate mapping
- capture request validation
- burst scheduler timing calculations
- burst manifest serialization
- image encoder path and format selection using synthetic frames
- capability matrix serialization

Platform contract tests use fake backends to verify service behavior without invoking real OS input or capture. Local manual diagnostics are handled by `kiseki doctor`, `kiseki notify test`, and explicit screenshot/input commands.

## Error Handling

Commands return structured error codes and clear messages. Unsupported platform behavior is not reported as a generic failure. Target ambiguity, unavailable backend, missing permissions, capture fallback refusal, and missed burst cadence each have distinct messages so users can understand whether to change configuration, command arguments, or platform setup.

## Implementation Order

Implementation should proceed from stable contracts outward:

1. CMake project, dependency wiring, and test harness.
2. Core configuration model, store, and validation.
3. CLI command skeleton and output conventions.
4. WebUI static configuration editor and config-only API.
5. Capability and doctor services.
6. Target model and fake target resolver.
7. Capture request, burst scheduler, image encoder, and manifest writer.
8. Platform screenshot backends.
9. Input event model and fake input backend.
10. Platform input backends.
11. Notification and heartbeat daemon.
12. End-to-end local diagnostics.
