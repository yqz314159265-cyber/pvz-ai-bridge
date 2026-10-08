"""Collect Python runtime and dependency license texts for a release bundle."""
from __future__ import annotations

import importlib.metadata as metadata
import re
import shutil
import sys
from pathlib import Path


LICENSE_NAME = re.compile(r"^(license|licence|copying|notice)(\.|$)", re.IGNORECASE)


def collect(destination: Path) -> int:
    destination.mkdir(parents=True, exist_ok=True)
    count = 0
    python_license = Path(sys.base_prefix) / "LICENSE.txt"
    if not python_license.is_file():
        raise RuntimeError(f"Python license file is missing: {python_license}")
    shutil.copy2(python_license, destination / "Python-LICENSE.txt")
    count += 1

    distributions = sorted(metadata.distributions(), key=lambda item: (item.metadata.get("Name") or "").casefold())
    for dist_index, dist in enumerate(distributions, 1):
        name = dist.metadata.get("Name") or "unknown"
        safe_name = re.sub(r"[^A-Za-z0-9._-]+", "_", name)
        candidates: list[Path] = []
        for relative in dist.files or ():
            filename = Path(str(relative)).name
            if LICENSE_NAME.match(filename):
                path = Path(dist.locate_file(relative))
                if path.is_file():
                    candidates.append(path)
        unique = sorted(set(candidates), key=lambda path: str(path).casefold())
        for index, source in enumerate(unique, 1):
            suffix = source.suffix or ".txt"
            label = source.name.replace("/", "_").replace("\\", "_")
            target = destination / f"{safe_name}-{dist_index}-{index}-{label}"
            if target.exists():
                raise RuntimeError(f"Duplicate license destination: {target}")
            shutil.copy2(source, target)
            count += 1
    if count < 2:
        raise RuntimeError("No Python dependency license texts were collected.")
    return count


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: collect_licenses.py DESTINATION")
    print(collect(Path(sys.argv[1])))
