# Native input sequences and recording

`input` operates in the current GUI session. `background window` sends selected-window messages where supported. CUA keeps its separate provider commands. The configuration WebUI does not execute these operations.

## Commands

```bash
kiseki input key --key left --hold-ms 500 --backend system
kiseki input key --key space --action down --backend system
kiseki input key --key space --action up --backend system
kiseki input combo --keys ctrl+shift+a --hold-ms 100 --backend system
kiseki input mouse --x 400 --y 300 --click left --click-count 2 --click-interval-ms 80 --hold-ms 20 --backend system
kiseki input mouse --wheel 120 --hwheel -120 --backend system
kiseki input drag --file points.txt --button right --modifiers ctrl --step-delay-ms 30 --start-hold-ms 200 --end-hold-ms 400 --backend system
kiseki input sequence --file sequence.json
```

`key --action` accepts `tap` (default), `down`, or `up`. `--hold-ms` applies to a tap or chord. Separate down/up commands leave input held between invocations; use a sequence when the operation needs automatic cleanup across steps.

## Backend selection

`--backend` accepts `auto` (default), `system`, or `driver`. `system` is the OS input path (`SendInput` on Windows, Quartz CGEvent on macOS, XTest on X11). `driver` is the Windows-only IbInputSimulator kernel-driver path. `auto` uses the driver only after its delivery is verified in the current process, and otherwise falls back to `system`; the command output then states the backend it used and why.

A Windows driver cannot report success it did not deliver. The first driver use runs a one-time self-test: it sends small, bounded, duplicated *relative* probes and measures the cursor displacement through a low-level mouse hook (driver events arrive without `LLMHF_INJECTED`; `SendInput` sets it). It never sends an absolute command, because some drivers (for example Razer) treat normalized absolute coordinates as huge relative deltas and would fling the cursor. The driver is `verified` only when the motion is bounded, direction-correct, and repeatable (consistent scaling and acceleration are fine); it is `silent` when the device opens but moves nothing (the Razer device with no Razer hardware, a leftover Logitech bus device), `unfaithful` when it flings, reverses, couples axes, or is erratic, and `deferred` when a button or key is held (so a probe cannot turn into a drag). The probe always restores the cursor to its origin. Explicit `--backend driver` fails with a precise error unless the driver is verified; `auto` silently uses `system` instead and says so.

Absolute moves on the driver never use an absolute command: they position with bounded relative steps (tiered larger-then-smaller so pointer acceleration cannot overshoot), reading `GetCursorPos` until the exact target pixel is reached. If positioning cannot converge, an absolute move under `auto` falls back to `system` for that move (it is idempotent) and the output states that; a relative move, a click, a key, or an explicit `--backend driver` request does not fall back.

The verdict is cached across invocations in `driver-verify.txt` next to the config file, keyed by driver type, DLL identity, and logon session (reset on reboot, DLL change, or after 12 hours), so repeated `auto` commands cost no self-test. `--driver-reverify` forces a fresh self-test and refreshes the cache.

Select the driver type with `--driver-type` (`any`, `logitech-ghub-new`, `logitech`, `razer`, `dd`, `mouclass`, or `sendinput`), the config field `input.windowsDriver`, or the `KISEKI_DRIVER` environment variable; precedence is flag, then environment, then config. `any` probes Logitech G HUB (new), legacy Logitech, Razer, DD, then MouClassInputInjection, and keeps the first that verifies delivery. `--driver-arg` (config `input.windowsDriverArgument`, env `KISEKI_DRIVER_ARG`) supplies the DD DLL path or the MouClassInputInjection target process id. `kiseki capabilities` reports the selected type and cached verification state under `input.driverBackend`, and `kiseki doctor` prints it on the `Input driver backend` line; neither moves the pointer.

Mouse buttons are `left`, `right`, `middle`, `x1`, and `x2`. Each also accepts `-down` and `-up`. `none` only moves or scrolls. A complete click accepts a positive `--click-count`, a nonnegative hold duration, and a gap after each up before the next down. macOS receives matching click states for both halves of each click; an explicit double click delivers states `1,1,2,2`.

