# AI 使用指南 / AI Usage Guide

## English

PvZ AI is a bounded local control bridge, not a strategy engine. The guide below describes how an AI client should inspect state, use the user's own instructions, and verify outcomes without inventing tactics.

### Supported environment and known issue

The supported target is Steam Plants vs. Zombies GOTY for Windows, version 1.2.0.1096, x86. The game must run in a visible Windows desktop session. On first launch, choose Simplified Chinese or English; the application UI then uses only the selected language. Native actions call normal in-game callbacks on the game thread; the bridge does not send OS mouse/keyboard messages.

The screenshot/overlay callback calling convention was corrected; a subsequent observation of about three minutes had no failures, with a maximum measured response latency of 85 ms. This is a short sample, not proof of long-term stability. The cause of the earlier idle/scene-transition hang has not been established, and final stability review remains in progress. Use a backed-up, recoverable test profile and do not assume unattended or long-running reliability.

### Start with observation

1. Read `health`, `state`, and `capabilities`. Check scene, mode, board identity, pause state, and terminal result before proposing an action.
2. Use `levels` as a mode-definition catalog only. A catalog entry does not prove the current profile has unlocked it, that the normal UI exposes it, or that an AI strategy can win it.
3. Submit a short action only when the current native capabilities advertise it. Re-read state after transitions, refusal, pause, or a changed board. A request being accepted or dispatched is not proof that the game completed it.
4. Report a win only when the game state explicitly reports a terminal success. Do not infer victory from a quiet board, an accepted action, or a mode name.
5. Pause for human review when state is ambiguous. `stop` or `detach` clears pending work; never resume a stale plan after the profile, scene, mode, or board changes.

The native layer remains authoritative for current card availability, sun, cooldowns, placement, inventory, visibility, unlock checks, and mode-specific support. Host-side schema validation is not evidence that the native game action is implemented.

### Use the human-authored strategy guide

The user's editable guide is `%LOCALAPPDATA%\PvZ AI Bridge\strategy.md` by default. `PVZ_AI_STRATEGY_FILE` can override it. Read it with `pvz-ai.exe guide [--path PATH]`, HTTP `GET /guide` (Bearer token required even though it is read-only), or the local MCP `strategy_guide` tool.

The bundled scaffold is intentionally blank. `filled: false` means the user has not marked their instructions as complete. `pvz-ai.exe guide --init [--path PATH]` creates that scaffold only if the selected file is missing; it never overwrites existing content. Ask the user to complete or clarify their own tactics when needed. Do not write a strategy on the user's behalf, claim that the guide proves a tactic, or learn tactics from video unless the user actually supplies and verifies them.

### Interfaces

- `pvz-ai.exe launcher` opens the desktop UI; first start asks for Simplified Chinese or English.
- `pvz-ai.exe install-skill [--destination PATH]` installs the PvZ Play skill and refuses to overwrite existing files. The launcher has an explicit install button; neither method edits Codex configuration automatically.
- `pvz-ai.exe doctor` performs read-only installation/build checks. When the game is already attached, `pvz-ai.exe health --pid <PID>` makes a fresh responsiveness/latency check. HTTP `GET /health` also performs an actual request; an unattached CLI health check reports that no game is attached.
- `pvz-ai.exe http` starts a loopback-only JSON API and creates the current-user-protected Bearer token under `%LOCALAPPDATA%\PvZ AI Bridge\local-api\token`; the installer does not create the token. Do not place it in prompts, logs, shared config, or screenshots.
- `pvz-ai.exe mcp` serves tools over local stdio. The launcher can copy a config that runs `mcp --api-url http://127.0.0.1:8765 --token-file <data-directory>\local-api\token` to attach to an existing HTTP host. Plain `mcp` owns a standalone host, so do not run it at the same time as the HTTP-host connection mode for the same game. Keep stdout reserved for MCP protocol output.
- The named-pipe protocol and state fields are documented in [`INTERFACE.md`](../INTERFACE.md).

HTTP `GET /status`, `/state`, `/levels`, `/capabilities`, and `/guide` are read-only. Action and lifecycle operations are protected local mutations. MCP exposes `strategy_guide` for reading the human guide; the guide itself is data, not an action policy that bypasses native checks.

### Bounded plans and models

