# Kiseki Feature Map

## CLI Surface

Global:

- `kiseki --config <path> ...` overrides the active config path.
- `kiseki --version` prints the version.

Configuration:

- `kiseki config path` prints the active config path.
- `kiseki config show` loads saved config or defaults and prints JSON.
- `kiseki config validate` validates saved config or defaults.

WebUI:

- `kiseki config-ui [--host <host>] [--port <port>]` starts the embedded local configuration server.
- The WebUI is configuration-only.
- Allowed API routes: `GET /api/config`, `PUT /api/config`, `GET /api/capabilities`.
- Never expose operational API routes from the WebUI.

Screenshots:

- `kiseki screenshot desktop --output <file.bmp>` captures the visible desktop to BMP.
- `kiseki screenshot burst --directory <dir> --prefix <name> --frames <n> --fps <n>` captures BMP frames.
- If burst options are omitted or zero, defaults come from config.
- Supported format is currently BMP.

Input:

- `kiseki input key --key <name> [--backend auto|driver|system]`
- `kiseki input combo --keys <a+b> [--backend auto|driver|system]`
- `kiseki input text --text <text>`
- `kiseki input text --file <utf8-file>`
- `kiseki input mouse --dx <n> --dy <n> [--click ...] [--backend ...]`
- `kiseki input mouse --x <n> --y <n> [--absolute] [--click ...] [--backend ...]`
- `kiseki input drag --file <points.txt> [--backend auto|driver|system]`

Mouse click values:

- `none`, `left`, `right`, `middle`
- `left-down`, `left-up`, `right-down`, `right-up`, `middle-down`, `middle-up`

Drag path files:

- Plain text, one absolute point per line: `x y`
- Empty lines are ignored only if truly empty.
- Lines beginning with `#` are comments.
- At least two points are required.

Daemon and notification:

- `kiseki daemon run` runs the heartbeat loop until disabled, interrupted, or error.
- `kiseki daemon run --once` runs one heartbeat cycle.
- Windows notifications use a dismissible `MessageBoxW`.
- Linux notifications use `notify-send`.

Diagnostics:

- `kiseki capabilities` prints machine-readable capability JSON.
- `kiseki doctor` prints version, config path, config status, runtime capabilities, and limitations.

## Config Schema

Current schema version is `1`.

Defaults:

- `webui.host`: `127.0.0.1`
- `webui.port`: `8787`
- `heartbeat.enabled`: `true`
- `heartbeat.intervalSeconds`: `300`
- `heartbeat.notificationEnabled`: `true`
- `heartbeat.message`: `Kiseki Input is running`
- `input.defaultBackend`: `background-window`
- `input.windowsDriver`: `AnyDriver`
- `input.linuxDriver`: `uinput`
- `input.backgroundInputEnabled`: `true`
- `screenshot.defaultOutputDirectory`: empty string, interpreted by CLI as current directory for burst
- `screenshot.burstFps`: `60`
- `screenshot.burstFrames`: `8`
- `screenshot.format`: `bmp`
- `safety.allowDriverInputWithoutTarget`: `true`
- `safety.allowBackgroundInputForGames`: `true`

Validation:

- `webui.host` must not be empty.
- `webui.port` must be nonzero.
- Enabled heartbeat requires `intervalSeconds >= 1`.
- Enabled heartbeat notification requires non-empty `message`.
- `input.defaultBackend` is `driver` or `background-window`.
- `input.windowsDriver` is one of `AnyDriver`, `SendInput`, `Logitech`, `LogitechGHubNew`, `Razer`, `DD`, `MouClassInputInjection`.
- `input.linuxDriver` is `uinput`.
- `screenshot.burstFps` and `screenshot.burstFrames` are `1..240`.
- `screenshot.format` is `bmp`.

## Platform Behavior

Windows:

- Screenshot uses Win32 GDI virtual-screen capture with `BitBlt` and `CAPTUREBLT`.
- Input `driver` uses `IbInputSimulator.dll` next to the executable.
- Input `system` uses Win32 `SendInput`.
- Input `auto` uses `SendInput` for combos containing `win`, `super`, or `meta`; otherwise it tries IbInputSimulator first and falls back to SendInput.
- Unicode text input uses `KEYEVENTF_UNICODE`.
- Absolute mouse coordinates are virtual-screen coordinates.

Linux:

- Input uses X11/XTest when compiled with X11 and `DISPLAY` is usable.
- Linux `driver` backend currently reports unavailable.
- Screenshot uses X11 root `XGetImage` when the session permits it.
- Some WSLg sessions block root capture; the CLI should report unavailable/failure clearly, not crash.

## Capability Limits

- `backgroundWindow` is currently reported unavailable by runtime capabilities.
- Capture `window` and `region` are not implemented.
- Game background input is only expected for targets that accept normal system window messages or public automation interfaces.
- No guarantee is made for Raw Input, DirectInput, protected fullscreen, or anti-cheat protected games.
