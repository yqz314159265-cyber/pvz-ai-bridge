# Internal contract v1

The Python host connects to a Windows named pipe `\\.\pipe\pvz-ai-<pid>`.
Requests and replies are UTF-8 JSON followed by one newline (maximum 1 MiB).
One request is outstanding per connection. Requests have `id`, `op`, `args`.
Responses echo `id` and contain `ok`, and either `result` or
`error: {code, message}`. Unsupported capabilities must be refused explicitly.

Operations: `hello`, `state`, `levels`, `action`, `pause`, `resume`, `detach`.
`action.args` has `type` plus semantic parameters. Coordinates use row/col
starting at 1. Entity IDs are scoped to board_id and must be checked again.
Actions: enter_level(mode_id), choose_cards(plants), start_level(),
plant(slot,row,col), shovel(row,col,layer), collect(entity_id),
continue(), restart(), back_to_menu(), and mode-specific actions advertised
by capabilities. The native layer is the authority on actual support.

State includes process/version, scene, mode_id, board_id, tick, paused,
sun, wave, plants, zombies, seed_packets, collectibles, capabilities and
result. Menus may have no board; empty arrays are valid. A seed packet includes
slot, plant_type, usable and cooldown. Entity records carry stable IDs.
Never expose hidden vase contents, RNG, or future spawns to model clients.

The Python package is `pvz_ai` under `src/pvz_ai`; the first Windows bundle uses Python 3.14.
Native build outputs go to `build/native/Release/`: pvz_bridge.dll and
pvz_injector.exe. Injector args: `--pid PID --dll ABSOLUTE_PATH`.
No native DLL or game process may be launched by tests by default.

Host responsibilities: save backup, launch/attach, one controlling client,
HTTP (loopback only + bearer token on mutations), CLI, stdio MCP, bounded task
plans and optional local-model adapter. Default real-time play; idle or lost
control must not blindly execute stale plans. Pause/stop clear pending plans.

Native responsibilities: identify exact supported build, hook the app update
on its main thread, snapshot and execute actions there, preserve original
game calls/rendering, error on unsupported operations, reversible detach.

## Local unreleased native additions / 本地未发布的原生接口补充

These fields and guards describe the current local source after v0.1.0. They
are not part of the v0.1.0 installer/API contract until a later build ships
them. The native source containing the deferred Award callback and hammer
action has compiled; current check results are recorded in
[VALIDATION.md](VALIDATION.md). The special-unlock regression has not yet
been replayed. Source:
[`native/src/game.cpp`](native/src/game.cpp) and
[`native/src/game_input.cpp`](native/src/game_input.cpp).

`state.dave_dialogue` reports bounded visible dialogue text as `text` (or
`null` when there is no active indexed line), `state` / `state_name`,
`message_index`, and `active`. It is an observation of Crazy Dave's dialogue
state, not a tutorial-step or progression policy. `state.chooser.cards[]`
adds `available` and `can_select`; the latter is false for Imitater because
its target-picker flow is not implemented. `start_level()` now rejects an
open game dialog and coalesces duplicate starts for the same observed board
and chooser, returning `already_started: true` while the transition proceeds.
`state.award_continue` reports `state` (`idle`, `pending`, `running`,
`complete`, or `failed`) and `error` (`null` or a message). Continuing from the
Award screen queues the normal Continue callback and returns `accepted: true`,
`queued: true`, and `transition_pending: true`; duplicate requests for that
Award screen are rejected. Poll `state` for completion or failure. The callback
runs later on the game thread, leaving the bridge able to service a normal
dialog action such as closing the Almanac while a nested game callback waits.
`action: hammer_zombie` accepts `{type, board_id, entity_id}`, with an integer
`entity_id` from 1 through `uint32`. It is available only in scene 3 with the
hammer cursor selected, no open dialog, and an unpaused board, for Adventure
mode 0 level 15 or Whack-a-Zombie mode 30. Before dispatch it rechecks the
board generation, target ID and fresh hit geometry, then uses normal
WidgetManager input; it does not send OS-level input or write zombie health.
The queued acknowledgment is not proof of an effect. Later,
`game_input.last_result` reports `target_entity_id`, `candidate_point`,
`hp_before` / `hp_after`, `helmet_hp_before` / `helmet_hp_after`,
`after_check_available`, `target_present_after`, and `effect_observed`.
Because it is an ordinary click at computed hammer geometry, overlap can hit
a different zombie. This new API has not yet been live-replayed.

