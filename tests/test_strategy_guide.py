"""Tests for the personal strategy-guide file boundary."""

from pathlib import Path

import pytest

from pvz_ai.strategy_guide import (
    BUNDLED_REFERENCE,
    MAX_GUIDE_BYTES,
    STATUS_FILLED,
    STATUS_UNFILLED,
    StrategyGuideError,
    default_guide_path,
    ensure_guide,
    read_guide,
)


def test_default_guide_uses_override_then_local_app_data(monkeypatch, tmp_path):
    override = tmp_path / "personal" / "my-strategy.md"
    monkeypatch.setenv("PVZ_AI_STRATEGY_FILE", str(override))
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path / "local"))
    assert default_guide_path() == override.resolve()

    monkeypatch.delenv("PVZ_AI_STRATEGY_FILE")
    assert default_guide_path() == (tmp_path / "local" / "PvZ AI Bridge" / "strategy.md").resolve()


def test_read_missing_guide_returns_unfilled_reference_without_creating_it(tmp_path):
    path = tmp_path / "not-created" / "strategy.md"

    guide = read_guide(path)

    assert guide["path"] == str(path.resolve())
    assert guide["filled"] is False
    assert guide["source"] == "bundled_scaffold"
    assert guide["content"] == BUNDLED_REFERENCE
    assert STATUS_UNFILLED in guide["content"]
    for heading in ("Mode", "Card lineup", "Layout", "Timing and pacing",
                    "Shovel rules", "Endless levels", "Prohibitions"):
        assert f"## {heading}" in guide["content"]
    assert not path.exists()


def test_scaffold_asset_matches_runtime_reference():
    asset = Path(__file__).parents[1] / "skills" / "pvz-play" / "assets" / "strategy-template.md"
    assert asset.read_text(encoding="utf-8") == BUNDLED_REFERENCE


def test_ensure_guide_creates_missing_file_and_never_overwrites_personal_content(tmp_path):
    path = tmp_path / "profile" / "strategy.md"
    created = ensure_guide(path)
    assert created["source"] == "user_file"
    assert created["filled"] is False
    assert created["content"] == BUNDLED_REFERENCE

    personal_content = f"{STATUS_FILLED}\r\n# My guide\r\nUse my stated plan.\r\n"
    path.write_bytes(personal_content.encode("utf-8"))
    before = path.read_bytes()
    result = ensure_guide(path)

    assert result["filled"] is True
    assert result["content"] == personal_content
    assert path.read_bytes() == before


def test_guide_is_filled_only_with_explicit_status_marker(tmp_path):
    path = tmp_path / "strategy.md"
    path.write_text(STATUS_FILLED + "\n## Mode\nMy tactics\n", encoding="utf-8")
    assert read_guide(path)["filled"] is True
    path.write_text("\ufeff" + STATUS_FILLED + "\nMy tactics\n", encoding="utf-8")
    assert read_guide(path)["filled"] is True
    path.write_text(STATUS_UNFILLED + "\n" + STATUS_FILLED, encoding="utf-8")
    assert read_guide(path)["filled"] is False
    path.write_text("# My guide\nMy tactics\n", encoding="utf-8")
    assert read_guide(path)["filled"] is False
    path.write_text("# My guide\n" + STATUS_FILLED + "\n", encoding="utf-8")
    assert read_guide(path)["filled"] is False


def test_read_rejects_oversized_and_invalid_utf8_guides(tmp_path):
    path = tmp_path / "strategy.md"
    path.write_bytes(b"x" * (MAX_GUIDE_BYTES + 1))
    with pytest.raises(StrategyGuideError, match="size limit"):
        read_guide(path)

    path.write_bytes(b"\xff\xfe")
    with pytest.raises(StrategyGuideError, match="UTF-8"):
        read_guide(path)
