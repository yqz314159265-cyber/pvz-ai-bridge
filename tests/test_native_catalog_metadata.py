"""Tests for merging bounded native verification metadata into static modes."""

from pvz_ai.catalog import MODE_CATALOG
from pvz_ai.host import Host


def test_native_level_metadata_is_preserved_without_overriding_static_identity():
    native = [{
        "mode_id": 43,
        "id": 999,
        "name": "untrusted replacement",
        "enum_name": "UntrustedEnum",
        "entry_implemented": True,
        "ai_win_verified": False,
        "entry_live_verified": True,
        "entry_action": "enter_level",
        "selector_button_id": 109,
        "definition_page": 3,
        "page_position": {"row": 2, "column": 4},
        "hidden_selector_page": False,
        "requires_owned_tree": False,
        "requires_visible_enabled_button": True,
        "name_token": "ZEN_GARDEN",
        "attacker_controlled_extra": "must not escape",
    }]

    merged = Host._merge_levels(native)
    by_id = {mode["id"]: mode for mode in merged}
    result = by_id[43]
    canonical = next(mode for mode in MODE_CATALOG if mode["id"] == 43)

    assert len(merged) == 73
    assert result["id"] == canonical["id"]
    assert result["name"] == canonical["name"]
    assert result["enum_name"] == canonical["enum_name"]
    assert result["native_status_known"] is True
    assert result["native_support"] == "entry_implemented"
    assert result["entry_implemented"] is True
    assert result["ai_win_verified"] is False
    assert result["entry_live_verified"] is True
    assert result["entry_action"] == "enter_level"
    assert result["selector_button_id"] == 109
    assert result["definition_page"] == 3
    assert result["page_position"] == {"row": 2, "column": 4}
    assert result["hidden_selector_page"] is False
    assert result["requires_owned_tree"] is False
    assert result["requires_visible_enabled_button"] is True
    assert result["name_token"] == "ZEN_GARDEN"
    assert "attacker_controlled_extra" not in result
    assert by_id[0]["native_status_known"] is False


def test_unknown_native_modes_and_duplicate_ids_do_not_supply_metadata():
    merged = Host._merge_levels([
        {"mode_id": 900, "entry_implemented": True, "name_token": "UNKNOWN"},
        {"mode_id": 13, "entry_implemented": True, "name_token": "FIRST"},
        {"mode_id": 13, "entry_implemented": False, "name_token": "SECOND"},
        {"id": 0, "entry_implemented": True, "name_token": "ALIAS_ONLY"},
    ])
    by_id = {mode["id"]: mode for mode in merged}

    assert len(merged) == 73
    assert by_id[13]["native_status_known"] is False
    assert by_id[13]["entry_implemented"] is False
    assert "name_token" not in by_id[13]
    assert by_id[0]["native_status_known"] is False


def test_malformed_native_metadata_is_omitted_field_by_field():
    merged = Host._merge_levels([{
        "mode_id": 50,
        "entry_implemented": True,
        "ai_win_verified": 1,
        "entry_live_verified": "true",
        "entry_action": [],
        "selector_button_id": 256,
        "definition_page": 4,
        "page_position": {"row": 1, "column": 2, "extra": 3},
        "hidden_selector_page": 0,
        "requires_owned_tree": False,
        "requires_visible_enabled_button": True,
        "name_token": "TREE_OF_WISDOM\nINJECTED",
    }])
    result = next(mode for mode in merged if mode["id"] == 50)

    assert result["native_status_known"] is True
    assert result["entry_implemented"] is True
    assert result["ai_win_verified"] is False
    assert result["requires_owned_tree"] is False
    assert result["requires_visible_enabled_button"] is True
    for key in ("entry_live_verified", "entry_action", "selector_button_id",
                "definition_page", "page_position", "hidden_selector_page", "name_token"):
        assert key not in result


def test_definition_and_grid_fields_keep_the_full_verified_native_range():
    result = next(mode for mode in Host._merge_levels([{
        "mode_id": 72,
        "definition_page": 3,
        "page_position": {"row": 3, "column": 4},
    }]) if mode["id"] == 72)
    assert result["definition_page"] == 3
    assert result["page_position"] == {"row": 3, "column": 4}
