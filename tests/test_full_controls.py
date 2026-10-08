"""Focused schema contract tests for the extended UI/profile control actions."""

import copy
import math

import pytest

from pvz_ai.templates import TemplateError, validate_template
from pvz_ai.host import Host


def _validate_action(action):
    document = {
        "name": "full controls schema test",
        "version": 1,
        "mode_id": "adventure",
        "goal": "validate bounded semantic action schema",
        "cards": [],
        "layers": {},
        "prerequisites": [],
        "horizon_ticks": 100,
        "actions": [{"action": copy.deepcopy(action)}],
    }
    return validate_template(document)["actions"][0]["action"]


@pytest.mark.parametrize("action", [
    {"type": "profile_open"},
    {"type": "profile_close", "dialog_id": 29},
    {"type": "profile_select", "name": "Test 1"},
    {"type": "profile_create", "name": "Test 1"},
    {"type": "profile_rename", "id": 7, "name": "Renamed"},
    {"type": "profile_delete", "id": 7},
    {"type": "profile_job", "job_id": 1},
    {"type": "profile_delete_confirm", "job_id": 41, "confirm": True},
    {"type": "zombatar", "operation": "open_zombatar"},
    {"type": "zombatar", "operation": "accept_terms"},
    {"type": "zombatar", "operation": "back"},
    {"type": "zombatar", "operation": "save_new"},
    {"type": "zombatar", "operation": "new_avatar"},
    {"type": "zombatar", "operation": "skin"},
    {"type": "zombatar", "operation": "category", "index": 0},
    {"type": "zombatar", "operation": "item", "index": 255},
    {"type": "zombatar", "operation": "color", "index": 0},
    {"type": "zombatar", "operation": "page", "direction": "next"},
    {"type": "zombatar", "operation": "saved_avatar", "direction": "previous"},
    {"type": "zombatar", "operation": "delete_new", "confirm": True},
    {"type": "ui", "control_id": "ui-test-token", "action": "activate"},
    {"type": "ui", "control_id": "ui-test-token", "action": "drag", "value": 0.5},
    {"type": "navigate", "route": "adventure"},
    {"type": "navigate", "route": "achievements_back"},
    {"type": "almanac_page", "page": "index"},
    {"type": "almanac_page", "page": "plants"},
    {"type": "almanac_page", "page": "zombies"},
    {"type": "almanac_entry", "kind": "plant", "index": 0},
    {"type": "almanac_entry", "kind": "plant", "index": 48},
    {"type": "almanac_entry", "kind": "zombie", "index": 32},
])
def test_extended_control_actions_accept_documented_shapes(action):
    assert _validate_action(action) == action


def test_profile_delete_schema_accepts_a_bounded_integer_id_without_mirroring_native_protection():
    # The schema checks shape/range only. Protected original-profile identity
    # policy is enforced by the native profile guard and is intentionally not
    # duplicated here.
    assert _validate_action({"type": "profile_delete", "id": 1})["id"] == 1


@pytest.mark.parametrize("action", [
    {"type": "profile_delete_confirm", "confirm": True},
    {"type": "profile_delete_confirm", "job_id": 1},
    {"type": "profile_delete_confirm", "job_id": 1, "confirm": False},
    {"type": "profile_delete_confirm", "job_id": 1, "confirm": 1},
    {"type": "profile_delete_confirm", "job_id": 0, "confirm": True},
    {"type": "profile_delete_confirm", "job_id": -1, "confirm": True},
    {"type": "profile_delete_confirm", "job_id": 2**32, "confirm": True},
    {"type": "profile_delete_confirm", "job_id": 1, "confirm": True, "target_id": 2},
    {"type": "profile_delete_confirm", "job_id": True, "confirm": True},
])
def test_profile_delete_confirmation_requires_exact_positive_job_and_true_confirmation(action):
    with pytest.raises(TemplateError):
        _validate_action(action)


