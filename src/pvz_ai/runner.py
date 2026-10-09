"""Bounded local-model gameplay loop and standalone CLI proposal runner."""
from __future__ import annotations

import argparse
import json
import math
import sys
import threading
import time
from contextlib import nullcontext
from datetime import datetime, timezone
from pathlib import Path

from .local_model import LocalModelClient, validate_action_proposal

MENU_SCENES = {"menu", "chooser", "seedchooser", "choose_cards", "level_select", "main_menu"}
MENU_ACTIONS = {"enter_level", "choose_cards", "start_level", "continue", "restart",
                "back_to_menu", "enter_garden", "open_shop", "shop_page", "shop_buy", "close_shop",
                "shop_advance_dialogue"}
PAUSE_CAPABILITIES = {"pause", "op.pause", "operation.pause"}
SUNFLOWER_TYPE = 1
PEASHOOTER_TYPE = 0
POTATO_MINE_TYPE = 4
SQUASH_TYPE = 17
STRONG_ZOMBIE_HEALTH = 400
COLLECTIBLE_KINDS = {
    "sun", "small_sun", "large_sun", "sun_small", "sun_large",
    "coin", "silver", "gold", "diamond", "silver_coin", "gold_coin",
    "diamond_coin", "coin_silver", "coin_gold", "coin_diamond",
}


