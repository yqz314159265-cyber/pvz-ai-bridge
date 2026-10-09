# Release Notes / 发布说明

## v0.1.1 source candidate / 源码候选版

The 2026-10-08 stop was superseded by the resumed work on 2026-10-09. Supervised/local-rule-script evidence now confirms levels 1–30; global level 31 (4-1) was won under direct root-agent API control after scripted setup, then stopped at the award screen at profile progress 32. Level 32 was not entered. This is not an all-50-level or fully autonomous result. See [the final Adventure record](FULL_ADVENTURE_TEST_2026-10-09.md) for intervention details, exact win times, backup evidence, mistakes, and limits.

This candidate includes richer internal observations, repeated-start chooser guards, deferred award continuation, the store tutorial dialogue action and corrected unlock predicate, parameter validation, and launcher language persistence. Public pure formation helpers balance rear Threepeaters across rows 2/5 and avoid redundant or misplaced Torchwoods. The private 3-10 player uses them; offline checks passed, but the corrected formation has not been replayed live. The final native Release build succeeded and the complete Python suite passed 247 tests. Active ice-cell behavior and live boss-field behavior remain unverified. Local policies are not bundled in the published installer. The published installer remains v0.1.0; no new installer or release was made.

2026-10-08 的停止状态已由 2026-10-09 恢复工作后的记录取代。受监督/本地规则脚本的逐关记录现确认 1–30 关；全局第 31 关（4-1）在脚本完成开局后由根代理直接通过 API 操作并获胜，随后停在进度 32 的奖励界面，未进入 32 关。这不代表全 50 关完成或全程自主游玩。介入细节、精确胜利时间、备份证据、失误和边界见[完整冒险记录](FULL_ADVENTURE_TEST_2026-10-09.md)。

本候选版本包括内部观测补充、重复开始选卡保护、奖励续接延迟、商店教学对白动作及修正后的解锁条件、参数校验和启动器语言持久化。公开纯阵型辅助函数将后排三线射手平衡布置于第 2/5 行，避免重复或错位火炬；私有玩家调用这些函数。离线检查通过，但尚未重打实测。最终原生 Release 构建成功，完整 Python 回归 247 项通过；活动冰格行为和僵王字段实战仍未验证。本地策略不随已发布安装器交付。当前已发布安装器仍为 v0.1.0；未制作新安装包或发布。

See [the stopped test report / 停止记录](TEST_FINDINGS_2026-10-08.md). The published release remains v0.1.0; its historical notes follow.
当前已发布版本仍为 v0.1.0，历史说明保留如下。

## v0.1.0 — First Preview / 首个预览版

### English

PvZ AI v0.1.0 is a per-user Windows preview of a local control bridge for Steam Plants vs. Zombies GOTY Edition, version 1.2.0.1096, x86. The game is installed separately through Steam. The installer bundles the application runtime; users do not need to install Python or a C++ toolchain. On first launch, the Tk desktop launcher asks the user to select Simplified Chinese or English; the application UI then uses the selected language.

The installed command line is `pvz-ai.exe`:

```powershell
pvz-ai.exe launcher
pvz-ai.exe doctor
pvz-ai.exe health --pid <PID>  # only when the game is already attached
pvz-ai.exe http
pvz-ai.exe mcp
pvz-ai.exe guide [--init] [--path PATH]
pvz-ai.exe install-skill [--destination PATH]
```

The bridge provides a bounded set of state-aware native actions, a loopback HTTP JSON API, MCP stdio, save backup, and a human-editable strategy guide. The running HTTP host creates its current-user-protected Bearer token; the installer does not. HTTP `GET /guide` requires the token even though it is read-only; MCP `strategy_guide` reads the same user guide. `GET /health` makes an actual responsiveness check. `guide --init` creates a blank bilingual scaffold only if the target file is missing. `install-skill` refuses to overwrite existing files, and the launcher provides an explicit install button; neither modifies Codex configuration automatically. User data defaults to `%LOCALAPPDATA%\PvZ AI Bridge`.

