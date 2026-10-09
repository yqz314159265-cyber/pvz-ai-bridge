# 冒险测试与错误记录 / Adventure test and findings

## 中文

2026-10-09 本轮按用户要求结束于 **4-1（全局第 31 关）的奖励界面**：`AI Test` 档案进度 32、金币 4050；未进入 4-2。已有 1–31 关奖励与进度记录，不能据此声称全部 50 关完成或全程自主通关。

1–30 关由本地规则脚本在监督下操作，期间调整策略及暂停介入。4-1 的选卡、开局仍由脚本完成；第 2 波 tick 5280 后停止自动玩家及监视器，由 Codex 逐个决定内部 API 动作，思考时暂停。薄执行器仅提交明确选定的动作，不运行策略循环。通关状态保存在私有 `artifacts/adventure-manual/round-019-award-settled.json`。

| 关卡 | 结果及证据边界 |
|---|---|
| 1-1 至 2-10 | 历史记录与本轮恢复记录确认奖励/进度；不是本轮全部重打 |
| 3-1 至 3-4 | 监督脚本通关；3-4 正常购买第八卡槽后使用八张卡 |
| 3-5 | 首棋盘失败；第二棋盘 UTC 14:41:26 通关，期间暂停监督一次；日志分段不算额外重试 |
| 3-6 | UTC 14:47:57 通关；两次坚果种植被拒，旧观测缺少冰道，不能证明拒绝原因 |
| 3-7 | UTC 14:56:54 通关；观察到包含泳池的 12 株后排向日葵及第三列射手 |
| 3-8 | UTC 15:03:21 通关 |
| 3-9 | UTC 15:15:53 通关；同一棋盘通过菜单暂停加载冰道观测模块，保留棋盘连续性证明 |
| 3-10 | UTC 15:26:24 通关；第 11 波人工辣椒救援第三行水路后脚本完成，不能算无介入通过 |
| 4-1 | 脚本开局后由 Codex 接管，最终领奖并停下；未进入下一关 |

### 已发现问题与修正

| 问题 | 改动 | 验证范围 |
|---|---|---|
| 选卡菜单反复上下移动 | 同一棋盘/选卡器重复开始合并，拒绝隐藏或被弹窗遮挡的选卡 | 重复开始及隐藏选卡保护有实机记录 |
| 奖励继续回调阻塞图鉴关闭请求 | 延后普通 Continue 回调，让嵌套游戏等待仍能处理桥接请求 | 构建成功；原首次解锁图鉴故障未重放 |
| 首次商店教学缺少对白继续接口，商店解锁条件过严 | 新增 `shop_advance_dialogue`；按目标二进制的普通解锁条件检查 | 实机 301→302，金币 510 不变；随后完成教学。独立主菜单打开商店未复测 |
| 传送带漏捡钱，寒冰菇/毁灭菇消耗过快 | 正常拾取；按突破风险施放、等待已在场爆炸物，保留对策 | 私有策略已迭代；不是所有场景均验证 |
| 泳池收入、第三列火力与土豆雷开局不足 | 睡莲与收入叠种、后排收入、第三列射手、提前种雷并计入启动时间 | 3-2/3-3/3-7 有具体叠种、射手、土豆雷启动证据 |
| 3-5 各行火力和坚果缺失 | 独立小僵尸策略，均衡两条水路，坚果前置、岸边樱桃覆盖邻行 | 首次失败保留，第二棋盘已通关 |
| 3-9 僵尸越过射手后仍被计为有保护 | 判断射手实际攻击方向，收入前预留应急阳光 | 离线检查；同关最终通关不证明每个分支 |
| 3-10 水路紧急处理没有睡莲前提 | 提前准备合法睡莲，辣椒按整行作用选择空格，等待已在场应急植物 | 人工睡莲上的辣椒救援后，第三行敌人清空 |
| 3-10 三线射手堆在边行、火炬重复且插在射手之间 | 后排优先第 2/5 行，平衡三线射手数量；每行最多一株有效火炬，前排留防御空间 | 公开纯阵型规划辅助函数与离线回归；修正后未重打 3-10 |
| 启动器每次询问语言，说明却称首次选择 | 用户数据 `language.json` 持久化；缺失或无效时恢复选择界面 | 中英文首次选择与保存后跳过选择的实际界面测试 |
| 默认 pytest 误导入归档实机脚本 | 默认只收集 `tests` | 完整回归结果见 [VALIDATION.md](../VALIDATION.md) |

### 操作失误与正确拒绝

4-1 人工接管期间也有失误：收取阳光后未等待正常动画结算，过早种阳光菇被“阳光不足”拒绝；冷却未结束时补小喷菇被拒绝；第一株窝瓜放在已越过的玩偶匣僵尸右侧，未及时击杀，目标随后被小喷菇消灭。第二株窝瓜放在第一行第九列，消灭了新玩偶匣僵尸。奖励结算期间对已在收取的金币再次请求拾取也被拒绝。这些是决策/时序失误及正常接口保护，不应归为资源或冷却故障。

