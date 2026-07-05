# Kiseki Feature Map

## CLI Surface

Global:

- `kiseki --config <path> ...` overrides the active config path.
- `kiseki --version` prints the version.
- `kiseki modes [--json]` prints the operation and screenshot mode guide. Use `--json` for weak-model or agent handoff.
- `kiseki teach record|annotate|transcribe ...` records and prepares Agivar-style teaching bundles: action events plus keyframes, optional human text, optional real media, and targeted annotations.

## Mode Selection Contract

Strict command split:

- `kiseki input ...` is current-session/non-background operation. It may use active focus or the real pointer.
- `kiseki screenshot ...` is current-session/non-background capture, even when it selects a target window.
- `kiseki background ...` is background, isolated-session, or target-routed operation/capture.
- `kiseki observe ui ...` is structured observation. It is neither a screenshot nor an input operation.

Screenshot families:

- Non-background screenshots: `screenshot desktop`, `screenshot burst`, `screenshot window`, `screenshot window-burst`.
- Background screenshots: `background window screenshot`, `background desktop screenshot`, `background cua screenshot`, `background cua state --output`.

Verification rule:

- Verify foreground/current-session work with `screenshot desktop` or `screenshot window`.
- Verify selected-window background work with `background window screenshot`.
- Verify Linux Xvfb work with `background desktop screenshot`.
- Verify CUA target-routed work with `background cua screenshot` or `background cua state --output`.
- Do not verify background actions with `screenshot desktop`; it captures the current visible desktop, not necessarily the background context.

Configuration:

- `kiseki config path` prints the active config path.
- `kiseki config show` loads saved config or defaults and prints JSON.
- `kiseki config validate` validates saved config or defaults.

WebUI:

- `kiseki config-ui [--host <host>] [--port <port>]` starts the embedded local configuration server.
- The WebUI can edit configuration and read local teaching bundles through browser file inputs.
- Allowed API routes: `GET /api/config`, `PUT /api/config`, `GET /api/capabilities`.
- Never expose operational API routes from the WebUI. Teaching viewing must stay local-file based and must not trigger input, screenshots, notifications, daemon control, shell commands, or process launch.

Targets:

- `kiseki target list` prints target windows as JSON.
- `kiseki target list --target-title <text>` filters by title substring.
- `kiseki target list --target-pid <pid>` filters by process id.
- `kiseki target list --target-window-id <id>` filters by platform window id.
- `kiseki target inspect --target-title <text>` prints the selected target and child receiver handles.
- Use `id` from this output with `--target-window-id` when a title selector is ambiguous; on Windows, explicit child HWND ids from `target inspect` can be selected directly.

Screenshots:

- `kiseki screenshot desktop --output <file.bmp>` captures the current visible desktop to BMP. This is non-background.
- `kiseki screenshot burst --directory <dir> --prefix <name> --frames <n> --fps <n>` captures current visible desktop BMP frames. This is non-background.
- `kiseki screenshot window --target-title <text> --output <file.bmp>` captures one target window through the current-session screenshot path. This is non-background.
- `kiseki screenshot window-burst --target-title <text> --directory <dir> --prefix <name> --frames <n> --fps <n>` captures BMP frames from one target window through the current-session screenshot path. This is non-background.
- `kiseki background window screenshot --target-title <text> --output <file.bmp>` captures one selected target window without activating it when the backend allows it. This is a background screenshot.
- Target selectors are `--target-title`, `--target-pid`, and `--target-window-id`.
- If burst options are omitted or zero, defaults come from config.
- Supported format is currently BMP.

Non-visual observation:

