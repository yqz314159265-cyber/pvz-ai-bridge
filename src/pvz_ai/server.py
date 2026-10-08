"""Authenticated loopback HTTP API and stdio MCP adapter."""
from __future__ import annotations
import json
import secrets
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

READS = {"/status": "status", "/state": "state", "/levels": "levels", "/capabilities": "capabilities",
         "/health": "health", "/guide": "guide"}


def mutation_auth_allowed(remote: str, authorization: str, token: str) -> bool:
    return remote in ("127.0.0.1", "::1") and secrets.compare_digest(authorization, f"Bearer {token}")


def serve_http(host, port: int = 8765, token: str | None = None):
    token = token or secrets.token_urlsafe(32)
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass
        def send(self, status, value):
            payload = json.dumps(value, ensure_ascii=False).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        def do_GET(self):
            route = urlparse(self.path).path
            if route not in READS:
                return self.send(404, {"error": "not_found"})
            try:
                if route == "/guide":
                    # A human-written guide can contain private notes. Unlike game
                    # liveness, it requires the same credential as control actions.
                    if not mutation_auth_allowed(self.client_address[0], self.headers.get("Authorization", ""), token):
                        return self.send(401, {"error": "unauthorized"})
                    from .strategy_guide import read_guide
                    result = read_guide()
                elif route == "/health":
                    from .health import check_health
                    result = check_health(host)
                elif route == "/status": result = host.status()
                elif route == "/capabilities": result = {"capabilities": host.request("state").get("capabilities", [])}
                else: result = host.request(READS[route])
                self.send(200, {"ok": True, "result": result})
            except Exception as e:
                self.send(503, {"ok": False, "error": str(e)})
        def do_POST(self):
            if self.client_address[0] not in ("127.0.0.1", "::1"):
                return self.send(403, {"error": "loopback_only"})
            if not mutation_auth_allowed(self.client_address[0], self.headers.get("Authorization", ""), token):
                return self.send(401, {"error": "unauthorized"})
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length < 0 or length > 1024 * 1024:
                    return self.send(413, {"error": "request_too_large"})
                body = json.loads(self.rfile.read(length) or b"{}")
                if not isinstance(body, dict):
                    return self.send(400, {"error": "object_required"})
                route = urlparse(self.path).path
                if route == "/attach":
                    result = host.attach(int(body["pid"]))
                elif route == "/launch":
                    result = host.launch()
                elif route == "/backup":
                    from .host import backup_saves
                    result = backup_saves()
                elif route == "/action":
                    if not isinstance(body.get("action"), dict):
                        return self.send(400, {"error": "action_object_required"})
                    result = host.request("action", body["action"])
                elif route == "/template/run":
                    if not isinstance(body.get("template"), dict):
                        return self.send(400, {"error": "template_object_required"})
                    result = host.run_template(body["template"],
                        max_actions=int(body.get("max_actions", 8)),
                        max_polls=int(body.get("max_polls", 40)))
                elif route in {"/pause", "/resume", "/stop", "/detach"}:
                    result = host.request(route[1:])
                else:
                    return self.send(404, {"error": "not_found"})
                self.send(200, {"ok": True, "result": result})
            except (ValueError, KeyError, json.JSONDecodeError) as e:
                self.send(400, {"ok": False, "error": str(e)})
            except Exception as e:
                self.send(503, {"ok": False, "error": str(e)})
    if port != 0 and port < 1024:
        raise ValueError("Choose an unprivileged HTTP port (1024 or higher).")
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    return server, token


def build_mcp_server(host):
    """Build an MCP server against the installed official SDK major version."""
    try:
        from mcp.server.fastmcp import FastMCP
        mcp = FastMCP("pvz-ai")
    except ModuleNotFoundError as e:
        if not (e.name == "mcp.server.fastmcp" or "mcp 2.x" in str(e)):
            raise RuntimeError("Install project dependencies to enable stdio MCP.") from e
        try:
            from mcp.server.mcpserver import MCPServer
        except ImportError as exc:
            raise RuntimeError("Install a supported MCP SDK version to enable stdio MCP.") from exc
        mcp = MCPServer("pvz-ai")
    @mcp.tool()
    def status() -> dict:
        """Report whether this host has an attached bridge."""
        return host.status()
    @mcp.tool()
    def health() -> dict:
        """Probe the game thread now; attached status alone does not prove responsiveness."""
        from .health import check_health
        return check_health(host)
    @mcp.tool()
    def strategy_guide() -> dict:
        """Read the human-written strategy guide. Unfilled scaffolds are not strategies."""
        if callable(getattr(host, "read_strategy_guide", None)):
            return host.read_strategy_guide()
        from .strategy_guide import read_guide
        return read_guide()
    @mcp.tool()
    def state() -> dict:
        """Read current public game state."""
        return host.request("state")
    @mcp.tool()
    def levels() -> dict:
        """Read levels advertised by the native bridge."""
        return host.request("levels")
    @mcp.tool()
    def capabilities() -> dict:
        """Read supported native actions, including any advertised garden or shop operations."""
        state = host.request("state")
        return {"capabilities": state.get("capabilities", [])}
    @mcp.tool()
    def launch() -> dict:
        """Launch the verified Steam entry point, wait for the verified runtime, and attach."""
        return host.launch()
    @mcp.tool()
    def attach(pid: int) -> dict:
        """Attach to a PID only after launcher ancestry and runtime PE checks pass."""
        return host.attach(pid)
    @mcp.tool()
    def action(type: str, parameters: dict) -> dict:
        """Request one advertised semantic action. Schema includes shop_page(page 0..3), garden care water/fertilizer with unsigned 32-bit plant IDs, and single-quantity shop_buy; native capability is authoritative."""
        return host.request("action", {"type": type, **parameters})
    @mcp.tool()
    def pause() -> dict:
        """Pause the game and clear pending plans."""
        return host.request("pause")
    @mcp.tool()
    def resume() -> dict:
        """Resume the game."""
        return host.request("resume")
    @mcp.tool()
    def stop() -> dict:
        """Clear pending plans and detach the bridge from the game process."""
        return host.request("stop")
    @mcp.tool()
    def detach() -> dict:
        """Detach the bridge from the game process."""
        return host.request("detach")
    @mcp.tool()
    def run_template(template: dict, max_actions: int = 8, max_polls: int = 40) -> dict:
        """Validate and execute a bounded template; reports plan status, never a claimed win."""
        return host.run_template(template, max_actions=max_actions, max_polls=max_polls)
    return mcp


def serve_mcp(host):
    """Expose the same host calls through the official MCP stdio SDK."""
    mcp = build_mcp_server(host)
    mcp.run(transport="stdio")
