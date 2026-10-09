"""Pure formation planning for the Adventure 3-10 pool board.

These helpers only inspect visible plant/cell data and return target cells.
They do not access the game bridge or issue actions.
"""
from __future__ import annotations

from collections.abc import Iterable, Mapping

Cell = tuple[int, int]

THREEPEATER = 18
TORCHWOOD = 22
PEA_SHOOTERS = {0, 7, THREEPEATER, 28, 40}
THREEPEATER_ROWS = (2, 5)
REAR_COLUMNS = (1, 2, 3, 4)


def _plant_type(plant: Mapping) -> int | None:
    value = plant.get("plant_type", plant.get("type"))
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def threepeater_candidates(
    plants: Iterable[Mapping], occupied_cells: Iterable[Cell], *, cols: int = 9
) -> tuple[Cell, ...]:
    """Return balanced rear targets, preferring the less-filled center lane.

    Multiple cards remain useful: the planner alternates rows 2 and 5 while
    both are equally populated, and each lane may use columns 1 through 4.
    """
    current = list(plants)
    occupied = set(occupied_cells)
    counts = {
        row: sum(_plant_type(plant) == THREEPEATER and plant.get("row") == row
                 for plant in current)
        for row in THREEPEATER_ROWS
    }
    ordered_rows = sorted(THREEPEATER_ROWS, key=lambda row: (counts[row], row))
    return tuple(
        (row, col)
        for row in ordered_rows
        for col in REAR_COLUMNS
        if col <= cols and (row, col) not in occupied
    )


def torchwood_candidates(
    plants: Iterable[Mapping], occupied_cells: Iterable[Cell], *, rows: int = 6
) -> tuple[Cell, ...]:
    """Return at most one useful Torchwood target per lane.

    Threepeater projectiles also pass through both adjacent lanes, so those
    lanes are included in the Torchwood coverage set. Column 6 is preferred,
    with column 5 as fallback; columns 7 through 9 stay open for spikes.
    """
    current = list(plants)
    occupied = set(occupied_cells)
    torch_rows = {
        plant.get("row") for plant in current
        if _plant_type(plant) == TORCHWOOD and isinstance(plant.get("row"), int)
    }
    sources_by_lane: dict[int, list[Mapping]] = {}
    for plant in current:
        kind, row, col = _plant_type(plant), plant.get("row"), plant.get("col")
        if kind not in PEA_SHOOTERS or not isinstance(row, int) or not isinstance(col, int):
            continue
        lanes = range(max(1, row - 1), min(rows, row + 1) + 1) if kind == THREEPEATER else (row,)
        for lane in lanes:
            sources_by_lane.setdefault(lane, []).append(plant)

    targets: list[Cell] = []
    for lane in range(1, rows + 1):
        sources = sources_by_lane.get(lane, [])
        if not sources or lane in torch_rows:
            continue
        for col in (6, 5):
            if all(plant["col"] < col for plant in sources) and (lane, col) not in occupied:
                targets.append((lane, col))
                break
    return tuple(targets)
