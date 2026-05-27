# Kiseki Input

Kiseki Input is a native C++ CLI for local desktop automation: keyboard and mouse input, system screenshots, replayable JSON macros, burst capture, heartbeat notifications, and a configuration-only WebUI in one small tool.

It is built for developers who want a practical automation lab instead of a pile of one-off scripts. The project is CLI-first, keeps each backend slice separate, and reports platform limits explicitly instead of pretending every desktop, game, or compositor behaves the same way.

## Why Try It

- Single native CLI for input, screenshots, macros, diagnostics, and heartbeat notifications.
- Replayable JSON macros make manual UI checks repeatable.
- Burst screenshots can grab short frame sequences such as 8 frames at 60 FPS.
- Target-window and explicit background-window screenshots are available for windows that accept the platform APIs.
- Linux can run an isolated Xvfb background desktop so GUI apps execute on a separate DISPLAY instead of the user's current desktop.
- macOS can use the optional Cua Driver backend for background app launch, per-window screenshots, AX/window state, targeted clicks, text, keys, hotkeys, and drags when `cua-driver` is installed and authorized.
- WebUI is intentionally configuration-only, so opening it does not create a remote control surface.
- Windows can use `IbInputSimulator.dll` when available and falls back to system input when it is not.
- Linux support uses native X11/XTest paths where the session permits it.
- macOS has an initial native backend for target listing, system screenshots, selected-window screenshots, and global CGEvent input; live hardware validation is still required before treating it as release-grade.

## See It Work

These GIFs were recorded from live CLI runs. Windows demos use the visible desktop and macro runner. Linux uses an isolated Xvfb `DISPLAY`. macOS uses the optional Cua Driver backend through `kiseki mac-background`; in the controlled run, Safari stayed frontmost and the cursor position did not move.

| Macro input in Notepad | Mouse macro in Paint | Heartbeat notification |
| --- | --- | --- |
| ![Kiseki CLI typing text into Notepad](docs/assets/demos/notepad-unicode.gif) | ![Kiseki CLI drawing a heart in Paint](docs/assets/demos/paint-macro.gif) | ![Kiseki heartbeat notification](docs/assets/demos/heartbeat-notification.gif) |

| Linux isolated DISPLAY | macOS CUA background |
| --- | --- |
| ![Kiseki Linux isolated Xvfb background desktop demo](docs/assets/demos/linux-isolated-display.gif) | ![Kiseki macOS CUA background TextEdit demo](docs/assets/demos/macos-cua-background.gif) |

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
- CLI target inspection for selected windows and child receiver handles
- CLI target-window screenshots, explicit background-window screenshots, and target-window burst screenshots
- CLI keyboard/mouse input
- CLI message-based background keyboard/mouse input for target windows that accept it
- CLI Linux background desktop lifecycle and input/screenshot commands when built with X11 and `Xvfb` is installed
- Initial macOS native backend for config path, target listing, desktop/window screenshots, burst screenshots, and global keyboard/mouse input
- Optional macOS CUA background operation commands through `kiseki mac-background ...`
- CLI JSON macros for sequencing input, screenshots, and waits
- Configurable heartbeat daemon with dismissible notifications
- Machine-readable capabilities: `kiseki capabilities`
- Human-readable diagnostics: `kiseki doctor`

