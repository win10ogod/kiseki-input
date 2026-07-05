# Drawing App Operation Guide

Use this reference when operating Paint, Krita, GIMP, Photoshop-like apps, canvas apps, DAWs with visual editors, or any target where the task is "draw", "paint", "sketch", "set color", "use brush", "use canvas", or "show a drawn result".

The goal is reliable agent operation, not a human-style tutorial. Follow the phases in order. Weak models must not skip phases.

## Core Rule

Do not send a draw path until these facts are known:

- target process/window id
- operation mode: foreground/global, Windows selected-window helper, Linux background desktop, or CUA target-routed
- coordinate space: screen coordinates or window-local coordinates
- canvas bounds or a conservative canvas point range
- active drawing tool
- visible foreground color and opacity
- before-screenshot artifact path

A successful CLI return code is not proof that drawing changed the canvas. Proof requires an after-screenshot from the same target and a visible pixel change.

## Mode Selection

Choose one mode and keep it explicit in notes and commands:

- Foreground/global: use `kiseki input ...`. Coordinates are screen coordinates. This can move the real pointer and depends on active focus. Verify with `screenshot desktop` or `screenshot window`. Use it for dense strokes and visible desktop demonstrations.
- Windows selected-window helper: use `background window text|key|mouse|drag` only for apps that accept normal window messages. Verify with `background window screenshot`. Do not expect it to control raw canvas/game input.
- Linux isolated background: use `background desktop ...` on an Xvfb display. Coordinates are the virtual display's screen coordinates. Verify with `background desktop screenshot`.
- CUA target-routed: use `background cua ...`. Coordinates for `--window-id` actions are window-local screenshot coordinates. Verify with `background cua screenshot` or `background cua state --output`.

For weak-model handoff, run `kiseki modes --json` before choosing a path. Do not verify background drawing with `screenshot desktop`; it captures the current visible desktop, not necessarily the background target.

Do not mix screen coordinates with window-local coordinates. Convert AX/window screen bounds to CUA local coordinates with:

```text
local_x = screen_x - target_window_x
local_y = screen_y - target_window_y
```

## Observation Order

Start every drawing task with target discovery:

```bash
kiseki target list
kiseki observe ui --target-window-id <id> --provider auto --max-depth 4 --max-elements 256
```

Then capture a before image from the same mode that will execute the action:

```bash
kiseki screenshot window --target-window-id <id> --output artifacts/live-test/draw-before.bmp
kiseki background window screenshot --target-window-id <id> --output artifacts/live-test/draw-before-background.bmp
kiseki background desktop screenshot --display :99 --output artifacts/live-test/draw-before-xvfb.bmp
```

For CUA target-routed operation:

```bash
kiseki background cua windows
kiseki background cua screenshot --window-id <window_id> --output artifacts/live-test/draw-before.png
```

Use AX/UIA for controls such as toolbar buttons, color wells, sliders, menus, status text, and tool names. Use screenshots for canvas contents; canvas pixels are usually not exposed as AX/UIA elements.

## Tool And Color Preparation

Treat tool and color as required state, not decoration.

Before drawing:

- Select a freehand brush, pencil, pen, or shape tool that creates visible marks.
- Set opacity/alpha to fully visible or a clearly visible value.
- Set brush size wide enough to survive screenshot compression and scaling; prefer 5 px or larger for tests.
- Set foreground color to a high-contrast color against the canvas, such as black, red, blue, or green.
- Verify the current color is not white, transparent, or the same as the canvas background.
- If color cannot be verified, stop and report "color not verified" instead of treating a blank canvas as input failure.

For weak models, use this exact decision rule:

```text
If after-screenshot is blank:
1. Check whether the tool was selected.
2. Check whether color/opacity was visible.
3. Check whether points were inside the canvas.
4. Check whether coordinate space was wrong.
5. Only then suspect the input backend.
```

## Path Design

Use simple closed shapes for tests: heart, triangle, square, spiral, or zig-zag.

Foreground/global `input drag` can use dense paths. Use delay and hold time when drawing apps drop fast mouse events:

```bash
kiseki input drag --file artifacts/live-test/shape-points.txt --backend system --step-delay-ms 6 --start-hold-ms 80 --end-hold-ms 40
```

`background cua draw` should use sparse control points. It sends segments through CUA and protects against excessive segment counts:

