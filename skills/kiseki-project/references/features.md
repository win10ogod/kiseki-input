# Command index

Use the selected executable with `<family> --help` and the concrete subcommand's `--help` for current flags. This index routes discovery; [README](../../../README.md) and [native input](../../../docs/native-input.md) hold the detailed contracts.

| Task | Command family / useful read-only entry |
| --- | --- |
| Version, modes, detected backends | `--version`, `modes --json`, `capabilities`, `doctor` |
| Find windows / child receivers | `target list`, `target inspect` |
| Structured controls/text | `observe ui` with `auto`, `window-tree`, `uia`, or `ax` |
| Current-session input | `input key`, `combo`, `text`, `mouse`, `drag`, `sequence` |
| Validate/replay a JSON sequence | `macro validate`, `macro run` |
| Current-session capture | `screenshot desktop`, `window`, `burst`, `window-burst` |
| Selected-window messages/capture | `background window text`, `key`, `mouse`, `drag`, `screenshot` |
| Linux isolated Xvfb display | `background desktop start`, `launch`, `text`, `key`, `mouse`, `screenshot`, `stop` |
| Optional CUA provider | `background cua status`, `windows`, `state`; requested action via `launch`, `click`, `text`, `key`, `hotkey`, `drag`, `draw`, `screenshot` |
| CUA overlay feedback | `background cua feedback status`, `enable`, `motion`, `style`, `preset` |
| Configuration | `config path`, `show`, `validate`; global `--config <path>` |
| Configuration/local bundle viewing | `config-ui` |
| Human demonstrations | `teach record`, `annotate`, `transcribe`; use [teaching skill](../../kiseki-teach-recording/SKILL.md) |
| Notifications and heartbeat | `notify`, `daemon run`; use `--once` only for an explicitly short daemon test |
| macOS permission diagnostics | `permissions macos screen-recording`, `accessibility`; prompt/settings options only when needed |

Choose the matching background/current-session mode from the entrypoint. Native selected-window input depends on target message/API acceptance; it is separate from CUA and an isolated display. Wayland portal support provides current-session screenshot capability, not native global input. CUA discovery is optional provider detection, not a delivery test.

Observation `auto` reports the actual source and any fallback; a strict provider request should remain strict. Native mouse/key flags, time fields, receiver binding, Mac transforms and Linux wheel accumulation are documented once in the native input guide rather than duplicated here.

CUA lifecycle and current native tools: `background cua setup|update|tools|describe|call|driver`. Read [CUA operation](cua.md) when using these commands.