@pytest.mark.parametrize("action", [
    {"type": "game_input", "surface": "board", "x": 1, "y": 1, "button": "left"},
    {"type": "game_input", "board_id": "board-current", "x": 1, "y": 1, "button": "left"},
    {"type": "game_input", "board_id": "board-current", "surface": "board", "y": 1, "button": "left"},
    {"type": "game_input", "board_id": "board-current", "surface": "board", "x": 800, "y": 1, "button": "left"},
    {"type": "game_input", "board_id": "board-current", "surface": "board", "x": 1, "y": 600, "button": "left"},
    {"type": "game_input", "board_id": "board-current", "surface": "board", "x": True, "y": 1, "button": "left"},
    {"type": "game_input", "board_id": "board-current", "surface": "board", "x": 1, "y": False, "button": "left"},
    {"type": "game_input", "board_id": "board-current", "surface": "unknown", "x": 1, "y": 1, "button": "left"},
    {"type": "game_input", "board_id": "board-current", "surface": "board", "x": 1, "y": 1, "button": "middle"},
    {"type": "game_input", "board_id": "", "surface": "board", "x": 1, "y": 1, "button": "left"},
    {"type": "game_input", "board_id": 12, "surface": "board", "x": 1, "y": 1, "button": "left"},
])
def test_malformed_game_input_is_rejected_by_host_before_pipe_access(action):
    with pytest.raises(TemplateError):
        Host().request("action", action)


@pytest.mark.parametrize("action", [
    {"type": "zombatar", "operation": "item", "index": True},
    {"type": "zombatar", "operation": "item", "index": 256},
    {"type": "zombatar", "operation": "color", "index": -1},
    {"type": "zombatar", "operation": "color", "index": False},
    {"type": "zombatar", "operation": "new_avatar", "index": 0},
])
def test_malformed_zombatar_item_color_and_new_avatar_are_rejected(action):
    with pytest.raises(TemplateError):
        _validate_action(action)


@pytest.mark.parametrize("action", [
    None,
    [],
    "profile_delete",
    {"type": []},
    {"type": {}},
    {"type": "profile_select", "name": []},
    {"type": "profile_create", "name": {}},
    {"type": "profile_rename", "id": True, "name": "Test"},
    {"type": "profile_rename", "id": [], "name": "Test"},
    {"type": "profile_delete", "id": False},
    {"type": "profile_delete", "id": []},
    {"type": "profile_delete", "id": 0},
    {"type": "profile_delete", "id": 2**32},
    {"type": "profile_job", "job_id": {}},
    {"type": "profile_close", "dialog_id": True},
    {"type": "profile_close", "dialog_id": []},
    {"type": "zombatar", "operation": []},
    {"type": "zombatar", "operation": {}},
    {"type": "zombatar", "operation": "category", "index": True},
    {"type": "zombatar", "operation": "category", "index": []},
    {"type": "zombatar", "operation": "page", "direction": []},
    {"type": "zombatar", "operation": "saved_avatar", "direction": {}},
    {"type": "zombatar", "operation": "delete_new", "confirm": 1},
    {"type": "ui", "control_id": [], "action": "activate"},
    {"type": "ui", "control_id": "ui-token", "action": []},
    {"type": "ui", "control_id": "ui-token", "action": {}},
    {"type": "ui", "control_id": "ui-token", "action": "drag", "value": True},
    {"type": "ui", "control_id": "ui-token", "action": "drag", "value": math.nan},
    {"type": "ui", "control_id": "ui-token", "action": "drag", "value": math.inf},
    {"type": "ui", "control_id": "ui-token", "action": "drag", "value": []},
    {"type": "ui", "control_id": "ui-token", "action": "drag", "value": {}},
    {"type": "ui", "control_id": "ui-token", "action": "drag", "value": 1.01},
    {"type": "navigate", "route": []},
    {"type": "navigate", "route": {}},
    {"type": "navigate", "route": "unknown"},
    {"type": "almanac_page", "page": []},
    {"type": "almanac_page", "page": {}},
    {"type": "almanac_page", "page": "everything"},
    {"type": "almanac_entry", "kind": [] , "index": 0},
    {"type": "almanac_entry", "kind": {}, "index": 0},
    {"type": "almanac_entry", "kind": "plant", "index": True},
    {"type": "almanac_entry", "kind": "plant", "index": 49},
    {"type": "almanac_entry", "kind": "zombie", "index": 33},
])
def test_malformed_extended_actions_raise_template_error(action):
    with pytest.raises(TemplateError):
        _validate_action(action)
