# CUA operation

Begin a new CUA workflow with `background cua setup`. It installs a missing native Driver and checks and applies selected-channel updates at each workflow start. `windows` and `launch` also prepare a workflow; `status` only checks readiness. Do not insert setup/discovery between a snapshot and its indexed action. Read [CUA integration](../../../docs/cua.md) for update controls, Driver 0.30.1 feature families and service ownership.

1. Set `KISEKI_CUA_SESSION` to a task-specific name for concurrent or repeated work. Convenience state/action/feedback commands use that name (default `kiseki`).
2. Call `start_session` with the same explicit JSON `session` if starting/reviving that named lifecycle. Arbitrary raw calls require their own supported session argument.
3. Discover PID/window IDs in the current host/session; reuse the task's established app. `launch --name` is portable; macOS bundle IDs and Windows AUMIDs/path details come from the installed provider's `launch_app` schema.
4. Read `state --pid ... --window-id ... --output ...`. Retain the exact snapshot ID, element token/index, screenshot scale and coordinate frame.
5. Use `--snapshot-id` with `--element-index`, or the returned `--element-token` for click/text/key. Re-observe when the app changes. Do not replace a stale token with an unverified index.
6. Confirm the app effect with fresh structured state or a screenshot. Keep current-session native input separate from selected-window CUA requests.
7. End the task's named session when complete. Stop a daemon only if the task owns it and no further use is requested.

Use `background cua tools` and `describe TOOL` to learn the installed provider. `call TOOL --file request.json` preserves all JSON fields, including new tools and platform-specific options. This is the route for scroll, window geometry, `delivery_mode`, current cursor themes, or any capability not covered by convenience flags. Never change delivery mode or reduce precision fields to turn an error into apparent success.

PNG screenshots use the current `get_window_state` capture route. JPEG and old custom-style commands depend on legacy provider tools and report their real errors if absent. Current visual feedback uses `session`; cursor themes use `set_agent_cursor_theme`. A style-dependent preset checks compatibility before sending changes.

When setup reports a warning, keep it in the result. An offline update can leave the installed version usable; first-time installation failure needs its exact installer error. macOS permissions belong to CuaDriver.app; Windows/Linux need the target graphical session. A custom `KISEKI_CUA_DRIVER`, version pin or `KISEKI_CUA_SOCKET` stays under its owner's control.

Current feature entries: `browser state` accepts `snapshot_format: "semantic_v2"` and full continuation/scope fields; `visual-regions` takes a retained capture ID; `theme` selects an installed theme. All accept `--json` or `--file`. Convenience observation/input commands accept `--provider-json` for additional native fields. Keep `capture_id` separate from `snapshot_id`, preserve delivery mode, and inspect ActionResult effects. Driver recording and extension management are available through their named families with native arguments after `--`.
