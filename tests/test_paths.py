from __future__ import annotations

from pathlib import Path
from unittest import mock

from pvz_ai import paths
from pvz_ai.host import Host


def test_resource_root_selects_meipass_only_when_frozen(tmp_path):
    with mock.patch.object(paths.sys, "frozen", True, create=True), \
         mock.patch.object(paths.sys, "_MEIPASS", str(tmp_path), create=True):
        assert paths.resource_root() == tmp_path.resolve()
        assert paths.default_native_dir() == tmp_path.resolve() / "native"
    with mock.patch.object(paths.sys, "frozen", False, create=True):
        assert paths.resource_root() == paths.source_root()
        assert paths.default_native_dir() == paths.source_root() / "build" / "native" / "Release"


def test_user_data_dir_prefers_override_then_localappdata(monkeypatch, tmp_path):
    override = tmp_path / "portable-state"
    monkeypatch.setenv("PVZ_AI_DATA_DIR", str(override))
    monkeypatch.setenv("LOCALAPPDATA", str(tmp_path / "local"))
    assert paths.user_data_dir() == override.resolve()
    monkeypatch.delenv("PVZ_AI_DATA_DIR")
    assert paths.user_data_dir() == tmp_path / "local" / "PvZ AI Bridge"


def test_registered_library_discovery_requires_exact_appmanifest(monkeypatch, tmp_path):
    primary = tmp_path / "Steam"
    library = tmp_path / "GameLibrary"
    (primary / "steamapps").mkdir(parents=True)
    (primary / "steamapps" / "libraryfolders.vdf").write_text(
        f'"libraryfolders" {{ "1" {{ "path" "{str(library).replace(chr(92), chr(92)*2)}" }} }}',
        encoding="utf-8")
    steamapps = library / "steamapps"
    install = steamapps / "common" / "Plants Vs Zombies"
    install.mkdir(parents=True)
    exe = install / "PlantsVsZombies.exe"
    exe.write_bytes(b"test fixture")
    (steamapps / "appmanifest_3590.acf").write_text(
        '"AppState" { "appid" "3590" "installdir" "Plants Vs Zombies" }', encoding="utf-8")
    monkeypatch.delenv("PVZ_AI_GAME", raising=False)
    monkeypatch.setattr(paths, "_steam_roots_from_registry", lambda: [primary])
    assert paths.discover_game_path() == exe.resolve()
    assert Host(None).game_path == exe.resolve()


def test_discovery_rejects_wrong_appmanifest_and_honors_explicit_game(monkeypatch, tmp_path):
    steam = tmp_path / "Steam"
    app = steam / "steamapps" / "common" / "Plants Vs Zombies"
    app.mkdir(parents=True)
    exe = app / "PlantsVsZombies.exe"
    exe.write_bytes(b"configured fixture")
    manifest = steam / "steamapps" / "appmanifest_3590.acf"
    manifest.write_text('"AppState" { "appid" "9999" "installdir" "Plants Vs Zombies" }', encoding="utf-8")
    monkeypatch.delenv("PVZ_AI_GAME", raising=False)
    monkeypatch.setattr(paths, "_steam_roots_from_registry", lambda: [steam])
    assert paths.discover_game_path() is None
    explicit = tmp_path / "exact-explicit.exe"
    host = Host(explicit)
    assert host.game_path == explicit


def test_environment_game_override_is_resolved_but_not_identity_bypassed(monkeypatch, tmp_path):
    exe = tmp_path / "PlantsVsZombies.exe"
    exe.write_bytes(b"not a verified game")
    monkeypatch.setenv("PVZ_AI_GAME", str(exe))
    assert paths.discover_game_path() == exe.resolve()
    assert Host(None).game_path == exe.resolve()