Wheel values preserve backend units. Windows uses 120 units per conventional detent and passes smaller deltas through. macOS uses pixel deltas. Positive vertical means up; positive horizontal means right. X11 core wheel input uses button detents: fractional 120-unit contributions accumulate in the sequence process until a detent is available. A standalone X11 invocation cannot preserve a fractional remainder after it exits.

The Windows `driver` backend routes mouse, wheel, X buttons, and keys through IbSendInput (full `INPUT` arrays), so capability depends on the driver type. Razer delivers horizontal wheel and X1/X2; Logitech and DD have no horizontal wheel and send whole 120-unit detents only; MouClassInputInjection injects mouse only, no wheel and no keyboard. Where a driver cannot represent a requested event faithfully it fails explicitly rather than sending a wrong one: on the `driver` backend, numpad Enter and the keys a vendor remaps (for example Razer's `rctrl`, `ralt`, `numpad-divide`) are refused with a message to use `--backend system`.

Physical key names include letters, digits, navigation/editing keys (`delete`, `home`, `end`, `pageup`, `pagedown`), punctuation names (`minus`, `equal`, `leftbracket`, `rightbracket`, `semicolon`, `quote`, `backslash`, `comma`, `period`, `slash`, `grave`), left/right modifiers (`lshift`, `rshift`, `lctrl`, `rctrl`, `lalt`, `ralt`, `rwin`), and `numpad0`–`numpad9`, `numpad-enter`, `numpad-add`, `numpad-subtract`, `numpad-multiply`, `numpad-divide`, `numpad-decimal`. Function keys follow the platform mapping (Windows/X11 F1–F24; macOS F1–F20). Unsupported platform keys fail explicitly. `input text` continues to use the platform's text path; key commands do not substitute Unicode text for physical events.

On Windows, holding `enter` does not suppress a `numpad-enter` tap, or vice versa. The shared `VK_RETURN` state cannot establish which physical Enter is held; Kiseki uses its exact acquired key identity for this check. Modifier borrowing still uses the side-specific modifier state.

## Sequence format and cleanup

`input sequence` and `macro run` use the same executor. A sequence executes in one process, preserving held state and backend connections:

```json
{
  "steps": [
    {"type":"key", "key":"space", "action":"down", "backend":"system"},
    {"type":"mouse", "x":400, "y":300, "click":"left-down", "backend":"system"},
    {"type":"mouse", "x":500, "y":300, "atMs":150, "backend":"system"},
    {"type":"mouse", "x":600, "y":320, "atMs":300, "backend":"system"},
    {"type":"mouse", "click":"left-up", "backend":"system"},
    {"type":"key", "key":"space", "action":"up", "backend":"system"}
  ]
}
```

Every input file is read before the first action. Paths retain their existing working-directory interpretation. `macro validate` checks files, keys, click values, timing, and unknown step fields. Optional `description` fields and top-level metadata are accepted. Misspelled action fields fail instead of silently using defaults.

| Step | Additional fields |
| --- | --- |
| `key` | `key`, `action` (`tap`, `down`, `up`), `holdMs`, `backend` |
| `combo` | `keys`, `holdMs`, `backend` |
| `mouse` | `dx`, `dy`, `x`, `y`, `absolute`, `click`, `clickCount`, `clickIntervalMs`, `holdMs`, `wheel`, `hwheel`, `backend` |
| `drag` | `file`, `button`, `modifiers` joined by `+`, `stepDelayMs`, `startHoldMs`, `endHoldMs`, `backend` |
| `background-mouse` | target selectors, `x`, `y`, `click`, `clickCount`, `clickIntervalMs`, `holdMs`, `heldButtons`, `receiverWindowId` |
| `background-drag` | target selectors, `file`, `button`, `stepDelayMs`, `startHoldMs`, `endHoldMs` |
| `text` | exactly one of `text` or `file` |
| `screenshot` / `background-screenshot` | `output`; background screenshots also accept target selectors |
| `sleep` | nonnegative `ms` |

Target selectors are `targetTitle`, `targetPid`, and `targetWindowId`. Every step may specify nonnegative, nondecreasing `atMs`, an absolute deadline measured from sequence execution start after preflight. A step runs after both its deadline and the previous step's completion. This is desktop scheduling, not a hard real-time guarantee.

On completion, failure, or normal SIGINT/SIGTERM cancellation, the executor attempts to release inputs it acquired, in reverse order. Chords and drags have the same scoped cleanup. Already-held user modifiers are borrowed by chords/drags and are not released by their cleanup. Explicit raw up commands still request an up. Release failures remain visible and do not skip other releases. Forced process termination cannot execute in-process cleanup.

macOS keeps submitted key, button, and pointer state until its private Quartz source counter catches up. Consecutive relative moves accumulate from the last submitted position, and a released modifier cannot leak into a following key through a stale state query. Once pending events are acknowledged, fresh system state incorporates external input again. The CLI drains its submitted native events before returning; C++ callers can use `synchronize_input()` on the sending thread. This confirms WindowServer processing of the source's events, not completion of the target application's response. Standalone raw down commands still leave the requested input held for a later up.

## Paths and selected-window drags

Paths retain all supplied points and accept either `x y` lines or `x y time_ms` lines. Timed paths require a timestamp on every point, begin at zero, and never decrease. They are measured after `startHoldMs`; they override the uniform step interval. Existing untimed paths preserve their step/start/end timing. Linux flushes X11 requests at each scheduling boundary.

Windows system mouse moves set `MOUSEEVENTF_MOVE_NOCOALESCE` to preserve explicit path points in the `WM_MOUSEMOVE` queue when a receiver is busy. Relative input still follows Windows pointer acceleration. This flag does not control an application's own sampling or drawing behavior. See [Microsoft's MOUSEINPUT reference](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-mouseinput).

