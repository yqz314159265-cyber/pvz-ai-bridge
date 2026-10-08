"""Read and initialize a user's human-editable PvZ strategy guide."""
from __future__ import annotations

import os
from pathlib import Path
from .paths import user_data_dir

MAX_GUIDE_BYTES = 64 * 1024
STATUS_FILLED = "<!-- pvz-strategy-guide: filled -->"
STATUS_UNFILLED = "<!-- pvz-strategy-guide: unfilled -->"

BUNDLED_REFERENCE = f"""{STATUS_UNFILLED}
# My Plants vs. Zombies Strategy Guide
# 我的《植物大战僵尸》策略指南

This is a fill-in scaffold, not a strategy recommendation. Replace the prompts
with tactics you personally want the assistant to follow. Set the status line
to `{STATUS_FILLED}` only after you have completed the sections you need.
这是供你填写的空白模板，不是策略建议。请把提示替换为你希望助手遵循的实际战术。
填写完需要的部分后，才把首行状态标记改为 `{STATUS_FILLED}`。

## Mode / 游戏模式

<!-- Fill in the mode or level types covered by this guide. -->
<!-- 填写本指南适用的游戏模式或关卡类型。 -->

## Card lineup / 卡片阵容

<!-- List your chosen plants, required cards, and any substitutions. -->
<!-- 列出你选择的植物、必选卡片以及可替换选项。 -->

## Layout / 布局

<!-- Describe your lane layout, opening, economy, and placement preferences. -->
<!-- 描述你的行列布局、开局、阳光经济和摆放偏好。 -->

## Timing and pacing / 时机与节奏

<!-- Describe when to plant, collect, wait, or use abilities. -->
<!-- 描述何时种植、收集、等待或使用能力。 -->

## Shovel rules / 铲除规则

<!-- Describe when plants may be removed and any plants that must stay. -->
<!-- 描述何时可以铲除植物，以及哪些植物必须保留。 -->

## Endless levels / 无尽关卡

<!-- Add your own wave, recovery, and resource rules for endless modes. -->
<!-- 填写你自己的无尽模式波次、恢复和资源规则。 -->

## Prohibitions / 禁止事项

<!-- List actions, plants, risks, or state changes the assistant must avoid. -->
<!-- 列出助手必须避免的操作、植物选择、风险或游戏状态变化。 -->
"""


class StrategyGuideError(ValueError):
    """The guide is too large or is not valid UTF-8 Markdown."""


def default_guide_path() -> Path:
    """Return the user-selected path or the per-user Local AppData location."""
    override = os.environ.get("PVZ_AI_STRATEGY_FILE")
    if override:
        return Path(os.path.expandvars(override)).expanduser().resolve(strict=False)
    return (user_data_dir() / "strategy.md").resolve(strict=False)


def _filled(content: str) -> bool:
    """Only treat a guide as user-filled when its explicit status marker says so."""
    lines = content.splitlines()
    return bool(lines and lines[0].lstrip("\ufeff").strip() == STATUS_FILLED)


def read_guide(path: str | os.PathLike[str] | None = None) -> dict:
    """Read a guide, returning the bundled unfilled scaffold when the file is absent.

    Reading never creates or changes files. The maximum accepted file size is
    ``MAX_GUIDE_BYTES``; malformed UTF-8 and oversized guides fail explicitly.
    """
    target = Path(path).expanduser().resolve(strict=False) if path is not None else default_guide_path()
    try:
        with target.open("rb") as stream:
            raw = stream.read(MAX_GUIDE_BYTES + 1)
    except FileNotFoundError:
        return {"path": str(target), "filled": False, "content": BUNDLED_REFERENCE,
                "source": "bundled_scaffold"}
    if len(raw) > MAX_GUIDE_BYTES:
        raise StrategyGuideError(f"Strategy guide exceeds the {MAX_GUIDE_BYTES}-byte size limit.")
    try:
        content = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise StrategyGuideError("Strategy guide must be valid UTF-8 Markdown.") from exc
    return {"path": str(target), "filled": _filled(content), "content": content,
            "source": "user_file"}


def ensure_guide(path: str | os.PathLike[str] | None = None) -> dict:
    """Create the scaffold only when the destination does not already exist.

    Uses exclusive creation so a concurrent user edit or another initializer
    cannot be overwritten. Existing content is returned unchanged.
    """
    target = Path(path).expanduser().resolve(strict=False) if path is not None else default_guide_path()
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        with target.open("xb") as stream:
            stream.write(BUNDLED_REFERENCE.encode("utf-8"))
    except FileExistsError:
        pass
    return read_guide(target)
