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

## Local unreleased geometry and tutorial observations

The following additions are in the current local native source after v0.1.0;
they have not shipped in the v0.1.0 installer. See
[`native/src/observations.cpp`](../native/src/observations.cpp),
[`native/src/game_input.cpp`](../native/src/game_input.cpp), and
[`native/src/game.cpp`](../native/src/game.cpp).

Plant records now include the observed `x`, `y`, `width`, and `height` fields
from the `GameObject` integer fields, plus `plant_hit_rect`, which represents
the ordinary plant hit area after type-specific trimming. The extra rectangle
is descriptive geometry; it is not a promise that every point accepts every
game action.

Seed-packet element geometry uses the verified hit-test fields: packet `x` is
at `+0x08`, `y` at `+0x0C`, width at `+0x10`, and height at `+0x14`.
The conveyor offset at `+0x30` is added to X. The observer publishes
`input_hit_rect` and its nested `board_hit_tested_point`. The latter
is `null` when no candidate point was confirmed by the actual Board packet
hit test (including cases outside active scene 3); clients must not treat the
rectangle alone as proof that a click will select the packet. The ordinary
seed hit-test points were read back in Adventure 1-8.

Zombie records add `body_rect` and `hammer_hit_rect` by calling the exact
dynamic rectangle routine at `0x546B80` with the verified `EBX=zombie` and
`EDI=&outputRect` convention. Invalid or out-of-bounds rectangles are `null`.
They also include `mind_controlled`, a boolean read from the exact zombie
record byte at `+0xB8`.
Zombie records also report the raw `chilled_counter` and `ice_trap_counter`.
The pinned reconstruction [`Zombie.h`](https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/Lawn/Zombie.h)
places them at `+0xAC` and `+0xB4`; its matching [`Zombie.cpp`](https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/Lawn/Zombie.cpp)
defines their chill and ice-trap behavior. The matching 1096 text image confirms the
fields in `ApplyChill` (`0x545430`, max of 1000/2000 at `+0xAC`) and
`HitIceTrap` (`0x546E90`, tests `+0xAC`, writes the trap duration at `+0xB4`).
These are raw update counters; they are not separately inferred booleans.
For the type-25 Dr. Zomboss, records add `phase` as an alias of the existing
numeric `status` (`ZombiePhase`) field and `phase_name` for the boss phase enum.
They also expose the raw `boss_projectile_reanimation_id`, the selected
`boss_projectile_row`, `boss_projectile_is_fire`, and `boss_target_row`.
Rows are converted to one-based values; unselected rows are `null`. The
reanimation ID is a raw stored identifier and does not by itself prove that
the animation currently exists. The pinned reconstruction header places its
projectile row and fire flag at `+0x148` and `+0x14C`, but disassembly of the
supported 1.2.0.1096 image shows the head-spit routine writing the row at
`+0x14C` and the fire/ice byte at `+0x150`. At `0x549FA0` it sets phase 89,
reads and clears the reanimation ID at `+0x140`, then writes those row and
flag fields. The projectile-contact routine at `0x54A5F0` checks the fire/ice
byte and stores the newly created reanimation ID at `+0x140` (`0x54A6FF`).
The target-row routine at `0x549640` writes a zero-based row to `+0x130`. The
phase names follow the pinned `ZombiePhase` enum; 89 is
`PHASE_BOSS_HEAD_SPIT` in both the enum and the supported image.
The hammer-hit region is the returned body rectangle with its height reduced
by 50; `hammer_hit_shape` describes the hammer overlap circle (radius 45,
center Y offset -20). These fields support geometric reasoning only: they do
not guarantee that a click or hammer action is available or will succeed.
Zombie body geometry was live-checked in Adventure 1-10. The earlier
coordinate-driven level-15 Whack-a-Zombie run did reach a win state, but no
Whack-a-Zombie plants were used and some hammer attempts had no effect; this
does not establish a sound or repeatable policy. A separate level-15
observation showed a conehead helmet dropping from 370 HP to 0 before that
zombie was killed. The newly added semantic `hammer_zombie` action has not
been live-replayed.

