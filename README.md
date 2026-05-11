# Kiseki Input

Kiseki Input is a native C++ CLI for local desktop automation: keyboard and mouse input, system screenshots, replayable JSON macros, burst capture, heartbeat notifications, and a configuration-only WebUI in one small tool.

It is built for developers who want a practical automation lab instead of a pile of one-off scripts. The project is CLI-first, keeps each backend slice separate, and reports platform limits explicitly instead of pretending every desktop, game, or compositor behaves the same way.

## Why Try It

- Single native CLI for input, screenshots, macros, diagnostics, and heartbeat notifications.
- Replayable JSON macros make manual UI checks repeatable.
- Burst screenshots can grab short frame sequences such as 8 frames at 60 FPS.
- Target-window screenshots and message-based background input are available for windows that accept the platform APIs.
- WebUI is intentionally configuration-only, so opening it does not create a remote control surface.
- Windows can use `IbInputSimulator.dll` when available and falls back to system input when it is not.
- Linux support uses native X11/XTest paths where the session permits it.
- macOS support is planned, but waits for real macOS hardware validation.

## See It Work

These GIFs were recorded from the Windows build using the CLI and macro runner. They are cropped to the actual target window or dialog.

| Macro input in Notepad | Mouse macro in Paint | Heartbeat notification |
| --- | --- | --- |
| ![Kiseki CLI typing text into Notepad](docs/assets/demos/notepad-unicode.gif) | ![Kiseki CLI drawing a heart in Paint](docs/assets/demos/paint-macro.gif) | ![Kiseki heartbeat notification](docs/assets/demos/heartbeat-notification.gif) |

## Try It In 60 Seconds

From the repository root after building:

```bash
kiseki doctor
kiseki target list
kiseki screenshot desktop --output screenshot.bmp
kiseki screenshot burst --directory frames --prefix frame --frames 8 --fps 60
kiseki macro validate --file docs/assets/demos/demo-notepad-unicode.json
kiseki --config docs/assets/demos/demo-notification-config.json daemon run --once
```

If `kiseki` is not on `PATH`, use the built executable path instead, such as `build/Debug/kiseki.exe` on Windows multi-config builds.

On Windows, the demo macros can drive visible desktop apps:

```bash
kiseki macro run --file docs/assets/demos/demo-notepad-unicode.json
kiseki macro run --file docs/assets/demos/demo-paint-macro.json
```

## What Works Today

This build includes:

- C++ CLI executable: `kiseki`
- JSON configuration defaults, validation, and persistence
- Config-only local WebUI: `kiseki config-ui`
- CLI desktop screenshots and burst screenshots
- CLI target listing for window id, PID, title, and geometry
- CLI target-window screenshots and target-window burst screenshots
- CLI keyboard/mouse input
- CLI message-based background keyboard/mouse input for target windows that accept it
- CLI JSON macros for sequencing input, screenshots, and waits
- Configurable heartbeat daemon with dismissible notifications
- Machine-readable capabilities: `kiseki capabilities`
- Human-readable diagnostics: `kiseki doctor`

Windows input first attempts to load `IbInputSimulator.dll` next to the executable. If that DLL is absent or cannot initialize, Windows falls back to `SendInput` and reports driver input unavailable. Target-window background input on Windows uses normal window messages such as `WM_CHAR`, `WM_KEYDOWN/UP`, and mouse messages. Linux input uses native X11/XTest for global input and X11 events for target-window background input when available. Screenshots are system-level BMP captures: Win32 GDI/`PrintWindow` on Windows, X11 `XGetImage` on Linux sessions that allow capture.

## Important Boundaries

- Operational actions are CLI-only. The WebUI can read/write config and show capabilities, but it cannot trigger input, screenshots, notifications, daemon control, shell commands, or process launch.
- Target-aware and background-window behavior must be checked through capabilities and platform support before relying on it.
- Game-class programs can be used for targeted screenshot experiments where the platform capture backend can access the surface. Background keyboard/mouse input depends on whether the target accepts system window messages or public automation events.
- Driver input is not a bypass mechanism. It follows the limits of the installed backend, desktop session, and target application.

## Command Map

```text
kiseki config path
kiseki config show
kiseki config validate
kiseki config-ui
kiseki target list
kiseki target list --target-title "Untitled"
kiseki screenshot desktop --output screenshot.bmp
kiseki screenshot burst --directory frames --prefix frame --frames 8 --fps 60
kiseki screenshot window --target-title "Untitled" --output window.bmp
kiseki screenshot window-burst --target-title "Untitled" --directory frames --prefix window --frames 8 --fps 60
kiseki input key --key shift
kiseki input combo --keys win+r
kiseki input text --text "hello"
kiseki input mouse --dx 0 --dy 0 --click none
kiseki input background-text --target-title "Untitled" --text "hello"
kiseki input background-key --target-title "Untitled" --key enter
kiseki input background-mouse --target-title "Untitled" --x 20 --y 20 --click left
kiseki macro validate --file macro.json
kiseki macro run --file macro.json
kiseki daemon run
kiseki daemon run --once
kiseki capabilities
kiseki doctor
```

## WebUI Contract

The embedded local WebUI exposes only:

```text
GET /api/config
PUT /api/config
GET /api/capabilities
```

It does not expose input, screenshot, notification, daemon, shell, or execution routes.

## Macros

Macros are JSON files with a `steps` array. They are executed only by the CLI and are useful for turning a visible desktop workflow into a repeatable check.

```json
{
  "name": "paint-demo",
  "steps": [
    {"type": "combo", "keys": "win+r", "backend": "system"},
    {"type": "text", "text": "mspaint.exe"},
    {"type": "key", "key": "enter", "backend": "system"},
    {"type": "sleep", "ms": 500},
    {"type": "drag", "file": "artifacts/live-test/heart-points.txt", "backend": "system"},
    {"type": "screenshot", "output": "artifacts/live-test/paint-macro.bmp"}
  ]
}
```

Supported macro step types: `key`, `combo`, `text`, `mouse`, `drag`, `screenshot`, and `sleep`.

Demo macro files live under `docs/assets/demos/`.

## Platform Notes

Windows screenshots, target listing, target-window screenshots, system input, and message-based background input were tested in this tree. Driver-level input requires an `IbInputSimulator.dll` build artifact from `IbInputSimulator`.

Linux support is implemented through X11/XTest and X11 window APIs. `kiseki target list` is the recommended first step before using target-window screenshots or background input, especially when a desktop environment exposes both a window-manager frame and a client window. Wayland or compositor-restricted sessions may report target input or screenshot capture unavailable instead of crashing.

macOS is a planned platform line. It is not excluded by project direction; it is not marked supported today because the maintainers currently do not have reliable macOS hardware for build, permission, and live desktop verification. See [docs/roadmap.md](docs/roadmap.md).

## Roadmap

See [docs/roadmap.md](docs/roadmap.md) for the Windows, Linux, and planned macOS platform lines.

## Build

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

When building with a multi-config generator, the executable is usually under `build/Debug/kiseki.exe` on Windows.

## Credits

Special thanks to [Chaoses-Ib/IbInputSimulator](https://github.com/Chaoses-Ib/IbInputSimulator). Kiseki Input's Windows driver backend is designed around `IbInputSimulator.dll` from that project.

Thanks to Codex and GPT-5.5 for implementation assistance, live Windows testing, macro verification, and demo production.
