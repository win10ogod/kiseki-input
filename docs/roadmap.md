# Kiseki Input Roadmap

Kiseki Input is an agent-first local automation CLI. The roadmap is organized around what an agent needs from a local desktop: target discovery, observation, action backend selection, repeatable execution, verification, and clear failure reporting.

## Current Platform Lines

### Windows

Windows is the primary live-tested platform today.

- CLI configuration, diagnostics, macros, heartbeat notifications, and WebUI configuration.
- Desktop screenshots and burst screenshots.
- Target window listing, target window screenshots, and target window burst screenshots.
- Target inspection for selected windows and child receiver handles.
- Global keyboard and mouse input through the configured backend.
- Direct selected-window input where the selected target accepts the relevant Windows APIs.
- Optional `IbInputSimulator.dll` integration when the DLL can be built and loaded.

### Linux

Linux support currently targets graphical X11 sessions and true-machine validation.

- CLI configuration, diagnostics, macros, heartbeat notifications, and WebUI configuration.
- X11 target listing.
- X11 desktop screenshots, target-window screenshots, and burst screenshots.
- X11/XTest global input where the active session accepts it.
- X11 target-window events where the selected target accepts them.
- Isolated Xvfb background desktops for running, clicking, typing into, and capturing Linux GUI applications on a separate `DISPLAY`.

Wayland and compositor-restricted sessions need their own backend work. WSL-only results are not treated as Linux desktop proof.

## Current Background Strategy

Hyper-V, Virtual Desktop, and Desktop Object are not part of the current implementation track.

Windows keeps background work scoped to observation: `screenshot background-window` captures a selected target window without introducing a separate Windows session backend. Direct selected-window input remains available where an application accepts the same-session Windows APIs, but it is not presented as a universal background operation method.

Linux first uses true background desktop operation: create an isolated X11 desktop with Xvfb, launch applications inside that `DISPLAY`, then use the normal Linux X11 screenshot and XTest input paths against that isolated desktop. This avoids taking over the user's physical Linux desktop session.

## macOS Line

macOS support now has an initial native backend in the codebase, but it is not marked release-grade until it passes real macOS build, permission, and live desktop verification.

The remaining gap is validation and backend expansion, not lack of interest in macOS support. The project should distinguish between compiled backend support and live-verified behavior on a real macOS desktop with the required user permissions.

### macOS Phase 0: Capability Design

- Add macOS to the capability matrix with permission-aware availability.
- Document Screen Recording and Accessibility requirements in `doctor`.
- Keep the CLI shape consistent with Windows and Linux.
- Define which backends are observation, input, target discovery, and app automation backends.

### macOS Phase 1: Foundation Build

- Build the core CLI on macOS.
- Support config, config validation, config-only WebUI, capabilities, doctor, macros, and heartbeat daemon flow.
- Add macOS config path behavior through `~/Library/Application Support/KisekiInput/config.json`.
- Keep operational WebUI routes out of scope.

### macOS Phase 2: Observation Backends

- Desktop and selected-window screenshot backend through ScreenCaptureKit when Screen Recording permission is available.
- Target/window discovery through Window Services in the current user GUI session.
- Burst screenshot path with the same user-facing options as other platforms.
- Clear failure output when Screen Recording permission or a compatible capture backend is missing.

ScreenCaptureKit is the active screenshot backend. Future work should extend it for high-performance streaming and richer capture modes.

### macOS Phase 3: Action Backends

- Global input backend for keyboard and mouse actions through Quartz CGEvent when Accessibility permission is available.
- Accessibility-based app interaction where the target exposes usable UI elements.
- Apple Events or application automation where an app exposes public automation.
- Clear backend selection in `capabilities` and `doctor`.

For agent workflows, structured automation and Accessibility-backed interaction should be preferred when available. Raw coordinate input remains a backend, not the project's identity.

### macOS Phase 4: Targeted Agent Workflows

- Target listing with stable identifiers when available.
- Target-aware screenshots and burst capture.
- Target-aware action routing through the best available backend.
- Macro examples that demonstrate observation, action, verification, and fallback behavior.

## Contribution Notes

macOS contributions are welcome when they include real macOS validation evidence:

- macOS version and hardware type.
- Required permissions granted or missing.
- Build command and test output.
- `kiseki doctor` output.
- Screenshot/input/target-list artifacts when relevant.

The project should stay honest about what has been tested, while keeping macOS as a first-class platform line.
