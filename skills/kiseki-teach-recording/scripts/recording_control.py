#!/usr/bin/env python3
"""Inspect a recording or request its stop without toggling a new recorder on."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path, PureWindowsPath
import platform
import time


def local_path(value):
    # The Windows worker writes Windows paths even when invoked from WSL.
    if platform.system() == "Linux" and "microsoft" in platform.release().lower():
        windows = PureWindowsPath(value)
        if windows.is_absolute() and len(windows.drive) == 2 and windows.drive[1] == ":":
            return Path("/mnt", windows.drive[0].lower(), *windows.parts[1:]).resolve()
    return Path(value).expanduser().resolve()


def status(state_file, bundle):
    result = {"ok": True, "stateFile": str(state_file), "bundle": str(bundle), "state": "not-started", "finalized": False}
    if state_file.exists():
        state = json.loads(state_file.read_text(encoding="utf-8-sig"))
        if not isinstance(state, dict) or state.get("kind") != "kiseki-teach-active-recording" or state.get("schemaVersion") != 1:
            raise ValueError("state file is not a Kiseki recording state")
        if not isinstance(state.get("outputDirectory"), str) or local_path(state["outputDirectory"]) != bundle:
            raise ValueError("state file belongs to a different bundle; it was left unchanged")
        if not isinstance(state.get("pid"), int) or isinstance(state["pid"], bool) or state["pid"] <= 0:
            raise ValueError("state file has no valid worker PID")
        if not isinstance(state.get("stopFile"), str) or not state["stopFile"]:
            raise ValueError("state file has no stopFile")
        stop = local_path(state["stopFile"])
        result.update(state="stop-requested" if stop.exists() else "active-state-present", pid=state["pid"], stopFile=str(stop), logFile=state.get("logFile"), workerLiveness="not-probed")
        return result
    manifest_path = bundle / "manifest.json"
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
        if not isinstance(manifest, dict):
            raise ValueError("manifest must be an object")
        complete = manifest.get("kind") == "kiseki-teach-recording" and "actualDurationMs" in manifest and (bundle / "SKILL.md").is_file()
        result.update(state="finalized" if complete else "bundle-incomplete", finalized=complete)
    return result


def control(action, state_file, bundle, wait_ms=0):
    state_file, bundle = local_path(str(state_file)), local_path(str(bundle))
    result = status(state_file, bundle)
    if action == "status" or result["finalized"]:
        return result
    if result["state"] not in {"active-state-present", "stop-requested"}:
        raise ValueError("no matching active state; no recorder was started and no stop file was written")
    # The worker checks existence, as implemented in recording.cpp. Exclusive
    # creation preserves an existing request and makes repeated stops harmless.
    try:
        with Path(result["stopFile"]).open("x", encoding="utf-8") as stream:
            json.dump({"requestedAtUtc": datetime.now(timezone.utc).isoformat()}, stream)
    except FileExistsError:
        pass
    result["state"] = "stop-requested"
    deadline = time.monotonic() + wait_ms / 1000
    while wait_ms and time.monotonic() < deadline:
        result = status(state_file, bundle)
        if result["finalized"]:
            return result
        time.sleep(min(0.1, max(0, deadline - time.monotonic())))
    if wait_ms:
        result.update(ok=False, error="stop requested; finalization is still pending. Inspect the worker log or repeat status/stop, not teach record.")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("status", "stop"))
    parser.add_argument("--state-file", required=True, type=Path)
    parser.add_argument("--bundle", required=True, type=Path, help="Must match the state file's outputDirectory")
    parser.add_argument("--wait-ms", type=int, default=0, help="Optional stop finalization wait; 0 only submits the request")
    args = parser.parse_args()
    try:
        if args.wait_ms < 0:
            raise ValueError("--wait-ms must be nonnegative")
        result = control(args.action, args.state_file, args.bundle, args.wait_ms)
    except (OSError, ValueError) as error:
        result = {"ok": False, "error": str(error)}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