`action: hammer_zombie` accepts `type`, the current `board_id`, and an integer
`entity_id` in the range 1 through `uint32`. The entity ID must have a nonzero
generation and match the current live DataArray record. It is offered only in
scene 3, with no dialog open, an unpaused board, and cursor type 7 (the hammer),
for Adventure mode 0 level 15 or Whack-a-Zombie mode 30. At the queued game
tick, it revalidates the board/surface and target, recalculates current body
and hammer geometry, then dispatches a normal WidgetManager click at the
candidate point. It does not synthesize OS input or write health fields. An
accepted queue response only means work was queued; inspect
`game_input.last_result` for `target_entity_id`, `candidate_point`,
`hp_before`, `hp_after`, `helmet_hp_before`, `helmet_hp_after`,
`after_check_available`, `target_present_after`, and `effect_observed`. If
geometries overlap, another zombie can receive the click. Native compilation
passed; current Python results are in [VALIDATION.md](../VALIDATION.md). The semantic hammer action itself has not been
live-replayed.

Award-screen continuation now appears in `state.award_continue` as
`{state, error}`, where `state` is `idle`, `pending`, `running`, `complete`, or
`failed`, and `error` is `null` or a message. `action: continue` queues the
normal Award Continue callback and returns `accepted: true`, `queued: true`,
and `transition_pending: true`. The callback is deferred to a later update
tick, so the bridge can continue serving ordinary UI actions during a nested
game callback wait. A duplicate request for the same Award screen is refused;
poll state and close any blocking modal before retrying a failed operation.

`state.dave_dialogue` exposes the current bounded visible line, dialogue state
and index. It is not a tutorial-step counter. `game_input` permits normal
board-surface clicks during scene-2 Adventure introductions, while retaining
the normal board generation, modal, paused-state, widget ownership, and
hit-test checks. Dave dialogue and normal continuation were live-checked in
Adventure 2-1. The source compiles with the deferred Award continuation change,
but its special-unlock regression has not yet been replayed.

## 本地未发布的几何与教程观测

以下补充来自当前本地原生源码，晚于 v0.1.0，尚未随 v0.1.0 安装包发布。实现见
[`native/src/observations.cpp`](../native/src/observations.cpp)、
[`native/src/game_input.cpp`](../native/src/game_input.cpp) 和
[`native/src/game.cpp`](../native/src/game.cpp)。

植物记录现在包含 `GameObject` 整数字段得到的 `x`、`y`、`width`、`height`，以及
按植物类型修正后的 `plant_hit_rect`。该矩形用于描述命中区域，不保证其中任意点都能
执行任意游戏操作。

种子包命中几何使用已核实的字段：包内 `x` 位于 `+0x08`，`y` 位于 `+0x0C`，宽度位于
`+0x10`，高度位于 `+0x14`；`+0x30` 的传送带偏移加到 X 坐标上。快照提供
`input_hit_rect` 及其内部的 `board_hit_tested_point`。
若没有候选点通过真实 Board 种子包命中测试，后者为 `null`（包括不在活动第 3 场景的
情况）；不能仅凭矩形推断点击一定能选中种子。冒险 1-8 已读回实际种子包命中点。

僵尸记录通过精确动态矩形函数 `0x546B80` 添加 `body_rect` 和 `hammer_hit_rect`；
已核实调用约定为 `EBX=僵尸对象`、`EDI=&输出矩形`。无效或越界矩形为 `null`。
另含布尔值 `mind_controlled`，读取精确僵尸记录的 `+0xB8` 字节。
僵尸记录还报告原始计数器 `chilled_counter` 和 `ice_trap_counter`。固定版本重建源码
[`Zombie.h`](https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/Lawn/Zombie.h)
将它们放在 `+0xAC` 和 `+0xB4`；对应的
[`Zombie.cpp`](https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/Lawn/Zombie.cpp)
定义了减速和冰陷阱行为。匹配的 1096 文本映像也验证了这些字段：
`ApplyChill`（`0x545430`）在 `+0xAC` 处将计数上限设为 1000/2000；
`HitIceTrap`（`0x546E90`）检查 `+0xAC`，并在 `+0xB4` 写入冰陷阱持续计数。
这里公开的是原始更新计数器，不额外推断布尔状态。
对于类型 25 的僵王，记录还会提供 `phase`（现有数值 `status`，即
`ZombiePhase`）别名以及 `phase_name`。另外还包含原始的
`boss_projectile_reanimation_id`、`boss_projectile_row`、
`boss_projectile_is_fire` 和 `boss_target_row`。行号转为从 1 开始；未选择的行返回
`null`。原始动画 ID 本身不证明该动画当前仍然存在。固定版本重建头文件把投射物行和
火球标志放在 `+0x148`、`+0x14C`；但支持的 1.2.0.1096 映像反汇编显示，吐球函数实际将行写入
`+0x14C`，火/冰字节写入 `+0x150`。`0x549FA0` 将阶段设为 89，读取并清除 `+0x140` 的动画
ID，然后写入行和标志。投射物接触函数 `0x54A5F0` 检查火/冰字节，并在 `0x54A6FF` 将新动画
ID 写入 `+0x140`。目标行函数 `0x549640` 将从零开始的行写入 `+0x130`。阶段名称依据固定
版本的 `ZombiePhase` 枚举；枚举和支持的映像均将 89 定义为 `PHASE_BOSS_HEAD_SPIT`。
锤击区域是在返回的身体矩形基础上将高度减 50；`hammer_hit_shape` 描述锤击重叠圆
（半径 45，圆心 Y 偏移 -20）。这些字段只供几何判断，不能保证点击或锤击操作可用或
成功。冒险 1-10 已实机检查僵尸身体几何。此前坐标操作的第 15 关打僵尸运行确实到达
胜利状态，但未使用打僵尸专用植物，且部分锤击没有效果，因此不能证明策略可靠或可重复。
另一次第 15 关观测到路障僵尸头盔生命值从 370 降至 0 后僵尸被击杀。新加入的语义化
`hammer_zombie` 动作尚未实机重放。

