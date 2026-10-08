# GOTY 1.2.0.1096 entity snapshot layout

The native observer uses the verified GOTY Steam 1.2.0.1096 profile from
[pvztools](https://github.com/BlueRoseMike/pvztools/blob/c60cb557b3260abfbc9cac1fec8eef0957670742/src/pvztools/addr/profiles/goty_1_2_0_1096_en.json).
That profile verifies the Board plant and zombie DataArrays and their strides;
it also lists the coin, grid-item, seed-bank, and seed-packet fields used here.
The native observer and supported-image checks are implemented in
[`native/src/game.cpp`](../native/src/game.cpp); scoped runtime checks are
summarized in the public [validation record](../VALIDATION.md).

## Board collections

| Collection | Board offset | Element stride | DataArray count |
|---|---:|---:|---|
| Zombies | `0xA8` | `0x168` | `mMaxUsedCount` at `+4` |
| Plants | `0xC4` | `0x14C` | `mMaxUsedCount` at `+4` |
| Coins | `0xFC` | `0xD8` | `mMaxUsedCount` at `+4` |
| Grid items | `0x134` | `0xEC` | `mMaxUsedCount` at `+4` |
| Seed bank pointer | `0x15C` | bank header | packet count at bank `+0x24` |

For each DataArray the block pointer is at its Board offset, `mMaxUsedCount`
is at `+4`, and `mMaxSize` is at `+8`. The observer checks both counts against
each other and caps reads at 4096 slots and 4 MiB. It validates the complete
memory range with `VirtualQuery` before copying it. The seed bank exposes at
most 10 packets of stride `0x50`, beginning at bank `+0x28`.

## Entity IDs

The public GOTY profile supplies the element strides. The public reconstruction
source defines `DataArrayItem` as an item followed by a 32-bit `mID` and defines
the ID as `(key << 16) | slot`:
[DataArray.h](https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/Sexy.TodLib/DataArray.h).
For plants, zombies, and grid items, the observer reads `mID` at `stride - 4`
and skips entries whose upper 16 key bits are zero. Coins are an exception:
the exact live 1096 `Coin` array has stride `0xD8`, but its ID is at `+0xD0`;
`+0xD4` is tail padding. Historical read-only runtime sampling confirmed the
active slot layout; dead-slot free-list values can also occupy the ID field.
IDs are scoped to the current board generation and must be
revalidated before an action. Seed packets are not DataArray items and have no
reported DataArray ID.

Plant, zombie, grid-item, and seed slots are reported as one-based public
coordinates/slots. The source stores plant/zombie/grid rows and columns and
seed packet indices as zero-based values, so the observer adds one. Health is
exposed as `health` and `hp`; plant and seed types are exposed as `plant_type`
with `type` retained as an alias.

Coin type values are from the public `CoinType` enum in
[ConstEnums.h](https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/ConstEnums.h):
sun, small sun, and large sun are 4, 5, and 6. The corresponding
[Coin::GetSunValue](https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/Lawn/Coin.cpp)
values are 25, 15, and 50. The observer labels these `sun`, `small_sun`, and
`large_sun`, reports that value as `amount`, and exposes collection animation
state as both `being_collected` and `collecting`.

`observation_complete: true` means the observer read each expected collection
under this layout and its bounds. `observation_status` reports the evidence
level; the live plant/zombie/coin paths have runtime checks, while the broader
core snapshot may still mark unresolved collections as pending validation.

## Zen Garden inventory

The GOTY profile identifies `LawnApp.mZenGarden` at `app + 0x93C` and
`LawnApp.mPlayerInfo` at `app + 0x94C`. The public reconstruction `PlayerInfo.h`
layout is stale for this GOTY build: read-only runtime inspection and exact 1096
code establish the offsets used here. Runtime inspection confirmed the coin
field, potted count, and potted array at the offsets below. The exact
1096 routine at `0x52DC90` calculates `PlayerInfo + 0x388 + index * 0x58`,
confirming the list start and element stride. The observer rejects counts above
200 and copies only `count * 0x58` bytes. Inventory positions can change when
plants are added or removed; they are not exposed as stable entity IDs.

The record offsets are confirmed by the live code at `0x52D4A0`, the exact
1096 `ZenGarden::GetPlantsNeed` routine. It reads seed type `+0x00`, garden type
`+0x04`, last watered `+0x18`, age `+0x24`, times fed `+0x28`, feedings per
growth `+0x2C`, stored need `+0x30`, and last fertilized `+0x40`; the fulfilled
time is `+0x38`. Grid X/Y are `+0x08/+0x0C`. This routine returns the game's
current need code in EAX with the potted record pointer in ESI. The observer
calls this exact read-only routine rather than duplicating its time and plant
rules. Profile purchases start at `+0x1EC`; the consumable count offset is
removed for fertilizer, bug spray, and chocolate. Public coin values are raw
profile coins multiplied by ten, matching the main state snapshot.

For a potted plant currently placed in the active garden, the snapshot joins
its inventory index to the Board Plant DataArray through
`Plant.mPottedPlantIndex +0x13C` and reports that plant's full DataArray ID.
That ID is scoped to the active Board and is rechecked by `care()`; an inventory
position by itself is not accepted for actions.

`care(app, plant_id, kind)` selects watering/fertilizer through the normal
toolbar path: it gets the tool rectangle from `0x40E2D0` plus the x adjustment
from `0x40E360`, checks the requested object type with `Board::MouseHitTest`
(`0x414C40`), then calls `Board::MouseDown` (`0x415430`). The argument order
and output buffer for `0x40E360` were checked against its call site in
`Board::MouseHitTest`: Board and tool type are stack arguments, and EDI points
to the rectangle's x field. Pointer movement uses the verified
`WidgetManager::MouseMove` entry at `0x54FB90`; implementation and behavior are
covered by [`native/src/garden.cpp`](../native/src/garden.cpp) and the public
[validation record](../VALIDATION.md). It refuses to move while a mouse button
is held.

Care is staged. The request queues the target coordinates and returns
`status: queued`; a later game-thread update rechecks the Board/garden, full
Plant ID, potted index, current need, consumable inventory, cursor type,
pointer coordinates, and plant highlight. It cancels on mismatch or after 100
updates/2 seconds. Only then does it call the exact garden tool dispatcher at
`0x52DFF0` with the garden object in ECX, cursor type 9 (water) or 10
(fertilizer) in EAX, and screen X/Y on the stack. The dispatcher routes those
tools through `0x52D5E0`, the ordinary feeding-tool handler, which performs its
own plant hit-test and need/inventory checks. The live `0x414608` call site
confirms that register convention. No plant or inventory fields are edited
directly. `status: dispatched` means the ordinary handler was invoked; a later
snapshot must verify the effect. Watering was subsequently verified on two
plants; both changed to `need: none` after the ordinary care action.

Target coordinates use the `GameObject` integer fields `mX/mY/mWidth/mHeight`
at `+0x08/+0x0C/+0x10/+0x14`; the first two dwords of a Plant are its app and
board pointers. The hit point is the center of the source-defined
`Plant::GetPlantRect`: usual plants trim 10 px from both horizontal sides,
PumpkinShell keeps the full left edge, Tallnut keeps the full width, and
CobCannon uses a fixed 140-by-80 rectangle. These rules are from the pinned
[GameObject.h](https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/Lawn/GameObject.h),
[Plant.h](https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/Lawn/Plant.h), and
[Plant.cpp](https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/Lawn/Plant.cpp).
They position the pointer only; the live game's plant highlight must confirm
the target before the tool dispatcher is invoked.

An initial profile snapshot was captured in mode `0`; a later live snapshot in
active main Zen Garden mode confirmed the potted plants and their Board Plant
IDs. The bounded layout read and active-garden join are therefore verified for
the tested garden. `observation_complete: true` means the bounded fields in
this schema were read; it does not claim an action succeeded by itself.

The public validation record summarizes the live care result without exposing
private fixture filenames or entity identifiers. It verifies normal watering
behavior on the tested 1096 main garden. Fertilizer, bug spray, and phonograph
have not been live-verified.

The game's raw garden values are main `0`, mushroom `1`, wheelbarrow `2`, and
aquarium `3`. The public bridge garden IDs are main `0`, mushroom `1`, and
aquatic `2`; wheelbarrow is reported by name with `garden_id: null` because it
is a transient carrying state, not a garden selector ID.