Windows selected-window mouse sequences retain the down event's child receiver and attach the current `MK_*` button/modifier state to movement. Separate CLI invocations can supply `--held-buttons left` and `--receiver-window-id <child-hwnd>` with the original target, including on the final up. `background window drag` manages this state automatically. On X11, select the intended receiver directly with `--target-window-id`; its explicit receiver ID must match that target. macOS native selected-window message input remains separate from current-session CGEvent input and the CUA provider.

Within a sequence, a selected-window down binds its resolved window and receiver for subsequent mouse steps using the same selector and for cleanup. Windows title changes, another matching title, or hiding the window do not redirect its up. A destroyed recipient or changed owning process produces a cleanup error. An explicit up releases the binding once its acquired buttons are all up; a later down resolves the selector again. C++ callers can share `BackgroundMouseOptions::binding` across split actions for the same behavior.

## Screenshot files and coordinates

```bash
kiseki screenshot window --target-window-id <id> --output window.png --json
kiseki screenshot desktop --output desktop.png --json
kiseki background window screenshot --target-window-id <id> --output window.png --json
kiseki background desktop screenshot --display :99 --output display.png --json
```

The output extension selects the file format: `.png` writes an opaque 8-bit RGB PNG (Windows Imaging Component, macOS ImageIO, Linux gdk-pixbuf when the portal slice is built; other X11 builds write an uncompressed PNG), and `.bmp` or no extension writes a 32-bit BMP. Other extensions fail before capture. `screenshot burst`, `window-burst`, and teaching keyframes write BMP frames.

The default text output remains available. JSON includes `format`, pixel dimensions, and optional `coordinates`. Images use a top-left origin. Convert a pixel target into the coordinate space named by `coordinates.space` using:

```text
screenX = originX + pixelX / pixelsPerUnitX
screenY = originY + pixelY / pixelsPerUnitY
```

