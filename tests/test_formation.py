from __future__ import annotations

from pvz_ai.formation import threepeater_candidates, torchwood_candidates


def plant(kind: int, row: int, col: int) -> dict:
    return {"plant_type": kind, "row": row, "col": col}


def test_threepeater_packets_continue_and_balance_center_rows():
    plants = []
    targets = []
    for _ in range(6):
        target = threepeater_candidates(plants, {(p["row"], p["col"]) for p in plants})[0]
        targets.append(target)
        plants.append(plant(18, *target))

    assert [row for row, _ in targets] == [2, 5, 2, 5, 2, 5]
    assert all(1 <= col <= 4 for _, col in targets)
    assert all(row in (2, 5) for row, _ in targets)


def test_threepeater_targets_avoid_occupied_rear_cells():
    plants = [plant(18, 2, 1), plant(18, 5, 1)]
    candidates = threepeater_candidates(plants, {(2, 1), (5, 1)})
    assert candidates[0] == (2, 2)
    assert all(col in (2, 3, 4) for _, col in candidates)


def test_torchwood_uses_adjacent_threepeater_projectiles_and_front_reserve():
    plants = [plant(18, 2, 4)]
    targets = torchwood_candidates(plants, set())
    assert targets == ((1, 6), (2, 6), (3, 6))
    assert all(col in (5, 6) for _, col in targets)
    # Every source shooter remains behind its torch target.
    assert all(4 < col for _, col in targets)


def test_torchwood_falls_back_to_five_and_suppresses_duplicate_lane():
    plants = [plant(0, 1, 4), plant(22, 2, 6)]
    targets = torchwood_candidates(plants, {(1, 6), (2, 6)})
    assert targets == ((1, 5),)


def test_torch_targets_stay_ahead_of_shooters_and_behind_spike_reserve():
    plants = [plant(0, 1, 4), plant(22, 2, 6)]
    targets = torchwood_candidates(plants, {(1, 4), (2, 6)})
    assert targets == ((1, 6),)
    assert targets[0][1] > plants[0]["col"]
    assert targets[0][1] < 7
