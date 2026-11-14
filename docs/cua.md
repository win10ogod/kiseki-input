# Native CUA integration

Kiseki uses the native Cua Driver on macOS, Linux, and Windows. CUA remains a runtime provider: the C++ build and native Kiseki commands do not need a Driver installation. The [installation guide](install.md) covers installers and first-run setup. The convenience commands are checked against **Cua Driver 0.49.9**; `tools`, `describe` and `call` reach whatever the installed Driver offers.

## Begin a workflow

```sh
kiseki background cua setup
kiseki background cua status
kiseki background cua windows
```

`setup`, `windows`, and `launch` are workflow boundaries. They install a missing official Driver, check for an update when the automatic check is due and apply it, and make sure the daemon runs. Input commands, state, screenshots, and raw tool calls do not update or restart the provider. After a workflow boundary that updates CUA, acquire fresh state before any element action.

Update cadence: an automatic check runs at most once per 24 hours for an installed binary and version, and uses the Driver's own one-hour cache (`check-update --json`). A successful check is recorded in Kiseki's `update-state.json`; a failed check is not recorded, stays visible in `warnings`, and is retried at the next workflow start. An installed provider stays usable when a check fails. `background cua update` checks now, bypassing the cache (`--no-cache`); `--apply` installs an available update. The selected stable/nightly channel is retained; after `driver -- channel set ...` run `update --apply`.

`setup` always checks the daemon. `windows` and `launch` skip that probe when nothing was installed or replaced: their call starts the daemon (through setup) only when it reports that no daemon is running, and is then repeated once — nothing ran the first time.

### What a Kiseki-started install or update changes

Kiseki runs the official installers, with these opt-outs unless you set the variable yourself:

| Effect of the official installer | Kiseki-started install/update |
| --- | --- |
| `cua-driver connect` registers the Driver with Claude Code, Codex, Cursor and Factory Droid | Skipped: `CUA_DRIVER_NO_CONNECT=1` |
| `cua-driver test` operates an app on the live desktop | Skipped: `CUA_DRIVER_NO_TEST=1` |
| Appending a PATH block to the shell startup file | Updates skip it (`CUA_DRIVER_NO_MODIFY_PATH=1`); a first install may add it once |
| Windows: logon task `cua-driver-serve` at RunLevel=Highest (UAC prompt) | Not registered. Install and update of the default per-user layout run `install.ps1 -NoAutoStart`; an existing task is refreshed by the installer |

`background cua driver -- update --apply` runs the Driver's own updater with its defaults. On macOS/Linux the official installer stops every `cua-driver` process, including the `update --apply` that started it; Kiseki therefore judges an update by the version installed afterwards and, when the updater was stopped by a signal, waits for the installer to finish. The setup report shows `previous_version`, `version` (installed now), `update.status` (`not_due`, `up_to_date`, `applied`, `check_failed`, `apply_failed`, `skipped`) and, after an update, `daemon_version` as the running daemon reports it.

### Daemon ownership

`status` is read-only. On macOS, `status --prompt` requests CuaDriver's Accessibility/Screen Recording grants. macOS startup opens the installed app bundle so its identity owns those grants. Windows/Linux startup launches the provider in the caller's desktop session. A custom daemon endpoint is used as configured and must already be running. Kiseki does not add permission-mode flags or replace the user's provider policies.

