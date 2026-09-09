---
name: kiseki-project
description: Operate desktop apps with Kiseki Input or develop its CLI and platform backends. Use for target discovery, structured observation, screenshots, precise keyboard/mouse sequences, selected-window or isolated-display operations, configuration, and native verification.
---

# Kiseki Project

Kiseki is a native C++ CLI for agent desktop operation. The WebUI edits configuration and inspects local teaching bundles; operation stays in CLI commands.

## Start with the actual task

| Request | Next step | Read only when needed |
| --- | --- | --- |
| Operate or inspect an app | Locate the executable, discover the target, choose the operation family | [Operating guide](references/operating.md) |
| Draw, drag, or manipulate a canvas | Establish the canvas and coordinate transform, then use a sequence/path | [Drawing guide](references/drawing-apps.md) |
| Inspect an existing teaching bundle or record a demonstration | Use the sibling teaching skill; existing-bundle inspection does not start a recorder | [kiseki-teach-recording](../kiseki-teach-recording/SKILL.md) |
| Change source, CLI, or WebUI | Inspect the relevant slice and its tests | [Architecture](references/architecture.md), [testing](references/testing.md) |
| Find a command, configure, notify, or run a daemon | Read the matching command's help | [Command index](references/features.md) |

Respect an established target, mode, timing, and authorization. Do not rerun the whole setup before each command or ask again for already authorized work. Resolve target ambiguity from discovery results; ask only when the user's choice is necessary, while continuing independent inspection.

## Find the current executable

From the repository root:

```bash
python3 skills/kiseki-project/scripts/inspect_runtime.py --probe
```

The helper reports the repository, executable, execution platform, version, mode matrix, and runtime capability JSON. It runs only `--version`, `modes --json`, and `capabilities`. `--exe <path>` selects an exact build without silently falling back. Without `--probe`, it only inspects paths/environment.

Use the returned executable for subsequent commands. In Bash examples below and in references, `kiseki_exe` means that path, and `run_dir` means a fresh task artifact directory. Quote both. PowerShell users can invoke the same executable directly with PowerShell path syntax.

- WSL defaults to the Windows `.exe` for Windows work. Native macOS/Linux work runs on those hosts with their binaries.
- Use an existing appropriate build for operation. Build when missing, when testing source changes, or when version evidence shows the binary is stale; reading a bundle or editing skills does not require rebuilding C++.
- Local machine paths, SSH keys, host IPs, and previous `/tmp` checkouts are not project defaults. Use available connection instructions for an authorized host; verify temporary paths before reuse.

## Operation loop

1. **Observe:** discover current PID/window IDs; use structured UI data for controls and text, screenshots for pixels/canvas.
2. **Bind:** retain host/session, target IDs, input backend, coordinate space, and matching screenshot family.
3. **Act:** execute the next useful action or bounded sequence within the requested task. Keep requested timing and path detail.
4. **Check:** inspect the expected state transition. A submitted input event is not proof that the application completed its response.
5. **Continue or recover:** use the observed result to choose the next action. After a failed command, inspect current state before retrying a mutation that may already have happened.

Use `input sequence` for mixed held-key/button actions, `input drag` for a path, and `macro validate` before executing a new sequence file. Read [native input](../../docs/native-input.md) for exact fields, timing, cleanup, physical key identities, wheel units, and screenshot transforms.

## Mode invariants

| Mode | Action family | Coordinates | Matching verification |
| --- | --- | --- | --- |
| Current GUI session | `input ...` | Screen/virtual-screen; macOS global points | `screenshot desktop` or `screenshot window` |
| Selected window | `background window ...` | Target client area | `background window screenshot` |
| Linux isolated display | `background desktop ...` | Chosen Xvfb display | `background desktop screenshot` |
| Optional CUA target | `background cua ...` | Provider window-local coordinates for window-targeted actions | `background cua screenshot` or `state --output` |

Keep the target, display, and coordinate transform attached to the observation. Window-local screenshot pixels are not automatically screen coordinates or client coordinates. A background request must retain its selected mode; do not silently move to current-session input to make an action work.

`capabilities` describes runtime detection; an optional provider being found does not prove target delivery. For a new CUA workflow, run `background cua setup` once (installs a missing Driver, checks for updates daily, starts its native daemon), then check status and discover the target. `windows` and `launch` also prepare a new workflow. Keep actions/state within that workflow; do not run setup between a snapshot and its indexed action. See [CUA operation](references/cua.md) for session/snapshot identity, current tools, and update overrides. Launch an app or change permissions only when the task needs it. For native platform claims, use that platform's actual result, not a WSL substitute.

## Completion

Report the achieved application state or code change, the useful evidence, and any unresolved limitation in the tested scope. Keep logs/screenshots under the task's ignored artifacts directory. Release inputs and stop only processes/recorders/servers created for this task unless the user asked to keep them running. For code edits, run relevant checks and `git diff --check`; [testing](references/testing.md) distinguishes script, unit, and live validation.
