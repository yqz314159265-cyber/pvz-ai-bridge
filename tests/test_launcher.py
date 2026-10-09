from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tkinter as tk

import pytest

import pvz_ai.launcher as launcher
import pvz_ai.__main__ as app_main
import pvz_ai.host as host_module
import pvz_ai.health as health_module
import pvz_ai.server as server_module
import pvz_ai.skill_install as skill_module
import pvz_ai.strategy_guide as guide_module


class _FakeHost:
    def __init__(self):
        self.closed = False

    def status(self):
        return {"attached": False}

    def request(self, *_args, **_kwargs):
        return {"attached": False}

    def close(self):
        self.closed = True


class _FakeServer:
    server_port = 8765

    def __init__(self):
        self.closed = False

    def serve_forever(self):
        return None

    def shutdown(self):
        self.closed = True

    def server_close(self):
        self.closed = True


def _texts(root):
    result = []
    pending = list(root.winfo_children())
    while pending:
        widget = pending.pop()
        pending.extend(widget.winfo_children())
        try:
            value = widget.cget("text")
        except tk.TclError:
            continue
        if isinstance(value, str):
            result.append(value)
    return result


def _run_tk_case_in_fresh_process(node_id, case, tmp_path):
    """Run Tk tests in a fresh process because Tcl/Tk cleanup is process-scoped."""
    marker = "PVZ_AI_LAUNCHER_TK_CASE"
    if os.environ.get(marker) == case:
        return True
    child_temp = tmp_path / "child-temp"
    child_temp.mkdir()
    env = os.environ.copy()
    env[marker] = case
    env["TEMP"] = str(child_temp)
    env["TMP"] = str(child_temp)
    result = subprocess.run(
        [sys.executable, "-m", "pytest", "-q", f"{Path(__file__).resolve()}::{node_id}",
         "--basetemp", str(child_temp / "pytest")],
        env=env, capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return False


@pytest.mark.parametrize("language", ["zh", "en"])
def test_launcher_language_picker_recovers_invalid_preference_and_shows_only_selected_ui(
        monkeypatch, tmp_path, language):
    """Exercise both language choices in isolated Tk processes; no game calls."""
    node_id = f"test_launcher_language_picker_recovers_invalid_preference_and_shows_only_selected_ui[{language}]"
    if not _run_tk_case_in_fresh_process(node_id, language, tmp_path):
        return

    fake_host = _FakeHost()
    fake_server = _FakeServer()
    monkeypatch.setattr(launcher, "user_data_dir", lambda: tmp_path)
    (tmp_path / "language.json").write_text('{"language":"fr"}', encoding="utf-8")
    monkeypatch.setattr(app_main, "_load_or_create_token", lambda _path: "test-token")
    monkeypatch.setattr(host_module, "Host", lambda: fake_host)
    monkeypatch.setattr(host_module, "backup_saves", lambda: {"created_utc": "test"})
    monkeypatch.setattr(server_module, "serve_http", lambda *_args: (fake_server, None))
    monkeypatch.setattr(health_module, "check_health", lambda _host: {"responsive": True})
    monkeypatch.setattr(guide_module, "ensure_guide", lambda: {"path": "unused", "filled": False})
    monkeypatch.setattr(skill_module, "install_skill", lambda: {"installed": True})
    monkeypatch.setattr(os, "startfile", lambda *_args: None, raising=False)

    real_tk = tk.Tk
    observed = {}

    def create_root(*args, **kwargs):
        root = real_tk(*args, **kwargs)

        def select_language_and_continue():
            widgets = []
            pending = list(root.winfo_children())
            while pending:
                widget = pending.pop()
                pending.extend(widget.winfo_children())
                try:
                    label = widget.cget("text")
                except tk.TclError:
                    continue
                if label == ("English" if language == "en" else "简体中文"):
                    widget.invoke()
                if label == "继续 / Continue":
                    widgets.append(widget)
            assert len(widgets) == 1
            widgets[0].invoke()
            root.after(150, inspect_selected_panel)

        def inspect_selected_panel():
            texts = _texts(root)
            observed["title"] = root.title()
            observed["texts"] = texts
            root.tk.call(root.protocol("WM_DELETE_WINDOW"))

        root.after(50, select_language_and_continue)
        root.after(4000, root.destroy)
        return root

    monkeypatch.setattr(tk, "Tk", create_root)
    result = launcher.run_launcher()

    assert result == 0
    assert "0.1.1" in observed["title"]
    assert launcher.TEXT[language]["title"] in observed["title"]
    selected_action = "start"
    opposite = "en" if language == "zh" else "zh"
    assert launcher.TEXT[language][selected_action] in observed["texts"]
    assert launcher.TEXT[opposite][selected_action] not in observed["texts"]
    visible = set(observed["texts"])
    assert not (visible & set(launcher.TEXT[opposite].values()))
    assert fake_host.closed
    assert fake_server.closed
    assert launcher._load_language_preference() == language


@pytest.mark.parametrize("language", ["zh", "en"])
def test_launcher_saved_language_skips_picker_and_shows_only_selected_ui(monkeypatch, tmp_path, language):
    """A fresh launcher process uses the saved language without showing the picker."""
    node_id = f"test_launcher_saved_language_skips_picker_and_shows_only_selected_ui[{language}]"
    case = f"saved-{language}"
    if not _run_tk_case_in_fresh_process(node_id, case, tmp_path):
        return

    fake_host = _FakeHost()
    fake_server = _FakeServer()
    monkeypatch.setattr(launcher, "user_data_dir", lambda: tmp_path)
    launcher._save_language_preference(language)
    monkeypatch.setattr(app_main, "_load_or_create_token", lambda _path: "test-token")
    monkeypatch.setattr(host_module, "Host", lambda: fake_host)
    monkeypatch.setattr(server_module, "serve_http", lambda *_args: (fake_server, None))

    real_tk = tk.Tk
    observed = {}

    def create_root(*args, **kwargs):
        root = real_tk(*args, **kwargs)

        def inspect_selected_panel():
            observed["title"] = root.title()
            observed["texts"] = _texts(root)
            root.tk.call(root.protocol("WM_DELETE_WINDOW"))

        root.after(50, inspect_selected_panel)
        root.after(4000, root.destroy)
        return root

    monkeypatch.setattr(tk, "Tk", create_root)
    assert launcher.run_launcher() == 0

    visible = set(observed["texts"])
    opposite = "en" if language == "zh" else "zh"
    assert launcher.TEXT[language]["title"] in observed["title"]
    assert "Language / 语言" not in observed["title"]
    assert "请选择语言 / Choose your language" not in visible
    assert launcher.TEXT[language]["start"] in visible
    assert launcher.TEXT[opposite]["start"] not in visible
    assert not (visible & set(launcher.TEXT[opposite].values()))
    assert fake_host.closed
    assert fake_server.closed


@pytest.mark.parametrize("language", ["zh", "en"])
def test_language_preference_persists_for_next_launch(monkeypatch, tmp_path, language):
    """The next process can load the saved choice and skip first-launch selection."""
    monkeypatch.setattr(launcher, "user_data_dir", lambda: tmp_path)

    assert launcher._load_language_preference() is None
    launcher._save_language_preference(language)

    assert launcher._load_language_preference() == language


def test_language_preference_rejects_malformed_and_invalid_values(monkeypatch, tmp_path):
    monkeypatch.setattr(launcher, "user_data_dir", lambda: tmp_path)
    preference = tmp_path / "language.json"
    preference.write_text("not json", encoding="utf-8")
    assert launcher._load_language_preference() is None

    preference.write_text('{"language":"fr"}', encoding="utf-8")
    assert launcher._load_language_preference() is None

    preference.write_text('{"language":[]}', encoding="utf-8")
    assert launcher._load_language_preference() is None
