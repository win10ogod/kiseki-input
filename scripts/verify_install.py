#!/usr/bin/env python3
"""Verify an installed package without starting GUI operations or CUA downloads."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    args = parser.parse_args()
    prefix = args.prefix.resolve()
    executable = prefix / "bin" / ("kiseki.exe" if sys.platform == "win32" else "kiseki")
    share = prefix / "share/kiseki"
    required = [executable, share / "install.html", share / "docs/install.md",
                share / "skills/kiseki-project/SKILL.md", share / "skills/kiseki-teach-recording/SKILL.md",
                share / "licenses/CLI11.txt", share / "licenses/nlohmann-json.txt", share / "licenses/cpp-httplib.txt"]
    for path in required:
        if not path.is_file():
            raise RuntimeError(f"installed file is missing: {path}")
    with tempfile.TemporaryDirectory(prefix="kiseki-install-check-") as directory:
        for command in (["--version"], ["modes", "--json"], ["capabilities"], ["background", "cua", "--help"]):
            subprocess.run([str(executable), *command], cwd=directory, check=True)
        completed = subprocess.run([sys.executable, str(share / "skills/kiseki-project/scripts/inspect_runtime.py"), "--probe"],
                                   cwd=directory, capture_output=True, text=True, encoding="utf-8", check=True)
        runtime = json.loads(completed.stdout)
        if Path(runtime["executable"]).resolve() != executable.resolve():
            raise RuntimeError("installed skill selected a different Kiseki executable")
    print(json.dumps({"ok": True, "prefix": str(prefix)}, indent=2))


if __name__ == "__main__":
    main()
