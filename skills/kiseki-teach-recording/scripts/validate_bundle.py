#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path, PureWindowsPath


def load_json(path):
    try:
        with path.open("r", encoding="utf-8-sig") as handle:
            data = json.load(handle)
        if not isinstance(data, dict):
            raise ValueError("expected a JSON object")
        return data
    except ValueError as error:
        raise ValueError(f"{path}: {error}") from error


def bundle_path(bundle, value):
    if not isinstance(value, str) or not value:
        raise ValueError(f"invalid bundle file path: {value!r}")
    path = (bundle / value).resolve()
    if Path(value).is_absolute() or PureWindowsPath(value).is_absolute() or not path.is_relative_to(bundle.resolve()):
        raise ValueError(f"bundle file path escapes the bundle: {value}")
    return path


def parse_jsonl(path):
    items = []
    with path.open("r", encoding="utf-8-sig") as handle:
        for line_number, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                value = json.loads(line)
                if not isinstance(value, dict):
                    raise ValueError(f"{path}:{line_number}: event must be an object")
                items.append(value)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: invalid JSONL: {error}") from error
    return items


def require(condition, message, errors):
    if not condition:
        errors.append(message)


def _validate_bundle(bundle):
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
        require(bundle_path(bundle, name).is_file(), f"{name} is missing", errors)
    if errors:
        return manifest, errors, warnings

    frames = load_json(bundle_path(bundle, frames_file))
    actions = load_json(bundle_path(bundle, actions_file))
    timeline = load_json(bundle_path(bundle, timeline_file))
    annotations = load_json(bundle_path(bundle, annotations_file))
    events = parse_jsonl(bundle_path(bundle, events_file))

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
    require(isinstance(selected_keyframes, list), "manifest.keyframes must be an array", errors)
    if errors:
        return manifest, errors, warnings
    for label, values in (("frames", frame_items), ("actions", action_items), ("timeline", timeline_items),
                          ("annotations", annotation_items), ("keyframes", selected_keyframes)):
        require(all(isinstance(v, dict) for v in values), f"{label}: every item must be an object", errors)
    if errors:
        return manifest, errors, warnings
    require(type(manifest.get("frameCount")) is int and len(frame_items) == manifest["frameCount"], "manifest.frameCount does not match frames.json", errors)
    require(type(manifest.get("actionCount")) is int and len(action_items) == manifest["actionCount"], "manifest.actionCount does not match actions.json", errors)
    require(type(manifest.get("eventCount")) is int and len(events) == manifest["eventCount"], "manifest.eventCount does not match events.jsonl", errors)
    require(type(manifest.get("keyframeCount")) is int and len(selected_keyframes) == manifest["keyframeCount"], "manifest.keyframeCount does not match keyframes[]", errors)
    require(len(selected_keyframes) > 0, "manifest.keyframes must contain at least one selected keyframe", errors)

    for frame in frame_items + selected_keyframes:
        path = frame.get("path")
        require(path and bundle_path(bundle, path).is_file(), f"frame file is missing: {path}", errors)

    def index(values, field, label):
        result = {}
        for position, value in enumerate(values):
            identifier = value.get(field)
            if type(identifier) is not int or identifier < 0 or identifier in result:
                errors.append(f"{label}[{position}].{field}: invalid or duplicate index")
            else:
                result[identifier] = value
        return result

    fi, ai, ei = index(frame_items, "index", "frames"), index(action_items, "actionIndex", "actions"), index(events, "index", "events")
    selected = set()
    for frame in selected_keyframes:
        identifier = frame.get("index")
        if type(identifier) is not int or identifier not in fi:
            errors.append("selected keyframe references an unknown frame")
        else:
            selected.add(identifier)
            require(all(frame.get(k) == fi[identifier].get(k) for k in ("path", "timestampMs")), "selected keyframe differs from frames.json", errors)
    for identifier, action in ai.items():
        event_id = action.get("index")
        if type(event_id) is not int or event_id not in ei:
            errors.append(f"action {identifier}: unknown raw event index")
        else:
            require(all(action.get(k) == ei[event_id].get(k) for k in ("type", "timestampMs")), f"action {identifier}: type/timestamp differs from raw event", errors)
    seen_frames, seen_actions, previous = set(), set(), -1
    for position, item in enumerate(timeline_items):
        timestamp = item.get("timestampMs")
        if type(timestamp) is not int or timestamp < previous:
            errors.append(f"timeline[{position}]: invalid or nonmonotonic timestamp")
        else:
            previous = timestamp
        field, source = ("frameIndex", fi) if item.get("kind") == "keyframe" else ("actionIndex", ai)
        identifier = item.get(field)
        if type(identifier) is not int or identifier not in source:
            errors.append(f"timeline[{position}]: unknown {field}")
        else:
            require(timestamp == source[identifier].get("timestampMs"), f"timeline[{position}]: timestamp mismatch", errors)
            if field == "frameIndex":
                seen_frames.add(identifier)
                require(item.get("path") == source[identifier].get("path"), f"timeline[{position}]: frame path mismatch", errors)
            else:
                seen_actions.add(identifier)
                require(item.get("eventIndex") == source[identifier].get("index"), f"timeline[{position}]: event index mismatch", errors)
    require(seen_frames == selected and seen_actions == set(ai), "timeline must reference every selected keyframe and action", errors)
    for position, annotation in enumerate(annotation_items):
        for field, source in (("frameIndex", fi), ("eventIndex", ei), ("actionIndex", ai)):
            if field in annotation:
                require(type(annotation[field]) is int and annotation[field] in source, f"annotation {position}: unknown {field}", errors)

    media = manifest.get("media", {})
    require(isinstance(media, dict), "manifest.media must be an object", errors)
    if not isinstance(media, dict):
        media = {}
    for label, name in [("instructionFile", manifest.get("instructionFile"))] + list(media.items()):
        if name:
            require(bundle_path(bundle, name).is_file(), f"{label}: referenced file is missing: {name}", errors)
    video_keyframes_path = media.get("videoKeyframes") if isinstance(media, dict) else None
    extracted_video_frames = 0
    if video_keyframes_path:
        index_path = bundle_path(bundle, video_keyframes_path)
        require(index_path.exists(), f"video keyframe index is missing: {video_keyframes_path}", errors)
        if index_path.exists():
            extraction = load_json(index_path)
            require(extraction.get("schemaVersion") == 1, "video keyframe index schemaVersion must be 1", errors)
            require(extraction.get("tool") == "ffmpeg", "video keyframe index tool must be ffmpeg", errors)
            for frame in extraction.get("frames", []):
                extracted_video_frames += 1
                path = frame.get("path")
                require(path and bundle_path(bundle, path).is_file(), f"extracted video keyframe is missing: {path}", errors)
            warnings.extend(extraction.get("warnings", []))

    require(isinstance(manifest.get("warnings", []), list), "manifest.warnings must be an array", errors)
    if isinstance(manifest.get("warnings", []), list):
        warnings.extend(manifest.get("warnings", []))
    if manifest.get("eventCaptureMode") == "incomplete-polling-fallback" and not any("polling" in str(w) for w in warnings):
        warnings.append("incomplete-polling-fallback: short taps, repeat, and wheel events may be missing")
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
        "eventSource": manifest.get("eventSource"),
        "eventCaptureMode": manifest.get("eventCaptureMode"),
    }
    return summary, errors, warnings


def validate_bundle(bundle):
    bundle = Path(bundle).resolve()
    try:
        summary, errors, warnings = _validate_bundle(bundle)
    except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
        summary, errors, warnings = None, [f"invalid bundle data: {error}"], []
    fields = {"title", "schemaVersion", "format", "durationMs", "frames", "selectedKeyframes", "actions", "events", "timelineItems", "annotations", "videoKeyframes", "eventSource", "eventCaptureMode"}
    summary = {key: value for key, value in (summary or {}).items() if key in fields}
    # All failure paths use the same machine-readable envelope, including
    # malformed JSON, wrong types, missing files and unfinished recordings.
    summary.update(ok=not errors, bundle=str(bundle), warnings=warnings)
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