Windows input first attempts to load `IbInputSimulator.dll` next to the executable. If that DLL is absent or cannot initialize, Windows falls back to `SendInput` and reports driver input unavailable. Target-window input on Windows uses normal window messages such as `WM_CHAR`, `WM_KEYDOWN/UP`, and mouse messages when a target accepts them. Linux input uses native X11/XTest for global input and X11 events for target-window background input when available. Screenshots are system-level BMP captures: Win32 GDI/`PrintWindow` on Windows, X11 `XGetImage` on Linux sessions that allow capture. `screenshot background-window` is the selected-window capture entry intended for non-activating background verification.

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
kiseki target inspect --target-title "Untitled"
kiseki screenshot desktop --output screenshot.bmp
kiseki screenshot burst --directory frames --prefix frame --frames 8 --fps 60
kiseki screenshot window --target-title "Untitled" --output window.bmp
kiseki screenshot background-window --target-title "Untitled" --output background-window.bmp
kiseki screenshot window-burst --target-title "Untitled" --directory frames --prefix window --frames 8 --fps 60
kiseki input key --key shift
kiseki input combo --keys win+r
kiseki input text --text "hello"
kiseki input mouse --dx 0 --dy 0 --click none
kiseki input background-text --target-title "Untitled" --text "hello"
kiseki input background-key --target-title "Untitled" --key enter
kiseki input background-mouse --target-title "Untitled" --x 20 --y 20 --click left
kiseki input background-drag --target-title "Paint" --file points.txt
kiseki background-desktop start --display :99 --width 1280 --height 720 --depth 24
kiseki background-desktop launch --display :99 --command "xterm"
kiseki background-desktop mouse --display :99 --x 20 --y 20 --click left
kiseki background-desktop text --display :99 --text "hello"
kiseki background-desktop screenshot --display :99 --output background.bmp
kiseki background-desktop stop --display :99
kiseki mac-background status --prompt
kiseki mac-background launch --bundle-id com.apple.Safari --url about:blank
kiseki mac-background windows --pid 1234
kiseki mac-background state --pid 1234 --window-id 5678 --output safari.jpg
kiseki mac-background screenshot --window-id 5678 --output safari.png
kiseki mac-background click --pid 1234 --window-id 5678 --x 100 --y 200
kiseki mac-background text --pid 1234 --text "hello"
kiseki mac-background hotkey --pid 1234 --window-id 5678 --keys cmd+c
kiseki mac-background drag --pid 1234 --window-id 5678 --from-x 10 --from-y 20 --to-x 180 --to-y 120
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
    {"type": "screenshot", "output": "artifacts/live-test/paint-macro.bmp"},
    {"type": "background-screenshot", "targetTitle": "Paint", "output": "artifacts/live-test/paint-background.bmp"}
  ]
}
```

Supported macro step types: `key`, `combo`, `text`, `mouse`, `drag`, `background-drag`, `screenshot`, `background-screenshot`, and `sleep`.

Demo macro files live under `docs/assets/demos/`.

## Platform Notes

Windows screenshots, target listing, target-window screenshots, system input, and message-based background input were tested in this tree. Driver-level input requires an `IbInputSimulator.dll` build artifact from `IbInputSimulator`.

Linux support is implemented through X11/XTest and X11 window APIs. `kiseki target list` is the recommended first step before using target-window screenshots or background input, especially when a desktop environment exposes both a window-manager frame and a client window. Linux true background desktop support uses `Xvfb` to create an isolated X11 `DISPLAY`; commands launched there can be clicked, typed into, and captured without using the physical desktop session. Wayland or compositor-restricted sessions may report target input or screenshot capture unavailable instead of crashing.

Windows background screenshot uses selected-window capture through `screenshot background-window`. It is the Windows background observation path for this project; it does not require a VM, Docker, or separate session backend. Windows selected-window input remains a compatibility helper for ordinary Win32 controls and apps that accept public window messages.

macOS has a native backend using Apple desktop APIs. Desktop and selected-window screenshots use ScreenCaptureKit and require Screen Recording permission in the active GUI session. Target listing uses Window Services. Global keyboard and mouse input uses Quartz CGEvent and requires Accessibility permission. For true macOS background app operation, Kiseki exposes an optional Cua Driver provider through `mac-background`; it requires `cua-driver`, Accessibility permission, and Screen Recording permission. See [docs/roadmap.md](docs/roadmap.md).

## Roadmap

See [docs/roadmap.md](docs/roadmap.md) for the Windows, Linux, and macOS platform lines.

## Build

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

When building with a multi-config generator, the executable is usually under `build/Debug/kiseki.exe` on Windows.

## Credits

Special thanks to [Chaoses-Ib/IbInputSimulator](https://github.com/Chaoses-Ib/IbInputSimulator). Kiseki Input's Windows driver backend is designed around `IbInputSimulator.dll` from that project.

Thank you to [trycua/cua](https://github.com/trycua/cua) for Cua Driver, which provides the macOS background computer-use backend that Kiseki can call when installed.

Thanks to Codex and GPT-5.5 for implementation assistance, live Windows testing, macro verification, and demo production.
