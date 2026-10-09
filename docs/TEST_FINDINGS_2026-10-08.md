# 测试停止记录 / Stopped test report — 2026-10-08

## 当前状态

用户要求停止后，自动操控程序已退出，游戏通过正常暂停接口停在冒险 **2-10、第 2 波、tick 5506**。不再继续开发、测试、打包或发布。

- **1-1 至 2-9：19 个关卡均有奖励界面及对应档案进度的记录。2-10 未通关。**
- 保留 `AI Test` 测试档案（ID 3，冒险进度 20）。暂停的棋盘仍在当前游戏进程中；磁盘备份不等同于该瞬间的完整进程快照。
- 停止后备份已通过文件哈希校验；原档案 `New`（ID 2）的 17 个文件与测试前基线一致。
- 这是有人工监督、期间调整过策略的测试，不是无人干预的稳定通关证明。
- 2-10 有三次开局：第一次被隐藏选卡误操作污染，已明确作废；第二次在策略检查中中止；第三次是当前暂停的有效复测。不能把前两次的表现合并成一次顺利通关。

## 已发现的问题与改动

| 问题 | 原因与已做改动 | 验证状态 |
|---|---|---|
| 选卡界面反复上下抖动 | 测试程序在过渡期间重复发起开始；增加等待和同一棋盘的重复请求合并 | 2-1 实测第二次返回 `already_started`，随后正常开局；最终隐藏选卡保护加入后，尚未重新验证普通选卡的完整正向流程 |
| 首次解锁图鉴后接口阻塞 | “继续”同步等待图鉴关闭，占住唯一请求通道；改为先返回，再在游戏主线程执行原回调 | 当次由用户手动关闭恢复；后续普通奖励过渡成功。**2-4 首次解锁图鉴的原故障路径尚未重跑** |
| 隐藏选卡对象也能被操作 | 过场中对象已分配，接口仅检查指针和场景；缩短等待后污染了 2-10 传送带 | 增加可见标志、弹窗及特殊关卡保护；实测 2-10 过场不公布选卡能力，错误选卡被拒绝，干净重开后进入当前复测 |
| 内部观测不足 | 缺少戴夫对白、植物移动坐标、准确卡片命中位置、僵尸受击区域等 | 已补字段；对白推进、普通卡片命中点及旧坐标锤击有实测。不能因此认为所有特殊植物和小游戏都已覆盖 |
| 卡片命中区域错误 | 卡片几何字段及传送带位移处理不完整，部分卡片可能与铲子区域重叠 | 修正几何，增加游戏内部命中测试得到的安全点；普通卡片读回验证通过。新版保龄球整体尚未重跑 |
| 传送带后两列空着 | 阵型把第一、二列固定留给向日葵 | 白天传送带允许远程植物填充后排；1-10 实测第一、二列全部利用 |
| 大嘴花与坚果不配合 | 缺少配套位置规则 | 添加第 7 列大嘴花、第 8 列坚果组合；1-10 的第 2、3 行实测出现该组合 |
| 夜间同时带阳光菇和向日葵 | 选卡保留备用向日葵，但种植策略不会使用，浪费卡槽 | 夜间只选一种产阳光植物；2-8、2-9 去掉向日葵，换寒冰菇。2-8 实测寒冰菇在第三行第三列种下，阳光 85→10 |
| 坚果保龄球打法差 | 早期滚动目标判断和爆炸坚果操作不完善 | 改进移动坐标、已有滚动坚果覆盖判断及爆炸坚果选取流程；**1-5 的通关来自旧策略，新策略只有本地检查** |
| 2-5 不用墓碑吞噬者和植物 | 专用策略只有锤击、拾取；普通 `plant` 又会拒绝锤子光标 7 | 新候选策略走内部选卡→放置流程，加入墓碑吞噬者、土豆雷、樱桃炸弹；**尚未重新实战。普通 `plant` 的光标限制仍保留** |
| 2-5 操作慢且部分锤击没效果 | 固定等待、重复读状态，以及客户端坐标到执行之间的时间差；具体空锤原因未完全确认 | 新增 `hammer_zombie`，执行时读取目标当前位置并返回受击前后状态；缩短专用测试循环等待。已构建及参数测试，**新命令尚未实战或测速** |
| 新锤击动作存在重叠目标歧义 | 游戏按受击区域及绘制顺序决定实际命中的僵尸 | 已在文档说明：可能打中重叠的另一只；`effect_observed` 只检查请求目标。**尚未实现实际命中对象的可靠归因** |
| 2-10 囤积毁灭菇、不处理单只突破 | 爆炸物原先主要按群聚条件使用 | 增加单只逼近时优先救场；已进入新的复测，但因用户停止，**尚未完成整关验证** |
| 寒冰菇位置及重复消耗 | 旧策略要求贴近目标，且只检查场上是否仍有寒冰菇，不能识别已冻结的僵尸 | 已改为安全空格施放；**连续重复施放的问题仍待处理，尚未加入冻结状态或施放间隔判断** |
| 夜间布局和墓碑处理粗糙 | 收入、前线火力、墓碑清理的资源顺序不合理 | 增加阳光预留、避开近处敌人的墓碑、将部分小喷菇铲除后升级大喷菇；有普通夜间关卡实测，但不能代表打法已优化完成 |
| 可能把魅惑僵尸当敌人 | 缺少阵营状态字段 | 增加 `mind_controlled`，策略排除友方；2-10 状态中已读到被魅惑僵尸，完整关卡验证未完成 |
| 打包版本在默认数据目录加载 DLL 失败 | 隔离数据目录可以加载；默认目录曾观察到加密属性，但未证明它是唯一原因 | 文档补充 `PVZ_AI_DATA_DIR` 替代目录办法；**没有修改用户目录加密设置，也没有宣称默认环境问题已根治** |

