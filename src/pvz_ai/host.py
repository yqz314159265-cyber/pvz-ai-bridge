"""Game discovery, process control, backup, and single-controller facade."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import threading
import time
import re
import uuid
from datetime import datetime, timezone
from .paths import (DEFAULT_GAME as _INITIAL_DEFAULT_GAME, default_native_dir,
                    discover_game_path, user_data_dir)

EXPECTED_VERSION = "1.2.0.1096"
STEAM_APP_ID = "3590"
DEFAULT_GAME = _INITIAL_DEFAULT_GAME
EXPECTED_LAUNCHER_SHA256 = "868f8e2bab0d6a7ef8afc4c5960c608eccef82bd086bd6e0c0e2670199a5ca45"
EXPECTED_RUNTIME_PATH = Path(r"C:\ProgramData\PopCap Games\PlantsVsZombies\popcapgame1.exe")
EXPECTED_RUNTIME_BASE = 0x400000
EXPECTED_RUNTIME_SIZE = 0x41E000
EXPECTED_RUNTIME_TIMESTAMP = 0x4D02B058
EXPECTED_TEXT_RVA = 0x1000
EXPECTED_TEXT_SIZE = 0x2D3E9C
EXPECTED_TEXT_SHA256 = "420db8be5f6d704d10ea1fb7c7c815623807b7a2cebb55c80fd66ce70447ff64"
PROGRAM_DATA = Path(os.environ.get("PROGRAMDATA", r"C:\ProgramData"))
SAVE_DIRS = [PROGRAM_DATA / "Steam" / "PlantsVsZombies" / "userdata",
             PROGRAM_DATA / "Steam" / "PlantsVsZombies" / "userdata_backup"]


def _launcher_identity_matches(product: str, version: str, digest: str) -> bool:
    return ("Plants vs. Zombies" in product and version == EXPECTED_VERSION and
            digest.casefold() == EXPECTED_LAUNCHER_SHA256)


def _registered_steam_roots() -> list[Path]:
    """Read Steam install locations from Windows registry without changing them."""
    if os.name != "nt":
        return []
    import winreg
    roots: list[Path] = []
    keys = (
        (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam"),
    )
    for hive, key_name in keys:
        try:
            with winreg.OpenKey(hive, key_name) as key:
                for value_name in ("SteamPath", "InstallPath", "SteamExe"):
                    try:
                        value, _ = winreg.QueryValueEx(key, value_name)
                    except OSError:
                        continue
                    path = Path(os.path.expandvars(str(value).strip().strip('"')))
                    roots.append(path.parent if path.name.casefold() == "steam.exe" else path)
        except OSError:
            continue
    return roots


def _manifest_values(manifest: Path) -> dict[str, str]:
    text = manifest.read_text(encoding="utf-8", errors="strict")
    return {key.casefold(): value.replace("\\\\", "\\") for key, value in
            re.findall(r'"([^"\\]+)"\s*"((?:[^"\\]|\\.)*)"', text)}


def _game_app_manifest(game_path: Path) -> tuple[Path, dict[str, str]]:
    """Return the exact appmanifest which owns this game executable."""
    game_path = Path(os.path.abspath(os.fspath(game_path)))
    if game_path.name.casefold() != "plantsvszombies.exe":
        raise RuntimeError("The configured executable is not the supported PlantsVsZombies.exe.")
    steamapps = game_path.parent.parent.parent
    manifest = steamapps / f"appmanifest_{STEAM_APP_ID}.acf"
    try:
        values = _manifest_values(manifest)
    except (OSError, UnicodeError) as exc:
        raise RuntimeError(f"Steam appmanifest {STEAM_APP_ID} is unavailable: {manifest}") from exc
    if values.get("appid") != STEAM_APP_ID:
        raise RuntimeError("Steam appmanifest does not identify Plants vs. Zombies app 3590.")
    install_dir = values.get("installdir", "")
    if not install_dir or Path(install_dir).name != install_dir:
        raise RuntimeError("Steam appmanifest has an invalid install directory.")
    expected = steamapps / "common" / install_dir / "PlantsVsZombies.exe"
    if _normpath(expected) != _normpath(game_path):
        raise RuntimeError("Configured game path does not match the exact appmanifest 3590 install path.")
    return manifest, values


def discover_steam_roots(game_path: Path | None = None) -> list[Path]:
    """Find Steam install/library roots from registry and the exact app 3590 manifest."""
    roots: list[Path] = []
    game_path = Path(game_path) if game_path is not None else discover_game_path()
    try:
        if game_path is not None:
            manifest, values = _game_app_manifest(game_path)
            roots.append(manifest.parent.parent)
            launcher = values.get("launcherpath")
            if launcher:
                launcher_path = Path(os.path.expandvars(launcher))
                roots.append(launcher_path.parent if launcher_path.name.casefold() == "steam.exe" else launcher_path)
    except (OSError, RuntimeError):
        pass
    roots.extend(_registered_steam_roots())

    # Steam's library list is read-only and only contributes roots for save discovery.
    for root in list(roots):
        library_file = root / "steamapps" / "libraryfolders.vdf"
        try:
            text = library_file.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for value in re.findall(r'"path"\s*"((?:[^"\\]|\\.)*)"', text):
            candidate = Path(value.replace("\\\\", "\\"))
            roots.append(candidate)

    unique: list[Path] = []
    seen: set[str] = set()
    for root in roots:
        try:
            normalized = _normpath(root)
        except (OSError, ValueError):
            continue
        if normalized not in seen:
            seen.add(normalized)
            unique.append(Path(os.path.abspath(os.fspath(root))))
    return unique


def _steam_roots(game_path: Path | None = None) -> list[Path]:
    """Compatibility name for the roots used by save discovery."""
    return discover_steam_roots(game_path)


def _steam_executable(game_path: Path) -> Path:
    manifest, values = _game_app_manifest(game_path)
    candidates: list[Path] = []
    launcher = values.get("launcherpath")
    if launcher:
        candidate = Path(os.path.expandvars(launcher))
        candidates.append(candidate if candidate.name.casefold() == "steam.exe" else candidate / "Steam.exe")
    for root in _registered_steam_roots():
        candidates.append(root / "Steam.exe")
    for candidate in candidates:
        if candidate.name.casefold() == "steam.exe" and candidate.is_file():
            return Path(os.path.abspath(os.fspath(candidate)))
    raise RuntimeError(f"Could not find Steam.exe from registry or appmanifest {STEAM_APP_ID}: {manifest}")


def discover_game(path: Path | None = None) -> dict:
    """Read-only exact-path/product/version discovery; never searches arbitrary installs."""
    path = Path(path) if path is not None else discover_game_path()
    if path is None:
        return {"found": False, "path": None, "expected_version": EXPECTED_VERSION,
                "reason": f"Steam app {STEAM_APP_ID} was not found in registered libraries; set PVZ_AI_GAME to configure it."}
    p = path.resolve()
    if not p.is_file():
        return {"found": False, "path": str(p), "expected_version": EXPECTED_VERSION}
    if os.name != "nt":
        return {"found": True, "path": str(p), "version": None, "supported": False,
                "reason": "Windows file version information is required."}
    try:
        import ctypes
        from ctypes import wintypes
        version = ctypes.WinDLL("version", use_last_error=True)
        size = version.GetFileVersionInfoSizeW(str(p), None)
        if not size:
            raise OSError("No file version resource")
        data = ctypes.create_string_buffer(size)
        if not version.GetFileVersionInfoW(str(p), 0, size, data):
            raise OSError("Cannot read version resource")
        ptr, length = wintypes.LPVOID(), wintypes.UINT()
        if not version.VerQueryValueW(data, "\\VarFileInfo\\Translation", ctypes.byref(ptr), ctypes.byref(length)):
            raise OSError("No version translation")
        lang, cp = (ctypes.cast(ptr, ctypes.POINTER(wintypes.WORD * 2)).contents)
        def query(field):
            out, n = wintypes.LPVOID(), wintypes.UINT()
            key = f"\\StringFileInfo\\{lang:04x}{cp:04x}\\{field}"
            if version.VerQueryValueW(data, key, ctypes.byref(out), ctypes.byref(n)) and n.value:
                return ctypes.wstring_at(out)
            return ""
        product, product_version = query("ProductName"), query("ProductVersion")
        digest = _file_sha256(p)
        supported = _launcher_identity_matches(product, product_version, digest)
        return {"found": True, "path": str(p), "product": product, "version": product_version,
                "sha256": digest, "supported": supported,
                "reason": None if supported else "Executable product, exact version, or SHA-256 does not match supported build."}
    except Exception as e:
        return {"found": True, "path": str(p), "version": None, "supported": False,
                "reason": f"Could not verify executable identity/version: {e}"}


def find_saves(game_path: Path | None = None) -> Path | None:
    steam_candidates = []
    for root in _steam_roots(game_path):
        userdata = root / "userdata"
        # Steam saves live below <SteamID>/<AppID>; restrict discovery to app 3590.
        try:
            steam_candidates.extend(p / "3590" for p in userdata.iterdir() if p.is_dir())
        except OSError:
            pass
    active_candidates = steam_candidates + [p for p in SAVE_DIRS if p.name.casefold() != "userdata_backup"]
    found = next((p for p in active_candidates if p.is_dir() and any(x.is_file() for x in p.rglob("*"))), None)
    if found is not None:
        return found
    return next((p for p in SAVE_DIRS if p.name.casefold() == "userdata_backup" and p.is_dir()
                 and any(x.is_file() for x in p.rglob("*"))), None)


def _iter_process_pids() -> list[int]:
    if os.name != "nt":
        return []
    import ctypes
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    values = (ctypes.c_ulong * 8192)()
    needed = ctypes.c_ulong()
    if not psapi.EnumProcesses(values, ctypes.sizeof(values), ctypes.byref(needed)):
        return []
    return [int(values[i]) for i in range(needed.value // ctypes.sizeof(ctypes.c_ulong)) if values[i]]


def _process_image_path(pid: int) -> Path | None:
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.QueryFullProcessImageNameW.argtypes = (wintypes.HANDLE, wintypes.DWORD,
                                                     wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD))
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return None
    try:
        buf = ctypes.create_unicode_buffer(32768)
        length = wintypes.DWORD(len(buf))
        if not kernel32.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(length)):
            return None
        return Path(buf.value)
    finally:
        kernel32.CloseHandle(handle)


def _process_parent_pid(pid: int) -> int | None:
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                    ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                    ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                    ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG),
                    ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateToolhelp32Snapshot.argtypes = (wintypes.DWORD, wintypes.DWORD)
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.Process32FirstW.argtypes = (wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W))
    kernel32.Process32NextW.argtypes = (wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W))
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    snapshot = kernel32.CreateToolhelp32Snapshot(0x2, 0)  # TH32CS_SNAPPROCESS
    if snapshot == ctypes.c_void_p(-1).value:
        return None
    try:
        entry = PROCESSENTRY32W(); entry.dwSize = ctypes.sizeof(entry)
        ok = kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while ok:
            if entry.th32ProcessID == pid:
                return int(entry.th32ParentProcessID)
            ok = kernel32.Process32NextW(snapshot, ctypes.byref(entry))
        return None
    finally:
        kernel32.CloseHandle(snapshot)


def _normpath(path: Path | str) -> str:
    return os.path.normcase(os.path.abspath(os.fspath(path)))


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _stage_bridge_dll(source: Path, loaded_dir: Path) -> tuple[Path, str]:
    """Create a uniquely named, hash-verified copy for one LoadLibrary lifetime."""
    source = Path(os.path.abspath(os.fspath(source)))
    if not source.is_file():
        raise RuntimeError("Native bridge DLL is not a regular file.")
    source_hash = _file_sha256(source)
    loaded_dir.mkdir(parents=True, exist_ok=True)
    destination = loaded_dir / f"pvz_bridge-{source_hash[:16]}-{uuid.uuid4().hex}.dll"
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0)
    fd = os.open(destination, flags, 0o600)
    try:
        with os.fdopen(fd, "wb") as dst:
            with source.open("rb") as src:
                shutil.copyfileobj(src, dst, length=1024 * 1024)
            dst.flush()
            os.fsync(dst.fileno())
    except Exception:
        # Do not replace or unlink a name which may already have been loaded.
        raise
    copied_hash = _file_sha256(destination)
    source_hash_after = _file_sha256(source)
    if copied_hash != source_hash or source_hash_after != source_hash:
        raise RuntimeError("Bridge DLL changed while staging or the copied SHA-256 does not match.")
    return Path(os.path.abspath(os.fspath(destination))), copied_hash


def _verify_launcher_process(pid: int, expected_path: Path) -> None:
    path = _process_image_path(pid)
    if path is None or _normpath(path) != _normpath(expected_path):
        raise RuntimeError("Target is not descended from the exact verified Steam launcher path.")
    if not path.is_file():
        raise RuntimeError("Verified Steam launcher image is not available for read-only identity checks.")
    identity = discover_game(path)
    if not identity.get("supported") or _file_sha256(path).casefold() != EXPECTED_LAUNCHER_SHA256:
        raise RuntimeError("Launcher product/version or SHA-256 does not match the verified Steam executable.")


def _processes_at_path(pids: set[int], expected_path: Path, image_path=None) -> set[int]:
    image_path = image_path or _process_image_path
    return {pid for pid in pids
            if (image := image_path(pid)) is not None and _normpath(image) == _normpath(expected_path)}


def _descends_from(pid: int, ancestors: set[int], parent_pid=None, max_depth: int = 32) -> bool:
    """Check the current parent chain, bounding traversal and rejecting cycles."""
    parent_pid = parent_pid or _process_parent_pid
    seen = {pid}
    current = pid
    for _ in range(max_depth):
        parent = parent_pid(current)
        if parent is None or parent <= 0 or parent in seen:
            return False
        if parent in ancestors:
            return True
        seen.add(parent)
        current = parent
    return False


def _wait_for_steam_process(existing: set[int], steam_exe: Path, timeout: float,
                            poll_interval: float = 0.2, iter_pids=None, image_path=None,
                            sleep=time.sleep) -> set[int]:
    iter_pids = iter_pids or _iter_process_pids
    image_path = image_path or _process_image_path
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pids = {pid for pid in iter_pids() if pid not in existing}
        matches = _processes_at_path(pids, steam_exe, image_path)
        if matches:
            return matches
        sleep(min(poll_interval, max(0, deadline - time.monotonic())))
    raise TimeoutError("Steam did not start from the verified Steam.exe path.")


def _wait_for_game_launcher(existing: set[int], steam_pids: set[int], game_path: Path,
                            timeout: float = 20.0, poll_interval: float = 0.2,
                            iter_pids=None, image_path=None, parent_pid=None,
                            sleep=time.sleep) -> int:
    """Wait for a new app 3590 launcher whose parent chain reaches the verified Steam process."""
    iter_pids = iter_pids or _iter_process_pids
    image_path = image_path or _process_image_path
    parent_pid = parent_pid or _process_parent_pid
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for pid in iter_pids():
            if pid in existing or pid in steam_pids:
                continue
            image = image_path(pid)
            if image is None or _normpath(image) != _normpath(game_path):
                continue
            if not _descends_from(pid, steam_pids, parent_pid=parent_pid):
                continue
            try:
                _verify_launcher_process(pid, game_path)
                return pid
            except (OSError, RuntimeError):
                continue
        sleep(min(poll_interval, max(0, deadline - time.monotonic())))
    raise TimeoutError("Steam app 3590 did not create a new verified game launcher before timeout.")


def _verify_runtime_pe(pid: int, verify_text: bool = True) -> None:
    """Verify the live x86 main module without requiring its deleted image file."""
    if os.name != "nt":
        raise RuntimeError("Read-only runtime PE verification requires Windows.")
    import ctypes
    from ctypes import wintypes
    class MODULEENTRY32W(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("th32ModuleID", wintypes.DWORD),
                    ("th32ProcessID", wintypes.DWORD), ("GlblcntUsage", wintypes.DWORD),
                    ("ProccntUsage", wintypes.DWORD), ("modBaseAddr", ctypes.POINTER(ctypes.c_ubyte)),
                    ("modBaseSize", wintypes.DWORD), ("hModule", wintypes.HMODULE),
                    ("szModule", wintypes.WCHAR * 256), ("szExePath", wintypes.WCHAR * 260)]
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.ReadProcessMemory.argtypes = (wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID,
                                           ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t))
    kernel32.CreateToolhelp32Snapshot.argtypes = (wintypes.DWORD, wintypes.DWORD)
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.Module32FirstW.argtypes = (wintypes.HANDLE, ctypes.POINTER(MODULEENTRY32W))
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    process = kernel32.OpenProcess(0x0410, False, pid)  # QUERY_INFORMATION | VM_READ
    if not process:
        raise RuntimeError("Could not read the live runtime process memory.")
    snapshot = None
    try:
        snapshot = kernel32.CreateToolhelp32Snapshot(0x8 | 0x10, pid)  # MODULE | MODULE32
        if snapshot == ctypes.c_void_p(-1).value:
            raise RuntimeError("Could not enumerate the live runtime's main module.")
        module = MODULEENTRY32W(); module.dwSize = ctypes.sizeof(module)
        if not kernel32.Module32FirstW(snapshot, ctypes.byref(module)):
            raise RuntimeError("Could not read the live runtime's main module.")
        base = ctypes.cast(module.modBaseAddr, ctypes.c_void_p).value
        if base != EXPECTED_RUNTIME_BASE or module.modBaseSize != EXPECTED_RUNTIME_SIZE:
            raise RuntimeError("Live runtime module base or image size is unsupported.")

        def read(address: int, size: int) -> bytes:
            buffer = ctypes.create_string_buffer(size)
            count = ctypes.c_size_t()
            if not kernel32.ReadProcessMemory(process, ctypes.c_void_p(address), buffer, size, ctypes.byref(count)) or count.value != size:
                raise RuntimeError("Could not read the complete live runtime PE data.")
            return buffer.raw

        dos = read(base, 64)
        if dos[:2] != b"MZ":
            raise RuntimeError("Live runtime has no valid DOS header.")
        (lfanew,) = struct.unpack_from("<I", dos, 0x3C)
        if not 64 <= lfanew <= 1024 * 1024:
            raise RuntimeError("Live runtime PE header offset is invalid.")
        fixed = read(base + lfanew, 24)
        if fixed[:4] != b"PE\0\0":
            raise RuntimeError("Live runtime has no valid PE signature.")
        machine, sections, timestamp, _, _, optional_size, _ = struct.unpack_from("<HHIIIHH", fixed, 4)
        optional = read(base + lfanew + 24, optional_size)
        if (machine != 0x14C or timestamp != EXPECTED_RUNTIME_TIMESTAMP or optional_size != 224 or
                optional[:2] != b"\x0b\x01" or struct.unpack_from("<I", optional, 56)[0] != EXPECTED_RUNTIME_SIZE or
                not 1 <= sections <= 32):
            raise RuntimeError("Live runtime PE machine, timestamp, or image size is unsupported.")
        section_bytes = read(base + lfanew + 24 + optional_size, sections * 40)
        text_rva = text_size = None
        for index in range(sections):
            section = section_bytes[index * 40:(index + 1) * 40]
            if section[:8].split(b"\0", 1)[0] == b".text":
                text_size, text_rva = struct.unpack_from("<II", section, 8)
                break
        if (text_rva, text_size) != (EXPECTED_TEXT_RVA, EXPECTED_TEXT_SIZE):
            raise RuntimeError("Live runtime .text section layout is unsupported.")
        if verify_text and hashlib.sha256(read(base + text_rva, text_size)).hexdigest() != EXPECTED_TEXT_SHA256:
            raise RuntimeError("Live runtime .text hash does not match the verified game image.")
    finally:
        if snapshot and snapshot != ctypes.c_void_p(-1).value:
            kernel32.CloseHandle(snapshot)
        kernel32.CloseHandle(process)


def _acquire_controller_lease(pid: int):
    """Acquire a current-session, current-user named mutex for one PID."""
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateMutexW.argtypes = (wintypes.LPVOID, wintypes.BOOL, wintypes.LPCWSTR)
    kernel32.CreateMutexW.restype = wintypes.HANDLE
    kernel32.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    kernel32.WaitForSingleObject.restype = wintypes.DWORD
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel32.ReleaseMutex.argtypes = (wintypes.HANDLE,)
    handle = kernel32.CreateMutexW(None, False, rf"Local\pvz-ai-controller-{pid}")
    if not handle:
        raise OSError(f"Could not create controller lease (Windows error {ctypes.get_last_error()}).")

    class Lease:
        def __init__(self):
            self.stop = threading.Event()
            self.ready = threading.Event()
            self.acquired = False
            self.thread = threading.Thread(target=self._hold, name=f"pvz-controller-{pid}", daemon=True)
            self.thread.start()
            self.ready.wait()

        def _hold(self):
            result = kernel32.WaitForSingleObject(handle, 0)
            self.acquired = result in (0, 0x80)  # WAIT_OBJECT_0 or WAIT_ABANDONED
            self.ready.set()
            if self.acquired:
                self.stop.wait()
                kernel32.ReleaseMutex(handle)
            kernel32.CloseHandle(handle)

        def release(self):
            if self.acquired:
                self.stop.set()
                self.thread.join()

    lease = Lease()
    if not lease.acquired:
        lease.thread.join()
        raise RuntimeError(f"Another host already controls process {pid}.")
    return lease


def _release_controller_lease(handle) -> None:
    if handle is not None:
        handle.release()


def _wait_for_game_process(existing: set[int], launcher_pid: int, game_path: Path,
                           timeout: float = 20.0, poll_interval: float = 0.2,
                           iter_pids=None, image_path=None, parent_pid=None,
                           verify_process=None, sleep=time.sleep) -> int:
    """Wait for a new diskless runtime child of the exact launcher and verify its in-memory PE."""
    iter_pids = iter_pids or _iter_process_pids
    image_path = image_path or _process_image_path
    deadline = time.monotonic() + timeout
    parent_pid = parent_pid or _process_parent_pid
    verify_process = verify_process or (lambda target_pid: _verify_runtime_pe(target_pid, verify_text=True))
    while time.monotonic() < deadline:
        for pid in iter_pids():
            if pid == launcher_pid or pid in existing:
                continue
            image = image_path(pid)
            if image is None:
                continue
            if _normpath(image) != _normpath(EXPECTED_RUNTIME_PATH):
                continue
            if parent_pid(pid) != launcher_pid:
                continue
            try:
                _verify_launcher_process(launcher_pid, game_path)
                verify_process(pid)
                return pid
            except (OSError, RuntimeError):
                continue
        sleep(min(poll_interval, max(0, deadline - time.monotonic())))
    raise TimeoutError("Steam launcher did not create a runtime with the verified in-memory PE fingerprint before timeout.")


def _tree_hash(root: Path) -> list[tuple[str, int, str]]:
    entries = []
    for file in sorted(p for p in root.rglob("*") if p.is_file()):
        digest = hashlib.sha256()
        with file.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        entries.append((str(file.relative_to(root)), file.stat().st_size, digest.hexdigest()))
    return entries


def backup_saves(source: Path | None = None, destination: Path | None = None) -> dict:
    src = source or find_saves()
    if src is None or not src.is_dir():
        raise RuntimeError("No PvZ userdata directory was found; no backup was created.")
    dst = destination or user_data_dir() / "backups" / datetime.now().strftime("%Y%m%d-%H%M%S")
    if dst.exists():
        raise FileExistsError(f"Backup destination already exists: {dst}")
    before = _tree_hash(src)
    dst.mkdir(parents=True)
    entries = []
    try:
        for file in sorted(p for p in src.rglob("*") if p.is_file()):
            rel = file.relative_to(src)
            target = dst / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
            digest = hashlib.sha256(target.read_bytes()).hexdigest()
            entries.append({"path": str(rel), "size": target.stat().st_size, "sha256": digest})
        after = _tree_hash(src)
        copied = [(entry["path"], entry["size"], entry["sha256"]) for entry in entries]
        if before != after or before != copied:
            raise RuntimeError("Save files changed during backup or the copied hashes do not match; backup rejected.")
    except Exception:
        shutil.rmtree(dst, ignore_errors=True)
        raise
    manifest = {"format": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
                "source": str(src.resolve()), "files": entries}
    (dst / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest


class Host:
    def __init__(self, game_path: Path | None = None, native_dir: Path | None = None):
        self.game_path = discover_game_path() if game_path is None else Path(game_path)
        self.native_dir = Path(native_dir) if native_dir is not None else default_native_dir()
        self.pid: int | None = None
        self._lock = threading.RLock()
        self._dispatch_lock = threading.Lock()
        self._pipe = None
        self._controller_lease = None
        self._plans: list[dict] = []
        self._plan_cancel = threading.Event()
        self._active_plan = False
        self.launch_timeout = 20.0

    def doctor(self) -> dict:
        game = discover_game(self.game_path)
        dll, injector = self.native_dir / "pvz_bridge.dll", self.native_dir / "pvz_injector.exe"
        return {"game": game, "native_dll": {"path": str(dll), "found": dll.is_file()},
                "injector": {"path": str(injector), "found": injector.is_file()},
                "pipe_transport": "Windows named pipe", "live_connection": False}

    def launch(self) -> dict:
        if self.game_path is None:
            raise RuntimeError("No Steam app 3590 install was discovered; set PVZ_AI_GAME to the exact game executable.")
        info = discover_game(self.game_path)
        if not info.get("supported"):
            raise RuntimeError("Refusing launch: exact supported game executable was not verified.")
        _game_app_manifest(self.game_path)
        steam_exe = _steam_executable(self.game_path)
        existing = set(_iter_process_pids())
        steam_pids = _processes_at_path(existing, steam_exe)
        startupinfo = None
        if os.name == "nt":
            startupinfo = subprocess.STARTUPINFO()
            startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startupinfo.wShowWindow = 0  # Hide only the Steam command helper; the game remains visible.
        subprocess.Popen([str(steam_exe), "-applaunch", STEAM_APP_ID],
                         cwd=str(steam_exe.parent), startupinfo=startupinfo)
        if not steam_pids:
            steam_pids = _wait_for_steam_process(existing, steam_exe, timeout=self.launch_timeout)
        launcher_pid = _wait_for_game_launcher(existing, steam_pids, self.game_path,
                                               timeout=self.launch_timeout)
        pid = _wait_for_game_process(existing, launcher_pid, self.game_path, timeout=self.launch_timeout,
                                     parent_pid=_process_parent_pid, verify_process=lambda game_pid: self._verify_runtime_memory(game_pid, verify_text=True))
        return self.attach(pid)

    def find_running_game(self) -> int:
        """Return the sole process matching the verified runtime and launcher identity."""
        matches = []
        for pid in _iter_process_pids():
            image = _process_image_path(pid)
            if image is None or _normpath(image) != _normpath(EXPECTED_RUNTIME_PATH):
                continue
            try:
                self._verify_target_process(pid, verify_text=False)
            except (OSError, RuntimeError):
                continue
            matches.append(pid)
        if not matches:
            raise RuntimeError("No running process matched the verified PvZ runtime and Steam launcher.")
        if len(matches) != 1:
            raise RuntimeError(f"Found {len(matches)} verified PvZ runtime processes; refusing ambiguous auto-attach.")
        return matches[0]

    def attach(self, pid: int) -> dict:
        if not isinstance(pid, int) or pid <= 0:
            raise ValueError("PID must be a positive integer.")
        # A ready bridge must be reused: an injected DLL cannot safely be replaced or
        # unloaded, and this avoids creating an unused copy on ordinary reattach.
        self._verify_target_process(pid, verify_text=False)
        if self._pipe is not None and self.pid not in (None, pid):
            raise RuntimeError("This host already controls a different process; detach it first.")
        try:
            return self.connect(pid)
        except Exception as exc:
            from .pipe import BridgeError
            if not isinstance(exc, BridgeError) or exc.code != "unavailable":
                raise
            if self._pipe is not None and self.pid == pid:
                raise RuntimeError("This host already has a bridge connection, but its pipe is unavailable; refusing a second injection.") from exc

        injector, dll = self.native_dir / "pvz_injector.exe", self.native_dir / "pvz_bridge.dll"
        if not injector.is_file() or not dll.is_file():
            raise RuntimeError("Native injector or bridge DLL is missing; build native targets first.")
        loaded_dir = (user_data_dir() / "native" / "loaded" if getattr(sys, "frozen", False)
                      else self.native_dir.parent / "loaded")
        staged_dll, _ = _stage_bridge_dll(dll, loaded_dir)
        self._acquire_lease(pid)
        try:
            subprocess.run([str(injector), "--pid", str(pid), "--dll", str(staged_dll)], check=True)
            return self.connect(pid)
        except Exception:
            self._release_lease()
            raise

    def connect(self, pid: int) -> dict:
        """Connect to an already injected process without modifying it."""
        if not isinstance(pid, int) or pid <= 0:
            raise ValueError("PID must be a positive integer.")
        # A bridge hook has patched the game .text by this point. Identity, launcher,
        # and PE layout still match; the injector already verified the untouched text.
        self._verify_target_process(pid, verify_text=False)
        with self._lock:
            if self._pipe is not None and self.pid != pid:
                raise RuntimeError("This host already controls a different process; detach it first.")
        self._acquire_lease(pid)
        from .pipe import PipeClient
        try:
            client = PipeClient(pid)
            hello = client.request("hello")
            if not isinstance(hello, dict):
                raise RuntimeError("Bridge hello did not return an object.")
            process = hello.get("process", {})
            version = hello.get("version") or process.get("version")
            if version != EXPECTED_VERSION:
                raise RuntimeError(f"Bridge hello version mismatch; expected {EXPECTED_VERSION}, got {version!r}.")
            if hello.get("ready") is not True:
                reason = hello.get("initialization_error") or "Native bridge reports ready=false."
                if hello.get("requires_restart"):
                    reason += " Restart the game before attaching again (requires_restart=true)."
                raise RuntimeError(reason)
            if not isinstance(hello.get("capabilities"), (dict, list)):
                raise RuntimeError("Bridge hello did not include advertised capabilities.")
            with self._lock:
                self.pid, self._pipe = pid, client
            return hello
        except Exception:
            with self._lock:
                if self.pid != pid:
                    self._release_lease()
            raise

    def _acquire_lease(self, pid: int) -> None:
        with self._lock:
            if self._controller_lease is not None:
                if self.pid == pid or self.pid is None:
                    return
                raise RuntimeError("This host already holds a controller lease for another process.")
            self._controller_lease = _acquire_controller_lease(pid)

    def _release_lease(self) -> None:
        with self._lock:
            lease, self._controller_lease = self._controller_lease, None
        _release_controller_lease(lease)

    def request(self, op: str, args: dict | None = None):
        args = args or {}
        if op == "action":
            from .templates import _validate_action
            args = _validate_action(args)
        if op in {"pause", "stop", "detach"}:
            self._plan_cancel.set()
        with self._lock:
            pipe = self._pipe
        if pipe is None:
            raise RuntimeError("No attached bridge. Use attach or launch first.")
        native_op = "detach" if op == "stop" else op
        if op == "action":
            state = pipe.request("state")
            capabilities = state.get("capabilities") if isinstance(state, dict) else None
            required = f"action.{args['type']}"
            if not isinstance(capabilities, list) or required not in capabilities:
                from .pipe import BridgeError
                raise BridgeError("unsupported_capability", f"Native bridge does not advertise {required} for the current state.")
        result = pipe.request(native_op, args)
        if op == "levels":
            return self._merge_levels(result)
        with self._lock:
            if op in {"pause", "stop", "detach"}:
                self._plans.clear()
            if op in {"stop", "detach"}:
                self._pipe, self.pid = None, None
                self._release_lease()
            return result

    @staticmethod
    def _merge_levels(native_levels):
        """Merge canonical modes with a strict allowlist of native verification metadata."""
        from .catalog import MODE_CATALOG
        native_by_id = {}
        duplicate_ids = set()
        known_ids = {mode["id"] for mode in MODE_CATALOG}
        if isinstance(native_levels, list):
            for item in native_levels:
                if isinstance(item, dict):
                    mode_id = item.get("mode_id")
                    if isinstance(mode_id, int) and not isinstance(mode_id, bool) and mode_id in known_ids:
                        if mode_id in native_by_id:
                            duplicate_ids.add(mode_id)
                        else:
                            native_by_id[mode_id] = item
        for mode_id in duplicate_ids:
            native_by_id.pop(mode_id, None)

        def native_metadata(item):
            """Copy only known fields with bounded types; static identity always wins."""
            if item is None:
                return {}
            metadata = {}
            for key in ("entry_implemented", "ai_win_verified", "entry_live_verified",
                        "hidden_selector_page", "requires_owned_tree",
                        "requires_visible_enabled_button"):
                if isinstance(item.get(key), bool):
                    metadata[key] = item[key]

            entry_action = item.get("entry_action")
            if entry_action is None or (isinstance(entry_action, str) and
                                        entry_action in {"enter_level", "garden_next"}):
                if "entry_action" in item:
                    metadata["entry_action"] = entry_action

            selector_id = item.get("selector_button_id")
            if selector_id is None or (isinstance(selector_id, int) and not isinstance(selector_id, bool)
                                       and selector_id in {101, 102, 109, 110}):
                if "selector_button_id" in item:
                    metadata["selector_button_id"] = selector_id

            page = item.get("definition_page")
            if isinstance(page, int) and not isinstance(page, bool) and 0 <= page <= 3:
                metadata["definition_page"] = page

            position = item.get("page_position")
            if isinstance(position, dict) and set(position) == {"row", "column"}:
                row, column = position["row"], position["column"]
                if (isinstance(row, int) and not isinstance(row, bool) and 0 <= row <= 3 and
                        isinstance(column, int) and not isinstance(column, bool) and 0 <= column <= 4):
                    metadata["page_position"] = {"row": row, "column": column}

            token = item.get("name_token")
            if (isinstance(token, str) and 0 < len(token) <= 128 and
                    all(0x20 <= ord(char) <= 0x7e for char in token)):
                metadata["name_token"] = token
            return metadata

        merged = []
        for mode in MODE_CATALOG:
            native = native_by_id.get(mode["id"])
            entry = native.get("entry_implemented") is True if native else False
            win = native.get("ai_win_verified") is True if native else False
            merged.append({**mode,
                "native_status_known": native is not None,
                "native_support": ("entry_implemented" if entry else
                                   "entry_not_implemented" if native is not None else "not_reported"),
                "entry_implemented": entry,
                "ai_win_verified": win,
                **native_metadata(native),
            })
        return merged

    def close(self) -> None:
        """Release this host's controller lease without injecting or stopping the game."""
        self._plan_cancel.set()
        with self._lock:
            self._pipe, self.pid = None, None
            self._plans.clear()
        self._release_lease()

    def status(self):
        return {"attached": self._pipe is not None, "pid": self.pid}

    def submit_plan(self, actions: list[dict], expires_tick: int, mode_id: str | int,
                    max_actions: int = 8) -> list[dict]:
        """Execute a short plan only while its scene, mode and tick bound remain valid."""
        if not isinstance(actions, list) or not 1 <= len(actions) <= min(max_actions, 32):
            raise ValueError("Plan must contain 1..max_actions actions (max_actions is capped at 32).")
        if isinstance(expires_tick, bool) or not isinstance(expires_tick, int) or expires_tick < 0:
            raise ValueError("expires_tick must be a nonnegative integer.")
        from .templates import _validate_action
        actions = [_validate_action(action) for action in actions]
        results = []
        with self._lock:
            if self._active_plan:
                raise RuntimeError("Another plan is already active for this controller.")
            self._active_plan = True
            self._plan_cancel.clear()
            self._plans = list(actions)
        board_id = None
        try:
            for action in actions:
                if self._plan_cancel.is_set():
                    break
                state = self.request("state")
                tick = state.get("tick")
                if not isinstance(tick, int) or isinstance(tick, bool) or tick > expires_tick or state.get("mode_id") != mode_id or state.get("paused"):
                    break
                if board_id is None:
                    board_id = state.get("board_id")
                elif state.get("board_id") != board_id:
                    break
                with self._dispatch_lock:
                    if self._plan_cancel.is_set():
                        break
                    result = self.request("action", action)
                results.append(result)
            return results
        finally:
            with self._lock:
                self._plans.clear()
                self._active_plan = False

    def run_guarded_plan(self, plan, max_actions: int = 8, max_polls: int = 40,
                         poll_interval: float = 0.05) -> list[dict]:
        """Run a GuardedPlan with fresh state and cancellation between every action."""
        if not 1 <= max_actions <= 8 or not 1 <= max_polls <= 400 or poll_interval < 0:
            raise ValueError("Use max_actions 1..8, max_polls 1..400, and a nonnegative poll interval.")
        results = []
        with self._lock:
            if self._active_plan:
                raise RuntimeError("Another plan is already active for this controller.")
            self._active_plan = True
            self._plan_cancel.clear()
        try:
            initial = self.request("state")
            initial_board = initial.get("board_id")
            if initial_board is not None:
                if plan.board_id is not None and plan.board_id != initial_board:
                    raise RuntimeError("Plan board_id does not match the current board.")
                plan.board_id = initial_board
            polls = actions_sent = 0
            while actions_sent < max_actions and polls < max_polls:
                if self._plan_cancel.is_set():
                    break
                state = self.request("state")
                polls += 1
                if state.get("paused"):
                    break
                action = plan.next_action(state)
                if action is None:
                    if getattr(plan, "halted", False) or getattr(plan, "done", False):
                        break
                    time.sleep(poll_interval)
                    continue
                try:
                    with self._dispatch_lock:
                        if self._plan_cancel.is_set():
                            break
                        result = self.request("action", action)
                except Exception:
                    plan.ack_action(False)
                    raise
                plan.ack_action(True)
                results.append(result)
                actions_sent += 1
            return results
        finally:
            with self._lock:
                self._plans.clear()
                self._active_plan = False

    def run_template(self, template: dict, max_actions: int = 8, max_polls: int = 40,
                     poll_interval: float = 0.05) -> dict:
        """Compile a template against current tick/board and run it within strict limits."""
        from .templates import compile_template
        state = self.request("state")
        tick = state.get("tick")
        if isinstance(tick, bool) or not isinstance(tick, int):
            raise RuntimeError("Current state has no valid tick; template cannot be safely scheduled.")
        plan = compile_template(template, tick, board_id=state.get("board_id"))
        accepted = self.run_guarded_plan(plan, max_actions=max_actions, max_polls=max_polls,
                                         poll_interval=poll_interval)
        return {"accepted_actions": accepted, "plan_done": plan.done, "plan_halted": plan.halted,
                "board_id": plan.board_id, "expires_tick": plan.expires_tick}

    def _verify_target_process(self, pid: int, verify_text: bool = True) -> None:
        if self.game_path is None:
            raise RuntimeError("Cannot verify the Steam launcher: no exact app 3590 game path was discovered.")
        image = _process_image_path(pid)
        if image is None:
            raise RuntimeError(f"Cannot identify executable for PID {pid}.")
        if _normpath(image) != _normpath(EXPECTED_RUNTIME_PATH):
            raise RuntimeError("Refusing PID: process image path is not the verified PopCap runtime path.")
        parent_pid = _process_parent_pid(pid)
        if parent_pid is None:
            raise RuntimeError("Refusing PID: could not identify its launcher parent.")
        _verify_launcher_process(parent_pid, self.game_path)
        self._verify_runtime_memory(pid, verify_text=verify_text)

    def _verify_runtime_memory(self, pid: int, verify_text: bool = True) -> None:
        _verify_runtime_pe(pid, verify_text=verify_text)
