# Background Desktop And Background Screenshot Design

## Goal

Implement the current practical background track:

- Linux uses an isolated X11 background desktop.
- Windows uses selected-window background screenshots.

## Scope

Linux background desktop is backed by `Xvfb`. Kiseki starts a separate `DISPLAY`, launches applications into it, and routes screenshot/input commands to that display. The physical Linux desktop is not used for those actions.

Windows selected-window observation resolves a target HWND, can inspect child receiver windows, and captures the selected target through `background window screenshot` without introducing a VM, Docker, or separate Windows session backend. Message-based selected-window input remains a selected-window helper where the selected application accepts normal Windows APIs.

## Non-Goals

Hyper-V, VM, Docker, Virtual Desktop, and Desktop Object are not part of the current Windows background screenshot track.

Windows direct selected-window operation is not universal canvas/raw-input control. Chromium canvas, Blender, Figma, DAWs, game engines, protected fullscreen, Raw Input, and DirectInput targets may ignore message-based input. Those limits are reported as backend behavior, not project identity.

WSL-only behavior is not treated as Linux desktop proof. Linux background desktop validation requires a real Linux host with X11 libraries and `Xvfb`.

## Commands

```text
kiseki target inspect --target-title <text>
kiseki target inspect --target-window-id <id>

kiseki background desktop start --display :99 --width 1280 --height 720 --depth 24
kiseki background desktop stop --display :99
kiseki background desktop launch --display :99 --command <shell-command>
kiseki background desktop screenshot --display :99 --output <file.bmp>
kiseki background desktop text --display :99 --text <text>
kiseki background desktop text --display :99 --file <utf8-file>
kiseki background desktop key --display :99 --key <name>
kiseki background desktop mouse --display :99 --x <n> --y <n> --click <button>
```

## Architecture

`src/platform/session/background_desktop.*` owns the Linux background desktop lifecycle and action routing. It starts `Xvfb`, tracks the PID in a state directory, launches commands with `DISPLAY` set, and scopes `DISPLAY` while reusing the existing X11 screenshot and input backends.

`src/platform/target/target.*` owns target inspection. Windows inspection returns the selected HWND plus child HWNDs, class names, bounds, and titles. Linux inspection uses the X11 child tree where available.

`src/cli/app.*` exposes both through dependency-injected CLI commands so tests can verify routing without invoking real OS input.

## Verification

Automated Windows build and CTest cover command routing and capability output. Linux X11 branches are also syntax-checked in WSL with `g++ -DKISEKI_HAS_X11=1`, but real background desktop proof must be gathered on a true Linux graphical host.
