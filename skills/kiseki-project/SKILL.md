---
name: kiseki-project
description: Use when working inside the Kiseki Input repository on CLI commands, configuration WebUI, screenshot capture, keyboard or mouse input, heartbeat notifications, platform backends, live Windows tests, Linux true-machine tests, build/test verification, or project documentation.
---

# Kiseki Project

## Overview

Kiseki Input is a pure C++ CLI with an embedded local WebUI for configuration only. Operational capabilities are CLI-only: screenshots, input simulation, notifications, daemon mode, capabilities, and diagnostics.

## First Rules

- Keep WebUI configuration-only. Do not add input, screenshot, shell, daemon, or execution routes to the WebUI.
- Prefer small platform slices: input, screenshot, notification, config, WebUI, and CLI wiring should remain separable.
- On Windows, use the Windows executable for live UI verification from WSL; WSL is only the orchestration shell.
- Do not claim Linux support from WSL-only results. Linux screenshot/input must be tested on a real Linux graphical session when the user asks for Linux proof.
- Do not overclaim game background input. It is not guaranteed for Raw Input, DirectInput, protected fullscreen, or anti-cheat protected games.

## Project Anchors

- Repo in WSL: `/mnt/f/輝色臻至/項目本體`
- Repo in Windows: `F:\輝色臻至\項目本體`
- Windows executable: `build/Debug/kiseki.exe`
- Live-test artifacts: `artifacts/live-test/`
- Windows IbInputSimulator source: `F:\輝色臻至\原始參考\IbInputSimulator`

## Quick Verification

```bash
"/mnt/c/Program Files/CMake/bin/cmake.exe" --build build
"/mnt/c/Program Files/CMake/bin/ctest.exe" --test-dir build --output-on-failure
```

Before final claims, also run `git diff --check` after edits. For live Windows UI work, capture screenshots under `artifacts/live-test/` and report the paths.

## References

- Full feature map: `references/features.md`
- Architecture and extension points: `references/architecture.md`
- Build, unit tests, and live UI recipes: `references/testing.md`
