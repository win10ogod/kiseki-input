# Kiseki Input

Kiseki Input is a cross-platform C++ CLI tool for configuration-first input and screenshot automation workflows.

## Demos

These GIFs were recorded from the Windows build with the CLI and macro runner. They are cropped to the actual target window or dialog.

| Macro input in Notepad | Mouse macro in Paint | Heartbeat notification |
| --- | --- | --- |
| ![Kiseki CLI typing text into Notepad](docs/assets/demos/notepad-unicode.gif) | ![Kiseki CLI drawing a heart in Paint](docs/assets/demos/paint-macro.gif) | ![Kiseki heartbeat notification](docs/assets/demos/heartbeat-notification.gif) |

## Current Build

This build includes:

- C++ CLI executable: `kiseki`
- JSON configuration defaults, validation, and persistence
- Config-only local WebUI: `kiseki config-ui`
- CLI desktop screenshots and burst screenshots
- CLI keyboard/mouse input
- CLI JSON macros for sequencing input, screenshots, and waits
- Configurable heartbeat daemon with dismissible notifications
- Machine-readable capabilities: `kiseki capabilities`
- Human-readable diagnostics: `kiseki doctor`

Windows input first attempts to load `IbInputSimulator.dll` next to the executable. If that DLL is absent or cannot initialize, Windows falls back to `SendInput` and reports driver input unavailable. Linux input uses native X11/XTest when available. Desktop screenshots are system-level BMP captures: Win32 GDI on Windows, X11 `XGetImage` on Linux sessions that allow root capture.

## Commands

```text
kiseki config path
kiseki config show
kiseki config validate
kiseki config-ui
kiseki screenshot desktop --output screenshot.bmp
kiseki screenshot burst --directory frames --prefix frame --frames 8 --fps 60
kiseki input key --key shift
kiseki input combo --keys win+r
kiseki input text --text "hello"
kiseki input mouse --dx 0 --dy 0 --click none
kiseki macro validate --file macro.json
kiseki macro run --file macro.json
kiseki daemon run
kiseki daemon run --once
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

## Macros

Macros are JSON files with a `steps` array. They are executed only by the CLI.

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

## Platform Notes

Windows screenshots and system input were tested in this tree. Driver-level input requires an `IbInputSimulator.dll` build artifact from `IbInputSimulator`.

WSL/Linux input was tested through X11/XTest. In the current WSLg session, X11 root screenshot capture is blocked by the compositor, so the Linux binary reports desktop screenshot unavailable instead of crashing. The same Linux code works on X11 sessions that permit `XGetImage` on the root window.

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
