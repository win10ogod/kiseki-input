#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path


def read_json(path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def slugify(value):
    value = value.strip().lower()
    value = re.sub(r"[^a-z0-9]+", "-", value)
    value = re.sub(r"-+", "-", value).strip("-")
    return value[:63] or "recorded-skill"


def read_text(path):
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8").strip()


def action_label(action):
    timestamp = action.get("timestampMs", 0)
    kind = action.get("type", "event")
    prefix = f"{timestamp}ms {kind}"
    if kind == "key":
        return f"{prefix} {action.get('key') or action.get('keyCode')} {action.get('state', '')}".strip()
    if kind == "mouse_button":
        return f"{prefix} {action.get('button', '')} {action.get('state', '')} at {action.get('x', '?')},{action.get('y', '?')}".strip()
    if kind == "mouse_move":
        return f"{prefix} to {action.get('x', '?')},{action.get('y', '?')}"
    if kind == "recorder_status":
        return f"{prefix} {action.get('level', '')}: {action.get('message', '')}".strip()
    return prefix


def compact_actions(actions, limit):
    important = [
        action for action in actions
        if action.get("type") in {"key", "mouse_button", "recorder_status"}
    ]
    if not important:
        important = actions
    return important[:limit]


def write_file(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="Draft a Codex skill from a Kiseki teaching bundle.")
    parser.add_argument("bundle", type=Path, help="Teaching bundle directory")
    parser.add_argument("--output-root", type=Path, default=Path("skills"), help="Directory that will contain the new skill")
    parser.add_argument("--name", help="Skill folder/name. Defaults to a slug from the bundle title")
    parser.add_argument("--description", help="Frontmatter description for the generated skill")
    parser.add_argument("--action-limit", type=int, default=80, help="Maximum action summaries to include in evidence")
    parser.add_argument("--overwrite", action="store_true", help="Overwrite an existing generated skill directory")
    args = parser.parse_args()

    bundle = args.bundle
    manifest_path = bundle / "manifest.json"
    if not manifest_path.exists():
        print(f"manifest.json not found: {manifest_path}", file=sys.stderr)
        return 2

    manifest = read_json(manifest_path)
    if manifest.get("kind") != "kiseki-teach-recording" or manifest.get("schemaVersion") != 2:
        print("bundle manifest is not a Kiseki schema v2 teaching recording", file=sys.stderr)
        return 2

    actions = read_json(bundle / manifest.get("actionsFile", "actions.json")).get("actions", [])
    timeline = read_json(bundle / manifest.get("timelineFile", "timeline.json"))
    annotations = read_json(bundle / manifest.get("annotationsFile", "annotations.json")).get("annotations", [])
    instruction = read_text(bundle / manifest.get("instructionFile", "instruction.txt"))

    title = manifest.get("title") or "Recorded Skill"
    skill_name = slugify(args.name or title)
    skill_dir = args.output_root / skill_name
    if skill_dir.exists() and not args.overwrite:
        print(f"output skill already exists: {skill_dir}; pass --overwrite to replace files", file=sys.stderr)
        return 2

    description = args.description or (
        f"Use when reproducing the workflow taught by the Kiseki teaching bundle '{title}', "
        "including its prerequisites, action sequence, keyframe evidence, and verification checks."
    )

    annotation_lines = []
    for annotation in annotations:
        target = []
        if "frameIndex" in annotation:
            target.append(f"frame {annotation['frameIndex']}")
        if "eventIndex" in annotation:
            target.append(f"event {annotation['eventIndex']}")
        label = " / ".join(target) or "general"
        annotation_lines.append(f"- {label}: {annotation.get('text', '').strip()}")

    selected_keyframes = manifest.get("keyframes", [])
    keyframe_lines = [
        f"- frame {frame.get('index')} at {frame.get('timestampMs')}ms: `{frame.get('path')}` ({frame.get('selectionReason', 'selected')})"
        for frame in selected_keyframes
    ]

    action_lines = [
        f"- #{action.get('actionIndex', action.get('index', '?'))}: {action_label(action)}"
        for action in compact_actions(actions, args.action_limit)
    ]

    skill_md = f"""---
name: {skill_name}
description: {description}
---

# {title}

## Purpose

Use this skill to reproduce the workflow taught by the source Kiseki teaching bundle. This draft was scaffolded by script; the LLM agent must refine it into stable operating procedure before relying on it for repeated automation.

## Prerequisites

- Confirm the target app, session type, permissions, and input/screenshot command family from `references/teaching-evidence.md`.
- Prefer stable UI state and explicit verification over raw pointer coordinates.
- If the source instruction is incomplete, ask for one concise clarification before executing.

## Procedure

1. Read `references/teaching-evidence.md`.
2. Convert the evidence into stable intent. Do not replay raw events blindly.
3. Prepare the target app and state described by the evidence.
4. Follow the stable action intent from the annotations, instruction, and selected keyframes.
5. Use raw `actions.json` timing only when timing is part of the workflow.
6. Verify completion with the screenshot or structured observation path described in the evidence.

## Verification

- Confirm the expected final UI state with the same operation mode family used by the workflow.
- If screenshots are used, compare against the selected keyframes listed in the evidence.
- Do not claim success from a different desktop/session than the one being operated.
"""

    evidence_md = f"""# Teaching Evidence

Source bundle: `{bundle}`

## Manifest

- Title: {title}
- Created at UTC: {manifest.get('createdAtUtc', '')}
- Duration ms: {manifest.get('actualDurationMs', manifest.get('maxDurationMs', 0))}
- Frames: {manifest.get('frameCount', 0)}
- Selected keyframes: {manifest.get('keyframeCount', 0)}
- Actions: {manifest.get('actionCount', 0)}
- Events: {manifest.get('eventCount', 0)}

## Human Instruction

{instruction or '(none)'}

## Annotations

{chr(10).join(annotation_lines) if annotation_lines else '- (none)'}

## Selected Keyframes

{chr(10).join(keyframe_lines) if keyframe_lines else '- (none)'}

## Compact Action Summary

{chr(10).join(action_lines) if action_lines else '- (none)'}

## Timeline File

- `{manifest.get('timelineFile', 'timeline.json')}` contains {len(timeline.get('items', []))} aligned items.

## Agent Notes

- Convert this evidence into stable instructions before considering the skill complete.
- Keep raw event details here; keep the main `SKILL.md` concise.
"""

    openai_yaml = f"""interface:
  display_name: "{title[:40]}"
  short_description: "Use the workflow from a Kiseki teaching bundle"
  default_prompt: "Use ${skill_name} to reproduce the taught workflow and verify the result."
"""

    write_file(skill_dir / "SKILL.md", skill_md)
    write_file(skill_dir / "references" / "teaching-evidence.md", evidence_md)
    write_file(skill_dir / "agents" / "openai.yaml", openai_yaml)
    print(skill_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
