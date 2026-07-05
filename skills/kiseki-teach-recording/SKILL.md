---
name: kiseki-teach-recording
description: Use when working in the Kiseki Input repo to create, inspect, validate, document, or compile Agivar-style screen teaching recordings into reusable Codex skills, including `kiseki teach record`, keyframe/action timelines, optional video/audio/transcript attachments, `teach annotate`, WebUI Teaching-tab review, or generating a new skill from a teaching bundle.
---

# Kiseki Teach Recording

## Purpose

Use this skill to turn a human screen demonstration into a compact teaching bundle and, when requested, compile that bundle into a reusable Codex skill. Keep the Agivar-style rule intact: the agent-facing source of truth is the action timeline plus selected keyframes, not full video analysis.

## Workflow

1. Build or select the current executable before recording:

```bash
"/mnt/c/Program Files/CMake/bin/cmake.exe" --build build --config Debug
```

2. Start a short teaching bundle recording. Use the Windows executable from WSL for Windows live tests. `teach record` is a toggle: the first command starts the detached recorder, and the next `teach record` command stops and finalizes it. Use `--duration-ms` only as a maximum safety timeout.

```bash
rm -rf artifacts/live-test/teach-demo artifacts/live-test/teach-demo-state.json
./build/Debug/kiseki.exe teach record \
  --output artifacts/live-test/teach-demo \
  --state-file artifacts/live-test/teach-demo-state.json \
  --duration-ms 10000 \
  --frame-interval-ms 500 \
  --event-poll-ms 25 \
  --title demo \
  --text "Describe the human intent here."
./build/Debug/kiseki.exe teach record \
  --state-file artifacts/live-test/teach-demo-state.json \
  --stop-timeout-ms 15000
```

3. If audio exists, transcribe it instead of inventing text. `teach transcribe` downloads `Systran/faster-whisper-large-v3` into `vendor/models/Systran/faster-whisper-large-v3` when the local model directory is missing or empty:

```bash
./build/Debug/kiseki.exe teach transcribe \
  --audio-file note.wav \
  --output artifacts/live-test/teach-demo/media/transcript.json
```

4. Attach precise guidance to a keyframe or action:

```bash
./build/Debug/kiseki.exe teach annotate \
  --session artifacts/live-test/teach-demo \
  --frame-index 0 \
  --text "This frame shows the state before the important click."
```

5. Validate the bundle before claiming success. Read `references/bundle-validation.md` for exact file, JSON, and evidence checks.

6. For human review, open `kiseki config-ui`, switch to the Teaching tab, select the bundle directory, and verify keyframes/events/instruction/transcript/annotations render. This is local file viewing only; the WebUI must not gain operational API routes.

7. When the user asks to teach an agent, create a new skill from a recording, or turn a bundle into reusable procedure, read `references/skill-generation.md`. The LLM-driven agent is the semantic compiler. Use scripts only for deterministic validation, indexing, and draft scaffolding:

```bash
python3 skills/kiseki-teach-recording/scripts/validate_bundle.py artifacts/live-test/teach-demo
python3 skills/kiseki-teach-recording/scripts/draft_skill_from_bundle.py artifacts/live-test/teach-demo --output-root skills --name <new-skill-name>
```

## Rules

- Do not create fake audio, fake transcript, or placeholder media. If audio is unavailable, use `--text` or `--text-file`.
- Do not claim a teaching bundle is effective unless `manifest.json`, `frames.json`, `actions.json`, `timeline.json`, `events.jsonl`, `annotations.json`, `SKILL.md`, and at least one selected keyframe parse successfully.
- Treat `actions.json` plus selected keyframes as the primary agent-facing teaching data. `events.jsonl` is the raw native event log.
- A teaching bundle is not the final reusable skill. The LLM-driven agent must compile it into a new skill by extracting stable intent, prerequisites, procedure, verification, and failure handling from the bundle.
- If a real `--video-file` is attached, verify `video_keyframes/index.json` when extraction is enabled. Missing `ffmpeg` should be reported as a warning, not silently ignored.
- Do not use desktop screenshot verification to prove background actions. Teaching recording is current-session capture unless the recorded workflow explicitly uses a background command family and matching background screenshot.
- On macOS, run `kiseki permissions macos screen-recording --prompt --open-settings` from the same GUI Terminal/app before recording if keyframes fail or no Screen Recording prompt appears. SSH-launched binaries can have different TCC permission context.
- On Linux, record the real session type before claims. X11 recordings use X11/XGetImage; Wayland recordings rely on XDG Desktop Portal current-session screenshots and may need compositor permission. Portal screenshot support is not Wayland global input.
- Keep large model artifacts out of git. The default model directory is `vendor/models/Systran/faster-whisper-large-v3`; it may be downloaded or pre-bundled locally.
- Keep the WebUI non-operational. It may inspect local teaching files but must not trigger input, screenshots, daemon control, shell commands, model downloads, or process launch.

## References

- Bundle schema and validation checklist: `references/bundle-validation.md`
- Compile a teaching bundle into a reusable skill: `references/skill-generation.md`
