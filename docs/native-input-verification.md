# Native input repair verification

## Follow-up: pending input state and stable recipients

The five issues reviewed against `e431c0c` were repaired and checked on 2026-09-06, after that change was merged. The Windows, macOS, and native Linux builds passed 97, 97, and 96 CTest cases respectively. The new macOS boundary executable compiles the actual input implementation against delayed system-state reads without posting desktop events; it covers stale key/button state, accumulated movement, external pointer reconciliation, borrowed Ctrl, and partial chord failure. Windows boundary checks now include both overlapping Enter directions and noncoalesced moves. CLI tests cover bound recipients through movement, failure cleanup, explicit up, and later reacquisition.

| Reported defect | Native regression result |
| --- | --- |
| macOS rapid taps disappear; released Shift affects the following key | AppKit and the passive event stream each received all 100 consecutive A down/up pairs. Twenty Shift+B / unmodified C cycles produced all 40 C events with Shift clear. A separate invocation of the built CLI also delivered 100/100 taps, compared with 11/100 in the review reproduction. |
| macOS relative moves read stale positions; split up returns to the old point | 100 consecutive `dx=1` actions moved 100 points, including through the actual CLI sequence (the review CLI moved 1 point). A raw split down/move/up released 180 points from the down, at the last requested position. Standalone CLI Shift-down and button-down remained held after process exit, and subsequent CLI up commands released them. |
| Windows dense mouse paths collapse in a busy receiver | A 100-point drag at 2 ms intervals reached a receiver sleeping 20 ms per motion with all 99 drag movements. The review reproduction received only 8 movements with default coalescing. |
| Windows main Enter prevents a numpad Enter tap | Both orderings delivered all eight down/up events with extended identities `0,1,1,0,1,0,0,1`. The boundary check also verifies the first Enter remains acquired until its own up. |
| Sequence cleanup resolves a changed title again | Native CLI sequences retained the same child receiver when the original title moved to another window, became ambiguous, or the original window became hidden. Each received its matching up. Destroying the recipient produced a nonzero cleanup error. |

The expanded opt-in probe and `test/native/verify_events.py` passed on all three native hosts. Existing split-button drags, timed paths, wheel/side buttons, mixed CLI input, and Windows scan flags still passed. macOS again produced an upright 2× screenshot with all four derived corner clicks received. This run checked 100-key capture in the in-process native event streams; the completed teaching-bundle checks below belong to the preceding repair run.

Full local logs and receiver JSONL are in ignored `artifacts/input-fixes-20260906` and `artifacts/live-test/input-state-{windows,mac,linux}`. The opt-in probe uses owned Win32/AppKit/X11 windows and restores the previous cursor/focus. The live results cover these native system backends and receivers; driver-device and CUA paths were not exercised in this follow-up.

The Quartz implementation uses an independent [private source state table](https://developer.apple.com/documentation/coregraphics/cgeventsourcestateid) and its [event counter](https://developer.apple.com/documentation/coregraphics/cgeventsource/counterforeventtype(_:eventtype:)) to retire pending state. Completion means the source's events have been processed by WindowServer; application behavior is verified separately by the receiver logs.

## Preceding repair verification

The eight issues reviewed against `520fc1d` were checked on native Windows, macOS, and X11 hosts on 2026-09-05/06. WSL was used to run Windows executables and orchestrate SSH; it was not used as a substitute for the native hosts.

| Host | Environment | CTest |
| --- | --- | --- |
| Windows | build 26200, interactive session, non-admin, system input backend | 95 passed |
| macOS | 26.5.2, arm64, authorized GUI Terminal | 94 passed |
| Linux | Ubuntu 24.04.4, aarch64, native X11 session | 94 passed |

## Checks against the reported issues

| Issue | Regression check and observed result |
| --- | --- |
| Partial chord / sequence failure leaves input held | The real Windows input implementation is compiled against deterministic Win32 boundary faults. Failures at each chord down/up boundary attempt every required cleanup; borrowed Ctrl remains held. Portable tests cover sequence backend failure, missing-file preflight, cleanup failure reporting, cancellation, and SIGINT. |
| Split drag loses held state or receiver | Native windows received left/right/middle down–move–up. Windows child-window drags crossing another child retained one receiver and correct `MK_*` flags. AppKit received dragged events for all three buttons. |
| macOS screenshot inversion / missing mapping | A four-color AppKit window produced an upright 1440×984-pixel capture for 720×492 points. Four pixel-derived targets reached the expected positions in the same window. Both scales were 2. |
| Incomplete native input model | Real receiver tests exercised Shift+left drag, Ctrl+right drag, Space held across a CLI mouse sequence, key hold, both wheel axes, and both side buttons. CLI tests check timing/button/modifier/point-field transmission. |
| Lost macro / Linux timing | Macro regression tests retain configured step/start/end durations and timestamped points. X11 received approximately 100 ms uniform gaps and 75/150/75 ms nonuniform gaps. macOS also retained the nonuniform schedule. |
| Windows scan / extended identity | Native arrow down/up carried scan 75 and extended=1. Boundary tests cover navigation, right modifiers, numpad Enter/divide, and ordinary keys that must remain nonextended. |
| macOS double-click state | AppKit received `1,1,2,2` for down/up/down/up. Button events use the requested position directly, avoiding a stale cursor query immediately after asynchronous movement. |
| Recorder loses short taps / blocks during screenshots | Native receiver, in-process event stream, and completed teaching bundle each retained all ten A down/up pairs on every host. Wheel events were present. macOS modifier transitions were checked separately. Recording sources were Windows hooks, macOS event tap, and XRecord; no polling fallback was used in these runs. |

An additional receiver/stream run on every host retained all ten key pairs when the last five taps had no inserted wait.

Completed teaching bundles passed `validate_bundle.py`: Windows captured 21 frames, macOS 14, and Linux 13. Their manifests, frame/action counts, timelines, JSONL, and referenced keyframes validated with no warnings. The macOS capture-coordinate test ran separately from recording so only one ScreenCaptureKit client was capturing at a time.

The receiver checks are in `test/native/verify_events.py`; the interactive probe is opt-in through `KISEKI_BUILD_NATIVE_INPUT_PROBE`. Commands and schema details are in [native-input.md](native-input.md). Local full evidence is retained under ignored `artifacts/live-test/input-fixes-*`; binaries, desktop recordings, and host connection details are excluded from the PR.

The live scope was the native system backend and dedicated Win32/AppKit/X11 windows. It did not cover a driver device, CUA, every application, or a multi-monitor/macOS 1× setup. macOS 2× mapping was measured directly. Platform prerequisites and the explicit incomplete polling fallback remain documented in the command guide.
