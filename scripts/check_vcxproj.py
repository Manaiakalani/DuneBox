#!/usr/bin/env python3
r"""Verify that Magic-Sand.vcxproj lists every source file under src/.

The Visual Studio project file (.vcxproj) references source files explicitly.
MSBuild only compiles files listed there, so a .cpp that exists on disk but is
missing from the project silently fails to link (the exact bug this guards
against). Conversely, an entry pointing at a deleted file breaks the build.

This check compares the <ClCompile>/<ClInclude> entries rooted at ``src\`` with
the actual ``src/**/*.{cpp,h}`` files on disk and reports any mismatch in
either direction. It is filesystem-only (no MSBuild), so it runs fast on Linux.
"""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VCXPROJ = REPO_ROOT / "Magic-Sand.vcxproj"
SRC_DIR = REPO_ROOT / "src"

# Only files under src/ are project-owned; addon paths (..\..\..\addons) are
# managed by OpenFrameworks and intentionally excluded.
SRC_PREFIX = "src\\"
SUFFIXES = {".cpp", ".h"}


def project_entries() -> set[str]:
    """Return src-rooted include paths from the vcxproj, normalised to POSIX."""
    tree = ET.parse(VCXPROJ)
    entries: set[str] = set()
    for elem in tree.iter():
        tag = elem.tag.split("}")[-1]
        if tag not in {"ClCompile", "ClInclude"}:
            continue
        inc = elem.get("Include")
        if inc and inc.startswith(SRC_PREFIX):
            entries.add(inc.replace("\\", "/"))
    return entries


def disk_entries() -> set[str]:
    """Return src/**/*.{cpp,h} paths relative to the repo root, POSIX style."""
    return {
        p.relative_to(REPO_ROOT).as_posix()
        for p in SRC_DIR.rglob("*")
        if p.suffix in SUFFIXES and p.is_file()
    }


def main() -> int:
    if not VCXPROJ.exists():
        print(f"ERROR: {VCXPROJ} not found", file=sys.stderr)
        return 2

    in_project = project_entries()
    on_disk = disk_entries()

    missing_from_project = sorted(on_disk - in_project)
    missing_from_disk = sorted(in_project - on_disk)

    if not missing_from_project and not missing_from_disk:
        print(f"OK: vcxproj and src/ are in sync ({len(on_disk)} files).")
        return 0

    if missing_from_project:
        print("Source files on disk but MISSING from Magic-Sand.vcxproj:")
        for path in missing_from_project:
            print(f"  + {path}")
    if missing_from_disk:
        print("vcxproj entries pointing at files that DO NOT exist on disk:")
        for path in missing_from_disk:
            print(f"  - {path}")
    print(
        "\nFix: open Magic-Sand.vcxproj and add/remove the listed "
        "<ClCompile>/<ClInclude> entries so they match src/.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
