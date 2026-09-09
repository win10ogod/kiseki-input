# Operating with Kiseki

Use an executable selected by the skill entrypoint. Run commands from the project root when using repo-relative input files. Set `run_dir` to a new directory for this task, for example `artifacts/live-test/<task-id>`; do not delete an old run to reuse its name.

## Discover and observe

```bash
"$kiseki_exe" target list
"$kiseki_exe" target inspect --target-window-id "$target_id"
"$kiseki_exe" observe ui --target-window-id "$target_id" --provider auto
```

Set `target_id` from current discovery output. Prefer IDs over broad title substrings when more than one window matches. Native target IDs, child receiver IDs, and CUA IDs are different namespaces; discover IDs in the family that uses them.

`observe ui` is structural data, not OCR. Read its `source` and any `fallbackReason`. Explicit `uia` or `ax` requests must not be silently replaced. If a limited observation omits the relevant control, expand the requested depth/elements or inspect the appropriate subtree rather than treating the control as absent. For canvas or rendered visual state, take a screenshot.

```bash
"$kiseki_exe" screenshot window --target-window-id "$target_id" --output "$run_dir/before.bmp" --json
```

For background actions replace this with the matching capture in the mode table. Inspect the image and metadata before deriving click coordinates. BMP-to-PNG conversion is format conversion; keep the original dimensions/metadata available.

## Keyboard, text, and precision

Use `input text --file` for literal UTF-8 content, especially text containing shell metacharacters. Use `input key` / `combo` for physical keys. Ordinary current-session commands need correct foreground focus; target selection on a screenshot does not redirect subsequent `input` commands.

Keep chords, raw down/up, paths, and mixed input in one `input sequence` invocation when they depend on held state. The executor releases what it acquired on normal completion, failure, or cancellation, preserving borrowed modifiers. A raw down sent as a standalone invocation intentionally remains held until an up.

Create a sequence JSON or path file with the actual target coordinates and requested timings, then:

```bash
"$kiseki_exe" macro validate --file "$run_dir/sequence.json"
"$kiseki_exe" input sequence --file "$run_dir/sequence.json"
```

The sequence's referenced file paths are resolved from the process working directory, not the JSON file's directory. The [native input guide](../../../docs/native-input.md) is the schema source: `action`, `holdMs`, `atMs`, click count/interval, button/modifiers, wheel axes, and timed `x y time_ms` path points. Do not replace a precise path with a sparse sample or add blanket sleeps to hide delivery failures.

macOS screenshot metadata supplies origin and pixels-per-unit. Convert pixels with `screen = origin + pixel / scale`; a Retina image may be twice the point dimensions. Windows selected-window actions use client coordinates, while a capture may include window decoration. Establish the client origin/transform; do not use a title-bar pixel as a client coordinate.

## Selected-window and CUA actions

For native selected-window input, use the inspected receiver and a sequence when a button stays down across steps. The sequence retains its acquired target/receiver through title changes and cleanup. Rediscover after explicit release if the next task targets a different window.

For CUA:

```bash
"$kiseki_exe" background cua status
"$kiseki_exe" background cua windows
```

Use the returned PID/window IDs with `state`, `screenshot`, and the requested action. `state` without `--output` is structural inspection; `state --output` also captures. Use current help for provider options. Status inspection does not require launching a new app or enabling feedback. CUA feedback is an overlay, not the real system pointer. Verify actual delivery and cursor/focus behavior when these are part of the request.

## Recover from an observed failure

| Observation | Useful next action |
| --- | --- |
| Title is ambiguous or target vanished | List/inspect targets again; select the matching existing app by identity and state |
| Command succeeded but app state did not change | Check correct target/focus, input family, coordinate transform, enabled control/tool, then target acceptance |
| Text appeared in the terminal | Stop sending input; inspect the foreground window and restore the intended target |
| App opens/saves asynchronously | Observe its readiness or completion state before the next dependent action; keep the user's timeout/timing constraints |
| macOS permission error from SSH | Inspect the actual launch context; use the authorized GUI Terminal/app when required; prompt/open settings only for an observed missing grant |
| Linux screenshot/input fails | Check actual session type, DISPLAY/XAUTHORITY or portal error; an absent SSH DISPLAY is not proof of no desktop |
| Recorder stop times out | Use the teaching control helper to inspect/wait for the same run; never blindly toggle another recording |

If uncertainty remains after an action, verify whether it partially succeeded before retrying. Do not repeat text entry, saves, or other non-idempotent actions merely because a command timed out.
