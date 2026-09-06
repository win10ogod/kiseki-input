# Native input repair verification

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
