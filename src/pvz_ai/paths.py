"""Portable resource, user-data, and exact Steam game path discovery."""
from __future__ import annotations

import os
from pathlib import Path
import re
import sys

STEAM_APP_ID = "3590"
GAME_EXECUTABLE = "PlantsVsZombies.exe"


def source_root() -> Path:
    """Project root for a source checkout."""
    return Path(__file__).resolve().parents[2]


def resource_root() -> Path:
    """Bundled resource root for PyInstaller, otherwise the source project root."""
    bundled = getattr(sys, "_MEIPASS", None) if getattr(sys, "frozen", False) else None
    return Path(bundled).resolve() if bundled else source_root()


def user_data_dir() -> Path:
    """Per-user writable state location; this function does not create it."""
    override = os.environ.get("PVZ_AI_DATA_DIR")
    if override:
        return Path(os.path.expandvars(override)).expanduser().resolve()
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        return Path(os.path.expandvars(local_app_data)).expanduser() / "PvZ AI Bridge"
    if os.name == "nt":
        return Path.home() / "AppData" / "Local" / "PvZ AI Bridge"
    xdg = os.environ.get("XDG_DATA_HOME")
    return (Path(xdg).expanduser() if xdg else Path.home() / ".local" / "share") / "PvZ AI Bridge"


def default_native_dir() -> Path:
    """Bundled native resources when frozen; build output in a source checkout."""
    if getattr(sys, "frozen", False):
        return resource_root() / "native"
    return source_root() / "build" / "native" / "Release"


def _steam_roots_from_registry() -> list[Path]:
    if os.name != "nt":
        return []
    import winreg

    roots: list[Path] = []
    locations = (
        (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam"),
    )
    for hive, key_name in locations:
        try:
            with winreg.OpenKey(hive, key_name) as key:
                for name in ("SteamPath", "InstallPath", "SteamExe"):
                    try:
                        value, _ = winreg.QueryValueEx(key, name)
                    except OSError:
                        continue
                    path = Path(os.path.expandvars(str(value).strip().strip('"')))
                    roots.append(path.parent if path.name.casefold() == "steam.exe" else path)
        except OSError:
            continue
    return roots


def _library_roots(roots: list[Path]) -> list[Path]:
    libraries = list(roots)
    for root in list(roots):
        vdf = root / "steamapps" / "libraryfolders.vdf"
        try:
            text = vdf.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for value in re.findall(r'"path"\s*"((?:[^"\\]|\\.)*)"', text):
            libraries.append(Path(value.replace("\\\\", "\\")))
    result: list[Path] = []
    seen: set[str] = set()
    for root in libraries:
        try:
            path = root.expanduser().resolve()
            key = os.path.normcase(str(path))
        except (OSError, RuntimeError, ValueError):
            continue
        if key not in seen:
            seen.add(key)
            result.append(path)
    return result


def _manifest_game(manifest: Path) -> Path | None:
    try:
        text = manifest.read_text(encoding="utf-8", errors="strict")
    except (OSError, UnicodeError):
        return None
    values = {key.casefold(): value.replace("\\\\", "\\") for key, value in
              re.findall(r'"([^"\\]+)"\s*"((?:[^"\\]|\\.)*)"', text)}
    if values.get("appid") != STEAM_APP_ID:
        return None
    install_dir = values.get("installdir", "")
    if not install_dir or Path(install_dir).name != install_dir:
        return None
    game = manifest.parent / "common" / install_dir / GAME_EXECUTABLE
    return game.resolve() if game.is_file() else None


def discover_game_path() -> Path | None:
    """Find the installed app 3590 executable through Steam's registered libraries."""
    configured = os.environ.get("PVZ_AI_GAME")
    if configured:
        return Path(os.path.expandvars(configured)).expanduser().resolve()
    roots = _library_roots(_steam_roots_from_registry())
    for root in roots:
        candidate = _manifest_game(root / "steamapps" / f"appmanifest_{STEAM_APP_ID}.acf")
        if candidate is not None:
            return candidate
    return None


DEFAULT_GAME = discover_game_path()