- `kiseki observe ui --target-title <text> [--provider auto|window-tree|uia|ax] [--max-depth <n>] [--max-elements <n>]` reads structured UI data without screenshots, OCR, or vision models.
- `window-tree` source is `platform-window-tree`: selected top-level window plus child receiver handles when the platform backend exposes them.
- `uia` source is `windows-uia`: Windows UI Automation control-view data with names, automation ids, class names, localized control types, bounds, enabled/offscreen state, and process id when the target exposes them.
- `ax` source is `macos-ax`: macOS Accessibility API data, matching the structured element surface used by Accessibility Inspector, with role, subrole, title/name, description, value, identifier, bounds, and enabled state when the target exposes them.
- `auto` prefers the deepest native provider available and reports the actual `source`. If it falls back, JSON includes `fallbackReason`; explicit providers fail instead of silently downgrading.
- Future deeper providers should keep the same shape: CUA state, Linux AT-SPI, browser CDP, and app-native scripting APIs.
- Do not describe `observe ui` as visual understanding. It is structured observation from platform/app APIs and must report its source in JSON.

Input:

- `kiseki input key --key <name> [--backend auto|driver|system]`
- `kiseki input combo --keys <a+b> [--backend auto|driver|system]`
- `kiseki input text --text <text>`
- `kiseki input text --file <utf8-file>`
- `kiseki input mouse --dx <n> --dy <n> [--click ...] [--backend ...]`
- `kiseki input mouse --x <n> --y <n> [--absolute] [--click ...] [--backend ...]`
- `kiseki input drag --file <points.txt> [--backend auto|driver|system] [--step-delay-ms <n>] [--start-hold-ms <n>] [--end-hold-ms <n>]`
- `kiseki background window text --target-title <text> --text <text>`
- `kiseki background window key --target-title <text> --key <name>`
- `kiseki background window mouse --target-title <text> --x <n> --y <n> [--click ...]`
- `kiseki background window drag --target-title <text> --file <points.txt>`

Integrated background command group:

- Use `kiseki background ...` for docs, demos, tests, and agent workflows.
- `kiseki background window screenshot|text|key|mouse|drag ...` is the selected-window background command surface.
- `kiseki background desktop <subcommand> ...` is the Linux isolated X11 desktop command surface.
- `kiseki background cua <subcommand> ...` is the optional CUA Driver command surface.
- Older direct background command families are removed and should fail before reaching platform backends.

Linux background desktop:

- `kiseki background desktop start --display :99 --width 1280 --height 720 --depth 24`
- `kiseki background desktop stop --display :99`
- `kiseki background desktop launch --display :99 --command <shell-command>`
- `kiseki background desktop screenshot --display :99 --output <file.bmp>`
- `kiseki background desktop text --display :99 --text <text>`
- `kiseki background desktop text --display :99 --file <utf8-file>`
- `kiseki background desktop key --display :99 --key <name>`
- `kiseki background desktop mouse --display :99 --x <n> --y <n> [--click ...]`

CUA background provider:

- `kiseki background cua status [--prompt]`
- `kiseki background cua launch --bundle-id <id> [--url <url>] [--new-instance]`
- `kiseki background cua windows [--pid <pid>] [--on-screen-only]`
- `kiseki background cua state --pid <pid> --window-id <id> [--output <image>] [--query <text>]`
- `kiseki background cua screenshot --window-id <id> --output <image> [--format png|jpeg]`
- `kiseki background cua click --pid <pid> --window-id <id> --x <n> --y <n> [--button left|right|double]`
- `kiseki background cua click --pid <pid> --window-id <id> --element-index <n>`
- `kiseki background cua text --pid <pid> --text <text>`
- `kiseki background cua key --pid <pid> --key <name>`
- `kiseki background cua hotkey --pid <pid> --keys <cmd+c>`
- `kiseki background cua drag --pid <pid> --window-id <id> --from-x <n> --from-y <n> --to-x <n> --to-y <n>`
- `kiseki background cua draw --pid <pid> --window-id <id> --file <points.txt> [--duration-ms <n>] [--steps <n>]`
- `kiseki background cua feedback status`
- `kiseki background cua feedback enable --enabled <true|false>`
- `kiseki background cua feedback motion [--start-handle <n>] [--end-handle <n>] [--arc-size <n>] [--arc-flow <n>] [--spring <n>] [--glide-duration-ms <n>] [--dwell-after-click-ms <n>] [--idle-hide-ms <n>]`
- `kiseki background cua feedback style [--reset] [--gradient-colors <#hex,#hex>] [--bloom-color <#hex>] [--image-path <path>]`
- `kiseki background cua feedback preset --name natural|fast|recording|quiet`

