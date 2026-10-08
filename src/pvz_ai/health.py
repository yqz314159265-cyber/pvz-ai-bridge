"""Fresh game-thread liveness, kept separate from cached connection status."""
from __future__ import annotations
import time
from datetime import datetime, timezone


def check_health(host) -> dict:
    status = host.status()
    result = {**status, "responsive": False,
              "checked_at": datetime.now(timezone.utc).isoformat()}
    if not status.get("attached"):
        return {**result, "reason": "not_attached"}
    started = time.monotonic()
    try:
        state = host.request("state")
        result.update(responsive=True, scene=state.get("scene_name"),
                      bridge_ticks=state.get("bridge_ticks"),
                      window_has_focus=state.get("window_has_focus"),
                      background_running=state.get("background_running"))
    except Exception as exc:
        result.update(reason="game_thread_unavailable", error=str(exc))
    result["latency_ms"] = round((time.monotonic() - started) * 1000, 1)
    return result
