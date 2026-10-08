"""Loopback HTTP adapter for the already-running PvZ AI launcher host."""
from __future__ import annotations

import json
import os
from pathlib import Path
import stat
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import (HTTPRedirectHandler, Request, ProxyHandler,
                            build_opener)

from .paths import user_data_dir


class _RejectRedirects(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class HttpController:
    """Host-compatible client that shares the launcher's controller lease."""

    def __init__(self, api_url: str = "http://127.0.0.1:8765",
                 token_file: str | os.PathLike | None = None):
        self.api_url = self._validate_url(api_url)
        self.token_file = Path(token_file) if token_file is not None else user_data_dir() / "local-api" / "token"
        self._token = self._read_token(self.token_file)
        # Bypass environment proxies: credentials must go directly to loopback.
        self._opener = build_opener(ProxyHandler({}), _RejectRedirects())

    @staticmethod
    def _validate_url(api_url: str) -> str:
        if not isinstance(api_url, str) or not api_url:
            raise ValueError("API URL must be an HTTP URL on 127.0.0.1.")
        try:
            parsed = urlsplit(api_url)
            port = parsed.port
        except ValueError as exc:
            raise ValueError("API URL has an invalid port or authority.") from exc
        if (parsed.scheme.casefold() != "http" or parsed.hostname != "127.0.0.1" or
                port is None or not 1024 <= port <= 65535 or parsed.username is not None or
                parsed.password is not None or parsed.path not in ("", "/") or
                parsed.query or parsed.fragment):
            raise ValueError("API URL must be http://127.0.0.1:<port> with no credentials, path, query, or fragment.")
        return f"http://127.0.0.1:{port}"

    @staticmethod
    def _read_token(path: Path) -> str:
        path = Path(path)
        try:
            if path.is_symlink():
                raise ValueError("Refusing a symbolic-link API token file.")
            info = path.stat()
            if not stat.S_ISREG(info.st_mode) or info.st_size > 4096:
                raise ValueError("API token file must be a regular file no larger than 4 KiB.")
            raw = path.read_bytes()
        except OSError as exc:
            raise RuntimeError(f"Could not read API token file: {path}") from exc
        try:
            token = raw.decode("utf-8").strip()
        except UnicodeError as exc:
            raise ValueError("API token file must contain UTF-8 text.") from exc
        if not 32 <= len(token) <= 4096:
            raise ValueError("API token must contain 32 to 4096 characters.")
        if any(ord(ch) < 33 or ord(ch) > 126 for ch in token):
            raise ValueError("API token must contain printable non-space ASCII characters only.")
        return token

    def _request(self, method: str, route: str, body: dict | None = None,
                 timeout: float = 30.0):
        allowed = {"GET": {"/status", "/state", "/levels", "/capabilities", "/health", "/guide"},
                   "POST": {"/action", "/pause", "/resume", "/stop", "/detach",
                            "/launch", "/attach", "/template/run"}}
        if route not in allowed.get(method, set()):
            raise ValueError("Unsupported launcher API operation.")
        data = None if method == "GET" else json.dumps(body or {}, ensure_ascii=False).encode("utf-8")
        request = Request(self.api_url + route, data=data, method=method, headers={
            "Authorization": f"Bearer {self._token}",
            "Accept": "application/json",
            **({"Content-Type": "application/json; charset=utf-8"} if data is not None else {}),
        })
        try:
            with self._opener.open(request, timeout=timeout) as response:
                payload = response.read(1024 * 1024 + 1)
                if len(payload) > 1024 * 1024:
                    raise RuntimeError("Launcher API response exceeded 1 MiB.")
        except HTTPError as exc:
            if 300 <= exc.code < 400:
                raise RuntimeError(f"Launcher API redirect refused (HTTP {exc.code}).") from None
            try:
                payload = exc.read(1024 * 1024 + 1)
                value = json.loads(payload.decode("utf-8")) if len(payload) <= 1024 * 1024 else {}
            except Exception:
                value = {}
            detail = value.get("error") if isinstance(value, dict) else None
            raise RuntimeError(f"Launcher API returned HTTP {exc.code}" +
                               (f": {detail}" if detail else ".")) from None
        except (URLError, TimeoutError, OSError) as exc:
            raise RuntimeError(f"Could not reach the loopback PvZ AI API: {exc}") from None
        try:
            value = json.loads(payload.decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError) as exc:
            raise RuntimeError("Launcher API returned invalid JSON.") from exc
        if not isinstance(value, dict) or value.get("ok") is not True or "result" not in value:
            detail = value.get("error") if isinstance(value, dict) else None
            raise RuntimeError(f"Launcher API request failed" + (f": {detail}" if detail else "."))
        return value["result"]

    def status(self) -> dict:
        return self._request("GET", "/status")

    def request(self, op: str, args: dict | None = None):
        if op in {"state", "levels", "capabilities", "health"}:
            return self._request("GET", f"/{op}")
        if op == "guide":
            return self.read_strategy_guide()
        if op == "action":
            if not isinstance(args, dict):
                raise ValueError("action requires an object.")
            return self._request("POST", "/action", {"action": args})
        if op in {"pause", "resume", "stop", "detach"}:
            return self._request("POST", f"/{op}")
        raise ValueError("Unsupported launcher API operation.")

    def launch(self) -> dict:
        return self._request("POST", "/launch", timeout=65.0)

    def attach(self, pid: int) -> dict:
        if isinstance(pid, bool) or not isinstance(pid, int) or pid <= 0:
            raise ValueError("PID must be a positive integer.")
        return self._request("POST", "/attach", {"pid": pid}, timeout=65.0)

    def run_template(self, template: dict, max_actions: int = 8, max_polls: int = 40) -> dict:
        if not isinstance(template, dict):
            raise ValueError("template must be an object.")
        return self._request("POST", "/template/run", {
            "template": template, "max_actions": max_actions, "max_polls": max_polls,
        }, timeout=65.0)

    def read_strategy_guide(self) -> dict:
        return self._request("GET", "/guide")

    def close(self) -> None:
        """A client close never detaches or stops the launcher-owned controller."""
        return None
