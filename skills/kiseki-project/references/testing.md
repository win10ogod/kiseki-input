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
./build/Debug/kiseki.exe input drag --file artifacts/live-test/heart-points.txt --backend system
./build/Debug/kiseki.exe screenshot desktop --output artifacts/live-test/paint-heart.bmp
```

Use `input drag` instead of separate `mouse left-down`, move, and `left-up` commands for drawing tests, because one process preserves button state more reliably.

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
