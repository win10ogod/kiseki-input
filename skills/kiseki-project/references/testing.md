# Verification by change type

Run checks that exercise the changed behavior. A working executable may be used without rebuilding for ordinary app operation or bundle inspection.

| Change | Appropriate checks |
| --- | --- |
| Skill prose/routing/examples | Skill format validation, relative links, read-only CLI help for documented options |
| Skill helper scripts | `python3 -m unittest discover -s test/skills -v`; real completed bundle checks; script smoke commands |
| CLI/platform code | Native build + CTest; relevant receiver/real-app check for claims about event delivery |
| WebUI | Config/API tests and browser inspection only when visible UI behavior changes |
| Teaching recorder lifecycle | Owned output/state paths, start/status/stop/finalization, bundle validator; avoid a second blind toggle |

## Native builds

Windows from WSL:

```bash
"/mnt/c/Program Files/CMake/bin/cmake.exe" --build build --config Debug
"/mnt/c/Program Files/CMake/bin/ctest.exe" --test-dir build -C Debug --output-on-failure
```

Native macOS/Linux:

```bash
cmake -S . -B build -DKISEKI_BUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Use the available native toolchain path; Homebrew tools may be absent from an SSH shell's PATH. Remote hosts, temporary checkouts, and authorization contexts must be discovered from the current environment, not copied from a previous test report.

For skill validation, use the installed `skill-creator/scripts/quick_validate.py` if available. If it is not installed, inspect YAML frontmatter, resource links, and unfinished placeholders locally; do not require another plugin merely to read this project's skills.

## Native input probe

The opt-in probe creates its own receiver windows, records actual events, and restores cursor/focus. It exercises input; do not run it for a read-only request.

```bash
cmake -S . -B build -DKISEKI_BUILD_NATIVE_INPUT_PROBE=ON
cmake --build build --config Debug
# Windows
./build/Debug/kiseki_native_input_probe.exe "$run_dir"
python3 test/native/verify_events.py windows "$run_dir"
# macOS / X11: use the native host and the correct platform argument
./build/kiseki_native_input_probe "$run_dir"
python3 test/native/verify_events.py macos "$run_dir"
```

Use `linux` for X11. macOS runs needing GUI permission should use the same authorized GUI Terminal/app as the intended workflow. The probe covers rapid keys, held modifiers/buttons, motion timing, matching releases, native event capture, and platform regressions; see [recorded verification scope](../../../docs/native-input-verification.md).

## Teaching and browser checks

Use [the teaching skill](../../kiseki-teach-recording/SKILL.md) for recording lifecycle. Choose unique bundle/state paths and explicit stop through its helper. During macOS teaching/probe comparison, pass `--skip-capture` to the probe and run its screenshot-coordinate check separately.

Only start `config-ui` when the task needs a browser check. Keep its process/session ID, open the returned local URL, inspect the relevant config or local Teaching view, and stop that server when finished unless asked to retain it. Its API is config-only; no operational actions belong in browser requests.

For manual app checks, open an owned test document/window, establish current target/tool/coordinates, then act and capture matching before/after evidence. Do not use fixed desktop coordinates or `Ctrl+A`/delete against an unverified existing document.

## Completion evidence

After edits run `git diff --check` and inspect the diff. Report the executable/source used, checks actually run, and material untested scope. Do not convert old native logs into proof of a newly changed backend. Build/CTest success and native receiver success are separate evidence.
