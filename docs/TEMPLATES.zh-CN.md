# 策略模板与本地模型

模板是有限的语义动作建议，不是游戏内脚本。JSON 模板必须写明 `name`、`version`（当前为 1）、`mode_id`、`goal`、`cards`、`layers`、`prerequisites`、`horizon_ticks` 和有限的 `actions`。动作只接受 `INTERFACE.md` 定义的语义操作；每步可用 `when` 写状态条件，支持 `scene`、`mode_id`、`paused`、`sun`、`wave`、`tick` 与 `eq/ne/lt/lte/gt/gte`。不支持 Python、表达式求值、循环或任意代码。

计划绑定模式、棋盘 ID 和 tick；首个有棋盘的状态会锁定棋盘 ID，也可在编译时传入。棋盘变化、模式改变、暂停、tick 倒退或 horizon 到期都会使计划停止。条件未满足时当前步骤会等待后续实时状态，不会被跳过。每次返回的动作只是待处理建议；调用方必须在游戏桥接层接受该动作后调用 `ack_action(True)` 才能推进，拒绝时调用 `ack_action(False)` 会停止计划并保留该步骤。无限生存等长期目标可放在 `goal` 中；控制器应按短 horizon 周期重新读取状态并生成新计划，不允许一个无限动作队列。游戏桥接层仍须校验阳光、冷却、解锁、格子占用和动作支持情况；模板不能绕过正常游戏规则。模板校验器识别花园与商店动作：`enter_garden`、`garden_care`、`garden_move`、`open_shop`、`shop_page`、`shop_buy`、`close_shop`。`shop_page.page` 只能是 0 到 3；花园植物 ID 使用无符号 32 位范围，`garden_care.tool` 可为 `water`、`fertilizer`、`bug_spray` 或 `phonograph`。商店 `shop_buy` 必须带 `quantity: 1` 和 `max_total_cost`，当前一次只允许购买一件。识别只代表参数符合模板格式；执行前仍要检查 native `capabilities`，不支持就明确拒绝。游戏侧应以合法购买函数、当前报价和库存为准，不能直接改金钱。模板只能作为示例，不能据此断言策略已经验证或能赢。

`cards` 是选卡提示；`layers` 用于记录水池睡莲叶、屋顶花盆、南瓜保护层和升级植物的布置意图；`prerequisites` 记录存档解锁或前置升级。它们不会自动解锁内容，也不会强制执行动作。格子坐标从 1 开始。示例 `templates/illustrative_day_1.json` 仅供说明。

## GOTY 模式目录与策略边界

`pvz_ai.catalog.MODE_CATALOG` 是原版 GOTY `GameMode` 枚举的静态目录（ID 0–72）；ID 73 是 `NUM_GAME_MODES` 计数哨兵，不是模式。枚举标签和编号可在 [PvZA11y 的 GameMode 枚举](https://github.com/CG8516/PvZA11y/blob/master/enums.cs) 与 [re-plants-vs-zombies 的 ConstEnums.h](https://github.com/Patoke/re-plants-vs-zombies/blob/main/ConstEnums.h) 交叉核对。池 Endless 是 ID 13（SurvivalEndless3）；目录的 `hidden` 是目录呈现分类，不保证特定存档已解锁或菜单实际显示。

目录的 `native_support` 固定为 `unknown`，`unlocked` 固定为 `None`。它不反映当前游戏存档、桥接层能力或关卡是否可进入；运行时必须以 host 从 native 获取的 `capabilities` 和当前场景状态为准。Adventure/Survival 的 `plant_defense` 只是普通防守规则类别；小游戏、Vasebreaker、I,Zombie、Zen Garden 和 Tree of Wisdom 的规则不同，应要求 native 明确广告相应动作，不可由模式 ID 推断 `plant` 或其他动作可用。内部/limbo 项没有通用动作配置。

`templates/illustrative_survival_day_endless.json` 与 `templates/illustrative_survival_pool_endless.json` 给出有限的开局布阵草图，使用正常选卡、开始与种植语义动作，不执行即时布置。池模式水路示例先请求睡莲叶，再在同格种植物。阳光条件只是等待阈值；游戏仍负责最终检查阳光、卡片冷却、解锁、占格和规则。卡片序号按模板选卡次序，使用前应核对 live seed packet。两份示例均为未验证的说明材料，不是可保证获胜的方案。当前 native 若未广告相应模式或动作，host 应拒绝；用户可手动进入模式后再使用适用能力，不能据目录声称自动进入已实现。

## 模仿视频策略

可以提供视频来源、作者/链接、关卡和规则版本，并按时间记录可见事件，例如：

```json
{
  "source": "用户提供的视频标题或链接",
  "observations": [
    {"time": "00:12", "scene": "选择卡片", "observation": "选择了向日葵与豌豆射手"},
    {"time": "00:35", "scene": "战斗", "observation": "在第 3 行第 2 列种下向日葵"}
  ],
  "uncertainties": ["画面未显示阳光数值"]
}
```

这只是人工整理的观察记录，不表示系统会自动观看、下载或训练视频模型。整理成模板前要区分画面事实与推测，并由用户确认适用于自己的关卡、卡片和存档。

## 本地模型接口

`LocalModelClient` 只向用户配置的本机 OpenAI 兼容 `/chat/completions` 发送当前可见状态，并返回经过严格校验的有限语义动作建议。接口地址只允许 `localhost`、`127.0.0.1` 或 `::1` 的 HTTP 服务；模块不配置云端服务、不携带凭据，也不执行模型返回的动作。调用方仍需将建议交给 host 的短计划接口，并在计划期限和模式内逐个验证。
