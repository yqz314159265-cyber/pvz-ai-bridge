"""Small, single-language desktop entry point; gameplay remains in the native bridge."""
from __future__ import annotations
import json
import os
from pathlib import Path
import queue
import sys
import threading

from .paths import resource_root, user_data_dir

TEXT = {
    "zh": {
        "title": "植物大战僵尸 AI 桥接器", "subtitle": "游戏保持可见 · AI 通过游戏内部接口操作",
        "start": "启动游戏并连接", "attach": "连接已打开的游戏", "health": "检查游戏响应",
        "compat": "启用兼容绘制", "backup": "备份存档", "guide": "编辑我的打法",
        "skill": "安装 AI 技能", "docs": "使用说明", "config": "复制 MCP 配置",
        "disconnect": "断开游戏连接", "ready": "服务已启动，等待连接游戏。",
        "busy": "正在处理，请稍候。", "error": "操作失败", "done": "完成", "closing": "正在断开连接…",
        "api": "本地接口", "data": "数据目录", "open_steam": "游戏由 Steam 正常启动；首次连接前自动备份存档。",
        "compat_note": "兼容绘制会在主界面打开设置，关闭 3D 加速，再关闭设置。",
        "compat_scene": "请先回到主界面并关闭所有弹窗。",
        "responsive": "游戏响应正常", "unresponsive": "游戏未响应；已连接状态不代表游戏正常运行。",
        "no_game": "尚未连接游戏", "copied": "MCP 配置已复制。请在 AI 客户端中添加；同时只运行一个控制主机。",
        "server_error": "无法启动本地服务。端口可能已被占用，请先关闭旧的桥接器。",
        "settings": "选择语言", "profile": "这些按钮不会开始自动闯关。打法由你填写，AI 根据当前能力执行。",
    },
    "en": {
        "title": "PvZ AI Bridge", "subtitle": "Visible game window · Native in-game control",
        "start": "Launch game and connect", "attach": "Connect to running game", "health": "Check game response",
        "compat": "Enable compatible rendering", "backup": "Back up saves", "guide": "Edit my strategy",
        "skill": "Install AI skill", "docs": "User guide", "config": "Copy MCP configuration",
        "disconnect": "Disconnect game", "ready": "Local service is ready. Connect a game to begin.",
        "busy": "Working, please wait.", "error": "Action failed", "done": "Done", "closing": "Disconnecting…",
        "api": "Local API", "data": "Data directory", "open_steam": "The game starts through Steam. Saves are backed up before the first connection.",
        "compat_note": "At the main menu, this opens Options, disables 3D Acceleration, then closes Options.",
        "compat_scene": "Return to the main menu and close all dialogs first.",
        "responsive": "Game is responding", "unresponsive": "Game did not respond. An attached connection alone is not a health check.",
        "no_game": "No game connected", "copied": "MCP configuration copied. Add it in your AI client; run only one controller at a time.",
        "server_error": "Cannot start the local service. Its port may be in use; close the previous bridge first.",
        "settings": "Choose language", "profile": "These buttons do not start automatic play. You provide tactics; the AI uses the available controls.",
    },
}