A JSON template is a finite list of validated semantic actions with state conditions, a board/mode identity, and an expiry horizon. The default execution ceiling is 8 actions and 40 polls. Plans stop on changes, expiry, pause, or refusal. No loops or arbitrary code execute inside a template.

The optional local-model adapter returns validated proposals from current visible state; it does not execute them by itself. Do not send state to a cloud model unless the user explicitly configures and approves that separate service. The included basic policy is restricted to a daytime Adventure example and does not demonstrate broad strategy coverage.

Never request or reveal hidden vase contents, future spawns, RNG, or other unavailable state. Generic board clicks do not mean every minigame has been verified.

### Current coverage and safety

Read the [English human-operation matrix](HUMAN_OPERATIONS.en.md) for the distinction between implemented, live-tested, and unverified actions. Some Adventure, Pool Endless, menu, profile, almanac, options, shop, garden, and Zombatar operations have been exercised. Special-mode rules and many individual minigame interactions remain incomplete or unverified. There is no all-mode automatic-win claim.

Make a save backup before experiments. Backups contain copied files and a SHA-256 manifest; automatic restore is not provided. On completion, stop pending work and detach. The DLL remains loaded until the game process exits; close the game normally before replacing or removing bridge files.

## 中文

PvZ AI 是一个有限能力的本地控制桥接器，不是策略引擎。AI 应先读取状态，遵循用户自己填写的指南，并核验实际结果；不能自行编造战术。

### 支持环境与已知问题

支持目标为 Steam《植物大战僵尸 GOTY》Windows 1.2.0.1096、x86。游戏必须在可见的 Windows 桌面会话中运行。原生动作调用游戏自己的主线程回调，不发送操作系统鼠标/键盘消息。

截图/覆盖层回调调用约定已修正；之后约 3 分钟观察未见失败，最高实测响应延迟为 85 ms。这只是短时样本，不证明长期稳定。此前空闲/切场挂起的根因尚未确认，最终稳定性复核仍在进行。请先备份并使用可恢复的测试档案，不要假设它可无人值守或长时间稳定运行。

### 先观察，再提出动作

1. 先读取 `health`、`state` 和 `capabilities`，确认场景、模式、棋盘身份、暂停状态和终局结果。
2. `levels` 只是模式定义目录。目录中有某模式，不代表当前档案已解锁、普通 UI 可进入或 AI 能获胜。
3. 只有当前 native capability 明确公布的动作才可请求。场景变化、拒绝、暂停或棋盘变化后重新读取状态。请求被接受或派发，不代表游戏已经完成操作。
4. 只有游戏状态明确报告终局胜利时才报告获胜。不能仅凭棋盘安静、动作被接受或模式名称推断胜利。
5. 状态不明时暂停并交给用户查看。`stop` 或 `detach` 会清除待处理计划；档案、场景、模式或棋盘变化后，不要继续旧计划。

当前卡片、阳光、冷却、落点、库存、可见性、解锁条件和模式规则仍由 native 负责检查。host 接受参数格式，不代表游戏动作已经实现。

### 读取用户自己编写的策略指南

默认指南位于 `%LOCALAPPDATA%\PvZ AI Bridge\strategy.md`；可用 `PVZ_AI_STRATEGY_FILE` 覆盖。可通过 `pvz-ai.exe guide [--path PATH]`、需 Bearer token 的 HTTP `GET /guide` 或本机 MCP 工具 `strategy_guide` 读取。

内置指南是刻意留空的双语模板。`filled: false` 表示用户尚未标记指南已完成。`pvz-ai.exe guide --init [--path PATH]` 只在目标文件不存在时创建模板，绝不覆盖已有内容。需要战术时，请用户填写或澄清自己的规则。不要替用户写策略、把指南当作策略有效性的证明，或声称从视频学会了战术。

### 接口

