from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading

import pytest

from pvz_ai.http_client import HttpController
from pvz_ai.server import serve_http


class FakeHost:
    def __init__(self):
        self.calls = []

    def status(self):
        self.calls.append(("status", None))
        return {"attached": True, "pid": 321}

    def request(self, op, args=None):
        self.calls.append((op, args))
        if op == "state":
            return {"mode_id": "zen_garden", "capabilities": ["action.plant"]}
        return {"received": op, "args": args}

    def launch(self):
        self.calls.append(("launch", None))
        return {"launched": True}

    def attach(self, pid):
        self.calls.append(("attach", pid))
        return {"attached": pid}

    def run_template(self, template, max_actions=8, max_polls=40):
        self.calls.append(("run_template", (template, max_actions, max_polls)))
        return {"accepted_actions": []}


def _client(tmp_path: Path, port: int) -> HttpController:
    token_file = tmp_path / "token"
    token_file.write_text("test-token-value-that-is-at-least-32-chars\n", encoding="utf-8")
    return HttpController(f"http://127.0.0.1:{port}", token_file)


def test_http_controller_reuses_launcher_host_and_maps_interface(tmp_path):
    backend = FakeHost()
    server, _ = serve_http(backend, 0, "test-token-value-that-is-at-least-32-chars")
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    client = _client(tmp_path, server.server_address[1])
    try:
        assert client.status() == {"attached": True, "pid": 321}
        assert client.request("state")["mode_id"] == "zen_garden"
        assert client.request("levels")["received"] == "levels"
        assert client.request("action", {"type": "plant", "plant_type": 1})["received"] == "action"
        assert client.request("pause")["received"] == "pause"
        assert client.request("resume")["received"] == "resume"
        assert client.request("stop")["received"] == "stop"
        assert client.request("detach")["received"] == "detach"
        assert client.attach(321) == {"attached": 321}
        assert client.launch() == {"launched": True}
        assert client.run_template({"actions": []}, 3, 12) == {"accepted_actions": []}
        client.close()
        assert [call[0] for call in backend.calls] == [
            "status", "state", "levels", "action", "pause", "resume", "stop", "detach",
            "attach", "launch", "run_template"]
        assert backend.calls[3] == ("action", {"type": "plant", "plant_type": 1})
        assert backend.calls[-1] == ("run_template", ({"actions": []}, 3, 12))
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


@pytest.mark.parametrize("url", [
    "https://127.0.0.1:8765", "http://localhost:8765", "http://[::1]:8765",
    "http://127.0.0.2:8765", "http://user@127.0.0.1:8765", "http://127.0.0.1:80",
    "http://127.0.0.1:8765/arbitrary", "http://127.0.0.1:8765/?token=secret",
])
def test_http_controller_rejects_non_loopback_or_non_root_urls(tmp_path, url):
    token_file = tmp_path / "token"
    token_file.write_text("test-token-value-that-is-at-least-32-chars", encoding="utf-8")
    with pytest.raises(ValueError, match="API URL"):
        HttpController(url, token_file)


def test_http_controller_rejects_invalid_token_files(tmp_path):
    token_file = tmp_path / "token"
    token_file.write_text("too-short", encoding="utf-8")
    with pytest.raises(ValueError, match="32 to 4096"):
        HttpController("http://127.0.0.1:8765", token_file)
    token_file.write_bytes(b"x" * 4097)
    with pytest.raises(ValueError, match="4 KiB"):
        HttpController("http://127.0.0.1:8765", token_file)


def test_http_controller_refuses_redirect_without_forwarding_token(tmp_path):
    forwarded = []

    class Destination(BaseHTTPRequestHandler):
        def do_GET(self):
            forwarded.append(self.headers.get("Authorization"))
            self.send_response(200)
            self.end_headers()
        def log_message(self, *_):
            pass

    destination = ThreadingHTTPServer(("127.0.0.1", 0), Destination)
    destination_thread = threading.Thread(target=destination.serve_forever, daemon=True)
    destination_thread.start()

    class Redirect(BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(302)
            self.send_header("Location", f"http://127.0.0.1:{destination.server_address[1]}/status")
            self.end_headers()
        def log_message(self, *_):
            pass

    redirect = ThreadingHTTPServer(("127.0.0.1", 0), Redirect)
    redirect_thread = threading.Thread(target=redirect.serve_forever, daemon=True)
    redirect_thread.start()
    client = _client(tmp_path, redirect.server_address[1])
    try:
        with pytest.raises(RuntimeError, match="redirect refused"):
            client.status()
        assert forwarded == []
    finally:
        redirect.shutdown()
        redirect.server_close()
        redirect_thread.join(timeout=2)
        destination.shutdown()
        destination.server_close()
        destination_thread.join(timeout=2)


def test_http_controller_only_allows_known_operations(tmp_path):
    token_file = tmp_path / "token"
    token_file.write_text("test-token-value-that-is-at-least-32-chars", encoding="utf-8")
    client = HttpController("http://127.0.0.1:8765", token_file)
    with pytest.raises(ValueError, match="Unsupported"):
        client.request("http://attacker.invalid/action")
    with pytest.raises(ValueError, match="action requires"):
        client.request("action", ["arbitrary"])
