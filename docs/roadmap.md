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
- Optional CUA Driver provider through `background cua` when `cua-driver` is installed in the interactive desktop session.
- Operation mode guidance through `modes --json`, separating current-session input, current-session screenshots, background screenshots, isolated background desktops, and CUA target-routed commands.

### Linux

Linux support currently targets graphical X11 sessions and true-machine validation.

- CLI configuration, diagnostics, macros, heartbeat notifications, and WebUI configuration.
- X11 target listing.
- X11 desktop screenshots, target-window screenshots, and burst screenshots.
- X11/XTest global input where the active session accepts it.
- X11 target-window events where the selected target accepts them.
- Isolated Xvfb background desktops for running, clicking, typing into, and capturing Linux GUI applications on a separate `DISPLAY`.
- Optional CUA Driver provider through `background cua`; upstream CUA currently marks Linux as pre-release while platform testing continues.

Wayland and compositor-restricted sessions need their own backend work. WSL-only results are not treated as Linux desktop proof.

### macOS

macOS support is split into two explicit paths.

- Native current-session backend: config, diagnostics, config-only WebUI, target listing, ScreenCaptureKit screenshots, burst screenshots, and Quartz CGEvent global input.
- Optional CUA background provider: background app launch, CUA window listing/state, per-window screenshot, targeted click/text/key/hotkey/drag, point-path drawing, and configurable agent-cursor visual feedback.
- Native input commands can affect the active GUI session and real pointer/focus. CUA commands live under `background cua`.
- Both paths depend on the user's macOS permissions: Screen Recording for capture and Accessibility for input/action routing.

## Current Background Strategy

Hyper-V, Virtual Desktop, and Desktop Object are not part of the current implementation track.

Windows native background work remains scoped to observation: `background window screenshot` captures a selected target window without introducing a separate Windows session backend. Direct selected-window input remains available where an application accepts the same-session Windows APIs, but it is not presented as a universal background operation method. CUA Driver is a separate optional provider line exposed through `background cua`.

Linux first uses true background desktop operation: create an isolated X11 desktop with Xvfb, launch applications inside that `DISPLAY`, then use the normal Linux X11 screenshot and XTest input paths against that isolated desktop. This avoids taking over the user's physical Linux desktop session.

CUA Driver is the cross-platform optional provider line for background app operation. Kiseki keeps the native Windows, Linux, and macOS paths for baseline platform support, and exposes CUA through `background cua` for agent workflows that need background launch, per-window observation, and target-routed actions.

## CUA Driver Line

CUA support is optional and runtime-detected. Installing `cua-driver` should not become a build requirement for Kiseki.

- Preferred command surface: `kiseki background cua ...`.
- Detect `cua-driver` from `PATH` or `KISEKI_CUA_DRIVER`; platform-specific default install locations are probed where known.
- Windows support requires CUA's installed driver in an interactive desktop session.
- Linux support follows upstream CUA's pre-release status and needs true graphical Linux validation, not WSL-only proof.
- macOS support still requires Accessibility and Screen Recording permissions.
- Public claims should distinguish CUA binary detection, CUA permission/status output, and live-verified target action artifacts.

## macOS Line

macOS support is a first-class platform line with native current-session support and an optional CUA background provider. The project distinguishes compiled support, permission availability, and live-verified behavior on a real logged-in macOS desktop.

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

### macOS Phase 5: CUA Background Provider

- Detect `cua-driver` from `PATH`, `KISEKI_CUA_DRIVER`, or `/Applications/CuaDriver.app/Contents/MacOS/cua-driver`.
- Expose CLI-only `background cua` commands for CUA permission status, background app launch, window listing, window state, screenshot, click, text, key, hotkey, drag, point-path drawing, and agent-cursor feedback.
- Keep CUA as an optional provider, not a hard dependency, so the native macOS backend remains usable without it.
- Continue validating on real macOS hardware with Accessibility and Screen Recording grants before expanding public claims or demos.

## Contribution Notes

macOS contributions are welcome when they include real macOS validation evidence:

- macOS version and hardware type.
- Required permissions granted or missing.
- Build command and test output.
- `kiseki doctor` output.
- Screenshot/input/target-list artifacts when relevant.

The project should stay honest about what has been tested, while keeping macOS as a first-class platform line.
