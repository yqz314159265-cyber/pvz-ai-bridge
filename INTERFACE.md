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
