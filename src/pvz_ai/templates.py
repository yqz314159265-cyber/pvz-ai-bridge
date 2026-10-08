"""Validated, bounded gameplay templates. Templates describe legal actions only."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any

MAX_ACTIONS = 64
MAX_HORIZON_TICKS = 60 * 60 * 10
ACTION_TYPES = {"enter_level", "choose_cards", "start_level", "plant", "shovel",
                "collect", "continue", "restart", "back_to_menu", "enter_garden",
                "garden_care", "garden_move", "open_shop", "shop_page", "shop_buy", "close_shop",
                "open_menu", "close_menu", "open_almanac", "close_almanac", "option_set"}
ACTION_TYPES |= {"profile_open", "profile_close", "profile_select", "profile_create",
                 "profile_rename", "profile_delete", "profile_job", "zombatar"}
ACTION_TYPES |= {"navigate", "ui", "almanac_page", "almanac_entry"}
ACTION_TYPES.add("click_to_start")
ACTION_TYPES.add("game_input")
ACTION_TYPES |= {"achievements_back", "achievements_scroll"}
ACTION_TYPES |= {"quit_open", "quit_cancel", "quit_confirm", "credits_skip"}
ACTION_TYPES |= {"profile_delete_confirm", "open_credits"}
ACTION_TYPES |= {"garden_chocolate", "garden_feed_snail", "garden_wheelbarrow_pick",
                 "garden_wheelbarrow_place", "garden_next", "tree_feed", "garden_sell"}
ACTION_TYPES |= {"garden_cursor_cancel", "garden_wake_snail", "garden_sale_confirm", "garden_sale_cancel"}
CONDITION_FIELDS = {"scene", "mode_id", "paused", "sun", "wave", "tick"}
OPS = {"eq", "ne", "lt", "lte", "gt", "gte"}


class TemplateError(ValueError):
    """Template or plan violates the bounded semantic schema."""


def _integer(value: Any, label: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise TemplateError(f"{label} must be an integer from {minimum} to {maximum}.")
    return value


def _validate_action(action: Any) -> dict:
    if not isinstance(action, dict) or not isinstance(action.get("type"), str) or action["type"] not in ACTION_TYPES:
        raise TemplateError("Each action must have a supported semantic type.")
    kind = action["type"]
    if kind == "ui":
        operation = action.get("action")
        fields = {"type", "control_id", "action"} | ({"value"} if operation == "drag" else set())
        if not isinstance(operation, str) or operation not in {"activate", "drag"} or set(action) != fields:
            raise TemplateError("ui requires activate or drag with a current control_id.")
        if not isinstance(action["control_id"], str) or not 1 <= len(action["control_id"]) <= 40:
            raise TemplateError("Invalid UI control token.")
        if operation == "drag" and (isinstance(action["value"], bool) or not isinstance(action["value"], (int,float)) or not 0 <= action["value"] <= 1):
            raise TemplateError("Slider value must be between 0 and 1.")
        return dict(action)
    if kind == "zombatar":
        operation = action.get("operation")
        fields = {
            "open_zombatar": set(), "accept_terms": set(), "back": set(), "save_new": set(), "new_avatar": set(), "skin": set(),
            "category": {"index"}, "page": {"direction"}, "saved_avatar": {"direction"},
            "item": {"index"}, "color": {"index"},
            "delete_new": {"confirm"},
        }
        if not isinstance(operation, str) or operation not in fields:
            raise TemplateError("Unsupported Zombatar operation.")
        if set(action) != {"type", "operation", *fields[operation]}:
            raise TemplateError("Unexpected Zombatar parameters.")
        if operation == "category": _integer(action["index"], "index", 0, 7)
        if operation in {"item", "color"}: _integer(action["index"], "index", 0, 255)
        if operation in {"page", "saved_avatar"} and (not isinstance(action["direction"], str) or action["direction"] not in {"next", "previous"}):
            raise TemplateError("direction must be next or previous.")
        if operation == "delete_new" and action["confirm"] is not True:
            raise TemplateError("Deleting a session-created Zombatar requires confirm=true.")
        return dict(action)
    required = {
        "enter_level": {"mode_id"}, "choose_cards": {"plants"}, "start_level": set(),
        "plant": {"slot", "row", "col"}, "shovel": {"row", "col", "layer"},
        "collect": {"entity_id"}, "continue": set(), "restart": set(), "back_to_menu": set(),
        "enter_garden": {"garden_id"}, "garden_care": {"plant_id", "tool"},
        "garden_move": {"plant_id", "row", "col"}, "open_shop": set(), "shop_page": {"page"},
        "shop_buy": {"item_id", "quantity", "max_total_cost"}, "close_shop": set(),
        "open_menu": set(), "close_menu": set(),
        "open_almanac": set(), "close_almanac": set(), "option_set": {"name", "value"},
        "profile_open": set(), "profile_close": {"dialog_id"},
        "profile_select": {"name"}, "profile_create": {"name"},
        "profile_rename": {"id", "name"}, "profile_delete": {"id"}, "profile_job": {"job_id"},
        "navigate": {"route"}, "almanac_page": {"page"}, "almanac_entry": {"kind", "index"},
        "click_to_start": set(),
        "game_input": {"board_id", "surface", "x", "y", "button"},
        "achievements_back": set(), "achievements_scroll": {"direction", "ticks"},
        "quit_open": set(), "quit_cancel": set(), "quit_confirm": {"confirm"}, "credits_skip": set(),
        "open_credits": set(), "profile_delete_confirm": {"job_id", "confirm"},
        "garden_chocolate": {"plant_id"}, "garden_feed_snail": {"stinky_id"},
        "garden_wheelbarrow_pick": {"plant_id"}, "garden_wheelbarrow_place": {"row", "col"},
        "garden_next": set(), "tree_feed": set(), "garden_sell": {"plant_id"},
        "garden_cursor_cancel": set(), "garden_wake_snail": {"stinky_id"},
        "garden_sale_confirm": {"plant_id", "confirm"}, "garden_sale_cancel": {"plant_id"},
    }[kind]
    if set(action) != {"type", *required}:
        raise TemplateError(f"Action {kind!r} has missing or unsupported parameters.")
    if kind == "quit_confirm":
        if action["confirm"] is not True:
            raise TemplateError("Quitting the game requires confirm=true.")
    elif kind == "game_input":
        if not isinstance(action["board_id"], str) or not 1 <= len(action["board_id"]) <= 128:
            raise TemplateError("game_input requires the current board_id.")
        if not isinstance(action["surface"], str) or action["surface"] not in {"board", "seed_chooser"}:
            raise TemplateError("Unknown game-input surface.")
        if not isinstance(action["button"], str) or action["button"] not in {"left", "right"}:
            raise TemplateError("Unknown game-input button.")
        _integer(action["x"], "x", 0, 799); _integer(action["y"], "y", 0, 599)
    elif kind == "achievements_scroll":
        if not isinstance(action["direction"], str) or action["direction"] not in {"up", "down"}:
            raise TemplateError("Unknown achievements scroll direction.")
        _integer(action["ticks"], "ticks", 1, 60)
    elif kind in {"garden_chocolate","garden_wheelbarrow_pick","garden_sell","garden_sale_confirm","garden_sale_cancel"}:
        _integer(action["plant_id"], "plant_id", 1, 2**32 - 1)
        if kind == "garden_sale_confirm" and action["confirm"] is not True:
            raise TemplateError("Selling the verified temporary-profile plant requires confirm=true.")
    elif kind in {"garden_feed_snail","garden_wake_snail"}:
        _integer(action["stinky_id"], "stinky_id", 1, 2**32 - 1)
    elif kind == "garden_wheelbarrow_place":
        _integer(action["row"], "row", 1, 10); _integer(action["col"], "col", 1, 20)
    elif kind == "navigate":
        if not isinstance(action["route"], str) or action["route"] not in {"adventure","mini_games","puzzle","options","help","shop","almanac","zen_garden","survival","achievements","achievements_back"}:
            raise TemplateError("Unsupported menu route.")
    elif kind == "almanac_page":
        if not isinstance(action["page"], str) or action["page"] not in {"index","plants","zombies"}: raise TemplateError("Unknown Almanac page.")
    elif kind == "almanac_entry":
        if not isinstance(action["kind"], str) or action["kind"] not in {"plant", "zombie"}: raise TemplateError("Unknown Almanac entry kind.")
        _integer(action["index"], "index", 0, 48 if action["kind"] == "plant" else 32)
    elif kind.startswith("profile_"):
        if "name" in action:
            name = action["name"]
            if not isinstance(name, str) or not 1 <= len(name) <= 12 or not all(c.isascii() and (c.isalnum() or c == " ") for c in name):
                raise TemplateError("Profile names require 1-12 ASCII letters, digits or spaces.")
            if name != name.strip() or "  " in name:
                raise TemplateError("Profile names cannot contain repeated or edge spaces.")
        if "id" in action: _integer(action["id"], "id", 1, 2**32 - 1)
        if "job_id" in action: _integer(action["job_id"], "job_id", 1, 2**32 - 1)
        if "dialog_id" in action: _integer(action["dialog_id"], "dialog_id", 29, 32)
        if kind == "profile_delete_confirm" and action["confirm"] is not True:
            raise TemplateError("Deleting the verified temporary profile requires confirm=true.")
    elif kind == "option_set":
        name, value = action["name"], action["value"]
        if name in {"music", "sound"}:
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 <= value <= 1:
                raise TemplateError("Volume must be a number from 0 to 1.")
        elif name in {"fullscreen", "hardware_acceleration"}:
            if not isinstance(value, bool): raise TemplateError("Checkbox value must be boolean.")
        else: raise TemplateError("Unknown option name.")
    elif kind == "plant":
        _integer(action["slot"], "slot", 1, 100); _integer(action["row"], "row", 1, 10); _integer(action["col"], "col", 1, 20)
    elif kind == "shovel":
        _integer(action["row"], "row", 1, 10); _integer(action["col"], "col", 1, 20)
        if not isinstance(action["layer"], str) or action["layer"] not in {"plant", "pumpkin", "lily", "pot"}:
            raise TemplateError("shovel layer must name a visible board layer.")
    elif kind == "choose_cards":
        plants = action["plants"]
        if not isinstance(plants, list) or not plants or len(plants) > 10 or not all(isinstance(p, str) and p for p in plants):
            raise TemplateError("plants must be a nonempty list of at most 10 plant names.")
    elif kind == "enter_level" and (not isinstance(action["mode_id"], (str, int)) or isinstance(action["mode_id"], bool)):
        raise TemplateError("mode_id must be a string or integer.")
    elif kind == "collect" and (not isinstance(action["entity_id"], (str, int)) or isinstance(action["entity_id"], bool)):
        raise TemplateError("entity_id must be a string or integer.")
    elif kind == "enter_garden":
        _integer(action["garden_id"], "garden_id", 0, 2)
    elif kind == "garden_care":
        _integer(action["plant_id"], "plant_id", 0, 2**32 - 1)
        if not isinstance(action["tool"], str) or action["tool"] not in {"water", "fertilizer", "bug_spray", "phonograph"}:
            raise TemplateError("Unknown garden care tool.")
    elif kind == "garden_move":
        _integer(action["plant_id"], "plant_id", 0, 2**32 - 1)
        _integer(action["row"], "row", 1, 10); _integer(action["col"], "col", 1, 20)
    elif kind == "shop_page":
        _integer(action["page"], "page", 0, 3)
    elif kind == "shop_buy":
        _integer(action["item_id"], "item_id", 0, 2**31 - 1)
        _integer(action["quantity"], "quantity", 1, 1)
        _integer(action["max_total_cost"], "max_total_cost", 0, 2**31 - 1)
    return dict(action)


def _condition(condition: Any) -> dict:
    if not isinstance(condition, dict) or set(condition) != {"field", "op", "value"}:
        raise TemplateError("A condition must contain exactly field, op, and value.")
    if condition["field"] not in CONDITION_FIELDS or condition["op"] not in OPS:
        raise TemplateError("Condition uses an unsupported field or operator.")
    if condition["field"] in {"tick", "sun", "wave"}:
        _integer(condition["value"], "condition value", 0, 2**31 - 1)
    elif condition["field"] == "paused":
        if not isinstance(condition["value"], bool) or condition["op"] not in {"eq", "ne"}:
            raise TemplateError("paused conditions require a boolean value and eq/ne operator.")
    elif condition["field"] in {"scene", "mode_id"}:
        if not isinstance(condition["value"], (str, int)) or isinstance(condition["value"], bool) or condition["op"] not in {"eq", "ne"}:
            raise TemplateError("scene and mode_id conditions require string/integer equality.")
    return dict(condition)


@dataclass
class GuardedPlan:
    """Finite sequence bound to a mode, board identity, and expiry tick."""
    mode_id: Any
    expires_tick: int
    steps: list[dict]
    board_id: Any = None
    index: int = 0
    executed: int = 0
    _last_tick: int = -1
    _pending: dict | None = None
    _halted: bool = False

    @property
    def actions(self) -> list[dict]:
        return [dict(s["action"]) for s in self.steps]

    @property
    def done(self) -> bool:
        """True after every finite step has been accepted."""
        return not self._halted and self.index >= len(self.steps)

    @property
    def halted(self) -> bool:
        """True when a safety boundary or rejected action stopped the plan."""
        return self._halted

    def next_action(self, state: dict) -> dict | None:
        """Return the current proposal; acknowledge it before advancing the plan."""
        if not isinstance(state, dict) or self._halted:
            return None
        tick = state.get("tick")
        if not isinstance(tick, int) or isinstance(tick, bool) or tick > self.expires_tick or tick < self._last_tick:
            self._halted = True
            return None
        if state.get("mode_id") != self.mode_id or state.get("paused") is True:
            self._halted = True
            return None
        current_board = state.get("board_id")
        if current_board is not None:
            if self.board_id is None:
                self.board_id = current_board
            elif current_board != self.board_id:
                self._halted = True
                return None
        elif self.board_id is not None:
            self._halted = True
            return None
        self._last_tick = tick
        if self._pending is not None:
            return dict(self._pending)
        if self.index >= len(self.steps):
            return None
        step = self.steps[self.index]
        # A false condition waits for fresh state; it does not consume the step.
        if not all(_condition_holds(c, state) for c in step["when"]):
            return None
        self._pending = dict(step["action"])
        return dict(self._pending)

    def ack_action(self, accepted: bool) -> None:
        """Advance only after host acceptance; rejection halts with step unconsumed."""
        if self._pending is None:
            raise TemplateError("There is no outstanding action proposal to acknowledge.")
        if not isinstance(accepted, bool):
            raise TemplateError("accepted must be a boolean.")
        if accepted:
            self.index += 1
            self.executed += 1
            self._pending = None
        else:
            self._halted = True

    acknowledge = ack_action


def _condition_holds(c: dict, state: dict) -> bool:
    actual, expected = state.get(c["field"]), c["value"]
    return {"eq": lambda: actual == expected, "ne": lambda: actual != expected,
            "lt": lambda: actual is not None and actual < expected,
            "lte": lambda: actual is not None and actual <= expected,
            "gt": lambda: actual is not None and actual > expected,
            "gte": lambda: actual is not None and actual >= expected}[c["op"]]()


def validate_template(data: Any) -> dict:
    if not isinstance(data, dict) or set(data) != {"name", "version", "mode_id", "goal", "cards", "layers", "prerequisites", "horizon_ticks", "actions"}:
        raise TemplateError("Template requires name, version, mode_id, goal, cards, layers, prerequisites, horizon_ticks, and actions.")
    if not isinstance(data["name"], str) or not data["name"].strip() or data["version"] != 1:
        raise TemplateError("Template needs a nonempty name and version 1.")
    if not isinstance(data["mode_id"], (str, int)) or isinstance(data["mode_id"], bool):
        raise TemplateError("mode_id must be a string or integer.")
    if not isinstance(data["goal"], str) or not data["goal"].strip() or len(data["goal"]) > 500:
        raise TemplateError("goal must be a nonempty description of at most 500 characters.")
    cards = data["cards"]
    if not isinstance(cards, list) or len(cards) > 10 or not all(isinstance(x, str) and x for x in cards):
        raise TemplateError("cards must be a list of at most 10 plant names.")
    layers = data["layers"]
    if not isinstance(layers, dict) or set(layers) - {"lily", "pot", "pumpkin", "upgrade"} or not all(isinstance(k, str) for k in layers):
        raise TemplateError("layers may contain only lily, pot, pumpkin, and upgrade entries.")
    for k, v in layers.items():
        if not isinstance(v, (str, list)):
            raise TemplateError(f"layers.{k} must be a plant name or list of plant names.")
    prereq = data["prerequisites"]
    if not isinstance(prereq, list) or not all(isinstance(x, str) for x in prereq):
        raise TemplateError("prerequisites must be a list of unlock or upgrade names.")
    horizon = _integer(data["horizon_ticks"], "horizon_ticks", 1, MAX_HORIZON_TICKS)
    actions = data["actions"]
    if not isinstance(actions, list) or not 1 <= len(actions) <= MAX_ACTIONS:
        raise TemplateError(f"actions must contain 1 to {MAX_ACTIONS} steps.")
    steps = []
    for step in actions:
        if not isinstance(step, dict) or set(step) - {"action", "when"} or "action" not in step:
            raise TemplateError("Each step requires action and may contain when.")
        when = step.get("when", [])
        if not isinstance(when, list):
            raise TemplateError("when must be a list of conditions.")
        steps.append({"action": _validate_action(step["action"]), "when": [_condition(c) for c in when]})
    return {"name": data["name"].strip(), "version": 1, "mode_id": data["mode_id"], "goal": data["goal"].strip(), "cards": list(cards),
            "layers": dict(layers), "prerequisites": list(prereq), "horizon_ticks": horizon, "actions": steps}


def compile_template(data: Any, now_tick: int, horizon_ticks: int | None = None,
                     board_id: Any = None) -> GuardedPlan:
    template = validate_template(data)
    start = _integer(now_tick, "now_tick", 0, 2**31 - 1)
    horizon = template["horizon_ticks"] if horizon_ticks is None else min(template["horizon_ticks"], _integer(horizon_ticks, "horizon_ticks", 1, MAX_HORIZON_TICKS))
    return GuardedPlan(template["mode_id"], start + horizon, template["actions"], board_id=board_id)
