# Native CUA integration

Kiseki uses the native Cua Driver on macOS, Linux, and Windows. CUA remains a runtime provider: the C++ build and native Kiseki commands do not need a Driver installation. The [installation guide](install.md) covers installers and first-run setup.

## Begin a workflow

```sh
kiseki background cua setup
kiseki background cua status
kiseki background cua windows
```

`setup`, `windows`, and `launch` are workflow boundaries. They install a missing official Driver, check for updates once every 24 hours, and start its daemon if necessary. Input commands, state, screenshots, and raw tool calls do not update or restart the provider. After a workflow boundary that updates CUA, acquire fresh state before any indexed action.

Automatic checks use the official `check-update --json --no-cache`; installation uses the official platform installer and updates use `update --apply`. The selected stable/nightly channel is retained. Checks and setup are serialized across Kiseki processes; successful checks are cached in the current user's Kiseki CUA state directory. Failed checks remain visible and are retried at the next workflow start. An installed provider stays usable when a check fails.

`status` is read-only. On macOS, `status --prompt` requests CuaDriver's Accessibility/Screen Recording grants. macOS startup opens the installed app bundle so its identity owns those grants. Windows/Linux startup launches the provider in the caller's desktop session. A custom daemon endpoint is used as configured and must already be running. Kiseki does not add permission-mode flags or replace the user's provider policies.

| Environment variable | Meaning |
| --- | --- |
| `KISEKI_CUA_DRIVER` | Exact executable override; invalid paths fail instead of falling back; not automatically updated |
| `KISEKI_CUA_AUTO_UPDATE=0` | Disable automatic updates, retaining manual update |
| `KISEKI_CUA_SESSION` | Named session used by the convenience state/action/feedback commands; default `kiseki` |
| `KISEKI_CUA_SOCKET` | Explicit provider socket/named pipe, forwarded to tool calls; owner manages lifecycle and updates |
| `KISEKI_CUA_STATE_DIR` | Override Kiseki's update cache, setup lock and daemon log directory |
| `CUA_DRIVER_RS_VERSION` / `CUA_DRIVER_VERSION` | Official installer pin; automatic updates preserve it |

Normal state locations are `%LOCALAPPDATA%\Kiseki\cua` (Windows), `~/Library/Application Support/Kiseki/cua` (macOS), and `$XDG_STATE_HOME/kiseki/cua` or `~/.local/state/kiseki/cua` (Linux). `setup --startup-wait-ms` controls only the readiness wait, not tool duration, response size, input timing, or path detail. A readiness timeout reports the next status/log check; it does not relaunch an action.

## Observe and act with the same session and snapshot

Use a distinct session name for independent concurrent workflows. Keep it stable across CLI invocations. For example, in Bash:

```sh
export KISEKI_CUA_SESSION=kiseki-editing-example
kiseki background cua call start_session --json '{"session":"kiseki-editing-example"}'
kiseki background cua state --pid 123 --window-id 456 --output before.png
```

Replace the PID/window ID with values from discovery. The state result carries current `snapshot_id`, `element_index` and `element_token` values. Newer CUA releases require the matching snapshot when using an index. Retain the IDs exactly; Windows native window IDs are represented as 64-bit integers.

```sh
kiseki background cua click --pid 123 --window-id 456 --element-index 7 --snapshot-id CURRENT_SNAPSHOT
kiseki background cua text --pid 123 --window-id 456 --element-token CURRENT_TOKEN --text "Example"
```

A new state snapshot can replace the old index map. Re-observe after changes and use the new tokens. Do not silently use an old index against a newer snapshot. Coordinate actions use the provider's window screenshot frame, including the scale reported in that observation.

PNG screenshots use `get_window_state` with the accessibility-tree walk disabled and screenshot enabled; they do not depend on the retired standalone `screenshot` tool. Kiseki verifies that a new nonempty file was produced before replacing an existing capture. JPEG requests retain the legacy screenshot route and return its actual availability/error, rather than writing PNG bytes under a JPEG name.

## Reach current and new provider features

```sh
kiseki background cua tools
kiseki background cua describe click
kiseki background cua call click --file request.json
kiseki background cua driver -- channel status --json
```

`tools` and `describe` use the installed native provider's tool registry and schemas. `call` accepts either `--json` or `--file`, preserves arbitrary tool fields, and returns the provider's structured stdout. `--output` saves the first returned image. It does not insert session fields into arbitrary raw calls: supply a named `session` when the tool accepts it. No timeout, response cap, tree limit, image cap, or action retry is added by the bridge.

Arguments go through native process creation and stdin, avoiding shell interpretation of JSON, text, quotes, `%PATH%`, or metacharacters. stdout and stderr remain separate; failure output and native exit codes are retained. Inspect the provider's effect/readback fields and a subsequent application observation before treating a submitted action as complete.

Current tools include platform-specific launch paths, scrolling, window geometry, session controls, verification, and cursor themes. These are available through `call` without waiting for new convenience flags. `background cua feedback status|enable|motion` use the session field supported by current native providers. CUA 0.25 replaced the legacy `set_agent_cursor_style` tool with installed cursor themes; the old style command reports that provider error, and legacy presets that require it check before sending any changes. To select a current theme:

```sh
kiseki background cua describe set_agent_cursor_theme
kiseki background cua call set_agent_cursor_theme --json '{"session":"kiseki-editing-example","theme_id":"cua.default"}'
```

End the task's session when finished:

```sh
kiseki background cua call end_session --json '{"session":"kiseki-editing-example"}'
```

## Updating and service ownership

`background cua update` forces a read-only current-channel check. Add `--apply` to install an available update. Run setup before a new observation/action workflow afterward. A daemon previously started by Kiseki is restarted when its recorded binary version changes. For a daemon launched separately with custom flags, retain those flags when restarting it; Kiseki does not reconstruct or silently replace an external launch configuration. Provider/package-manager installation errors remain visible.

Upstream changes can alter a tool schema. `describe` and the raw call path provide the current contract; legacy tool failure does not trigger a retry through a different input backend. The native Kiseki input and screenshot families remain separate from CUA target routing.

Official references: [installation](https://cua.ai/docs/how-to-guides/driver/install), [updating](https://cua.ai/docs/how-to-guides/driver/update), [CLI reference](https://cua.ai/docs/reference/cua-driver/cli-reference), [platform support](https://cua.ai/docs/reference/cua-driver/platform-support).
