#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


def load_json(path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def parse_jsonl(path):
    items = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                items.append(json.loads(line))
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: invalid JSONL: {error}") from error
    return items


def require(condition, message, errors):
    if not condition:
        errors.append(message)


def validate_bundle(bundle):
    errors = []
    warnings = []
    manifest_path = bundle / "manifest.json"
    require(manifest_path.exists(), "manifest.json is missing", errors)
    if errors:
        return None, errors, warnings

    manifest = load_json(manifest_path)
    require(manifest.get("kind") == "kiseki-teach-recording", "manifest.kind must be kiseki-teach-recording", errors)
    require(manifest.get("schemaVersion") == 2, "manifest.schemaVersion must be 2", errors)
    require(
        manifest.get("format") == "agivar-style-action-keyframe-bundle",
        "manifest.format must be agivar-style-action-keyframe-bundle",
        errors,
    )

    frames_file = manifest.get("framesFile", "frames.json")
    actions_file = manifest.get("actionsFile", "actions.json")
    timeline_file = manifest.get("timelineFile", "timeline.json")
    events_file = manifest.get("eventsFile", "events.jsonl")
    annotations_file = manifest.get("annotationsFile", "annotations.json")

    required_files = [frames_file, actions_file, timeline_file, events_file, annotations_file, "SKILL.md"]
    for name in required_files:
        require((bundle / name).exists(), f"{name} is missing", errors)
    if errors:
        return manifest, errors, warnings

    frames = load_json(bundle / frames_file)
    actions = load_json(bundle / actions_file)
    timeline = load_json(bundle / timeline_file)
    annotations = load_json(bundle / annotations_file)
    events = parse_jsonl(bundle / events_file)

    frame_items = frames.get("frames", [])
    action_items = actions.get("actions", [])
    timeline_items = timeline.get("items", [])
    annotation_items = annotations.get("annotations", [])
    selected_keyframes = manifest.get("keyframes", [])

    require(frames.get("schemaVersion") == 1, "frames.schemaVersion must be 1", errors)
    require(actions.get("schemaVersion") == 1, "actions.schemaVersion must be 1", errors)
    require(timeline.get("schemaVersion") == 2, "timeline.schemaVersion must be 2", errors)
    require(annotations.get("schemaVersion") == 1, "annotations.schemaVersion must be 1", errors)
    require(isinstance(frame_items, list), "frames.frames must be an array", errors)
    require(isinstance(action_items, list), "actions.actions must be an array", errors)
    require(isinstance(timeline_items, list), "timeline.items must be an array", errors)
    require(isinstance(annotation_items, list), "annotations.annotations must be an array", errors)
    require(len(frame_items) == manifest.get("frameCount"), "manifest.frameCount does not match frames.json", errors)
    require(len(action_items) == manifest.get("actionCount"), "manifest.actionCount does not match actions.json", errors)
    require(len(events) == manifest.get("eventCount"), "manifest.eventCount does not match events.jsonl", errors)
    require(len(selected_keyframes) == manifest.get("keyframeCount"), "manifest.keyframeCount does not match keyframes[]", errors)
    require(len(selected_keyframes) > 0, "manifest.keyframes must contain at least one selected keyframe", errors)

    for frame in selected_keyframes:
        path = frame.get("path")
        require(path and (bundle / path).exists(), f"selected keyframe file is missing: {path}", errors)

    media = manifest.get("media", {})
    video_keyframes_path = media.get("videoKeyframes") if isinstance(media, dict) else None
    extracted_video_frames = 0
    if video_keyframes_path:
        index_path = bundle / video_keyframes_path
        require(index_path.exists(), f"video keyframe index is missing: {video_keyframes_path}", errors)
        if index_path.exists():
            extraction = load_json(index_path)
            require(extraction.get("schemaVersion") == 1, "video keyframe index schemaVersion must be 1", errors)
            require(extraction.get("tool") == "ffmpeg", "video keyframe index tool must be ffmpeg", errors)
            for frame in extraction.get("frames", []):
                extracted_video_frames += 1
                path = frame.get("path")
                require(path and (bundle / path).exists(), f"extracted video keyframe is missing: {path}", errors)
            warnings.extend(extraction.get("warnings", []))

    warnings.extend(manifest.get("warnings", []))
    summary = {
        "ok": not errors,
        "bundle": str(bundle),
        "title": manifest.get("title", ""),
        "schemaVersion": manifest.get("schemaVersion"),
        "format": manifest.get("format"),
        "durationMs": manifest.get("actualDurationMs", manifest.get("maxDurationMs", 0)),
        "frames": len(frame_items),
        "selectedKeyframes": len(selected_keyframes),
        "actions": len(action_items),
        "events": len(events),
        "timelineItems": len(timeline_items),
        "annotations": len(annotation_items),
        "videoKeyframes": extracted_video_frames,
        "warnings": warnings,
    }
    return summary, errors, warnings


def main():
    parser = argparse.ArgumentParser(description="Validate a Kiseki teaching bundle.")
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()

    summary, errors, _warnings = validate_bundle(args.bundle)
    if summary is None:
        summary = {"ok": False, "bundle": str(args.bundle)}
    summary["errors"] = errors
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if not errors else 2


if __name__ == "__main__":
    raise SystemExit(main())
