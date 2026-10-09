# v0.1.0 validation / 验证记录

Validated on Windows x64 with Steam GOTY 1.2.0.1096 x86 on 2026-10-08.
验证环境：Windows x64、Steam GOTY 1.2.0.1096 x86，日期 2026-10-08。

- Native Release build succeeded; 224 Python checks passed, including both language-selection flows and authenticated HTTP sharing. 原生 Release 构建成功，224 项 Python 检查通过，含双语言选择和鉴权 HTTP 共用控制主机。
- The packaged EXE completed a real MCP initialization, tool listing, and status read without a system Python dependency. 打包 EXE 已通过真实 MCP 初始化、工具列表和状态读取，不依赖用户另装 Python。
- The installer was installed into an isolated directory and its EXE verified the Steam target, controlled the live Almanac through 10 actions, and served a real MCP proxy client with 14 tools. The proxy read a valid UTF-8 Chinese guide and shared the same game controller. 隔离安装后的 EXE 能识别 Steam 游戏、执行图鉴 10 步操作，并通过真实 MCP 代理提供 14 个工具；中文指南读取正常，使用同一个游戏控制主机。
- Creating a guide twice preserved human content; installing a skill twice refused overwrite. Uninstall exited successfully, removed the installed executable, and preserved the separate guide and skill directories. 二次初始化保留人工指南，二次安装 Skill 拒绝覆盖；卸载成功移除程序并保留独立的个人指南和技能目录。
- The Almanac page callback now follows the target's register calling convention. Its 10-step open/page/select/close flow passed after a fresh game launch. 图鉴翻页已修正寄存器调用约定；新启动游戏后的 10 步打开、切页、选择、关闭流程通过。
- Normal quit confirmation, Steam restart, CLICK TO START after loading, and garden/tree/main-menu navigation passed. 普通退出确认、Steam 重启、加载完成后的 CLICK TO START 和花园/智慧树/主菜单往返通过。
- Software rendering is enabled through the normal Options checkbox. The setting survived a game restart. A 180-second background observation made 174 successful state reads with zero failures and a maximum measured latency of 84.9 ms. 已通过游戏设置关闭 3D 加速，重启后设置保留；180 秒后台观察读取 174 次，失败 0 次，最高响应 84.9 ms。
- The earlier intermittent stall was sampled in the graphics-driver path. Software rendering is a compatibility workaround, not proof of the driver or bridge's underlying fault. Hours-long unattended play has not been verified. 之前间歇性停顿采样位于显卡绘制路径；软件绘制是兼容处理，尚不足以证明驱动或桥接器的底层根因，未验证数小时无人值守运行。

The public package contains no game binaries/assets, saved games, tokens, raw memory dumps, or personal run logs. Private test records and hash-verified save backups remain local.
公开包不含游戏文件/素材、存档、token、内存转储或私人运行日志。原始测试记录和带校验清单的存档备份仅保留在本地。

See [operation coverage / 操作覆盖](docs/HUMAN_OPERATIONS.en.md) and [中文矩阵](docs/HUMAN_OPERATIONS.zh-CN.md). A passing interface check does not prove an autonomous win, a completed strategy, or support for every game mode.
接口检查通过不代表自动获胜、打法已完成或所有模式均受支持。

## 2026-10-09 candidate evidence / 候选版本证据

The private live-test record confirms actual awards/progression through global level 31 (4-1). Levels 1–30 used the local rule script under supervision; level 31 was directly controlled action-by-action by the root agent through the API after scripted setup. It ended at the award screen with profile progress 32 and 4050 coins. No level 32 entry or all-50 completion is claimed. The 15:50:16 UTC audit verified the save backup and found all 17 original-profile files unchanged from baseline. Detailed provenance is in [the private-run summary](docs/FULL_ADVENTURE_TEST_2026-10-09.md); raw evidence remains private.

The final native Release build succeeded and the complete Python regression passed 247 tests, including both first-choice/saved-choice UI languages and pure formation helpers. The private level-30 formation fixtures also passed; no live player was restarted. The published v0.1.0 validation above remains a historical snapshot; these candidate changes have not produced a new installer. Active ice-cell behavior and live boss-field behavior remain unverified, and the corrected 3-10 formation has not been replayed live.

2026-10-09 私有实机记录确认胜利奖励/档案进度达到全局第 31 关（4-1）。1–30 关由本地规则脚本在监督下操作；第 31 关完成脚本开局后，由根代理逐个通过 API 直接控制动作。最终停在进度 32、金币 4050 的奖励界面，没有声称进入 32 关或完成全部 50 关。UTC 15:50:16 的审计确认备份校验通过，原档案 17 个文件与基线一致。证据链见[私有运行摘要](docs/FULL_ADVENTURE_TEST_2026-10-09.md)，原始材料保持私有。

最终原生 Release 构建成功，完整 Python 回归 247 项通过，包含双语言首次/保存后界面流程及纯阵型辅助函数。私有 3-10 阵型检查也通过，没有重启自动玩家。上文 v0.1.0 验证是历史快照；本候选改动尚未形成新安装包。活动冰格行为和僵王字段实战未验证，修正后的 3-10 阵型尚未重打实测。

## Earlier v0.1.1 snapshot (2026-10-08) / 较早的候选版记录

As of the stop on 2026-10-08, Adventure 1-1 through 2-9 have verified award/progress records (19 levels). Level 2-10 is paused at wave 2 and remains incomplete. These were supervised iterative runs with strategy changes. The Whack-a-Zombie win used the old coordinate-driven policy, without plants and with some ineffective hammer attempts.

The latest Python regression run passed 235 checks and the final native Release build succeeded. This does not establish live correctness of the new `hammer_zombie` action or plant use in Whack-a-Zombie. The original Almanac-unlock failure and improved bowling have not been replayed. Hidden selection was refused during the clean 2-10 restart. Repeated Ice-shroom use remains unresolved.

After stopping, all 17 original-profile files matched the pre-test baseline and the retained test saves were copied with hash verification. No 0.1.1 commit, push, installer, or release was completed. See [the stopped test report](docs/TEST_FINDINGS_2026-10-08.md) for changes, invalidated attempts, remaining bugs, and exact validation boundaries.

截至 2026-10-08 用户要求停止时，1-1 至 2-9 共 19 关有奖励界面及对应档案进度记录；2-10 暂停在第 2 波，尚未完成。这是期间调整策略的受监督测试。2-5 的胜利来自旧坐标锤击策略，未使用植物，且有部分无效果锤击。

最近 Python 回归 235 项通过，最后的原生 Release 构建成功；这不代表新锤击命令或 2-5 植物流程已实测。首次图鉴解锁原故障和改进保龄球尚未重测；2-10 干净重开时已验证隐藏选卡被拒绝。寒冰菇重复施放仍待修复。

停止后复核原档案 17 个文件与基线一致，并完成保留测试档案的哈希校验备份。本次没有完成 0.1.1 的提交、推送、安装包或发布。详见[停止记录](docs/TEST_FINDINGS_2026-10-08.md)。