def basic_adventure_policy(state: dict) -> dict | None:
    """Limited deterministic day Adventure policy; requires a complete, explicit schema.

    This is a small farming/defense example, not a general PvZ solver. It intentionally
    refuses conveyor decks, non-day backgrounds, and unknown scene or state formats.
    """
    if not isinstance(state, dict) or state.get("mode_id") != 0 or state.get("background_id") != 0:
        return None
    if state.get("conveyor") is not False or state.get("observation_complete") is not True:
        return None
    if state.get("scene_name") != "playing" and state.get("scene") != 3:
        return None
    capabilities = _caps(state)
    seeds = state.get("seed_packets")
    plants = state.get("plants")
    zombies = state.get("zombies")
    collectibles = state.get("collectibles")
    sun = state.get("sun")
    if (not isinstance(seeds, list) or not isinstance(plants, list) or not isinstance(zombies, list) or
            not isinstance(collectibles, list) or not isinstance(sun, int) or isinstance(sun, bool)):
        return None

    def packet_for(plant_type: int, require_ready: bool = True):
        for packet in seeds:
            if not isinstance(packet, dict) or packet.get("plant_type") != plant_type:
                continue
            cost = packet.get("cost")
            slot = packet.get("slot")
            cooldown = packet.get("cooldown_remaining")
            ready = (packet.get("usable") is True and isinstance(cooldown, int) and
                     not isinstance(cooldown, bool) and cooldown == 0)
            if (isinstance(cost, int) and not isinstance(cost, bool) and
                    cost >= 0 and
                    isinstance(slot, int) and not isinstance(slot, bool) and 1 <= slot <= 100 and
                    (not require_ready or ready)):
                return packet
        return None

    occupied = {(p.get("row"), p.get("col")) for p in plants if isinstance(p, dict)}
    visible_zombies = [z for z in zombies if isinstance(z, dict) and
                       isinstance(z.get("row"), int) and not isinstance(z.get("row"), bool) and
                       1 <= z["row"] <= 5 and isinstance(z.get("x"), (int, float)) and
                       not isinstance(z.get("x"), bool) and math.isfinite(z["x"])]
    row_zombies: dict[int, list[dict]] = {}
    for zombie in visible_zombies:
        row_zombies.setdefault(zombie["row"], []).append(zombie)

    # Emergency plants get first chance; costs and readiness always come from the live seed packets.
    if "action.plant" in capabilities:
        for zombie in sorted(visible_zombies, key=lambda z: z["x"]):
            row, x = zombie["row"], zombie["x"]
            mine = packet_for(POTATO_MINE_TYPE)
            if x > 450 and mine is not None and sun >= mine["cost"] and (row, 3) not in occupied:
                if not any(isinstance(p, dict) and p.get("plant_type") == POTATO_MINE_TYPE and
                           p.get("row") == row and p.get("col") in {2, 3, 4} for p in plants):
                    return {"type": "plant", "slot": mine["slot"], "row": row, "col": 3}
            squash = packet_for(SQUASH_TYPE)
            if x < 320 and squash is not None and sun >= squash["cost"]:
                col = max(1, min(9, int((x - 40) / 80) + 1))
                if (row, col) not in occupied:
                    return {"type": "plant", "slot": squash["slot"], "row": row, "col": col}

    peashooter = packet_for(PEASHOOTER_TYPE)
    if "action.plant" in capabilities and peashooter is not None:
        defended_rows = {p.get("row") for p in plants if isinstance(p, dict) and
                         p.get("plant_type") == PEASHOOTER_TYPE and isinstance(p.get("col"), int)}
        # Add a second shooter when a row has several visible zombies or one has high health.
        for row in sorted(row_zombies, key=lambda r: min(z["x"] for z in row_zombies[r])):
            row_z = row_zombies[row]
            peas = sorted(p.get("col") for p in plants if isinstance(p, dict) and
                          p.get("plant_type") == PEASHOOTER_TYPE and p.get("row") == row and
                          isinstance(p.get("col"), int))
            multiple = len(row_z) >= 2 or any(isinstance(z.get("health"), int) and
                                                not isinstance(z.get("health"), bool) and
                                                z["health"] >= STRONG_ZOMBIE_HEALTH for z in row_z)
            if row in defended_rows:
                if multiple and len(peas) < 2 and sun >= peashooter["cost"]:
                    for col in (5, 4, 6):
                        if (row, col) not in occupied:
                            return {"type": "plant", "slot": peashooter["slot"], "row": row, "col": col}
                continue
            if sun >= peashooter["cost"]:
                x = min(z["x"] for z in row_z)
                col = 3 if x > 240 else 1
                if (row, col) not in occupied:
                    return {"type": "plant", "slot": peashooter["slot"], "row": row, "col": col}

    if "action.collect" in capabilities:
        for item in collectibles:
            if not isinstance(item, dict) or not isinstance(item.get("kind"), str):
                continue
            kind = item["kind"].casefold().replace("-", "_").replace(" ", "_")
            if kind not in COLLECTIBLE_KINDS or item.get("being_collected") is True:
                continue
            entity_id = item.get("id")
            if isinstance(entity_id, (str, int)) and not isinstance(entity_id, bool):
                return {"type": "collect", "entity_id": entity_id}

    # Farm two rear columns across five rows, while reserving enough sun for undefended rows.
    sunflower = packet_for(SUNFLOWER_TYPE)
    if "action.plant" in capabilities and sunflower is not None:
        pea_reserve = 0
        if row_zombies:
            pea = packet_for(PEASHOOTER_TYPE, require_ready=False)
            guarded = {p.get("row") for p in plants if isinstance(p, dict) and p.get("plant_type") == PEASHOOTER_TYPE}
            if pea is not None:
                pea_reserve = len(set(row_zombies) - guarded) * pea["cost"]
        if sun >= sunflower["cost"] + pea_reserve:
            for row in range(1, 6):
                for col in (1, 2):
                    if (row, col) not in occupied:
                        return {"type": "plant", "slot": sunflower["slot"], "row": row, "col": col}

    return None


def _caps(state: dict) -> set[str]:
    value = state.get("capabilities", [])
    return {x for x in value if isinstance(x, str)} if isinstance(value, list) else set()


def _is_menu_scene(state: dict) -> bool:
    name = state.get("scene_name")
    return ((isinstance(name, str) and name.casefold() in MENU_SCENES) or
            state.get("scene") in {1, 2})


def _is_live_board(state: dict) -> bool:
    return state.get("board_id") is not None and (
        state.get("scene") == 3 or state.get("scene_name") == "playing")


def _terminal_outcome(state: dict) -> str | None:
    """Return a terminal UI/result label when the game has reported one."""
    scene_name = state.get("scene_name")
    if state.get("scene") == 4 or scene_name == "lost":
        return "lost"
    if state.get("scene") == 5 or scene_name == "award":
        return "award"
    result = state.get("result")
    if isinstance(result, str):
        normalized = result.casefold()
        if normalized in {"lost", "loss", "lose", "defeat"}:
            return "lost"
        if normalized in {"won", "win", "victory"}:
            return "won"
        if normalized in {"award", "complete", "completed"}:
            return "award"
        if normalized == "quit":
            return "quit"
    if isinstance(result, dict):
        if result.get("lost") is True or result.get("won") is False:
            return "lost"
        if result.get("won") is True:
            return "won"
        if result.get("complete") is True or result.get("terminal") is True:
            return "board_result"
    return None


