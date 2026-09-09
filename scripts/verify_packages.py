#!/usr/bin/env python3
"""Verify actual package payloads; --installers also runs Windows silent installers (CI)."""
import argparse
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import zipfile


def verify(prefix):
    subprocess.run([sys.executable, str(Path(__file__).with_name("verify_install.py")), str(prefix)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--installers", action="store_true")
    args = parser.parse_args()
    count = 0
    for package in sorted(args.directory.resolve().iterdir()):
        if not package.is_file() or not any(package.name.endswith(x) for x in (".zip", ".tar.gz", ".pkg", ".deb", ".exe")):
            continue
        if package.suffix == ".exe" and not args.installers:
            continue
        with tempfile.TemporaryDirectory(prefix="kiseki-package-check-") as directory:
            root = Path(directory).resolve()
            if package.suffix == ".zip":
                with zipfile.ZipFile(package) as archive:
                    archive.extractall(root)
            elif package.name.endswith(".tar.gz"):
                # Generated local release archives, not external input.
                with tarfile.open(package) as archive:
                    for member in archive.getmembers():
                        (root / member.name).resolve().relative_to(root)
                        if member.issym():
                            (root / member.name).parent.joinpath(member.linkname).resolve().relative_to(root)
                        elif member.islnk():
                            (root / member.linkname).resolve().relative_to(root)
                    archive.extractall(root)
            elif package.suffix == ".pkg":
                subprocess.run(["pkgutil", "--expand-full", str(package), str(root / "expanded")], check=True)
            elif package.suffix == ".deb":
                subprocess.run(["dpkg-deb", "-x", str(package), str(root)], check=True)
            else:
                # NSIS requires /D to be last and unquoted, even with spaces.
                prefix = root / "installed"
                subprocess.run(f'"{package}" /S /D={prefix}', check=True)
            binaries = list(root.rglob("kiseki.exe" if sys.platform == "win32" else "kiseki"))
            binaries = [p for p in binaries if p.is_file() and p.parent.name == "bin"]
            if len(binaries) != 1:
                raise RuntimeError(f"{package.name}: expected one CLI payload, found {len(binaries)}")
            verify(binaries[0].parent.parent)
            print(f"verified {package.name}")
            count += 1
    if not count:
        raise RuntimeError("no package payloads were verified")


if __name__ == "__main__":
    main()