2-5 的旧策略确实通关，并观察到普通击杀、路障头盔 370→0、铁桶头盔 1100→200；但没有使用植物，且存在无效果锤击。因此这里只认定通关记录存在，不认定打法优秀或可靠。

## 改动位置与版本状态

- 原生接口：`native/src/game.cpp`、`game_input.cpp/.hpp`、`observations.cpp`。
- Python 动作校验与检查：`src/pvz_ai/templates.py`、`tests/test_templates.py`；新增锤击动作参数校验。
- 版本准备：项目版本、启动器标题、安装器及构建脚本已改为 **0.1.1 候选版**，启动器相关检查同步更新。
- 文档：接口、实体布局、两种语言的使用指南、验证记录、发布说明和本记录。
- 本地测试程序及三套策略位于 `tools/`。该目录被 Git 忽略，**这些策略改动尚未纳入版本控制，也不是已发布安装器内置的自动玩家**。停止时另存了源文件快照以供后续核对。

**当前仓库提交/标签仍为 `a33fb6b` / `v0.1.0`。本次工作区改动尚未提交、推送、打包或发布为 0.1.1。现有首版安装器不含这些新改动。**

最近一轮 Python 回归为 **235 项通过**；最后的原生 Release 构建成功。这些检查不替代上表中缺失的实机复测。系统临时测试目录曾发生权限冲突，改用项目内独立临时目录后检查通过。

## 后续待办（仅记录，未执行）

1. 修复寒冰菇重复施放，明确锤击实际命中对象，并完成 2-5 植物流程及速度复测。
2. 重测首次解锁图鉴的阻塞路径、普通选卡正向流程和改进后的 1-5 保龄球。
3. 完成当前 2-10 有效复测；保留测试档案，记录尝试次数与失败/中止原因。
4. 决定哪些测试脚本需要纳入 Git；回归完成后再整理提交和制作 0.1.1 安装包。目前不继续执行这些事项。

## English record

The user requested a stop. The controller exited and the game is paused at Adventure **2-10, wave 2, tick 5506**. The retained `AI Test` profile is at Adventure level 20. There are verified award/progress records for **1-1 through 2-9 (19 levels)**; **2-10 is incomplete**. A verified disk backup was taken after stopping, and all 17 original-profile files still match the pre-test baseline. A disk backup is not a snapshot of the currently paused process.

Implemented changes include repeated-start coalescing, deferred award continuation, hidden-chooser guards, richer dialogue/entity/hit-test observations, a queued entity-based hammer action, improved conveyor placement, removal of redundant night Sunflower selection, and local candidate plant use for Whack-a-Zombie. Live evidence exists for the duplicate-start guard, rejection of hidden selection in 2-10, rear shooters and Chomper/Wall-nut pairing in 1-10, and Ice-shroom use in 2-8.

Important remaining gaps: the original Almanac-unlock failure has not been replayed; improved bowling, the new hammer command, and Whack-a-Zombie plant use have not been live-retested. Generic `plant` still rejects the hammer cursor; the candidate uses normal internal packet selection and placement instead. Overlapping zombies can cause the hammer to strike a different entity. Repeated Ice-shroom use remains unresolved. Emergency conveyor behavior has changed, but the final level is unfinished.

The first 2-10 attempt was invalidated after hidden-chooser calls introduced inappropriate cards. A second attempt was stopped during strategy review. The third is the currently paused clean retest. This was supervised iterative testing, not a flawless unattended run.

The latest Python run passed **235 checks**, and the native Release build succeeded. The source tree contains uncommitted **0.1.1 candidate** changes; the current commit/tag remains **a33fb6b / v0.1.0**. No 0.1.1 commit, push, package, or release was completed. Local policy scripts are Git-ignored and are not a bundled autonomous player. Further work is listed only; it is not continuing after the stop request.