def _action_rejected(result: Any) -> bool:
    return result is False or (isinstance(result, dict) and result.get("accepted") is False)


def _wait(poll_interval: float, deadline: float, clock, sleep) -> None:
    remaining = max(0.0, deadline - clock())
    if remaining:
        sleep(min(poll_interval, remaining))


def _trace(stream, event: str, **fields) -> None:
    if stream is None:
        return
    record = {"time_utc": datetime.now(timezone.utc).isoformat(), "event": event, **fields}
    stream.write(json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n")
    stream.flush()


def _fresh_enough(observed: dict, current: dict, max_state_age_ticks: int) -> bool:
    old_tick, tick = observed.get("tick"), current.get("tick")
    if (not isinstance(old_tick, int) or isinstance(old_tick, bool) or
            not isinstance(tick, int) or isinstance(tick, bool) or tick < old_tick or
            tick - old_tick > max_state_age_ticks):
        return False
    if current.get("mode_id") != observed.get("mode_id"):
        return False
    if current.get("board_id") != observed.get("board_id"):
        return False
    return True


def run_strategy(host, model, goal: str, *, max_steps: int = 100, max_seconds: float = 300,
                 max_state_age_ticks: int = 120, max_actions_per_proposal: int = 1,
                 poll_interval: float = 0.1, cancel_event=None,
                 pause_on_model_error: bool = False, pause_on_finish: bool = True,
                 trace_path: Path | str | None = None,
                 clock=time.monotonic, sleep=time.sleep) -> dict:
    """Run repeated observe/propose/validate/act steps with strict live-state guards.

    `max_steps` counts observations, including waits for complete state. Each action
    is based on a fresh state and discarded if board, mode, or tick deadline changed
    while the local model was responding. This loop is bounded and interruptible; it
    makes no claim that the goal was achieved.
    """
    if not isinstance(goal, str) or not goal.strip() or len(goal) > 1000:
        raise ValueError("goal must be a nonempty string of at most 1000 characters.")
    if (isinstance(max_steps, bool) or not isinstance(max_steps, int) or not 1 <= max_steps <= 100_000 or
            not isinstance(max_seconds, (int, float)) or isinstance(max_seconds, bool) or
            not math.isfinite(max_seconds) or max_seconds <= 0):
        raise ValueError("max_steps must be 1..100000 and max_seconds must be positive.")
    if (isinstance(max_state_age_ticks, bool) or not isinstance(max_state_age_ticks, int) or
            not 0 <= max_state_age_ticks <= 100_000 or
            isinstance(max_actions_per_proposal, bool) or not isinstance(max_actions_per_proposal, int) or
            not 1 <= max_actions_per_proposal <= 8 or
            not isinstance(poll_interval, (int, float)) or isinstance(poll_interval, bool) or
            not math.isfinite(poll_interval) or poll_interval < 0):
        raise ValueError("Invalid state-age, proposal-size, or poll interval limit.")
    cancel_event = threading.Event() if cancel_event is None else cancel_event
    started = clock()
    deadline = started + max_seconds
    iterations = actions = 0
    outcome = "step_limit"
    safe_pause_required = False
    safe_pause_state = None
    trace = None
    if trace_path is not None:
        path = Path(trace_path)
        path.parent.mkdir(parents=True, exist_ok=True)
        trace = path.open("a", encoding="utf-8")
    try:
        _trace(trace, "start", goal=goal.strip(), max_steps=max_steps, max_seconds=max_seconds)
        while iterations < max_steps and clock() < deadline:
            if cancel_event.is_set():
                outcome = "cancelled"
                break
            state = host.request("state")
            iterations += 1
            if not isinstance(state, dict):
                outcome = "invalid_state"
                _trace(trace, "error", message="Host returned a non-object state.")
                break
            tick = state.get("tick")
            if not isinstance(tick, int) or isinstance(tick, bool):
                outcome = "invalid_state"
                _trace(trace, "error", message="State tick is missing or invalid.")
                break
            terminal = _terminal_outcome(state)
            if terminal is not None:
                outcome = terminal
                _trace(trace, "stopped", reason=terminal, tick=tick)
                break
            if state.get("paused") is True:
                outcome = "paused"
                _trace(trace, "stopped", reason="game_paused", tick=tick)
                break

            scene = state.get("scene_name", state.get("scene"))
            menu_scene = _is_menu_scene(state)
            complete = state.get("observation_complete", True)
            if complete is False and not menu_scene:
                _trace(trace, "wait", reason="observation_incomplete", tick=tick, scene=scene)
                _wait(poll_interval, deadline, clock, sleep)
                continue

            capabilities = _caps(state)
            prompt = ("Goal: " + goal.strip() + "\nUse only ordinary visible-state actions. "
                      "Do not infer hidden state. The runner permits only actions advertised "
                      "in the current capabilities and may discard stale proposals. Propose at most "
                      f"{max_actions_per_proposal} action(s).")
            try:
                if model is None:
                    baseline = basic_adventure_policy(state)
                    proposals = [] if baseline is None else validate_action_proposal([baseline], 1)
                else:
                    original_timeout = getattr(model, "timeout", None)
                    if isinstance(original_timeout, (int, float)) and not isinstance(original_timeout, bool):
                        model.timeout = min(original_timeout, max(0.001, deadline - clock()))
                    try:
                        proposals = model.propose_actions(state, system_prompt=prompt,
                                                          max_actions=max_actions_per_proposal)
                    finally:
                        if original_timeout is not None:
                            model.timeout = original_timeout
                    proposals = validate_action_proposal(proposals, max_actions_per_proposal)
            except Exception as exc:
                if clock() >= deadline:
                    outcome = "time_limit"
                    _trace(trace, "model_deadline", tick=tick, error=type(exc).__name__)
                    break
                _trace(trace, "model_error", tick=tick, error=type(exc).__name__, message=str(exc))
                if (pause_on_model_error and _is_live_board(state) and
                        capabilities.intersection(PAUSE_CAPABILITIES)):
                    try:
                        host.request("pause")
                        outcome = "paused_after_model_error"
                    except Exception as pause_exc:
                        outcome = "model_error"
                        _trace(trace, "pause_error", error=type(pause_exc).__name__)
                else:
                    outcome = "model_error"
                break

            allowed = []
            for action in proposals:
                kind = action["type"]
                if f"action.{kind}" not in capabilities:
                    continue
                if complete is False and (not menu_scene or kind not in MENU_ACTIONS):
                    continue
                allowed.append(action)
            if not allowed:
                _trace(trace, "no_action", tick=tick, reason="empty_or_unadvertised_proposal")
                _wait(poll_interval, deadline, clock, sleep)
                continue

            dispatch_lock = getattr(host, "_dispatch_lock", None)
            discard_reason = None
            action_error = None
            rejected_result = False
            with dispatch_lock if dispatch_lock is not None else nullcontext():
                fresh = host.request("state")
                if (not isinstance(fresh, dict) or not _fresh_enough(state, fresh, max_state_age_ticks) or
                        fresh.get("paused") is True or
                        (fresh.get("observation_complete", True) is False and not menu_scene)):
                    discard_reason = "stale_state_or_boundary_changed"
                else:
                    terminal = _terminal_outcome(fresh)
                    if terminal is not None:
                        outcome = terminal
                        _trace(trace, "stopped", reason=terminal, tick=fresh.get("tick"))
                        break
                    fresh_caps = _caps(fresh)
                    action = next((a for a in allowed if f"action.{a['type']}" in fresh_caps), None)
                    if action is None:
                        discard_reason = "capability_changed"
                    elif cancel_event.is_set() or clock() >= deadline:
                        outcome = "cancelled" if cancel_event.is_set() else "time_limit"
                        break
                    else:
                        try:
                            result = host.request("action", action)
                        except Exception as exc:
                            action_error = exc
                        else:
                            rejected_result = _action_rejected(result)
            if discard_reason is not None:
                _trace(trace, "discard", tick=tick, reason=discard_reason)
                _wait(poll_interval, deadline, clock, sleep)
                continue
            if action_error is not None:
                # A transport failure may arrive after the game accepted the request.
                # Never send the same proposal again when its outcome is uncertain.
                safe_pause_required = True
                safe_pause_state = fresh
                outcome = "action_outcome_unknown"
                _trace(trace, "action_outcome_unknown", tick=fresh.get("tick"), action=action,
                       error=type(action_error).__name__, code=getattr(action_error, "code", None),
                       message=str(action_error))
                break
            if rejected_result:
                _trace(trace, "action_rejected", tick=fresh.get("tick"), action=action, result=result)
                _wait(poll_interval, deadline, clock, sleep)
                continue
            actions += 1
            event = "action_accepted_pending" if isinstance(result, dict) and result.get("verification_pending") is True else "action_accepted"
            _trace(trace, event, tick=fresh.get("tick"), board_id=fresh.get("board_id"),
                   action=action, result=result)
            _wait(poll_interval, deadline, clock, sleep)
        else:
            outcome = "step_limit" if iterations >= max_steps else "time_limit"
        if clock() >= deadline and outcome == "step_limit":
            outcome = "time_limit"
        if (pause_on_finish or safe_pause_required) and outcome not in {"paused", "paused_after_model_error", "won", "lost", "award", "quit", "board_result"}:
            final_state = None
            try:
                final_state = host.request("state")
            except Exception as exc:
                _trace(trace, "finish_state_error", error=type(exc).__name__)
            if final_state is None and safe_pause_required:
                # Use the last observed live board to make a best-effort pause even
                # when the transport remains unavailable after the uncertain action.
                final_state = safe_pause_state
            if (isinstance(final_state, dict) and _is_live_board(final_state) and
                    final_state.get("paused") is not True and _terminal_outcome(final_state) is None):
                if _caps(final_state).intersection(PAUSE_CAPABILITIES):
                    try:
                        host.request("pause")
                        outcome = outcome + "_paused"
                        _trace(trace, "paused_on_finish")
                    except Exception as exc:
                        _trace(trace, "pause_on_finish_error", error=type(exc).__name__)
                else:
                    _trace(trace, "pause_on_finish_skipped", reason="capability_not_advertised")
        _trace(trace, "finish", outcome=outcome, iterations=iterations, actions=actions,
               elapsed_seconds=max(0.0, clock() - started))
        return {"outcome": outcome, "goal": goal.strip(), "iterations": iterations, "actions": actions,
                "elapsed_seconds": max(0.0, clock() - started)}
    finally:
        if trace is not None:
            trace.close()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="python -m pvz_ai.runner", description="Bounded local-model PvZ strategy loop")
    parser.add_argument("--pid", type=int, required=True, help="PID of an already attached supported game process")
    parser.add_argument("--goal", required=True, help="Natural-language goal for this bounded run")
    parser.add_argument("--model-base-url", help="Local OpenAI-compatible server, for example http://127.0.0.1:1234/v1")
    parser.add_argument("--model", help="Local model name configured on that server")
    parser.add_argument("--basic-policy", action="store_true", help="Use the limited deterministic day Adventure policy instead of a model")
    parser.add_argument("--max-steps", type=int, default=100)
    parser.add_argument("--max-seconds", type=float, default=300)
    parser.add_argument("--max-state-age-ticks", type=int, default=120)
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--pause-on-model-error", action="store_true")
    parser.add_argument("--continue-on-finish", action="store_true", help="Leave the game running when this bounded run ends")
    args = parser.parse_args(argv)
    if ((args.basic_policy and (args.model or args.model_base_url)) or
            (not args.basic_policy and not (args.model and args.model_base_url))):
        parser.error("choose --basic-policy, or provide both --model-base-url and --model")
    from .host import Host
    host = Host()
    try:
        host.connect(args.pid)
        model = None if args.basic_policy else LocalModelClient(args.model_base_url, args.model)
        result = run_strategy(host, model, args.goal, max_steps=args.max_steps,
                              max_seconds=args.max_seconds, max_state_age_ticks=args.max_state_age_ticks,
                              trace_path=args.trace, pause_on_model_error=args.pause_on_model_error,
                              pause_on_finish=not args.continue_on_finish)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        normal_outcome = result["outcome"].removesuffix("_paused")
        return 0 if normal_outcome in {"step_limit", "time_limit", "cancelled"} else 2
    except KeyboardInterrupt:
        print(json.dumps({"outcome": "cancelled"}), flush=True)
        return 130
    except Exception as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, ensure_ascii=False), file=sys.stderr)
        return 2
    finally:
        host.close()


if __name__ == "__main__":
    raise SystemExit(main())
