# Background Window Operations Plan

## Goal

Add target-window background operation support while keeping the project honest about platform boundaries.

Windows will be live-tested. Linux will receive native X11 implementations and normal build/test coverage, but WSL will not be treated as proof of Linux desktop behavior.

## Scope

- Resolve target windows by title substring, PID, or platform window id.
- Capture a specific target window to BMP.
- Capture a burst from a specific target window.
- Send text, key, and mouse messages to a target window without intentionally switching foreground.
- Report unsupported sessions clearly, especially Wayland/no-DISPLAY Linux sessions.

## Boundaries

- WebUI remains configuration-only.
- Background input is message/API based. Some applications ignore synthetic background messages by design.
- The tool does not install hooks, inject into target processes, bypass protected input paths, or make universal guarantees for every game or fullscreen renderer.

## Implementation Tasks

1. Add CLI-facing target option structs and dependency hooks.
2. Add failing CLI tests for:
   - `screenshot window`
   - `screenshot window-burst`
   - `background window text`
   - `background window key`
   - `background window mouse`
3. Add `platform/target`:
   - common `TargetQuery` and `TargetWindow`
   - Windows `EnumWindows` resolver
   - Linux X11 resolver
4. Add target-window screenshot:
   - Windows `PrintWindow`
   - Linux `XGetImage` on target window
   - burst wrapper using the same timing model as desktop burst
5. Add background input:
   - Windows `PostMessageW` to text-like/focused child where possible
   - Linux X11 `XSendEvent`
6. Wire CMake and runtime capabilities.
7. Run CTest.
8. Live-test only Windows background behavior:
   - create/open Notepad target
   - send background text while another window remains foreground
   - capture target window
   - capture 8 frames at 60 FPS from target window

## Verification

- Windows CMake build succeeds.
- Windows CTest succeeds.
- Linux/WSL build or at least CTest succeeds where local dependencies allow.
- Windows live artifacts are written under `artifacts/live-test/`.
