#!/usr/bin/env python3
"""Create or verify a SHA-256 sidecar for every release package."""
import argparse
import hashlib
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    packages = sorted(p for p in args.directory.iterdir() if p.is_file() and
                      any(p.name.endswith(suffix) for suffix in (".exe", ".zip", ".pkg", ".deb", ".tar.gz")))
    if not packages:
        raise RuntimeError("no release packages were produced")
    for package in packages:
        digest = hashlib.sha256()
        with package.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        expected = f"{digest.hexdigest()}  {package.name}\n"
        sidecar = package.with_name(package.name + ".sha256")
        if args.verify:
            if sidecar.read_text(encoding="ascii") != expected:
                raise RuntimeError(f"checksum mismatch: {package.name}")
        else:
            sidecar.write_text(expected, encoding="ascii")
        print(expected, end="")


if __name__ == "__main__":
    main()