The launcher can copy an MCP configuration that connects to an existing HTTP host using `pvz-ai.exe mcp --api-url http://127.0.0.1:8765 --token-file <data-directory>\local-api\token`. Plain `mcp` runs a standalone host; do not run it alongside the HTTP-host connection mode for the same game because only one controller can attach.

A development validation on 2026-10-08 recorded a successful full native Release build and 224 passing Python tests, plus selected live UI and gameplay actions for the exact target build, including a ten-step almanac flow. This is a scoped snapshot, not a comprehensive stability guarantee or proof of autonomous wins.

#### Known limitations

- The screenshot/overlay callback calling convention was corrected, and a software-rendering compatibility workaround is enabled and persists across restart. A 180-second observation recorded 174 health reads, no failures, and a maximum latency of 84.9 ms. Long-term stability remains untested; the workaround does not prove the root cause of the earlier hang.
- Support is limited to Steam Windows GOTY 1.2.0.1096 x86. Other builds and distributions are unverified.
- This is a control bridge, not a complete autonomous player. Many special-mode and minigame actions are unverified; no all-mode win strategy is included.
- A catalog entry does not prove a mode is unlocked or normally enterable. An accepted API request is not proof of completed action or victory.
- Save backups are available, but automatic restore is not provided.

### 中文

PvZ AI v0.1.0 是面向 Windows 用户的本地控制桥接预览，支持 Steam《植物大战僵尸 GOTY》1.2.0.1096、x86。游戏需另行通过 Steam 安装。安装器内含应用运行时，不需要另行安装 Python 或 C++ 编译工具链。首次启动 Tk 桌面启动器时选择简体中文或 English；之后应用界面只显示所选语言。

安装后提供 `pvz-ai.exe` 命令：

```powershell
pvz-ai.exe launcher
pvz-ai.exe doctor
pvz-ai.exe health --pid <PID>  # 仅在游戏已接入时使用
pvz-ai.exe http
pvz-ai.exe mcp
pvz-ai.exe guide [--init] [--path PATH]
pvz-ai.exe install-skill [--destination PATH]
```

桥接器提供有限的状态感知原生动作、仅本机 HTTP JSON API、MCP stdio、存档备份和可人工编辑的策略指南。Bearer token 由运行中的 HTTP 主机创建并受当前用户保护，安装器不创建 token。HTTP `GET /guide` 虽只读仍要求 Bearer token；MCP `strategy_guide` 读取同一用户指南。`GET /health` 会实际检查响应。`guide --init` 仅在目标文件不存在时创建空白双语模板。`install-skill` 拒绝覆盖已有文件；启动器提供显式安装按钮，两种方式都不会自动修改 Codex 配置。用户数据默认位于 `%LOCALAPPDATA%\PvZ AI Bridge`。

启动器可复制连接现有 HTTP 主机的 MCP 配置：`pvz-ai.exe mcp --api-url http://127.0.0.1:8765 --token-file <数据目录>\local-api\token`。普通 `mcp` 是独立主机模式；同一游戏不能同时连接两个控制主机。

2026-10-08 开发验证记录包括完整原生 Release 构建成功、224 项 Python 测试通过，以及对目标版本部分 UI 与游戏操作的实测，其中包括图鉴十步流程。这只是限定范围的时点记录，不代表全面稳定，也不证明自动获胜。

#### 已知限制

- 截图/覆盖层回调调用约定已修正，并启用了重启后仍保留的软件渲染兼容方案。180 秒观察完成 174 次 health 读取，未见失败，最高延迟为 84.9 ms。长期稳定性仍未验证；该方案不能证明此前挂起的根因。
- 仅支持 Steam Windows GOTY 1.2.0.1096 x86；其他版本与发行渠道未经验证。
- 本产品是控制桥接，不是完整自动玩家。许多特殊模式和小游戏动作尚未验证，没有覆盖所有模式的获胜策略。
- 模式目录不证明当前档案已解锁或普通菜单可进入；API 接受请求不等于动作完成或获胜。
- 可以创建存档备份，但不提供自动恢复。

参阅[中文安装与使用](README.zh-CN.md)、[Setup and use](README.en.md)、[AI 使用指南 / AI guide](AI_GUIDE.md) 和[普通玩家操作矩阵](HUMAN_OPERATIONS.zh-CN.md)。