Kiseki records the daemon it starts (`daemon.json`: pid, binary, the launched binary's version and, on Linux, DISPLAY/WAYLAND_DISPLAY/DBUS_SESSION_BUS_ADDRESS/XDG_RUNTIME_DIR). It restarts only that daemon, when the installed version or (Linux) the desktop session changed. A daemon started by an installer, an autostart task or the user is reported as `daemon_owner: "external"`, the stale record is removed, and it is left running; on Linux a mismatched session bus or missing X11 display of such a daemon is reported as a warning. A symlinked launcher (`~/.local/bin/cua-driver`) is resolved to its target on macOS/Linux so every shell uses one binary.

| Environment variable | Meaning |
| --- | --- |
| `KISEKI_CUA_DRIVER` | Exact executable override; invalid paths fail instead of falling back; not automatically updated |
| `KISEKI_CUA_AUTO_UPDATE=0` | Disable automatic updates, retaining manual update |
| `KISEKI_CUA_SESSION` | Named session used by the convenience state/action/feedback commands; default `kiseki` |
| `KISEKI_CUA_SOCKET` | Explicit provider socket/named pipe, forwarded to tool calls; owner manages lifecycle and updates |
| `KISEKI_CUA_STATE_DIR` | Override Kiseki's setup lock, update record, daemon ownership and log directory |
| `CUA_DRIVER_RS_VERSION` / `CUA_DRIVER_VERSION` | Official installer pin; automatic updates preserve it |
| `CUA_DRIVER_NO_CONNECT` / `CUA_DRIVER_NO_TEST` / `CUA_DRIVER_NO_MODIFY_PATH` | Installer opt-outs; Kiseki sets them to `1` only when unset |

Normal state locations are `%LOCALAPPDATA%\Kiseki\cua` (Windows), `~/Library/Application Support/Kiseki/cua` (macOS), and `$XDG_STATE_HOME/kiseki/cua` or `~/.local/state/kiseki/cua` (Linux); files are private to the user. `setup --startup-wait-ms` controls only the readiness wait, not tool duration, response size, input timing, or path detail. A readiness timeout reports the next status/log check; it does not relaunch an action.

## Observe and act with the same session and snapshot

Use a distinct session name for independent concurrent workflows. Keep it stable across CLI invocations. For example, in Bash:

```sh
export KISEKI_CUA_SESSION=kiseki-editing-example
kiseki background cua session start
kiseki background cua state --pid 123 --window-id 456 --output before.png
```

Replace the PID/window ID with values from discovery. The state answer carries `snapshot_id`; with the default `tree_format: "markdown"` each actionable row is tagged `[N]`, and its element token is `<snapshot_id>:N` (for example `s0000002a:11`). `--tree-format elements` returns each row's `element_token` and frame explicitly. Driver 0.49 accepts elements only as `element_token`; Kiseki sends `--element-index N --snapshot-id S` as the token `S:N` and rejects an index without its snapshot, or one that disagrees with `--element-token`, before anything is sent. Retain IDs exactly; Windows native window IDs are 64-bit integers.

```sh
kiseki background cua click --pid 123 --window-id 456 --element-token s0000002a:7
kiseki background cua text --pid 123 --window-id 456 --element-index 3 --snapshot-id s0000002a --text "Example"
kiseki background cua hotkey --pid 123 --keys ctrl+end --element-token s0000002a:0
```

`text` with an element token writes through the element's value interface; read the whole value back afterwards. On Windows 0.49.9, writing into a non-empty Notepad document inserted the text at the caret while the result said `confirmed`.

A newer read of the same window stales the previous tokens (the answer lists `invalidated_snapshot_ids`); a screenshot-only read (`state --no-tree --output`, `screenshot`) keeps them valid. Re-observe after changes and use the new tokens; `state --since SNAPSHOT_ID` (or `latest`) returns only what changed and a new `snapshot_id`.

A provider session ends after five minutes without use or by `end_session`; the provider then rejects calls naming it. Kiseki starts the session you named again and repeats the call once, unchanged (Driver 0.49 and earlier wording); it never revives a session you did not name, and a failing `start_session` is reported together with the original error. Each CLI call is its own transport connection, so `session list` (`list_sessions`) does not show sessions started by earlier calls; `session get` reads one by name.

### State, screenshots and the coordinate frame

Driver 0.49 bounds a read by default: 250 elements, a one-second tree walk, and the configured screenshot long-edge cap (`max_image_dimension`, for example 1366 or 1568). Kiseki adds no caps of its own and exposes the provider's controls:

| `state` option | Provider field |
| --- | --- |
| `--max-elements N`, `--max-depth N`, `--timeout-ms N`, `--query TEXT` | `max_elements`, `max_depth`, `timeout_ms`, `query` |
| `--max-image-dimension N` (`0` = native resolution) | `max_image_dimension` |
| `--tree-format markdown\|elements\|both`, `--full-output`, `--verbose` | `tree_format`, `full_output`, `verbose` |
| `--since SNAPSHOT_ID\|latest` | `since` |
| `--no-tree` (needs `--output`) | `include_accessibility_tree: false` |
| no `--output` | `include_screenshot: false` (structural read; no inline base64 image) |

stdout is the provider's answer unchanged. Kiseki adds a note on stderr when the tree is truncated, and when the screenshot is reduced relative to the window: x/y actions use the pixels of the window's latest capture, and the effective ratio is `screenshot_width / window_bounds.width`. A reported `screenshot_scale` is the display's backing scale, not the image's ratio after the cap (observed: a 1000×700 pt window captured as 1366×956 px while `screenshot_scale` said 2.0). Use `--max-image-dimension 0` when precision needs native pixels. x/y actions need a pixel frame: make `state --output` or `screenshot` the latest read of that window before them. A structural read (`state` without `--output`) leaves no frame, and a window never captured or a restarted daemon has none; the action is then refused with `screenshot_context_missing` and nothing happens (verified on Windows 0.49.9).

`background cua screenshot --window-id ID [--pid PID] --output FILE` reads only the image (`include_accessibility_tree: false`); `--pid` avoids an unfiltered `list_windows` lookup. The output name decides the encoding unless `--format` is given (a mismatch is rejected). JPEG uses `get_window_state`'s `screenshot_format: "jpeg"` and `--quality` as `screenshot_quality`; Driver 0.49.9 offers this on macOS, not on Windows or Linux, where Kiseki reports that JPEG is unavailable and writes nothing. `state --output FILE.jpg` behaves the same way. A file named `.jpg` must hold JPEG bytes and one named `.png` must not; every requested output is validated before any earlier file is replaced.

## Reach current and new provider features

```sh
kiseki background cua tools
kiseki background cua describe click
kiseki background cua call click --file request.json
kiseki background cua driver -- channel status --json
```

`tools` and `describe` use the installed native provider's tool registry and schemas. `call` accepts either `--json` or `--file`, preserves arbitrary tool fields, and returns the provider's structured stdout. `--output` saves the first returned image. It does not insert session fields into arbitrary raw calls: supply a named `session` when the tool accepts it. The bridge adds no timeout, response cap, tree limit, image cap, or action retry; the provider's own defaults apply unless the request sets them.

Arguments go through native process creation and stdin, avoiding shell interpretation of JSON, text, quotes, `%PATH%`, or metacharacters. stdout and stderr remain separate; failure output and native exit codes are retained (a process ended by a signal reports 128 + signal). Driver 0.37+ answers carry `hint` and `next_call` recovery fields; they stay in the output. Inspect the provider's effect/readback fields and a subsequent application observation before treating a submitted action as complete.

Convenience `state`, `click`, `text`, `key`, `hotkey`, `drag`, and `draw` accept `--provider-json` for additional native fields; conflicts with fields already set by the convenience command are rejected before dispatch. The action commands also take `--delivery-mode background|foreground` as an explicit choice; Kiseki never picks or changes it. `text` sends `--delay-ms` only when given (Driver 0.39+ types in batches by default). `press_key` names its modifier field `modifiers`; `click` and `drag` use `modifier` — Kiseki maps `--modifiers` accordingly. `launch` takes `--aumid` and `--path` for Windows `launch_app`; `--new-instance` is accepted on macOS, a no-op on Windows, and refused by the Linux 0.49.9 schema. When the provider refuses an unknown field, Kiseki points to `describe TOOL`.

```sh
kiseki background cua state --pid 123 --window-id 456 --max-image-dimension 0 --timeout-ms 5000 --output native.png
kiseki background cua click --pid 123 --window-id 456 --x 20 --y 30 --provider-json '{"capture_id":"CURRENT_CAPTURE"}'
```

`capture_id` identifies retained image evidence and is separate from accessibility `snapshot_id`. Keep the same daemon endpoint and named session through capture, action and verification. A refused capture-bound action must not be retried after stripping its capture ID. Native `effect: refused` results, including structured result envelopes, produce a nonzero Kiseki exit code. `submitted` and `unverifiable` remain visible as those exact effects; they are not relabeled as confirmed.

An answer that is only a short code saying the action could not be delivered (`{"code":"background_unavailable"}`, `{"code":"screenshot_context_missing",...}`, or another code ending in `_unavailable`, `_refused`, `_denied`, `_failed` or `_missing`, with no `effect`) is also a refusal: nothing was done, and Kiseki exits nonzero. Seen on Cua Driver 0.28.1 with Krita 5.3.1 on macOS 26: a `drag` into the canvas in background mode answers `background_unavailable`, and a Qt dialog's `state` is degraded (`ax_window_unresolved`, no elements) with background input refused. Such targets can be driven in foreground mode only when the user allows it; it uses global input, moves the real pointer and needs the target in front.

```sh
kiseki background cua call bring_to_front --json '{"pid":123,"window_id":456}'
kiseki background cua drag --pid 123 --window-id 456 --from-x 400 --from-y 300 --to-x 800 --to-y 500 --delivery-mode foreground
```

In foreground mode the driver cannot always read the effect back (`effect: unverifiable`); take a new `state` picture to see what happened.

### Drawing

`draw` sends each segment of the point path as its own drag (press, move, release); a polyline is a series of strokes, not one held stroke. With Driver 0.48+ `run_steps`, up to 64 segments go in one call and `--stroke-gap-ms` (≤ 2000 ms) is the pause between them, so the timing between segments is the requested gap rather than a process start per segment. Points and per-segment parameters are sent exactly; when the point file gives `time_ms` for every point, each segment lasts the time between its two points (`--duration-ms` then conflicts). Without `run_steps`, or with a gap above 2000 ms, Kiseki sends one `drag` call per segment. The first refused segment stops the path and is named. Linux 0.49.9 also offers `mouse_button_down`, `mouse_drag` and `mouse_button_up` (X11 background delivery) for a held stroke through `call`; Windows 0.49.9 does not list them.

### Agent-cursor feedback

`feedback status|enable|disable|motion|style|preset` act on the session named by `KISEKI_CUA_SESSION`; the overlay is not the system pointer. `enable` shows it (`--enabled false` or `disable` hides it). `motion` takes `--style`, `--timing`, `--turn-radius` and repeatable `--effect NAME=on|off|default` (trail, glow, magnet, ripple, squish) besides the tuning knobs. Settings belong to that live session: on Windows 0.49.9 they persisted across CLI calls while the session lived; on Linux 0.49.9 they were observed not to persist between calls. Saved defaults for new sessions are Driver configuration: `background cua config -- set cursor.motion.style comet_swoop`.

CUA 0.25 replaced custom cursor colors (`set_agent_cursor_style`) with installed themes. On such Drivers `feedback style` reports this with the next step, and `preset natural|fast|recording` applies visibility and motion only and says that colors were not applied; `quiet` hides the cursor. Themes:

```sh
kiseki background cua cursor-theme -- list
kiseki background cua theme --json '{"theme_id":"cua.default"}'
```

End the task's session when finished:

```sh
kiseki background cua session end
```

## Cua Driver 0.39–0.49 changes

| Driver | Change | In Kiseki |
| --- | --- | --- |
| 0.39 | `element_index`/`snapshot_id` removed from `click`, `type_text`, `press_key`, `hotkey` (`additionalProperties: false`); token is `<snapshot_id>:<row>` | Index + snapshot sent as that token; `hotkey` takes element options |
| 0.39 | JPEG screenshots in `get_window_state` (macOS: `screenshot_format`, `screenshot_quality`) | `screenshot --format jpeg`, `state --output x.jpg`; precise refusal elsewhere |
| 0.39 | Batched `type_text` by default (`delay_ms` 0) | `text --delay-ms` sent only when given |
| 0.39–0.44 | `get_window_state` bounded by default, markdown tree, `since` diff reads, `full_output`, `display_only`, configured `max_image_dimension` | `state` options above; stderr notes for truncation and reduced frames |
| 0.37, 0.49 | Refusals carry `hint`/`next_call` and say what works instead | Kept in output; Kiseki adds daemon/setup and capture hints |
| 0.41 | `cua-driver test` and `connect` run by installers | Kiseki-started installs/updates opt out |
| 0.48 | `run_steps` (≤ 64 steps, `delay_ms` ≤ 2000); `escalate_session` and `get_session_state` removed | `draw` batches; `session state` verb removed (`session get` remains) |
| 0.49 | Ended-session message names the session differently | Revival recognises both wordings |
| — | `list_windows` returns `windows` (with `app_name`, `bounds`, `z_index`) and `_legacy_windows`; `app` filter | `windows --app TEXT` |

Earlier changes still in effect: 0.34 motion styles (`feedback motion --style`, saved defaults via `config`); 0.36 macOS needs the daemon from CuaDriver.app (`background cua health` checks the WindowServer connection); 0.37 `browser_select` (`browser select`) and `get_guide` (`guide`); 0.38 a macOS `.app` path as `launch --launch-path` is sent as `name`.

The forwarded families (`recording`, `extension`, `cursor-theme`, `cursor`, `manifest`, `doctor`, `skills`, `history`, `perception`, `config`) pass their arguments to `cua-driver` unchanged; put flags after `--`, for example `kiseki background cua skills -- status --json`. `health` and `guide` call `health_report` and `get_guide`; neither takes a session. Run `health` first when a workflow does nothing.

### Browser semantic snapshots

The `browser` family exposes `state`, `prepare`, `navigate`, `click`, `type`, `dialog`, `upload`, `download`, `pointer`, and `select`. Each accepts the provider's full `--json` or `--file` request and optional `--output` image destination. These commands use `KISEKI_CUA_SESSION` when the request omits `session`; an explicit session is preserved.

```sh
kiseki background cua browser state --json '{"pid":123,"window_id":456}'
kiseki background cua browser state --json '{"target_id":"RETURNED_TARGET","tab_id":"RETURNED_TAB","snapshot_format":"semantic_v2"}'
```

First bind the exact native browser window, then use its returned target and tab IDs. `semantic_v2` supplies an outline, typed action refs, content refs, visibility and continuation. Preserve `continuation`, `query` and `scope_ref` as returned/required by the current schema; a partial snapshot is not evidence that omitted controls are absent. Browser preparation is an explicit operation and is not automatically performed by state inspection.

### Visual regions, recording and extensions

```sh
kiseki background cua visual-regions --json '{"capture_id":"CURRENT_CAPTURE","options":{"kinds":["text","icon"]}}'
kiseki background cua extension -- list --json
kiseki background cua recording -- start trajectory-directory
kiseki background cua recording -- stop
```

`visual-regions` passes the capture-bound `parse_visual_regions` request with the same named session as capture. The optional perception extension is a separately installed preview; this bridge neither installs it implicitly nor replaces its `not_installed` or capture-validity result. Driver trajectories record agent operations and are distinct from Kiseki's human demonstration `teach record` bundles.

## Updating and service ownership

Run setup before a new observation/action workflow after `update --apply`. A daemon previously started by Kiseki is restarted when its recorded binary version changes. For a daemon launched separately with custom flags, retain those flags when restarting it; Kiseki does not reconstruct or silently replace an external launch configuration. Provider/package-manager installation errors remain visible.

Upstream changes can alter a tool schema. `describe` and the raw call path provide the current contract; legacy tool failure does not trigger a retry through a different input backend. The native Kiseki input and screenshot families remain separate from CUA target routing.

Official references: [installation](https://cua.ai/docs/how-to-guides/driver/install), [updating](https://cua.ai/docs/how-to-guides/driver/update), [CLI reference](https://cua.ai/docs/reference/cua-driver/cli-reference), [platform support](https://cua.ai/docs/reference/cua-driver/platform-support).
