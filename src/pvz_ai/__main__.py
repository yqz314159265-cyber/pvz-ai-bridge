from __future__ import annotations
import argparse
import csv
import json
import os
import secrets
import sys
from pathlib import Path
import subprocess
from .host import Host, backup_saves
from .server import serve_http, serve_mcp
from .paths import user_data_dir


def _current_user_sid() -> str:
    result = subprocess.run(["whoami.exe", "/user", "/fo", "csv", "/nh"],
                            check=True, capture_output=True, text=True)
    for line in result.stdout.splitlines():
        fields = next(csv.reader([line]), [])
        if fields and fields[-1].startswith("S-"):
            return fields[-1]
    raise RuntimeError("Could not determine the current Windows user SID to protect the API token file.")


def _protect_token_file(path: Path) -> None:
    if os.name != "nt":
        try:
            os.chmod(path, 0o600)
        except OSError as exc:
            raise RuntimeError("Could not restrict API token file permissions.") from exc
        return
    sid = _current_user_sid()
    subprocess.run(["icacls.exe", str(path), "/inheritance:r", "/grant:r", f"*{sid}:(F)"],
                   check=True, capture_output=True, text=True)


def _load_or_create_token(path: Path) -> str:
    path = Path(os.path.abspath(os.fspath(path)))
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise RuntimeError("Refusing a symbolic-link API token file.")
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0), 0o600)
    except FileExistsError:
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 4096:
            raise RuntimeError("API token file must be a regular file no larger than 4 KiB.")
        token = path.read_text(encoding="utf-8").strip()
        if len(token) < 32:
            raise RuntimeError("API token file is empty or too short; remove it to generate a new token.")
        _protect_token_file(path)
        return token
    token = secrets.token_urlsafe(32)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(token + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        _protect_token_file(path)
    except Exception:
        # Do not leave a partially written or unprotected credential behind.
        try:
            path.unlink()
        except OSError:
            pass
        raise
    return token


def main(argv=None):
    # A frozen Windows console defaults to the system code page. API clients
    # and redirected JSON files require UTF-8, including Chinese guide text.
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if callable(reconfigure):
            reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(prog="pvz-ai", description="PvZ AI Bridge / 植物大战僵尸 AI 接口")
    from . import __version__
    parser.add_argument("--version", action="version", version=__version__)
    sub = parser.add_subparsers(dest="command", required=True)
    launcher = sub.add_parser("launcher", help="Open control panel / 打开控制面板")
    launcher.add_argument("--port", type=int, default=8765)
    guide = sub.add_parser("guide", help="Read human strategy / 读取人工打法")
    guide.add_argument("--init", action="store_true", help="Create scaffold if missing / 创建空白模板，不覆盖")
    guide.add_argument("--path", type=Path)
    install_skill = sub.add_parser("install-skill", help="Install packaged AI skill / 安装 AI 技能")
    install_skill.add_argument("--destination", type=Path)
    sub.add_parser("doctor", help="Check the exact game path and native build without changing anything")
    backup = sub.add_parser("backup", help="Copy saves and write a SHA-256 manifest")
    backup.add_argument("--source", type=Path)
    backup.add_argument("--destination", type=Path)
    launch = sub.add_parser("launch", help="Launch verified game and attach native bridge")
    launch.add_argument("--game", type=Path)
    attach = sub.add_parser("attach", help="Inject bridge into a verified game process")
    attach.add_argument("pid", type=int); attach.add_argument("--game", type=Path)
    for name in ("status", "health", "state", "levels", "pause", "resume", "stop", "detach"):
        item = sub.add_parser(name)
        item.add_argument("--pid", type=int, help="Connect to an already attached game process")
    action = sub.add_parser("action", help="Request a semantic native action as JSON")
    action.add_argument("type"); action.add_argument("parameters", nargs="?", default="{}")
    action.add_argument("--pid", type=int, help="Connect to an already attached game process")
    template = sub.add_parser("run-template", help="Validate and run a bounded JSON task template")
    template.add_argument("file", type=Path, help="JSON template file")
    template.add_argument("--pid", type=int, required=True, help="PID of an already attached game process")
    template.add_argument("--max-actions", type=int, default=8)
    template.add_argument("--max-polls", type=int, default=40)
    http = sub.add_parser("http", help="Serve authenticated loopback JSON API")
    http.add_argument("--port", type=int, default=8765)
    http.add_argument("--token", help="Deprecated compatibility option; visible to local process inspection")
    http.add_argument("--token-file", type=Path, help="Read/create the private bearer-token file")
    http.add_argument("--attach-running", action="store_true",
                      help="Attach to exactly one already-running, verified PvZ runtime before serving")
    mcp = sub.add_parser("mcp", help="Serve stdio MCP tools")
    mcp.add_argument("--api-url", help="Use an existing launcher HTTP controller, e.g. http://127.0.0.1:8765")
    mcp.add_argument("--token-file", type=Path, help="Existing launcher's private bearer-token file")
    args = parser.parse_args(argv)
    host = None
    try:
        if args.command == "launcher":
            from .launcher import run_launcher
            return run_launcher(args.port)
        elif args.command == "guide":
            from .strategy_guide import read_guide, ensure_guide
            result = (ensure_guide if args.init else read_guide)(args.path)
        elif args.command == "install-skill":
            from .skill_install import install_skill as install
            result = install(args.destination)
        elif args.command == "doctor":
            result = Host().doctor()
        elif args.command == "backup":
            result = backup_saves(args.source, args.destination)
        elif args.command == "launch":
            host = Host(args.game); result = host.launch()
        elif args.command == "attach":
            host = Host(args.game); result = host.attach(args.pid)
        elif args.command == "http":
            host = Host()
            if args.token and args.token_file is not None:
                raise ValueError("Use either --token or --token-file, not both.")
            token_path = None if args.token else (args.token_file or
                user_data_dir() / "local-api" / "token")
            token = args.token if args.token else _load_or_create_token(token_path)
            server, _ = serve_http(host, args.port, token)
            try:
                if args.attach_running:
                    pid = host.find_running_game()
                    host.attach(pid)
                print(json.dumps({"url": f"http://127.0.0.1:{server.server_port}",
                                  "token_file": str(token_path) if token_path else None}), flush=True)
                server.serve_forever()
            finally:
                server.server_close(); host.close()
            return 0
        elif args.command == "mcp":
            if args.api_url:
                from .http_client import HttpController
                host = HttpController(args.api_url, args.token_file)
            else:
                if args.token_file:
                    raise ValueError("--token-file requires --api-url for MCP.")
                host = Host()
            serve_mcp(host); return 0
        else:
            game_path = getattr(args, "game", None)
            host = Host(game_path) if game_path is not None else Host()
            if getattr(args, "pid", None) is not None:
                host.connect(args.pid)
            if args.command == "status": result = host.status()
            elif args.command == "health":
                from .health import check_health
                result = check_health(host)
            elif args.command == "action":
                params = json.loads(args.parameters)
                if not isinstance(params, dict): raise ValueError("parameters must be a JSON object")
                result = host.request("action", {"type": args.type, **params})
            elif args.command == "run-template":
                if args.file.stat().st_size > 1024 * 1024:
                    raise ValueError("Template file exceeds 1 MiB.")
                template_data = json.loads(args.file.read_text(encoding="utf-8"))
                if not isinstance(template_data, dict):
                    raise ValueError("Template file must contain a JSON object.")
                result = host.run_template(template_data, args.max_actions, args.max_polls)
            else: result = host.request(args.command)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except Exception as e:
        print(json.dumps({"ok": False, "error": str(e)}, ensure_ascii=False), file=sys.stderr)
        return 2
    finally:
        if host is not None:
            host.close()


if __name__ == "__main__":
    raise SystemExit(main())