`action: hammer_zombie` 接收 `type`、当前 `board_id` 和 1 到 `uint32` 范围内的整数
`entity_id`。实体 ID 必须有非零代际，并匹配当前存活的 DataArray 记录。仅在第 3 场景、
无对话框、棋盘未暂停且光标类型为 7（锤子）时提供，适用于冒险模式 0 第 15 关或打僵尸
模式 30。在排队后的游戏更新周期中，它会重新验证棋盘/表面和目标，重新计算最新身体及锤击
几何，然后在候选点调用普通 WidgetManager 点击。它不会合成操作系统输入，也不会写入生命值。
排队接受仅代表任务已进入队列；应查看 `game_input.last_result` 中的 `target_entity_id`、
`candidate_point`、`hp_before`、`hp_after`、`helmet_hp_before`、`helmet_hp_after`、
`after_check_available`、`target_present_after` 和 `effect_observed`。几何重叠时，点击可能
作用于另一只僵尸。原生编译已通过；当前 Python 检查结果见 [VALIDATION.md](../VALIDATION.md)。语义化锤击动作尚未实机重放。

Award 结算画面的继续状态现显示在 `state.award_continue` 中，字段为 `{state, error}`；
`state` 可为 `idle`、`pending`、`running`、`complete` 或 `failed`，`error` 为 `null` 或错误
文本。`action: continue` 会排队调用游戏原有 Award Continue 回调，并返回
`accepted: true`、`queued: true` 和 `transition_pending: true`。该回调延迟到后续更新周期执行，
因此游戏回调嵌套等待期间，桥接仍能处理普通 UI 操作。同一结算画面的重复请求会被拒绝；
轮询状态，若失败需先关闭阻挡操作的模态对话框，再决定后续处理。

`state.dave_dialogue` 暴露有界的当前可见台词、对话状态和索引，不是教程步骤计数器。
`game_input` 允许冒险第 2 场景开场期间通过普通棋盘表面点击，同时保留棋盘代际、模态
对话框、暂停状态、控件归属和命中测试检查。冒险 2-1 的戴夫台词及正常继续已实机检查。
延迟 Award 继续改动已编译，但特殊解锁回归尚未重放。

### Ice surface / 冰车冰面

The supported 1096 image's `IsIceAt` at `0x4114C0` reads per-row int32
arrays at Board `+0x624` (minimum X) and `+0x63C` (timer). The writer at
`0x53F254` confirms the same arrays, zombie X plus 118, and a 3000-tick
timer. Ice is a row interval, not a GridItem; GridItem type 3 is a ladder.
`state.ice_rows` reports `{row, min_x, timer, active, first_blocked_col}`.
`state.ice_cells` enumerates blocked `{row,col}` cells using the exact
ordinary-lawn condition: timer nonzero, min X <=750, then columns from
clamp((min X +12 -40)/80, 0,8) through 8 (returned columns are one-based).
The snapshot reads these fields without calling game functions or modifying
ice. Garden geometry is outside this ordinary-lawn interpretation.

当前 1096 映像的 `0x4114C0` 与 `0x53F254` 已交叉核对冰面读取和写入位置。
冰面是按行保存的连续范围，物品类型 3 实际是梯子。
`ice_rows` 返回原始行数据及首个禁种列，`ice_cells` 返回普通草坪的禁种格。
更新在 3-9 开局通过普通菜单暂停加载，六行均实测读到初始 min_x=800、timer=0；
实际冰车产生、冰面消退的变化仍待后续观测。整个接口只读，不修改冰面或种植规则。