def run_launcher(port: int = 8765) -> int:
    import tkinter as tk
    from tkinter import ttk, scrolledtext, messagebox
    from .host import Host, backup_saves, find_saves
    from .server import serve_http
    from .health import check_health
    from .strategy_guide import ensure_guide
    from .skill_install import install_skill
    from .__main__ import _load_or_create_token

    root = tk.Tk()
    root.title("Language / 语言")
    root.geometry("360x185")
    root.resizable(False, False)
    selected = tk.StringVar(value="zh")
    selection = ttk.Frame(root, padding=22)
    selection.pack(fill="both", expand=True)
    ttk.Label(selection, text="请选择语言 / Choose your language").pack(pady=(0, 12))
    ttk.Radiobutton(selection, text="简体中文", variable=selected, value="zh").pack(anchor="w")
    ttk.Radiobutton(selection, text="English", variable=selected, value="en").pack(anchor="w")
    ready = tk.BooleanVar(value=False)
    ttk.Button(selection, text="继续 / Continue", command=lambda: ready.set(True)).pack(pady=12)
    root.protocol("WM_DELETE_WINDOW", lambda: (ready.set(True), selected.set("")))
    root.wait_variable(ready)
    language = selected.get()
    if not language:
        root.destroy()
        return 0
    selection.destroy()
    t = TEXT[language]
    root.title(t["title"] + " 0.1.0")
    root.geometry("810x570")
    root.minsize(760, 510)
    root.resizable(True, True)
    host = Host()
    try:
        token = _load_or_create_token(user_data_dir() / "local-api" / "token")
        server, _ = serve_http(host, port, token)
    except Exception as exc:
        messagebox.showerror(t["error"], t["server_error"] + "\n\n" + str(exc), parent=root)
        host.close()
        root.destroy()
        return 2
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    frame = ttk.Frame(root, padding=18)
    frame.pack(fill="both", expand=True)
    ttk.Label(frame, text=t["title"], font=("Segoe UI", 18, "bold")).pack(anchor="w")
    ttk.Label(frame, text=t["subtitle"]).pack(anchor="w", pady=(4, 12))
    ttk.Label(frame, text=f"{t['api']}: http://127.0.0.1:{server.server_port}").pack(anchor="w")
    ttk.Label(frame, text=f"{t['data']}: {user_data_dir()}").pack(anchor="w", pady=(2, 10))
    status = tk.StringVar(value=t["ready"])
    ttk.Label(frame, textvariable=status, wraplength=740).pack(anchor="w", pady=5)
    controls = ttk.Frame(frame)
    controls.pack(fill="x", pady=8)
    log = scrolledtext.ScrolledText(frame, height=10, wrap="word", state="disabled")
    log.pack(fill="both", expand=True, pady=(8, 8))
    ttk.Label(frame, text=t["open_steam"], wraplength=740).pack(anchor="w")
    ttk.Label(frame, text=t["profile"], wraplength=740).pack(anchor="w", pady=(3, 0))
    events = queue.Queue()
    state = {"busy": False, "closing": False, "backed_up": False}
    buttons = []

    def append(message):
        log.configure(state="normal")
        log.insert("end", message + "\n")
        log.see("end")
        log.configure(state="disabled")

    def start_job(label, operation):
        if state["busy"] or state["closing"]:
            return
        state["busy"] = True
        status.set(t["busy"])
        append(label)
        for button in buttons:
            button.configure(state="disabled")
        def run():
            try:
                events.put((True, label, operation()))
            except Exception as exc:
                events.put((False, label, str(exc)))
        threading.Thread(target=run, daemon=True).start()

    def backup_first():
        if not state["backed_up"]:
            source = find_saves(host.game_path)
            if source is not None:
                backup_saves(source)
            state["backed_up"] = True

    def connect(launch):
        backup_first()
        reply = host.launch() if launch else host.attach(host.find_running_game())
        return {"connected": bool(reply), "health": check_health(host)}

    def compatible():
        current = host.request("state")
        if current.get("scene") != 1 or current.get("dialogs", {}).get("dialog_count") != 0:
            raise RuntimeError(t["compat_scene"])
        opened = host.request("action", {"type": "open_menu"})
        if not opened.get("accepted"):
            raise RuntimeError(str(opened))
        try:
            result = host.request("action", {"type": "option_set", "name": "hardware_acceleration", "value": False})
            if not result.get("accepted"):
                raise RuntimeError(str(result))
            observed = host.request("state")["options"]["controls"]["hardware_acceleration"]["value"]
            if observed is not False:
                raise RuntimeError("Rendering setting was not applied.")
            return {"compatible_rendering": True}
        finally:
            host.request("action", {"type": "close_menu"})

    def edit_guide():
        result = ensure_guide()
        # Use an editor explicitly: shell association for Markdown may be a viewer.
        import subprocess
        subprocess.Popen(["notepad.exe", result["path"]])
        return {"path": result["path"], "filled": result["filled"]}

    def open_docs():
        name = "README.zh-CN.md" if language == "zh" else "README.en.md"
        os.startfile(resource_root() / "docs" / name)
        return t["done"]

    def copy_config():
        command = str(Path(sys.executable).resolve())
        arguments = ["mcp"] if getattr(sys, "frozen", False) else ["-m", "pvz_ai", "mcp"]
        arguments += ["--api-url", f"http://127.0.0.1:{server.server_port}",
                      "--token-file", str(user_data_dir() / "local-api" / "token")]
        entry = {"command": command, "args": arguments}
        if not getattr(sys, "frozen", False):
            entry["env"] = {"PYTHONPATH": str(resource_root() / "src")}
        root.clipboard_clear()
        root.clipboard_append(json.dumps({"mcpServers": {"pvz-ai": entry}}, ensure_ascii=False, indent=2))
        append(t["copied"])

    operations = [
        ("start", lambda: connect(True)), ("attach", lambda: connect(False)),
        ("health", lambda: check_health(host)), ("compat", compatible),
        ("backup", lambda: {"backup": backup_saves()["created_utc"]}),
        ("guide", edit_guide), ("skill", lambda: install_skill()), ("docs", open_docs),
        ("disconnect", lambda: host.request("detach")),
    ]
    for index, (key, operation) in enumerate(operations):
        button = ttk.Button(controls, text=t[key], command=lambda k=key, op=operation: start_job(t[k], op))
        button.grid(row=index // 3, column=index % 3, sticky="ew", padx=3, pady=3)
        buttons.append(button)
    for column in range(3):
        controls.columnconfigure(column, weight=1)
    ttk.Button(controls, text=t["config"], command=copy_config).grid(row=3, column=0, columnspan=3, sticky="ew", padx=3, pady=3)

    def close():
        if state["busy"]:
            status.set(t["busy"])
            return
        if state["closing"]:
            return
        state["closing"] = True
        status.set(t["closing"])
        def stop():
            try:
                if host.status().get("attached"):
                    host.request("detach")
            except Exception:
                pass
            finally:
                server.shutdown()
                server.server_close()
                host.close()
                events.put((True, "__closed__", None))
        threading.Thread(target=stop, daemon=True).start()

    def poll():
        while not events.empty():
            success, label, value = events.get_nowait()
            if label == "__closed__":
                root.destroy()
                return
            state["busy"] = False
            for button in buttons:
                button.configure(state="normal")
            if success:
                live = value.get("health", value) if isinstance(value, dict) else {}
                if isinstance(live, dict) and "responsive" in live:
                    summary = t["responsive"] if live["responsive"] else t["unresponsive"] if live.get("attached") else t["no_game"]
                    status.set(summary)
                    append(summary + (f" ({live['latency_ms']} ms)" if "latency_ms" in live else ""))
                else:
                    status.set(t["done"])
                    detail = value.get("path") or value.get("backup") if isinstance(value, dict) else None
                    append(t["done"] + (": " + str(detail) if detail else ""))
            else:
                status.set(t["error"])
                append(t["error"] + ": " + value)
        root.after(100, poll)

    root.protocol("WM_DELETE_WINDOW", close)
    root.after(100, poll)
    root.mainloop()
    return 0
