# Kiseki Testing Guide

## Standard Build And Unit Tests

From WSL, use Windows CMake when validating the Windows build:

```bash
"/mnt/c/Program Files/CMake/bin/cmake.exe" --build build
"/mnt/c/Program Files/CMake/bin/ctest.exe" --test-dir build --output-on-failure
```

Then run:

```bash
git diff --check
git status --short --branch
```

For Linux true-machine validation, use the local Linux toolchain on that machine:

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Do not substitute WSL-only behavior for true Linux proof unless the user explicitly asks for WSL.

## Windows Live UI Verification

Use the Windows executable from WSL:

```bash
mkdir -p artifacts/live-test
./build/Debug/kiseki.exe screenshot desktop --output artifacts/live-test/desktop-before.bmp
```

Inspect a selected Windows target without sending input:

```bash
./build/Debug/kiseki.exe target list
./build/Debug/kiseki.exe target inspect --target-window-id 0x123456
```

For Windows shell hotkeys, use system backend:

```bash
./build/Debug/kiseki.exe input combo --keys win+r --backend system
```

For driver-specific checks, use:

```bash
./build/Debug/kiseki.exe input key --key shift --backend driver
```

If focus is wrong, capture the desktop first and correct focus. Do not continue typing blindly into the terminal.

## Notepad Unicode Recipe

Create `artifacts/live-test/notepad-input.txt` with `apply_patch`:

```text
WSADFGHJKL, 你好
```

Then run:

```bash
./build/Debug/kiseki.exe input combo --keys win+r --backend system
./build/Debug/kiseki.exe input text --text "notepad.exe"
./build/Debug/kiseki.exe input key --key enter --backend system
./build/Debug/kiseki.exe input combo --keys ctrl+a --backend system
./build/Debug/kiseki.exe input key --key backspace --backend system
./build/Debug/kiseki.exe input text --file artifacts/live-test/notepad-input.txt
./build/Debug/kiseki.exe screenshot desktop --output artifacts/live-test/notepad-final.bmp
```

Visually inspect the screenshot. Expected visible text: `WSADFGHJKL, 你好`.

## Paint Drag Recipe

Create `artifacts/live-test/heart-points.txt` with `apply_patch`:

```text
740 330
710 300
670 315
670 365
740 425
810 365
810 315
770 300
740 330
```

Then run:

```bash
./build/Debug/kiseki.exe input combo --keys win+r --backend system
./build/Debug/kiseki.exe input text --text "mspaint.exe"
./build/Debug/kiseki.exe input key --key enter --backend system
./build/Debug/kiseki.exe observe ui --target-title "Paint"
./build/Debug/kiseki.exe input drag --file artifacts/live-test/heart-points.txt --backend system
./build/Debug/kiseki.exe screenshot desktop --output artifacts/live-test/paint-heart.bmp
```

Use `input drag` instead of separate `mouse left-down`, move, and `left-up` commands for drawing tests, because one process preserves button state more reliably.
Use `observe ui` before screenshot-based verification when the task can be answered from platform/app structure. It must produce structured JSON without invoking screenshots, OCR, or visual model parsing. On Windows, verify both strict UIA and fallback behavior when relevant:

```bash
./build/Debug/kiseki.exe observe ui --target-title "Chrome" --provider uia --max-depth 4 --max-elements 256
./build/Debug/kiseki.exe observe ui --target-title "Chrome" --provider window-tree
```

On macOS, verify AX observation against a real GUI target after granting Accessibility permission to the terminal/Codex host:

```bash
./build/kiseki observe ui --target-title "Krita" --provider ax --max-depth 4 --max-elements 256
./build/kiseki observe ui --target-title "Krita" --provider auto --max-depth 4 --max-elements 256
```

## Paint Macro Recipe

Create `artifacts/live-test/paint-macro.json` with `apply_patch`:

```json
{
  "name": "paint-heart",
  "steps": [
    {"type": "combo", "keys": "win+r", "backend": "system"},
    {"type": "text", "text": "mspaint.exe"},
    {"type": "key", "key": "enter", "backend": "system"},
    {"type": "sleep", "ms": 1200},
    {"type": "drag", "file": "artifacts/live-test/heart-points.txt", "backend": "system"},
    {"type": "screenshot", "output": "artifacts/live-test/paint-macro.bmp"}
  ]
}
```

Then run:

```bash
./build/Debug/kiseki.exe macro validate --file artifacts/live-test/paint-macro.json
./build/Debug/kiseki.exe macro run --file artifacts/live-test/paint-macro.json
```

