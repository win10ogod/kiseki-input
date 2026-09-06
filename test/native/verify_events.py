#!/usr/bin/env python3
"""Verify native receiver evidence, optionally against a completed teaching bundle."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("platform", choices=("windows", "macos", "linux"))
parser.add_argument("directory", type=Path)
parser.add_argument("--teaching", type=Path)
args = parser.parse_args()

def read(path):
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]

received = read(args.directory / "receiver-events.jsonl")
stream = read(args.directory / "stream-events.jsonl")
failures = []
summary = {"platform": args.platform}

def require(condition, message):
    if not condition:
        failures.append(message)

def events(phase, kind=None):
    return [e for e in received if e["phase"] == phase and (kind is None or e["kind"] == kind)]

def dragging(e, button):
    if args.platform == "macos":
        return e["kind"] == "drag" and e["button"] == button
    mask = (1, 2, 16)[button] if args.platform == "windows" else (256, 1024, 512)[button]
    return e["kind"] == "move" and bool(e.get("buttons", 0) & mask)

for phase, button in ((1, 0), (2, 1), (3, 2)):
    buttons = events(phase, "button")
    require([(e["button"], e["down"]) for e in buttons] == [(button, True), (button, False)], f"phase {phase}: missing matching button pair")
    count = sum(dragging(e, button) for e in events(phase))
    require(count >= 2, f"phase {phase}: split drag lost held-button state")
    summary[f"splitDrag{button}Moves"] = count

for phase, button, modifier in ((4, 0, "shift"), (5, 1, "ctrl")):
    moves = [e for e in events(phase) if dragging(e, button)]
    require(len(moves) >= 3 and all(e[modifier] for e in moves), f"phase {phase}: drag lost {modifier}")
    # Compare distinct requested positions; some window servers suppress a duplicate
    # motion at the start point. The nonuniform path must retain its 150/75 ms gaps.
    distinct = []
    for e in moves:
        if not distinct or e["x"] != distinct[-1]["x"]:
            distinct.append(e)
    gaps = [b["nativeTimeMs"] - a["nativeTimeMs"] for a, b in zip(distinct, distinct[1:])]
    expected = [100, 100] if phase == 4 else [150, 75]
    require(len(gaps) >= 2 and all(abs(a-b) <= 45 for a, b in zip(gaps[-2:], expected)), f"phase {phase}: incorrect path timing {gaps}")
    summary[f"phase{phase}MotionGapsMs"] = gaps

clicks = events(6, "button")
require(len(clicks) == 4, "double click did not reach the window as two pairs")
if args.platform == "macos":
    counts = [e["clickCount"] for e in clicks]
    require(counts == [1, 1, 2, 2], f"macOS click state mismatch: {counts}")
    summary["doubleClickStates"] = counts

wheel = events(7, "wheel")
if args.platform == "macos":
    require(any(e["deltaY"] > 0 and e["deltaX"] > 0 for e in wheel), "macOS wheel axes/direction missing")
else:
    require(any(not e["horizontal"] and e["delta"] > 0 for e in wheel), "vertical wheel event missing")
    require(any(e["horizontal"] and e["delta"] < 0 for e in wheel), "horizontal wheel event missing")

keys = events(8, "key")
require(sum(e["down"] for e in keys) == 100 and sum(not e["down"] for e in keys) == 100, "receiver lost fast taps")
summary["receiverFastKeyDowns"] = sum(e["down"] for e in keys)
code = {"windows": 65, "macos": 0, "linux": 38}[args.platform]

def verify_stream(values, label):
    keys = [e for e in values if e.get("type") == "key" and e.get("keyCode") == code]
    downs = sum(e.get("state") == "down" for e in keys)
    ups = sum(e.get("state") == "up" for e in keys)
    require(downs == 100 and ups == 100, f"{label}: expected 100 complete fast taps, got {downs}/{ups}")
    require(any(e["type"] == "mouse_wheel" for e in values), f"{label}: wheel events missing")
    require(all(a["timestampMs"] <= b["timestampMs"] for a, b in zip(values, values[1:])), f"{label}: timestamps not monotonic")
    summary[label] = {"keyDowns": downs, "keyUps": ups, "events": len(values)}

verify_stream(stream, "nativeStream")
if args.platform == "macos":
    for modifier_code in (56, 59, 62):
        states = [e["state"] for e in stream if e.get("type") == "key" and e.get("keyCode") == modifier_code]
        expected_pairs = 21 if modifier_code == 56 else 1
        require(states == ["down", "up"] * expected_pairs, f"macOS modifier {modifier_code} state mismatch: {states}")
if args.teaching:
    manifest = json.loads((args.teaching / "manifest.json").read_text(encoding="utf-8"))
    require(manifest.get("eventCaptureMode") == "native-event-stream", "teaching fell back to polling")
    require(not manifest["warnings"], "teaching has warnings")
    verify_stream(read(args.teaching / "events.jsonl"), "teaching")
    summary["teachingFrames"] = manifest["frameCount"]

require(len(events(10, "button")) == 4, "side-button pairs missing")
sequence = events(11)
require(sum(e["kind"] == "key" and e["down"] for e in sequence) == 1, "sequence Space-down missing")
require(sum(e["kind"] == "key" and not e["down"] for e in sequence) == 1, "sequence Space-up missing")
require(sum(dragging(e, 0) for e in sequence) >= 2, "CLI mixed sequence did not drag")
relative = events(18, "button")
split = events(19, "button")
require(len(relative) == 2 and len(split) == 2, "rapid pointer scenarios lost a button pair")
if len(relative) == 2 and len(split) == 2:
    # Windows relative SendInput follows pointer acceleration; macOS uses points.
    if args.platform == "macos":
        distance = relative[0]["x"] - split[0]["x"]
        summary["relative100PointDistance"] = distance
        require(abs(distance - 100) <= 1, f"relative moves did not accumulate: {distance}")
    distance = split[1]["x"] - split[0]["x"]
    summary["splitReleaseDistance"] = distance
    require(abs(distance - 180) <= 1, f"split up used a stale cursor position: {distance}")
plain_code = {"windows": 67, "macos": 8, "linux": 54}[args.platform]
# A Windows IME may translate key-down VKs to VK_PROCESSKEY; the scan still
# identifies the requested physical C independently of text composition.
plain = [e for e in events(20, "key") if (e["scan"] == 46 if args.platform == "windows" else e["code"] == plain_code)]
require(len(plain) == 40 and all(not e["shift"] for e in plain), "released Shift leaked into following key or suppressed taps")
summary["unmodifiedFollowingKeyEvents"] = len(plain)
enters = events(21, "key")
require(len(enters) == 8, "held main/numpad Enter suppressed the other physical key")
summary["overlappingEnterEvents"] = len(enters)
if args.platform == "windows":
    dense = [e for e in events(22) if dragging(e, 0)]
    require(len(dense) >= 99, f"busy receiver lost dense path points: {len(dense)}")
    require([e["extended"] for e in enters] == [0, 1, 1, 0, 1, 0, 0, 1], "overlapping Enter physical identities were lost")
    summary["busyReceiverPathPoints"] = len(dense)
    for scenario in (23, 24, 25):
        target = events(scenario)
        buttons = events(scenario, "button")
        require([e["down"] for e in buttons] == [True, False], f"phase {scenario}: changed target lost cleanup")
        require(len({e["receiver"] for e in target}) == 1, f"phase {scenario}: sequence changed recipient")
    summary["boundRecipientCases"] = ["renamed", "ambiguous title", "hidden"]
    destroyed = json.loads((args.directory / "destroyed-recipient.json").read_text())
    require(destroyed["destroyed"] and destroyed["exitCode"] != 0 and "cleanup" in destroyed["error"],
            "destroyed recipient falsely reported successful cleanup")
    summary["destroyedRecipientCleanupExit"] = destroyed["exitCode"]
    arrows = events(9, "key")[:2]
    require(len(arrows) == 2 and all(e["scan"] == 75 and e["extended"] == 1 for e in arrows), "Windows arrow scan/extended identity incorrect")
    summary["arrowScanExtended"] = [[e["scan"], e["extended"]] for e in arrows]
    for phase, button in ((12, 0), (13, 1), (14, 2)):
        target = events(phase)
        require(len({e["receiver"] for e in target}) == 1, f"phase {phase}: background drag changed child receiver")
        require(sum(dragging(e, button) for e in target) == 2, f"phase {phase}: background drag lost button state")

mapping = args.directory / "capture-mapping.json"
if mapping.exists():
    capture = json.loads(mapping.read_text(encoding="utf-8"))
    require(capture["upright"], "screenshot is inverted")
    clicks = [e for e in events(16, "button") if e["down"]]
    require(len(clicks) == 4, "screenshot-derived corner clicks did not reach the window")
    if len(clicks) == 4:
        require(all(abs(e["x"]-x) <= 4 and abs(e["y"]-y) <= 4 for e, (x,y) in zip(clicks, [(50,410),(670,410),(50,50),(670,50)])), "pixel-to-screen corner mapping is incorrect")
    summary["capture"] = {"upright": capture["upright"], "scaleX": capture["pixelsPerUnitX"], "scaleY": capture["pixelsPerUnitY"], "receivedCornerClicks": len(clicks)}
summary["failures"] = failures
summary["ok"] = not failures
print(json.dumps(summary, indent=2))
raise SystemExit(0 if not failures else 2)
