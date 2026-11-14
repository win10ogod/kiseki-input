#!/usr/bin/env python3
"""Locate Kiseki and optionally run read-only CLI diagnostics. No GUI actions."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess


def discover_repo(start):
    for directory in (start, *start.parents):
        if (directory / "src/cli/app.cpp").is_file() and (directory / "CMakeLists.txt").is_file():
            return directory
    return None


def inspect(repo=None, executable=None, probe=False):
    wsl = platform.system() == "Linux" and "microsoft" in platform.release().lower()
    host = "wsl" if wsl else platform.system().lower()
    windows = host in {"wsl", "windows"}
    installed_prefix = next((directory for directory in Path(__file__).resolve().parents
                             if (directory / "share/kiseki/skills").is_dir()), None)
    repo = repo.resolve() if repo else (discover_repo(Path.cwd()) or
        (discover_repo(Path(__file__).resolve().parent) if installed_prefix is None else None))
    names = ["build/Debug/kiseki.exe", "build/Release/kiseki.exe", "build/kiseki.exe"] if windows else ["build/kiseki"]
    candidates = [repo / name for name in names] if repo else []
    if installed_prefix:
        candidates.insert(0, installed_prefix / "bin" / ("kiseki.exe" if windows else "kiseki"))
    on_path = shutil.which("kiseki.exe" if windows else "kiseki")
    if on_path:
        candidates.append(Path(on_path))
    if executable:
        selected = Path(executable).expanduser().resolve()
    else:
        selected = next((p.resolve() for p in candidates if p.is_file()), None)
    result = {
        "ok": selected is not None and selected.is_file(),
        "host": host,
        "executionPlatform": "windows" if (selected and selected.suffix.lower() == ".exe") or (selected is None and windows) else ("macos" if host == "darwin" else "linux"),
        "repo": str(repo) if repo else None,
        "executable": str(selected) if selected else None,
        "candidates": [str(p) for p in candidates],
        "sessionEnvironment": {k: os.environ.get(k) for k in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_SESSION_TYPE")},
        "checks": {},
        "errors": [],
    }
    if not result["ok"]:
        result["errors"].append("Kiseki executable not found; build for the intended host or pass --exe.")
        return result
    if probe:
        for name, arguments in (("version", ["--version"]), ("modes", ["modes", "--json"]), ("capabilities", ["capabilities"])):
            try:
                completed = subprocess.run([str(selected), *arguments], cwd=repo, capture_output=True, text=True, encoding="utf-8", errors="replace")
                stdout = completed.stdout.strip()
                # Raw stdout is kept only when it could not be parsed; parsed output is in "result".
                check = {"exitCode": completed.returncode, "stderr": completed.stderr.strip()}
                result["checks"][name] = check
                if completed.returncode:
                    result["errors"].append(f"{name} exited {completed.returncode}")
                if name == "version":
                    check["result"] = stdout
                    check.update(parse_version(stdout))
                else:
                    try:
                        check["result"] = json.loads(stdout)
                    except ValueError:
                        check["stdout"] = stdout
                        raise
            except (OSError, ValueError) as error:
                result["errors"].append(f"{name}: {error}")
        compare_build_commit(result, repo)
        result["ok"] = not result["errors"]
    return result


def parse_version(text):
    """`0.1.0 (commit abc123, dirty)` -> version, commit, tree; older builds print only the version."""
    match = re.match(r"^(\S+)(?: \(commit ([0-9a-f]+|unknown)(?:, (dirty|tree state unknown))?\))?$", text)
    if not match:
        return {}
    tree = {"dirty": "dirty", "tree state unknown": "unknown"}.get(match.group(3), "clean" if match.group(2) not in (None, "unknown") else "unknown")
    return {"version": match.group(1), "commit": match.group(2), "tree": tree}


def compare_build_commit(result, repo):
    """Warn when the executable was built from a different commit than the checkout (read-only git query)."""
    commit = result["checks"].get("version", {}).get("commit")
    git = shutil.which("git")
    if not commit or commit == "unknown" or not repo or not git:
        return
    try:
        completed = subprocess.run([git, "-C", str(repo), "rev-parse", "HEAD"], capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return
    source = completed.stdout.strip()
    if completed.returncode or not source:
        return
    check = result["checks"]["version"]
    check["sourceCommit"] = source[:len(commit)]
    check["matchesSource"] = source.startswith(commit)
    if not check["matchesSource"]:
        result.setdefault("warnings", []).append(
            f"executable was built from commit {commit}; the checkout is at {check['sourceCommit']}. Rebuild before testing source changes.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--exe", help="Exact executable override; no fallback if this fails")
    parser.add_argument("--probe", action="store_true", help="Run --version, modes --json, and capabilities only")
    args = parser.parse_args()
    result = inspect(args.repo, args.exe, args.probe)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["ok"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
