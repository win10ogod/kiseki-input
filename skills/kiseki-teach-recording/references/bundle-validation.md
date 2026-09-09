# Bundle validation and evidence

```bash
python3 skills/kiseki-teach-recording/scripts/validate_bundle.py "$bundle"
```

The command is read-only, needs only Python 3.9+, and reports JSON even for missing files, malformed JSON/JSONL or wrong value types. Exit 0 means structural checks passed; exit 2 includes diagnostic `errors`. Warnings remain visible and do not automatically reject a useful partial recording.

## What is checked

- Manifest kind `kiseki-teach-recording`, schema 2 and format `agivar-style-action-keyframe-bundle`.
- Referenced frames/actions/timeline/events/annotations files plus the bundle reading guide `SKILL.md`.
- Object/array shapes, manifest counts, frame paths and at least one selected keyframe.
- Unique frame/action/event indexes; selected frame path/timestamp consistency.
- Action-to-raw-event type/timestamp linkage; timeline frame/action/event references and monotonic ordering; annotation targets.
- Referenced instruction/media files and extracted video keyframe index/assets when supplied.
- Capture source/mode and recorder warnings, including incomplete polling fallback.

Paths in a bundle manifest are relative to the bundle, not to the shell working directory. Referenced files must remain inside the bundle. The validator does not execute recorded commands, decode every image, prove that the pictured task completed, or establish that a generated skill generalizes.

## Reading order

1. `manifest.json`: task title, recording options, source/mode, warnings, actual artifact paths.
2. Referenced instruction, then `annotations.json`: human intent and corrections.
3. `actions.json`: every relevant action, including wheel events, motion inside drags, key up/down and late completion steps.
4. `timeline.json`: map actions to selected keyframes and inspect those images.
5. Referenced transcript/media when needed; raw `events.jsonl` when resolving timing, scan/repeat or capture questions.

`frames.json` indexes all captured frames; `manifest.keyframes` selects the primary teaching views. `actions.json` preserves compact event records; their `index` points to the raw event and `actionIndex` identifies the action. Annotations may refer to frame or event indexes. Native event timestamps/units and screenshot coordinate transforms must be interpreted in their recorded platform/session.

## Interpret failures

| Result | Next action |
| --- | --- |
| Active recorder or partially written files | Inspect the original state/log and finalize; do not treat a partial manifest as complete |
| Missing media/frame | Locate the source artifact or report the missing evidence; do not manufacture a replacement |
| Malformed JSON or broken indexes | Report the exact file/reference; preserve the original while making any authorized repair in a copy |
| Polling fallback or recorder gap warning | Use available evidence, but do not assert complete short-tap, wheel or repeat counts |
| Structure passes but goal/final state is unclear | Read relevant instructions, late actions and keyframes; ask only for essential intent that remains absent |

Report evidence at the level actually achieved: structural validation, visual interpretation, or a replay verified in the target app. A script's exit code alone does not make a teaching bundle effective.