Visually inspect `artifacts/live-test/paint-macro.bmp` or a converted PNG before claiming live macro success.

## WebUI Browser Recipe

Start the server in a long-running command session:

```bash
./build/Debug/kiseki.exe config-ui --host 127.0.0.1 --port 8765
```

Open it in Windows:

```bash
cmd.exe /c start "" "http://127.0.0.1:8765"
```

Check API health:

```bash
curl http://127.0.0.1:8765/api/config
curl http://127.0.0.1:8765/api/capabilities
```

Capture visible proof:

```bash
./build/Debug/kiseki.exe screenshot desktop --output artifacts/live-test/webui-open.bmp
```

Stop the `config-ui` session before final response unless the user asks to leave it running.

## Screenshot Burst Recipe

```bash
mkdir -p artifacts/live-test/burst
./build/Debug/kiseki.exe screenshot burst --directory artifacts/live-test/burst --prefix frame --frames 8 --fps 60
```

Expected files:

- `frame_0000.bmp`
- `frame_0001.bmp`
- through `frame_0007.bmp`

## Heartbeat Recipe

Use `--once` first so testing does not leave a daemon running:

```bash
./build/Debug/kiseki.exe daemon run --once
```

If the test opens a dismissible Windows message box, close it before continuing. For long-running daemon tests, record the session id and stop it before final response unless explicitly asked to keep it running.

## Linux Background Desktop Recipe

Run only on a real Linux host with X11 libraries and `Xvfb` installed. WSL-only results are not Linux desktop proof.

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
mkdir -p artifacts/live-test
./build/kiseki background-desktop start --display :99 --width 1280 --height 720 --depth 24
./build/kiseki background-desktop launch --display :99 --command "xterm"
./build/kiseki background-desktop mouse --display :99 --x 40 --y 40 --click left
./build/kiseki background-desktop text --display :99 --text "kiseki background desktop"
./build/kiseki background-desktop screenshot --display :99 --output artifacts/live-test/background-desktop.bmp
./build/kiseki background-desktop stop --display :99
```

Expected result: the BMP shows the virtual X11 desktop and launched app. The physical desktop cursor and focus should not move during the sequence.

## macOS CUA Background Recipe

Run only on a logged-in macOS GUI session with Cua Driver installed.

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/kiseki mac-background status --prompt
./build/kiseki mac-background launch --bundle-id com.apple.Safari --url about:blank
./build/kiseki mac-background windows
./build/kiseki mac-background state --pid <pid> --window-id <window_id> --output artifacts/live-test/mac-cua-state.jpg
./build/kiseki mac-background screenshot --window-id <window_id> --output artifacts/live-test/mac-cua-window.png
./build/kiseki mac-background click --pid <pid> --window-id <window_id> --x 100 --y 100
./build/kiseki mac-background text --pid <pid> --text "kiseki mac cua"
./build/kiseki mac-background feedback preset --name natural
./build/kiseki mac-background feedback status
./build/kiseki mac-background draw --pid <pid> --window-id <window_id> --file artifacts/live-test/mac-cua-points.txt --duration-ms 80 --steps 5 --max-segments 96
./build/kiseki mac-background screenshot --window-id <window_id> --output artifacts/live-test/mac-cua-draw.png
```

Expected result: CUA reports granted Accessibility and Screen Recording permissions, captures the selected window, and routes at least one action to the target. For backgrounded targets, CUA drag should use the pid-routed path and not take over the user's real cursor; for frontmost targets, CUA may use a HID-style drag path and the real cursor can visibly move. For drawing, inspect the after-screenshot; a successful return code alone is not enough. Do not claim live CUA support if the Mac cannot be reached or permissions are missing.

For visible CUA agent-cursor feedback across multiple CLI calls, set a fresh session id for that run:

```bash
export KISEKI_CUA_SESSION=kiseki-$(date +%s)
```

Do not reuse an old CUA session id after CUA reports `session ended`; choose a new id.

When testing macOS drawing:

- Use `input drag --file` only for foreground/global drawing tests where taking the current pointer/focus is acceptable. Before judging the input path, select the intended drawing tool and set a visible foreground color; pale/white foreground color can make successful input look blank.
- Use `mac-background draw --file` only for CUA target-routed drawing. Coordinates must be window-local screenshot coordinates, and point files should contain sparse control points rather than dense per-pixel samples.
- Prefer a controlled canvas or simple drawing app and capture before/after CUA screenshots.
- If the target ignores background drag events, report that target behavior exactly instead of converting the claim into general macOS failure.
