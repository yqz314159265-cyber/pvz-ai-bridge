import json

import pytest

from pvz_ai.local_model import LocalModelClient, LocalModelError, parse_chat_completion, validate_action_proposal


def test_local_endpoint_restriction():
    with pytest.raises(ValueError):
        LocalModelClient("https://api.example.com/v1", "model")
    with pytest.raises(ValueError):
        LocalModelClient("http://user:secret@localhost:1234/v1", "model")


def test_parses_tool_call_and_validates_semantic_action():
    payload = {"choices": [{"message": {"tool_calls": [{"function": {"name": "propose_actions", "arguments": json.dumps({"actions": [{"type": "plant", "slot": 1, "row": 2, "col": 3}]})}}]}}]}
    assert parse_chat_completion(payload) == [{"type": "plant", "slot": 1, "row": 2, "col": 3}]


def test_rejects_invalid_or_overlong_model_output():
    with pytest.raises(LocalModelError):
        validate_action_proposal([{"type": "plant", "slot": 1, "row": 2, "col": 3, "cheat": True}])
    with pytest.raises(LocalModelError):
        validate_action_proposal([{"type": "continue"}] * 9)
    with pytest.raises(LocalModelError):
        parse_chat_completion({"choices": [{"message": {"content": "not json"}}]})


def test_local_model_can_propose_valid_garden_or_shop_actions():
    actions = validate_action_proposal([
        {"type": "garden_care", "plant_id": 2**32 - 1, "tool": "water"},
        {"type": "shop_page", "page": 3},
        {"type": "shop_buy", "item_id": 3, "quantity": 1, "max_total_cost": 100},
    ], 3)
    assert [item["type"] for item in actions] == ["garden_care", "shop_page", "shop_buy"]


def test_local_model_rejects_unsupported_garden_and_multi_quantity_shop_actions():
    with pytest.raises(LocalModelError, match="invalid semantic action"):
        validate_action_proposal([
            {"type": "garden_care", "plant_id": 42, "tool": "music"},
        ])
    with pytest.raises(LocalModelError, match="invalid semantic action"):
        validate_action_proposal([
            {"type": "shop_buy", "item_id": 3, "quantity": 2, "max_total_cost": 100},
        ])
