import copy
import json
from pathlib import Path

import pytest

from pvz_ai.templates import TemplateError, compile_template, validate_template

EXAMPLE = json.loads((Path(__file__).parents[1] / "templates" / "illustrative_day_1.json").read_text(encoding="utf-8"))
ENDLESS_EXAMPLES = [
    json.loads(path.read_text(encoding="utf-8"))
    for path in sorted(Path(__file__).parents[1].joinpath("templates").glob("illustrative_survival_*_endless.json"))
]


def test_example_validates_and_compiles_to_finite_plan():
    parsed = validate_template(EXAMPLE)
    plan = compile_template(parsed, now_tick=100)
    assert plan.mode_id == "adventure"
    assert plan.expires_tick == 1900
    assert len(plan.actions) == 3


def test_endless_formation_examples_validate_as_bounded_normal_actions():
    assert {doc["mode_id"] for doc in ENDLESS_EXAMPLES} == {11, 13}
    for doc in ENDLESS_EXAMPLES:
        validated = validate_template(doc)
        plan = compile_template(validated, now_tick=0, board_id="manual-entry-board")
        assert plan.expires_tick == doc["horizon_ticks"]
        assert all(step["action"]["type"] != "instant_apply" for step in validated["actions"])
        assert all(step["action"]["type"] in {"choose_cards", "start_level", "plant"} for step in validated["actions"])


@pytest.mark.parametrize("mutate", [
    lambda d: d["actions"][0].update(action={"type": "plant", "row": 1, "col": 1, "slot": 1, "python": "__import__('os')"}),
    lambda d: d.update(horizon_ticks=0),
    lambda d: d["actions"].append({"action": {"type": "eval", "code": "x"}}),
    lambda d: d["actions"][2].update(when=[{"field": "__class__", "op": "eq", "value": "x"}]),
])
def test_rejects_invalid_or_executable_template(mutate):
    bad = copy.deepcopy(EXAMPLE)
    mutate(bad)
    with pytest.raises(TemplateError):
        validate_template(bad)


def test_plan_stops_when_expired_paused_or_wrong_mode():
    plan = compile_template(EXAMPLE, 50, horizon_ticks=2)
    assert plan.next_action({"tick": 51, "mode_id": "adventure", "paused": False, "sun": 50})["type"] == "choose_cards"
    plan.ack_action(True)
    assert plan.next_action({"tick": 52, "mode_id": "adventure", "paused": False, "sun": 50})["type"] == "start_level"
    plan.ack_action(True)
    assert plan.next_action({"tick": 53, "mode_id": "adventure", "paused": False, "sun": 50}) is None
    assert compile_template(EXAMPLE, 5).next_action({"tick": 5, "mode_id": "other", "paused": False}) is None
    assert compile_template(EXAMPLE, 5).next_action({"tick": 5, "mode_id": "adventure", "paused": True}) is None


def test_false_condition_waits_for_later_state_and_ack_controls_advancement():
    plan = compile_template(EXAMPLE, 10, board_id="b1")
    first = {"tick": 10, "mode_id": "adventure", "paused": False, "board_id": "b1", "sun": 0}
    assert plan.next_action(first)["type"] == "choose_cards"
    assert plan.next_action(first)["type"] == "choose_cards"
    plan.ack_action(True)
    assert plan.next_action(first)["type"] == "start_level"
    plan.ack_action(True)
    assert plan.next_action(first) is None  # condition waits; it is not consumed
    later = {**first, "tick": 11, "sun": 50}
    assert plan.next_action(later)["type"] == "plant"
    plan.ack_action(False)
    assert plan.next_action({**later, "tick": 12}) is None


def test_plan_stops_after_board_identity_changes():
    plan = compile_template(EXAMPLE, 10)
    state = {"tick": 10, "mode_id": "adventure", "paused": False, "board_id": "board-a", "sun": 50}
    assert plan.next_action(state)["type"] == "choose_cards"
    plan.ack_action(True)
    changed = {**state, "tick": 11, "board_id": "board-b"}
    assert plan.next_action(changed) is None


@pytest.mark.parametrize("action", [
    {"type": "enter_garden", "garden_id": 0},
    {"type": "garden_care", "plant_id": 42, "tool": "water"},
    {"type": "garden_move", "plant_id": 42, "row": 1, "col": 2},
    {"type": "open_shop"},
    {"type": "shop_page", "page": 0},
    {"type": "shop_page", "page": 3},
    {"type": "garden_care", "plant_id": 2**32 - 1, "tool": "fertilizer"},
    {"type": "garden_move", "plant_id": 2**32 - 1, "row": 1, "col": 2},
    {"type": "shop_buy", "item_id": 3, "quantity": 1, "max_total_cost": 100},
    {"type": "close_shop"},
])
def test_garden_and_shop_actions_validate_as_proposals_only(action):
    template = copy.deepcopy(EXAMPLE)
    template["actions"] = [{"action": action}]
    assert validate_template(template)["actions"][0]["action"] == action


@pytest.mark.parametrize("action", [
    {"type": "enter_garden", "garden_id": 3},
    {"type": "garden_care", "plant_id": 42, "tool": "invented"},
    {"type": "garden_move", "plant_id": -1, "row": 1, "col": 2},
    {"type": "garden_care", "plant_id": 2**32, "tool": "water"},
    {"type": "garden_care", "plant_id": 42, "tool": "spray"},
    {"type": "shop_page", "page": -1},
    {"type": "shop_page", "page": 4},
    {"type": "shop_buy", "item_id": 3, "quantity": 11, "max_total_cost": 100},
    {"type": "shop_buy", "item_id": 3, "quantity": 2, "max_total_cost": 100},
    {"type": "shop_buy", "item_id": 3, "quantity": 1, "max_total_cost": -1},
])
def test_garden_and_shop_actions_reject_invalid_parameters(action):
    template = copy.deepcopy(EXAMPLE)
    template["actions"] = [{"action": action}]
    with pytest.raises(TemplateError):
        validate_template(template)
