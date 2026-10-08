"""Explicit, non-overwriting installation of the packaged AI skill."""
from __future__ import annotations
import os
from pathlib import Path
import shutil
from .paths import resource_root


def install_skill(destination: Path | None = None) -> dict:
    source = resource_root() / "skills" / "pvz-play"
    if not (source / "SKILL.md").is_file():
        raise RuntimeError("Packaged skill is missing / 安装包中缺少 Skill")
    codex_dir = Path(os.environ.get("CODEX_HOME", str(Path.home() / ".codex")))
    target = Path(destination) if destination is not None else codex_dir / "skills" / "pvz-play"
    if target.exists():
        raise FileExistsError(f"Existing skill preserved / 已保留原有技能: {target}")
    shutil.copytree(source, target, dirs_exist_ok=False)
    return {"installed": True, "path": str(target), "restart_ai_client": True}