`game_input(surface, x, y, button)` accepts ordinary surface clicks on the
seed chooser and board. Board clicks also work during the scene-2 Adventure
intro (including Dave/shovel lessons), but retain modal-dialog, paused-board,
active-widget ownership, board-generation and real hit-test checks. This is
generic input support, not proof that every minigame interaction has been
tested or that an automated strategy can win. Live checks so far cover Dave
dialogue and continuing normally in Adventure 2-1, duplicate `start_level`
coalescing in 2-1, seed hit-test points in 1-8, and zombie hit geometry in
1-10. The earlier coordinate-driven level-15 Whack-a-Zombie run did reach a
win state, but no Whack-a-Zombie plants were used and some hammer attempts had
no effect, so it is not evidence of a sound or repeatable policy. A separate
level-15 observation saw a conehead helmet go from 370 HP to 0 before the
zombie was killed. The deferred Award callback's special-unlock regression
has not yet been replayed.

本地源码中加入了以下接口和保护；它们晚于 v0.1.0，尚不属于该安装包或
API 版本的承诺。实现见 [`native/src/game.cpp`](native/src/game.cpp) 与
[`native/src/game_input.cpp`](native/src/game_input.cpp)。

`state.dave_dialogue` 提供有界的可见对话 `text`（没有活动台词时为 `null`）、
`state` / `state_name`、`message_index` 和 `active`。这表示戴夫对话状态，不是
教程步骤或进度策略。`state.chooser.cards[]` 增加 `available` 和 `can_select`；
模仿者目标选择流程尚未实现，因此其 `can_select` 为 false。`start_level()` 现在会
拒绝有游戏对话框时启动，并合并同一已观测棋盘和选卡器上的重复启动请求；过渡进行中
重复调用会返回 `already_started: true`。
`state.award_continue` 提供 `state`（`idle`、`pending`、`running`、`complete` 或
`failed`）和 `error`（`null` 或错误文本）。在 Award 结算场景继续时，现在会排队调用
游戏原有 Continue 回调，并返回 `accepted: true`、`queued: true`、
`transition_pending: true`；同一结算画面的重复请求会被拒绝。调用方应轮询 `state` 查看
完成或失败。回调稍后在游戏主线程运行；若游戏回调正在嵌套等待，桥接仍可处理普通对话框
操作，例如关闭 Almanac。
`action: hammer_zombie` 接收 `{type, board_id, entity_id}`，其中 `entity_id` 是 1 到
`uint32` 范围内的整数。仅当处于第 3 场景、已选锤子工具、棋盘未暂停且没有打开的对话框，
并且模式为冒险模式 0 的第 15 关或打僵尸模式 30 时才提供。执行前会重新检查当前棋盘代际、
目标 ID 和最新命中几何，再调用游戏自己的 WidgetManager 输入；它不发送操作系统级输入，也
不写入僵尸生命值。排队确认不代表已产生效果。之后 `game_input.last_result` 会提供
`target_entity_id`、`candidate_point`、`hp_before` / `hp_after`、`helmet_hp_before` /
`helmet_hp_after`、`after_check_available`、`target_present_after` 和 `effect_observed`。
该操作是在计算出的锤击几何位置执行普通点击，重叠时可能打到另一只僵尸。此新 API 已编译，
但尚未实机重放。