| Platform | `space` | Image bounds |
| --- | --- | --- |
| macOS | `macos-global-points` | Native resolution. The desktop, or the full window including the title bar and excluding its shadow. |
| Windows | `windows-virtual-screen` (coordinates accepted by `input mouse --absolute`) | Window captures cover the visible frame (`boundsSource: dwm-extended-frame-bounds`), without the invisible resize borders included in `GetWindowRect`. `clientArea` gives the client rectangle in image pixels. Scales are 1 unless the process is DPI-virtualized. |
| X11 | `x11-root-pixels` of the captured display | Desktop captures start at 0,0. Window captures cover the client window at its root position. |

Do not treat a full-window image as a client-only capture. A 720×492-point Retina window can produce a 1440×984-pixel image with both scales equal to 2. On Windows, subtracting `clientArea.x`/`y` from an image pixel gives the client-area offset used by `background window` input when both scales are 1. Providers that do not supply a verified transform return `coordinates: null`. Teaching frames preserve available transforms, screen-space `mouse`, image-space `mousePixels`, and corrected `mouseNorm` values.

## Teaching events and verification

Windows uses a dedicated low-level hook thread; macOS uses a passive session event tap; X11 builds with libXtst RECORD headers/libraries use XRecord. The detached X11 recording worker initializes Xlib threading before opening a display; C++ embedders must call `XInitThreads` before their first Xlib call. Native callbacks queue events independently of screenshot capture. JSON conversion and file writing occur when the recording worker drains that queue. `--event-poll-ms` controls draining and the interval of the explicitly marked polling fallback; it does not limit native event sampling.

Native records include `timestampMs`, `timestampUs`, source timestamp/unit, keycode/scan, repeat information, button state, and wheel axes where exposed by the source. macOS button records include click count. The manifest names `eventSource` and `eventCaptureMode`. If a native event source cannot start, independent state polling remains available with an explicit warning that short taps, repeat, and wheel events can be missing. macOS event-tap permission is Input Monitoring for the recording process or its terminal host. A disabled macOS tap produces a gap warning and is re-enabled.

The opt-in native probe opens its own window and moves the current pointer/focus, restoring them when it finishes. It is not part of unattended CTest. The output directory is a required argument; `--help` prints usage and sends no input, and an unknown flag is rejected:

```bash
cmake -S . -B build -DKISEKI_BUILD_NATIVE_INPUT_PROBE=ON
cmake --build build --config Debug
# Windows:
build/Debug/kiseki_native_input_probe.exe artifacts/live-test/native
python test/native/verify_events.py windows artifacts/live-test/native
# Windows driver backend (records delivery honestly; see Backend selection):
build/Debug/kiseki_native_input_probe.exe artifacts/live-test/native-driver --backend driver
python test/native/verify_events.py windows artifacts/live-test/native-driver --driver
# macOS / X11:
./build/kiseki_native_input_probe artifacts/live-test/native
python3 test/native/verify_events.py macos artifacts/live-test/native
# Use "linux" for X11 verification. On macOS, run in an authorized GUI Terminal.
```

The default `--backend system` run is unchanged. `--backend driver` exercises the Windows driver path: when the configured driver verifies delivery it checks that absolute moves land exactly and that buttons/keys are matched and released; when the driver does not deliver on the host it records `driver-status.json` and sends no further input, and the verifier reports it as undelivered rather than failing.

When comparing with `teach record`, start the recorder before the probe, stop it afterward, and pass `--teaching <bundle>` to the verifier. On macOS, pass `--skip-capture` as the probe's second argument during that comparison to avoid two simultaneous ScreenCaptureKit capture clients. Run the probe separately without that flag for the four-corner screenshot/coordinate test. Validate finalized bundles with `skills/kiseki-teach-recording/scripts/validate_bundle.py`.

Implementation references: [Windows low-level keyboard hook](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc), [Windows KEYBDINPUT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-keybdinput), [Apple passive event-tap options](https://developer.apple.com/documentation/coregraphics/cgeventtapoptions).
