"""Write an external package tree and SHA-256 manifest for a release directory."""
from __future__ import annotations

import hashlib
import json
import os
import sys
from pathlib import Path


def manifest(root: Path) -> tuple[list[dict], str]:
    root = Path(os.path.abspath(root))
    if not root.is_dir():
        raise FileNotFoundError(f"Package directory is missing: {root}")
    records = []
    lines = [f"Package: {root.name}"]
    for path in sorted((item for item in root.rglob("*") if item.is_file()),
                       key=lambda item: item.relative_to(root).as_posix().casefold()):
        relative = path.relative_to(root).as_posix()
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        records.append({"path": relative, "size": path.stat().st_size, "sha256": digest})
        lines.append(f"{digest}  {path.stat().st_size:>10}  {relative}")
    return records, "\n".join(lines) + "\n"


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: write_manifest.py PACKAGE_ROOT JSON_PATH TREE_PATH")
    package_root, json_path, tree_path = map(Path, sys.argv[1:])
    records, tree = manifest(package_root)
    json_path.parent.mkdir(parents=True, exist_ok=True)
    tree_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps({"root": package_root.name, "files": records},
                                    ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tree_path.write_text(tree, encoding="utf-8")
