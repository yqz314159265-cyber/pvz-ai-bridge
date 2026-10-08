from pvz_ai.catalog import MODE_BY_ID, MODE_CATALOG


def test_game_mode_ids_match_goty_enum_and_exclude_count_sentinel():
    assert len(MODE_CATALOG) == 73
    assert [entry["id"] for entry in MODE_CATALOG] == list(range(73))
    assert MODE_BY_ID[0]["enum_name"] == "Adventure"
    assert [MODE_BY_ID[i]["enum_name"] for i in range(11, 16)] == [
        "SurvivalEndless1", "SurvivalEndless2", "SurvivalEndless3",
        "SurvivalEndless4", "SurvivalEndless5",
    ]
    assert MODE_BY_ID[13]["name"] == "Survival: Pool (Endless)"
    assert MODE_BY_ID[43]["enum_name"] == "ZenGarden"
    assert MODE_BY_ID[50]["enum_name"] == "TreeOfWisdom"
    assert MODE_BY_ID[60]["enum_name"] == "VaseBreakerEndless"
    assert MODE_BY_ID[70]["enum_name"] == "IZombieEndless"
    assert MODE_BY_ID[71]["enum_name"] == "UpsellTest"
    assert MODE_BY_ID[72]["enum_name"] == "Intro"
    assert 73 not in MODE_BY_ID  # NUM_GAME_MODES is a count, not a mode.


def test_catalog_visibility_is_metadata_not_an_unlock_or_native_claim():
    assert not MODE_BY_ID[0]["hidden"]
    assert not MODE_BY_ID[13]["hidden"]
    assert MODE_BY_ID[11]["hidden"]
    assert not MODE_BY_ID[16]["hidden"] and not MODE_BY_ID[35]["hidden"]
    assert MODE_BY_ID[36]["hidden"] and not MODE_BY_ID[43]["hidden"]
    assert not MODE_BY_ID[50]["hidden"]
    assert all(not MODE_BY_ID[i]["hidden"] for i in range(51, 71))
    assert all(MODE_BY_ID[i]["hidden"] for i in (71, 72))
    assert all(entry["unlocked"] is None for entry in MODE_CATALOG)
    assert all(entry["native_support"] == "unknown" for entry in MODE_CATALOG)


def test_nonstandard_modes_require_mode_specific_advertised_actions():
    for mode_id in range(16, 36):
        assert MODE_BY_ID[mode_id]["action_profile"] == "minigame"
    for mode_id in range(51, 61):
        assert MODE_BY_ID[mode_id]["action_profile"] == "vasebreaker"
    for mode_id in range(61, 71):
        assert MODE_BY_ID[mode_id]["action_profile"] == "izombie"
    assert MODE_BY_ID[43]["action_profile"] == "garden"
    assert MODE_BY_ID[50]["action_profile"] == "tree"
