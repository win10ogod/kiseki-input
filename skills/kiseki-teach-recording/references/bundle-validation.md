# Teaching Bundle Validation

Use this checklist before saying a Kiseki teaching recording is effective.

## Required Files

A valid bundle directory must contain:

- `manifest.json`
- `frames.json`
- `actions.json`
- `timeline.json`
- `events.jsonl`
- `annotations.json`
- `SKILL.md`
- `keyframes/` with at least one `.bmp` keyframe

`instruction.txt`, `media/video`, `media/audio`, transcript files, and `video_keyframes/index.json` are optional. Optional files must be real files when referenced by `manifest.json`.

## JSON Shape

`manifest.json` must include:

- `kind: "kiseki-teach-recording"`
- `schemaVersion: 2`
- `format: "agivar-style-action-keyframe-bundle"`
- `framesFile`
- `actionsFile`
- `eventsFile`
- `timelineFile`
- `annotationsFile`
- `keyframes[]`
- `frameCount`
- `actionCount`
- `eventCount`
- `keyframeCount`
- `warnings[]`

`frames.json` must include `schemaVersion: 1`, `frameFormat: "bmp"`, and `frames[]`. Each frame should include `index`, `timestampMs`, `path`, `width`, and `height`; `mouse` and `mouseNorm` are present when the platform can sample cursor position.

`actions.json` must include `schemaVersion: 1` and `actions[]`. Actions are compact native events with `actionIndex`, `index`, `timestampMs`, and `type`. This is the primary agent-facing action sequence.

`timeline.json` must include `schemaVersion: 2`, `durationMs`, and `items[]`. Items should reference selected keyframes by `frameIndex`, actions by `actionIndex`, and raw events by `eventIndex`.

`events.jsonl` must parse line-by-line as JSON. Common event types are:

- `mouse_move`
- `mouse_button`
- `key`
- `recorder_status`

`annotations.json` must include `schemaVersion: 1` and `annotations[]`. Each annotation should target `frameIndex`, `eventIndex`, or both.

When `manifest.media.videoKeyframes` exists, that file must parse as JSON and include `schemaVersion: 1`, `source`, `tool: "ffmpeg"`, `frames[]`, and `warnings[]`. Extracted frame paths must point to real JPEG files.

## Smoke Commands

Use a short recording in ignored artifacts:

```bash
rm -rf artifacts/live-test/teach-skill-smoke
./build/Debug/kiseki.exe teach record \
  --output artifacts/live-test/teach-skill-smoke \
  --state-file artifacts/live-test/teach-skill-smoke-state.json \
  --duration-ms 10000 \
  --frame-interval-ms 400 \
  --event-poll-ms 25 \
  --title teach-skill-smoke \
  --text "Validate screen teaching recording."
sleep 2
./build/Debug/kiseki.exe teach record \
  --state-file artifacts/live-test/teach-skill-smoke-state.json \
  --stop-timeout-ms 15000
./build/Debug/kiseki.exe teach annotate \
  --session artifacts/live-test/teach-skill-smoke \
  --frame-index 0 \
  --text "Initial keyframe."
```

Parse the result:

```bash
node -e "const fs=require('fs'); const dir='artifacts/live-test/teach-skill-smoke'; const m=JSON.parse(fs.readFileSync(dir+'/manifest.json','utf8')); const f=JSON.parse(fs.readFileSync(dir+'/'+m.framesFile,'utf8')); const ac=JSON.parse(fs.readFileSync(dir+'/'+m.actionsFile,'utf8')); const t=JSON.parse(fs.readFileSync(dir+'/'+m.timelineFile,'utf8')); const an=JSON.parse(fs.readFileSync(dir+'/'+m.annotationsFile,'utf8')); const e=fs.readFileSync(dir+'/'+m.eventsFile,'utf8').trim().split(/\r?\n/).filter(Boolean).map(JSON.parse); const ok=m.kind==='kiseki-teach-recording' && m.schemaVersion===2 && m.format==='agivar-style-action-keyframe-bundle' && m.keyframes.length>0 && f.frames.length>=m.keyframes.length && ac.actions.length===m.actionCount && t.items.length>=m.keyframes.length && Array.isArray(an.annotations); if(!ok) process.exit(2); console.log(JSON.stringify({frames:f.frames.length,keyframes:m.keyframes.length,actions:ac.actions.length,events:e.length,timelineItems:t.items.length,annotations:an.annotations.length},null,2));"
```

## Evidence Levels

- Verified: The bundle was produced by the toggle form of `kiseki teach record`, required files exist, JSON/JSONL parse, `actions.json` and `timeline.json` agree with `manifest.json`, and at least one selected keyframe file exists.
- Partially verified: CLI command routes and tests pass, but no live recording artifact was produced.
- Not verified: Only documentation or code was inspected.

State which level was achieved in final reports.