```bash
kiseki background cua draw --pid <pid> --window-id <window_id> --file artifacts/live-test/shape-local-points.txt --duration-ms 160 --steps 10 --max-segments 128
```

Do not feed dense per-pixel paths into CUA draw unless `--max-segments` is intentionally raised and the target has already been tested.

## Windows Paint Foreground Recipe

Use this for visible Windows drawing tests:

```bash
mkdir -p artifacts/live-test
./build/Debug/kiseki.exe input combo --keys win+r --backend system
./build/Debug/kiseki.exe input text --text "mspaint.exe"
./build/Debug/kiseki.exe input key --key enter --backend system
./build/Debug/kiseki.exe target list
./build/Debug/kiseki.exe observe ui --target-title "Paint" --provider auto --max-depth 4 --max-elements 256
./build/Debug/kiseki.exe screenshot desktop --output artifacts/live-test/paint-before.bmp
```

Then select/confirm a visible tool and color. If uncertain, use the UI or keyboard to choose a brush/pencil and a dark foreground color before drawing.

Create points inside the canvas, not over the ribbon:

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

Run:

```bash
./build/Debug/kiseki.exe input drag --file artifacts/live-test/heart-points.txt --backend system --step-delay-ms 6 --start-hold-ms 80 --end-hold-ms 40
./build/Debug/kiseki.exe screenshot desktop --output artifacts/live-test/paint-after.bmp
```

Report success only if `paint-after.bmp` visibly contains the shape.

## macOS Krita Recipe

Use this for macOS drawing app tests:

```bash
./build/kiseki target list
./build/kiseki observe ui --target-title "Krita" --provider ax --max-depth 4 --max-elements 256
./build/kiseki screenshot window --target-title "Krita" --output artifacts/live-test/krita-before.bmp
```

AX can identify tool buttons and status text. It may show the selected freehand brush as an `AXCheckBox` with a description like a brush/freehand tool and value `1`. If the expected drawing tool is not selected, select it before drawing. If using CUA clicks from AX bounds, convert screen bounds to window-local coordinates.

For foreground drawing, use screen coordinates:

```bash
./build/kiseki input drag --file artifacts/live-test/krita-screen-points.txt --backend system --step-delay-ms 8 --start-hold-ms 100 --end-hold-ms 60
./build/kiseki screenshot window --target-title "Krita" --output artifacts/live-test/krita-after.bmp
```

For CUA target-routed drawing, first get CUA identifiers and a CUA screenshot:

```bash
./build/kiseki background cua status --prompt
./build/kiseki background cua windows
./build/kiseki background cua screenshot --window-id <window_id> --output artifacts/live-test/krita-cua-before.png
```

Create window-local points inside the canvas area from the CUA screenshot, then run:

```bash
./build/kiseki background cua draw --pid <pid> --window-id <window_id> --file artifacts/live-test/krita-local-points.txt --duration-ms 180 --steps 10 --max-segments 128
./build/kiseki background cua screenshot --window-id <window_id> --output artifacts/live-test/krita-cua-after.png
```

If CUA reports success but the screenshot is unchanged, classify the result using the failure checklist below. Do not claim background drawing support from return code alone.

## Failure Checklist

When drawing fails or looks blank, record which checks passed:

- Target window: correct PID/window id/title.
- Permission: Accessibility and Screen Recording where required.
- Focus/mode: foreground input had focus, or CUA target ids were correct.
- Coordinates: points fall inside the visible canvas and use the right coordinate space.
- Tool: brush/pencil/freehand/shape tool selected.
- Color: visible high-contrast foreground color and nonzero opacity.
- Timing: hold/delay/duration slow enough for the app to accept drag events.
- Screenshot: before and after were captured from the same target and same display/session.

Only after these checks pass should the backend be suspected.

## Reporting Template

Use this shape in final reports:

```text
Mode: foreground/global | selected-window helper | Linux Xvfb | CUA target-routed
Target: pid=<pid>, window=<id>, title=<title>
Coordinate space: screen | window-local
Tool/color: verified | not verified
Before screenshot: <path>
Action command: <command summary>
After screenshot: <path>
Result: visible change | no visible change | blocked
Reason if blocked/unchanged: <specific checklist item>
```

Do not omit `tool/color` or screenshot paths.
