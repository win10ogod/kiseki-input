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

Targets:

- `kiseki target list` prints target windows as JSON.
- `kiseki target list --target-title <text>` filters by title substring.
- `kiseki target list --target-pid <pid>` filters by process id.
- `kiseki target list --target-window-id <id>` filters by platform window id.
- Use `id` from this output with `--target-window-id` when a title selector is ambiguous.

Screenshots:

- `kiseki screenshot desktop --output <file.bmp>` captures the visible desktop to BMP.
- `kiseki screenshot burst --directory <dir> --prefix <name> --frames <n> --fps <n>` captures BMP frames.
- `kiseki screenshot window --target-title <text> --output <file.bmp>` captures one target window.
- `kiseki screenshot window-burst --target-title <text> --directory <dir> --prefix <name> --frames <n> --fps <n>` captures BMP frames from one target window.
- Target selectors are `--target-title`, `--target-pid`, and `--target-window-id`.
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
- `kiseki input background-text --target-title <text> --text <text>`
- `kiseki input background-key --target-title <text> --key <name>`
- `kiseki input background-mouse --target-title <text> --x <n> --y <n> [--click ...]`

Macros:

- `kiseki macro validate --file <macro.json>` validates JSON macro structure.
- `kiseki macro run --file <macro.json>` executes a macro step sequence.
- Macro execution stops at the first failing step and returns that step's nonzero code.

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
- Target-window screenshots use `PrintWindow`.
- Background-window input uses normal Win32 window messages and targets text-like child controls when possible.

Linux:

- Input uses X11/XTest when compiled with X11 and `DISPLAY` is usable.
- Linux `driver` backend currently reports unavailable.
- Screenshot uses X11 root `XGetImage` when the session permits it.
- Target-window background input uses X11 events.
- Target-window screenshots use X11 `XGetImage` on the selected window.
- Some compositor sessions block capture; the CLI should report unavailable/failure clearly, not crash.

macOS:

- macOS is a planned platform line, not a completed support claim.
- The blocker is lack of reliable macOS hardware for build, permission, and live desktop validation.
- Roadmap details live in `docs/roadmap.md`.

## Capability Limits

- `backgroundWindow` is reported available when the platform/session supports target-window operations.
- Capture `window` is implemented. Capture `region` is not implemented.
- Game background input is only expected for targets that accept normal system window messages or public automation interfaces.
- No guarantee is made for targets that ignore system window messages or public automation events.

## Macro Schema

Macro files are JSON objects with a non-empty `steps` array. Supported step types:

- `{"type":"key","key":"enter","backend":"system"}`
- `{"type":"combo","keys":"win+r","backend":"system"}`
- `{"type":"text","text":"hello"}` or `{"type":"text","file":"input.txt"}`
- `{"type":"mouse","dx":1,"dy":1,"click":"left","backend":"system"}`
- `{"type":"mouse","x":640,"y":360,"click":"left","backend":"system"}`
- `{"type":"drag","file":"points.txt","backend":"system"}`
- `{"type":"screenshot","output":"screen.bmp"}`
- `{"type":"sleep","ms":500}`

Backends default to `auto` when omitted. Text steps require exactly one of `text` or `file`. Mouse absolute movement requires both `x` and `y`.
