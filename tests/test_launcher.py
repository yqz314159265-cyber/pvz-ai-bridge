from __future__ import annotations

from pathlib import Path
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


@pytest.mark.parametrize("language", ["zh", "en"])
def test_launcher_language_picker_shows_only_selected_ui(monkeypatch, language):
    """Exercise Tk callbacks in-process; no mouse automation or game calls."""
    import os
    from types import SimpleNamespace

    fake_host = _FakeHost()
    fake_server = _FakeServer()
    monkeypatch.setattr(launcher, "user_data_dir", lambda: Path("C:/PvZ-AI-Bridge-test"))
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
    assert "0.1.0" in observed["title"]
    assert launcher.TEXT[language]["title"] in observed["title"]
    selected_action = "start"
    opposite = "en" if language == "zh" else "zh"
    assert launcher.TEXT[language][selected_action] in observed["texts"]
    assert launcher.TEXT[opposite][selected_action] not in observed["texts"]
    visible = set(observed["texts"])
    assert not (visible & set(launcher.TEXT[opposite].values()))
    assert fake_host.closed
    assert fake_server.closed
