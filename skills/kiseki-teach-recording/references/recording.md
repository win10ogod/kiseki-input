# Recording lifecycle

Use the intended native host/executable. For Windows operation from WSL, use the Windows executable. Native macOS GUI capture/input may need the same authorized GUI Terminal/app that will run Kiseki; SSH shell capability alone does not settle GUI permission. For Linux, establish the actual X11 or Wayland/portal session and inspect native-event/fallback warnings.

## Start a new demonstration

Create new paths for this run; do not delete an existing bundle or recorder state to make an example work. Bash example from the repository root, with `kiseki_exe` already set to the selected executable:

```bash
mkdir -p artifacts/teach
bundle=$(mktemp -d artifacts/teach/teach-XXXXXXXX)
state_file="${bundle}.state.json"
"$kiseki_exe" teach record --output "$bundle" --state-file "$state_file" --title "Task demonstration" --text "The user's actual goal"
```

`--state-file` is a repo-supported control option intentionally hidden from ordinary help. It lets concurrent tasks use distinct recorder state. Save these paths and the returned worker PID/log. Do not use someone else's active state file. This helper workflow follows the current `kiseki-teach-active-recording` schema v1 in `src/platform/teach/recording.cpp`.

The default duration is 0: record until stopped. Set `--duration-ms` only when the user wants a maximum duration or a specifically bounded smoke test. Keep requested frame/event settings; `--event-poll-ms` controls queue draining and fallback polling, not the native event sampling rate.

Let the requested human demonstration or authorized test run. A successful start only establishes that the detached worker started; it does not finalize a bundle.

## Status and stop

```bash
python3 skills/kiseki-teach-recording/scripts/recording_control.py status --state-file "$state_file" --bundle "$bundle"
python3 skills/kiseki-teach-recording/scripts/recording_control.py stop --state-file "$state_file" --bundle "$bundle" --wait-ms 15000
python3 skills/kiseki-teach-recording/scripts/validate_bundle.py "$bundle"
```

`active-state-present` means the matching state exists, not that worker liveness has been proven. `stop-requested` may still be flushing events/images. Check its log and native PID if progress is uncertain. `finalized` detects completed output and absent active state; run the bundle validator afterward for structural checks. An incomplete bundle is preserved for diagnosis.

The helper only writes the stop file specified by a matching state. A missing/malformed state, a different bundle, or a pending stop is not a reason to delete state, restart recording, or use the public toggle as an unconditional stop. If an automatic duration already ended the run, the helper reports finalization without starting another one.

## Optional annotation and media

Use an existing frame/event index from the validated bundle:

```bash
"$kiseki_exe" teach annotate --session "$bundle" --frame-index 0 --text "Describe the verified state in this frame"
```

For an existing audio attachment, first look for a usable transcript. If transcription is needed, inspect `teach transcribe --help` and the configured/local model. The default tool can download `Systran/faster-whisper-large-v3` into `vendor/models/Systran/faster-whisper-large-v3` if missing; this is not a prerequisite for text-only demonstrations or bundle inspection.

```bash
"$kiseki_exe" teach transcribe --audio-file "$audio_file" --output "$bundle/media/transcript.json"
```

An independently generated transcript is not automatically attached to an already finalized manifest. Preserve it as a named supporting artifact or attach it through the task's intended bundle workflow; do not claim that creating the file changed `manifest.media`.

If real video is supplied, inspect the extraction index/warnings when enabled. No full-video analysis is required when action/keyframe evidence answers the task. During macOS recording plus native probe tests, use probe `--skip-capture`; run its separate four-corner screenshot check afterward.