### 保存、交付与未验证项

UTC 15:50:16 只读审计确认原档案 `New` 的 17 个文件与基线相同。测试档案仍保留，项目内备份 `backups/adventure-finalize-20261009T155016Z-61da7592` 的副本哈希校验通过。备份助手的历史胜利汇总仅覆盖 1–20 关；21–31 的胜利另由逐关日志和人工接管记录证明。

公开源码包含接口修复、纯阵型规划辅助函数、测试及中英文文档；私有自动玩家/监视器仍在 Git 忽略的 `tools/`，没有随安装器交付。人工填写的 Skill 策略模板保持空白。当前安装器仍是 v0.1.0，本轮未发布新安装器。

冰道数组已按目标二进制核对、构建并加载，实机仅确认初始空冰道；活动冰道消退/阻挡尚未验证。僵王字段和冰火球候选规则仅静态/离线核对，尚未到达僵王关卡。新 `hammer_zombie`、改进保龄球以及首次图鉴解锁原故障仍待复测。内部观测和状态检查通过，不等于全部操作或策略都可靠。

全过程保留游戏窗口，不通过截图、桌面鼠标键盘模拟操作；没有修改阳光、冷却、解锁或存档进度。存档、令牌、原始内存与私人运行日志保持本地。较早暂停时点见 [2026-10-08 记录](TEST_FINDINGS_2026-10-08.md)。

## English

The session stopped as requested at the **4-1 (global level 31) award screen** on 2026-10-09: `AI Test` progress 32, coins 4050. Level 32 was not entered. Awards/progression exist through levels 1–31; this is neither all-50 completion nor fully autonomous play.

Levels 1–30 used a supervised local rule script, with policy changes and interventions. The script also chose the deck and opened 4-1. At wave 2, tick 5280, the player and watchdog were stopped; Codex chose each subsequent internal API action and paused while reasoning. The thin executor submitted only specified moves, without a strategy loop. The final private snapshot is `artifacts/adventure-manual/round-019-award-settled.json`.

Level 25 first lost, then won on its second actual board at 14:41:26 UTC. Two log segments on that board are not two retries. Levels 26–30 won at 14:47:57, 14:56:54, 15:03:21, 15:15:53 and 15:26:24 UTC. Level 30 needed a manual row-3 Jalapeno rescue at wave 11 before the script finished; it was not an intervention-free pass.

Corrections cover repeated/hidden chooser starts, deferred award continuation, ordinary store tutorial dialogue and its unlock predicate, conveyor coin pickup, more conservative Ice/Doom use, pool income/foundations, column-three fire and early Potato Mine planning. Level 25 received a dedicated balanced policy. Level 29's coverage check now considers shooters' directions. Level 30's urgent water response can prepare a pad and use a line-wide Jalapeno. Its formation correction balances rear Threepeaters in rows 2/5, places one useful Torchwood per lane ahead of shooters, and reserves front defensive space. Pure planning helpers and their regression tests are public; the corrected 3-10 formation has not been replayed live.

The launcher now remembers the chosen Chinese/English UI separately from the installer language, recovering missing/invalid preferences through the picker. Both actual first-choice and saved-choice UI flows are tested. Default pytest collection excludes archived live scripts. Current build/regression results are in [VALIDATION.md](../VALIDATION.md).

Manual mistakes are retained: a Sun-shroom request before collected sun settled was correctly refused; a Puff-shroom request during cooldown was refused; an ineffective Squash to the right of a Jack that had already passed did not promptly kill it, and Puff-shrooms finished the target. A later Squash at row 1/column 9 killed its Jack. An already-collecting coin during award settlement was also correctly refused. These do not demonstrate resource/cooldown interface defects.

The 15:50:16 UTC audit verified the retained test-save backup and all 17 original-profile files against baseline. Backup: `backups/adventure-finalize-20261009T155016Z-61da7592`. The backup helper's historical win summary covers only levels 1–20; later wins have separate run/manual evidence. Saves and raw evidence remain private.

The public source includes interface fixes, pure formation helpers, tests and bilingual documentation. Private players/watchdogs remain Git-ignored; no bundled autoplayer is claimed. The human-editable Skill strategy scaffold remains blank. The published installer is still v0.1.0; this session produced no new installer.

Active ice trails, live boss/projectile behavior, the semantic hammer action, improved bowling and the original first-Almanac-unlock failure path still need live tests. Initial empty ice arrays and statically verified offsets are not proof of those scenarios. The game remained visible, with no screenshot recognition, OS input simulation, resource/cooldown/unlock writes or save-progress editing. See [the earlier stopped snapshot](TEST_FINDINGS_2026-10-08.md) for historical boundaries.