`game_input(surface, x, y, button)` 可在选卡器和棋盘表面发送普通点击。冒险模式第 2
场景的戴夫/铲子教学期间也允许点击棋盘，但仍检查模态对话框、暂停状态、活动控件归属、
棋盘代际和实际命中测试。这是通用输入能力，不代表每个小游戏操作都已验证，也不代表
自动策略能够获胜。当前实机检查覆盖冒险 2-1 的戴夫台词及正常继续、2-1 重复启动合并、
1-8 种子包命中点和 1-10 僵尸命中几何。此前坐标操作的第 15 关打僵尸运行确实到达
胜利状态，但未使用打僵尸专用植物，而且部分锤击没有效果，因此不能视为可靠或可重复策略。
另一次第 15 关实机观察到路障僵尸头盔生命值从 370 降至 0 后被击杀。延迟 Award 回调的
特殊解锁回归测试尚未重放。

## Installed host / 已安装的主机

`GET /health` probes the game thread and returns `responsive`, `latency_ms`,
and an observation timestamp. `GET /status` only reports the cached connection.
`GET /guide` requires the Bearer token and reads the human-authored Markdown
guide; `filled: false` means its scaffold has not been marked complete.
`pvz-ai guide --init` creates a missing guide without overwriting user content.

`GET /health` 实际检查游戏主线程是否响应；`GET /status` 只报告缓存的连接状态。
`GET /guide` 需要 Bearer token，读取人工填写的策略；`filled: false` 表示仍未填写完成。
`pvz-ai guide --init` 只创建缺失的文件，保留已有内容。

Run `pvz-ai mcp --api-url http://127.0.0.1:8765` to share the launcher's
existing controller. Plain `pvz-ai mcp` owns a separate controller; do not
attach both to the same game. Closing the proxy client leaves the launcher
running. The `strategy_guide` MCP tool reads the guide from that controller.

运行 `pvz-ai mcp --api-url http://127.0.0.1:8765` 可共用启动面板的控制主机。
普通 `pvz-ai mcp` 创建独立主机，同一游戏只允许一个控制主机。关闭代理客户端不会
关闭启动面板；MCP 的 `strategy_guide` 从相同的主机读取人工策略。

`action: shop_advance_dialogue` takes no parameters. It is advertised only
when the sole StoreScreen dialog exposes its ordinary click-to-continue speech
flag (`shop.can_advance_dialogue`). It invokes the game's StoreScreen MouseDown
speech branch, which returns before item hit tests. Inspect `message_before`,
`message_after`, and `dialogue_pending`; this action cannot select a store item.
Live verification advanced the first store introduction from 301 to 302 with
the test profile's 510 coins unchanged. The subsequent tutorial and normal
store-close route also reached level 25. A running Python API must be restarted
after its action schema changes; loading a DLL alone does not update that schema.

`shop_advance_dialogue` 无参数，仅在唯一的商店对话框公开
`shop.can_advance_dialogue` 时提供。它调用游戏普通“点击继续对白”分支，在商品
命中测试之前返回。结果包含 `message_before`、`message_after`、`dialogue_pending`。
首次商店教学实测索引 301→302，测试档案金币维持 510；后续对白与普通关闭流程进入
第 25 关。更新动作校验规则后需重启 Python API，单独加载新 DLL 不会刷新规则。

`open_shop` now follows the exact supported image's ordinary unlock predicate:
completed Adventure, the game's seen-upsell flag, or Adventure progress >=25.
The permitted entry scenes remain main menu and Zen Garden. This is a guard
correction, not an unlock mutation. Exact binary predicate: 0x4598B0, profile
fields +0x58, +0x374, +0x50; the reconstructed source's address differs.

`open_shop` 按本版本普通解锁条件检查：冒险已完成、游戏自身的已观看推销标记，
或冒险进度至少 25。入口仍限定主菜单和花园。这项修正不修改解锁状态。
