# Kiseki Input

Kiseki Input is a cross-platform C++ CLI tool for configuration-first input and screenshot automation workflows.

## Current Build

This build includes:

- C++ CLI executable: `kiseki`
- JSON configuration defaults, validation, and persistence
- Config-only local WebUI: `kiseki config-ui`
- CLI desktop screenshots and burst screenshots
- CLI keyboard/mouse input
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

## Platform Notes

Windows screenshots and system input were tested in this tree. Driver-level input requires an `IbInputSimulator.dll` build artifact from `F:\輝色臻至\原始參考\IbInputSimulator`.

WSL/Linux input was tested through X11/XTest. In the current WSLg session, X11 root screenshot capture is blocked by the compositor, so the Linux binary reports desktop screenshot unavailable instead of crashing. The same Linux code works on X11 sessions that permit `XGetImage` on the root window.

## Build

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

When building with a multi-config generator, the executable is usually under `build/Debug/kiseki.exe` on Windows.