Macros:

- `kiseki macro validate --file <macro.json>` validates JSON macro structure.
- `kiseki macro run --file <macro.json>` executes a macro step sequence.
- Macro execution stops at the first failing step and returns that step's nonzero code.

Teaching recordings:

- `kiseki teach record [--output <dir>] [--duration-ms <n>] [--frame-interval-ms <n>] [--event-poll-ms <n>] [--title <text>] [--text <text>|--text-file <file>]` toggles recording. First call starts a detached recorder; the next call stops and finalizes it. `--duration-ms 0` means record until stopped.
- `kiseki teach record ... [--video-file <file>] [--audio-file <file>] [--transcript-file <file>]` attaches only real existing media/transcript files.
- `kiseki teach record ... [--video-keyframe-interval-ms <n>] [--video-keyframe-max <n>] [--no-video-keyframes]` controls review JPEG extraction from real video files through `ffmpeg`.
- `kiseki teach annotate --session <dir> --frame-index <n> --text <text>` attaches guidance to a keyframe.
- `kiseki teach annotate --session <dir> --event-index <n> --text <text>` attaches guidance to a recorded action.
- `kiseki teach transcribe --audio-file <file> --output <transcript.json> [--model <local-dir>] [--model-id Systran/faster-whisper-large-v3]` runs the optional faster-whisper transcription path and downloads the model when the local directory is missing or empty.
- Teaching bundles are model-facing action sequences plus keyframes. Do not feed full video to a teach agent when `actions.json`, `timeline.json`, selected keyframes, instruction text, transcript, and annotations are sufficient.
- The default transcription model path is `vendor/models/Systran/faster-whisper-large-v3`; model artifacts should be downloaded or pre-bundled there, not committed to git.
- The standard bundle shape is `manifest.json` schema v2, `frames.json`, `actions.json`, `timeline.json`, `events.jsonl`, `instruction.txt` when text exists, `annotations.json`, selected `keyframes/*.bmp`, generated `SKILL.md`, optional `media/*`, and optional `video_keyframes/index.json`.

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
- `kiseki modes --json` prints a machine-readable command selection guide for operation mode, screenshot mode, coordinate space, and matching verification command.

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
- Explicit background window screenshots use the same selected-window capture backend and must not activate the target.
- Background-window input uses normal Win32 window messages and targets text-like child controls when possible.
- `target inspect` reports child HWNDs, class names, bounds, and titles so selected-window automation can choose concrete receivers.
- Windows background screenshot is the supported Windows background observation path; it does not require a VM, Docker, or separate session backend.
- Optional CUA background operation uses `cua-driver` when installed in the interactive desktop session and is exposed through `kiseki background cua ...`.

Linux:

- Input uses X11/XTest when compiled with X11 and `DISPLAY` is usable.
- Linux `driver` backend currently reports unavailable.
- Screenshot uses X11 root `XGetImage` when the session permits it.
- Wayland current-session desktop screenshots use XDG Desktop Portal when the build finds `gio-2.0` and `gdk-pixbuf-2.0`; the compositor may require permission or return denial. This does not provide Wayland global input.
- Target-window background input uses X11 events.
- Target-window screenshots use X11 `XGetImage` on the selected window.
- Explicit background window screenshots use the same selected-window X11 capture backend.
- Background desktop support starts `Xvfb` and runs applications on an isolated `DISPLAY`.
- Background desktop screenshot/input commands temporarily select that `DISPLAY` and use the same X11 screenshot and XTest input paths.
- Optional CUA background operation uses `cua-driver` when installed; upstream marks Linux CUA support as pre-release, so true graphical Linux validation is required before claims.
- Some compositor sessions block capture; the CLI should report unavailable/failure clearly, not crash.