- `pvz-ai.exe launcher` 打开桌面界面；首次启动会要求选择简体中文或 English。
- `pvz-ai.exe install-skill [--destination PATH]` 安装 PvZ Play skill，拒绝覆盖已有文件。启动器也有显式安装按钮；两种方式都不会自动修改 Codex 配置。
- `pvz-ai.exe doctor` 只读检查安装和原生文件。游戏已接入时用 `pvz-ai.exe health --pid <PID>` 发起新的响应性/延迟检查；HTTP `GET /health` 也会实际请求，未接入的 CLI 检查会报告当前没有接入游戏。
- `pvz-ai.exe http` 启动仅监听回环地址的 JSON API，并在 `%LOCALAPPDATA%\PvZ AI Bridge\local-api\token` 创建受当前用户保护的 Bearer token；安装器不创建 token。只读 `GET /guide` 也需 token，以保护用户批注。不要把 token 放进提示、日志、共享配置或截图。
- `pvz-ai.exe mcp` 通过本机 stdio 提供工具。启动器可复制连接现有 HTTP 主机的配置：`mcp --api-url http://127.0.0.1:8765 --token-file <数据目录>\local-api\token`。普通 `mcp` 会拥有独立主机；同一游戏不要同时运行两种连接模式。stdout 必须保留给 MCP 协议。
- 命名管道协议和状态字段见 [`INTERFACE.md`](../INTERFACE.md)。

HTTP `GET /status`、`/state`、`/levels`、`/capabilities`、`/guide` 为只读路由。动作与生命周期操作是受保护的本机变更。MCP 的 `strategy_guide` 用于读取用户指南；指南是数据，不能绕过 native 检查。

### 有限计划与模型

JSON 模板是有限的语义动作列表，并带状态条件、棋盘/模式身份和过期时限。默认最多执行 8 个动作、轮询 40 次。状态变化、过期、暂停或拒绝会停止计划；模板中不能执行循环或任意代码。

可选本地模型适配器只根据当前可见状态返回经过校验的建议，不会自行执行动作。除非用户明确配置并同意单独的云端服务，否则不要把游戏状态发送给云模型。内置基础策略只适用于受严格检查的白天冒险示例，不代表广泛策略覆盖。

不得请求或透露花瓶隐藏内容、未来刷怪、随机数等不可见状态。通用 board 点击也不等于每个小游戏已验证。

### 覆盖与存档安全

请查看[中文普通玩家操作矩阵](HUMAN_OPERATIONS.zh-CN.md)，区分已实现、已实测和未验证项目。目前仅实测部分冒险、泳池无尽、菜单、档案、图鉴、选项、商店、花园和 Zombatar 操作；特殊模式和许多小游戏交互仍不完整或未验证。没有覆盖所有模式的自动获胜声明。

实验前先备份存档。备份会复制文件并生成 SHA-256 清单，但不提供自动恢复。完成后停止待处理任务并分离桥接；DLL 会一直加载到游戏进程退出。替换或删除桥接文件前请正常退出游戏。

## Direct supervision and source helpers / 直接监督与源码辅助函数

During direct model control, stop any competing player first. Pause while reasoning, resume for ordinary actions and animations, then read a fresh state. A successful `collect` request starts normal collection; it does not immediately increase spendable sun. Check the updated sun balance and packet cooldown before planting. Do not repeat collection on an already-collecting entity. An award screen and increased profile progress confirm a win; a plant request's acknowledgment does not.

源码方式直接接管时，先停止其他玩家。思考时暂停，执行普通动作与动画时恢复，然后读取新状态。`collect` 成功只是开始普通收集，不会立即增加可花费阳光；种植前核对更新后的阳光和卡片冷却，不重复收取正在收集的实体。奖励界面与档案进度共同确认通关，种植请求接受并不证明获胜。

For source developers, [`pvz_ai.formation`](https://github.com/yqz314159265-cyber/pvz-ai-bridge/blob/main/src/pvz_ai/formation.py) offers pure `threepeater_candidates` and `torchwood_candidates` cell planners for level 3-10. They issue no API calls and are not an installed autonomous player. Supply current occupied/blocked cells, then independently verify terrain, Lily Pads, ice, enemies, cooldown and the native capabilities before choosing a move. Offline tests cover formation properties; the corrected 3-10 formation has not been replayed live.

源码开发者可使用 [`pvz_ai.formation`](https://github.com/yqz314159265-cyber/pvz-ai-bridge/blob/main/src/pvz_ai/formation.py) 的两个纯格位规划函数，辅助 3-10 三线射手与火炬布局。它们不调用 API，也不是安装版自动玩家。传入最新占用/阻挡格后，仍须自行检查地形、睡莲、冰道、敌人、冷却与原生能力，再决定动作。离线测试检查阵型性质，修正后的 3-10 尚未重打实测。人工打法模板仍由用户填写。