CUA Driver:

- Optional CUA background operation uses `cua-driver` when installed and authorized, and is exposed through `kiseki background cua ...`.
- CUA binary lookup checks `$KISEKI_CUA_DRIVER`, `cua-driver` on `PATH`, Windows `%LOCALAPPDATA%\Programs\Cua\cua-driver\bin\cua-driver.exe`, macOS `/Applications/CuaDriver.app/Contents/MacOS/cua-driver`, and Linux `~/.local/bin/cua-driver`.
- CUA coordinates are window-local screenshot coordinates for commands that take `--window-id`.
- CUA action commands run without a declared session by default for reliable one-shot CLI use. Set `KISEKI_CUA_SESSION` to a fresh per-run id when the agent-cursor overlay should follow a sequence of actions.
- CUA drag behavior depends on target state and the platform driver. Verify with before/after screenshots rather than return code alone.
- `background cua feedback ...` controls CUA's visual agent-cursor overlay only; it is not the system pointer.
- Public support claims require real platform build, permission/status checks, and live desktop validation. WSL is not Linux CUA proof.

macOS:

- macOS has a native current-session backend and can also use the optional CUA background provider.
- Config path uses `~/Library/Application Support/KisekiInput/config.json`.
- Target listing uses Apple Window Services in the active GUI session.
- Desktop and selected-window screenshots use ScreenCaptureKit and require Screen Recording permission.
- Global keyboard and mouse input uses Quartz CGEvent and requires Accessibility permission.
- Use `kiseki permissions macos screen-recording --prompt --open-settings` and `kiseki permissions macos accessibility --prompt --open-settings` from the same GUI Terminal/app that will run Kiseki when TCC does not show a prompt automatically. Do not treat an SSH-launched denial as proof that the GUI Terminal lacks permission.
- `kiseki input ...` commands are global/current-session operations; they can move the real pointer or depend on focus.
- Target-window background input is not reported available until a target-specific automation backend exists.
- Use `input drag --file` for continuous foreground drawing when taking the current pointer/focus is acceptable. For drawing apps, prepare the target state first: pick the brush/shape tool, set a visible foreground color, and use `--start-hold-ms`/`--step-delay-ms` when the app needs slower pointer cadence.
- Use `background cua draw` for target-routed CUA drawing; it composes sparse point paths into drag segments and should be verified with `background cua screenshot` or `background cua state`. Dense sampled paths should stay on foreground `input drag`; CUA draw has `--max-segments` to prevent accidental long-running segment storms.
- Drawing app operation requires before/after screenshots, verified canvas coordinates, selected tool, and visible foreground color. See `references/drawing-apps.md` before operating Krita, Paint, or other canvas-style apps.
- Roadmap details live in `docs/roadmap.md`.

## Capability Limits

- `backgroundWindow` is reported available when the platform/session supports target-window operations.
- `session.backgroundDesktop` is reported available when `Xvfb` is available on a Linux X11 build.
- `session.cuaBackground` is reported available when a Cua Driver binary is found on the current platform.
- `session.macosCuaBackground` is the macOS-specific CUA detail field and is only true on macOS when a Cua Driver binary is found.
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
- `{"type":"background-drag","targetTitle":"Paint","file":"points.txt"}`
- `{"type":"screenshot","output":"screen.bmp"}`
- `{"type":"background-screenshot","targetTitle":"Paint","output":"window.bmp"}`
- `{"type":"sleep","ms":500}`

Backends default to `auto` when omitted. Text steps require exactly one of `text` or `file`. Mouse absolute movement requires both `x` and `y`.
