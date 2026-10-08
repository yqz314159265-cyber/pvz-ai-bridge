# 普通玩家操作与接口覆盖（Steam GOTY Windows 1.2.0.1096）

本清单针对 Steam GOTY Windows 1.2.0.1096；玩法事实以该版本随附 Windows readme 为准，Steam 页面用于核对 GOTY 内容和成就：[Steam 商店页](https://store.steampowered.com/app/3590/Plants_vs_Zombies_GOTY_Edition/)。EA 可访问的同名 readme 是 Mac 版 1.0.40，只作相关说明参考，不能证明 Windows 1096 的实现细节：[EA/PopCap readme](https://akamai.cdn.ea.com/eadownloads/u/f/manuals/GAME-PVZ/en_US_readme.html)。

下表严格区分“源码已实现”“该操作已在此版本实机验证”和“尚未实现/未验证”。源码里存在入口不等于可用按钮当前解锁，也不等于已实机通过。已知实机结果见[验证摘要](../VALIDATION.md)；接口范围见[契约](../INTERFACE.md)。实现核对了 [game.cpp](../native/src/game.cpp)、[game_input.cpp](../native/src/game_input.cpp)、[menu.cpp](../native/src/menu.cpp)、[selector_controls.cpp](../native/src/selector_controls.cpp)、[profiles.cpp](../native/src/profiles.cpp)、[options_controls.cpp](../native/src/options_controls.cpp)、[garden.cpp](../native/src/garden.cpp)、[shop.cpp](../native/src/shop.cpp)、[zombatar.cpp](../native/src/zombatar.cpp)。

本页中文内容的英文等效版本见 [Human Operation Matrix](HUMAN_OPERATIONS.en.md)。实现状态仅限对应 build 和当前普通可见/启用条件。

## 操作矩阵

| 功能 | 普通玩家操作与约束 | 当前接口/状态 |
|---|---|---|
| 标题页 | 点击“Click to Start”，进入主菜单 | `action.click_to_start` 已实现；本线程两次实机点击均通过 |
| 主菜单路线 | 点击当前可见且启用的 Adventure、Mini-Games、Puzzle、Options、Help、Shop、Almanac、Zen Garden、Survival、Achievements 等按钮 | `action.navigate(route)` 经当前 UI 控件快照和普通 selector callback；源码已实现。图鉴、Credits 等子路线另见对应行。Selector 的 `quit` route 仍标 unsupported；退出使用独立生命周期动作。Change User、selector Zombatar、Quick Play 不通过 selector API；Zombatar 另有独立接口 |
| 成就查看 | 读取 20 个 earned/shown 标记；打开成就页并上下滚动 | selector 快照读取状态；普通成就入口有源码。向上、向下各 3 个滚轮刻度已实机验证；`achievements_back` 通过普通游戏返回控件路线实机验证。这不代表每个成就详情/跳转路径均已覆盖；没有直接写入成就状态的 API |
| 冒险 | 进入冒险、选卡、开局、继续已存档游戏、领取结算 | mode 0、`continue`、选卡/开始路径已实现。普通白天冒险第二关曾实机通关；奖励领取遇到自动收币竞争，需人工恢复结算，不能标记为全自动通过 |
| 关卡目录 | 查询规范模式、游戏定义 token、入口路由和 native 验证元数据 | Native `/levels` 实机 GET 返回 73 项（Adventure + 72 个定义项）；host 用静态规范目录合并，并只接收白名单验证字段。此目录不是解锁表，完整映射见[当前目录说明](LEVEL_CATALOG.zh-CN.md) |
| 普通挑战页 | Survival、小游戏、拼图页打开后，只点击对应页面中当前真实可见且启用的按钮；隐藏页与未解锁项拒绝 | `action.enter_level(mode_id)`：页面 0/1/3 的按钮列表、定义页和可见/禁用状态由源码检查；mode 13 泳池无尽入口实机验证。其余各 mode 的菜单流程源码实现但未逐项实测 |
| 隐藏/内部关卡 | 不绕过锁定或隐藏页面；不通过 Limbo 进入 | 页面 2 中未列入普通入口的挑战 mode 会被拒绝。mode 43 Zen Garden 使用 selector 的普通专用入口；mode 50 Tree of Wisdom 通过已购买后花园 Next Garden 正常轮转，入口与返回已实测 |
| 选卡 | 选择可用种子卡并开始；受当前种子包、槽位和解锁状态限制 | `choose_cards`、`start_level` 已实现；冒险和 Pool Endless 选卡实测。Imitater 目标选择被明确拒绝；没有独立“取消选中卡”语义动作 |
| 棋盘种植 | 选卡槽、落在合法格子；受阳光、冷却、地形和当前模式限制 | `action.plant` 已实现，普通冒险和 Pool Endless 实测。特殊模式仅在 native 明确放行时可用；不支持借模式 ID 推断规则 |
| 铲除 | 铲除目标格普通植物 | `action.shovel` 已实现，限 layer=`plant`；覆盖层/墓碑等不支持。另在实机以 `game_input` 选中 shovel（cursor 0→6）再右键取消（6→0），验证没有移除植物 |
| 收集战斗掉落物 | 收集当前有效的阳光/金币等对象；对象可能被游戏自动收走而失效 | `action.collect` 已实现；阳光已实测。普通关卡结算时与游戏自动收币竞争发生过一次，控制器安全拒绝重试；金币全场景尚未覆盖 |
| 普通点击/取消 | 左键点击当前棋盘/选卡 UI；右键取消种子/工具 | `action.game_input` 可对普通 board 与 seed-chooser 路由左/右键输入，并异步重验；通用 board shovel 选择与右键取消已实机验证（cursor 0→6→0，植物未移除）；通用 seed-chooser、其他普通 board 点击虽有路由实现，仍须逐项验证，不等于各玩法均已覆盖专用语义动作。Zen Garden/Tree 用专门工具取消：`garden_cursor_cancel` 已实现、未实测 |
| 玉米加农炮 | 瞄准、发射、取消 | 专用动作未实现；即使基础种植能种下 Cob Cannon，也不代表可操作其瞄准/发射 |
| 坚果保龄球 | 释放坚果并观察碰撞/反弹 | 小游戏专用动作未实现 |
| Vasebreaker | 正常击碎可见花瓶；仅呈现动作后的可见结果 | 专用动作未实现。不得提供未打碎花瓶的隐藏内容 |
| I, Zombie | 选择并部署僵尸 | 专用规则/部署动作未实现 |
| 三消、老虎机及其他小游戏 | 按各游戏规则交换、点击或释放 | 只有进入菜单/挑战页的部分源码路径；具体小游戏交互未实现 |
| 水族馆小游戏 | 喂鱼、敲玻璃、收集金币 | Zombiquarium 挑战可枚举；相应 gameplay actions 未实现。不可把 Zen Garden 水族馆植物操作混称为小游戏支持 |
| 传送带 | 使用传送带提供的卡片、落位、取消 | 普通 `plant` 会识别传送带并绕过阳光费用检查，但各传送带玩法/取消交互未完整验证 |
| 暂停/继续 | 桥接器 pause/resume；区分游戏菜单暂停与失焦行为 | `/pause`、`/resume` 已实现，后台推进与显式暂停实测；不是任意场景的通用游戏内暂停按钮 API |
| 选项对话框 | 普通打开/关闭、返回游戏 | `open_menu`、`close_menu` 已实现；实机图鉴流程中打开/关闭也通过。保存/返回主菜单走游戏确认流程 |
| 音乐/音效 | 读取并通过普通 Slider 设置 Music、Sound | 普通 `action.ui` Slider 拖动已实测：两个滑块的请求值 0.1、0.9 因像素格量化分别读回约 0.097345、0.902655，之后均恢复为 0。不要把请求值当成精确读回值 |
| 全屏/硬件加速 | 读取并切换 Fullscreen、Hardware Acceleration 复选框 | `option_set` 已实机分别切换并恢复两个复选框；此结论限于该次验证，不外推到其他显示设备 |
| 图鉴 | 打开、关闭、选择植物/僵尸、切 Index/Plants/Zombies 页 | `open_almanac`、`close_almanac`、`almanac_page`、`almanac_entry` 已实机完整执行 10 个步骤且均成功，选中植物/僵尸并显示预览。图鉴页面、分页和返回路线均按普通控件完成验证 |
| Credits | 主菜单选项中的 Credits、通关后进入片尾、跳过/退出 | 主菜单 Credits 普通回调及正常通关片尾进入、播放场景、Escape 确认后回主菜单均已实机验证。播放中的 Replay/Main Menu 控件激活被拒绝/未通过，不能宣称已支持；“重播片尾 bonus video”仍未验证 |
| 帮助 | 打开帮助页并按普通继续控件返回 | 帮助页正常打开及 `action.continue` 已实机验证；内容逐项浏览未覆盖 |
| 退出游戏 | 主菜单 Quit、确认退出或取消 | `quit_open`、`quit_cancel`、带显式 `confirm=true` 的 `quit_confirm` 已实现；打开、取消和普通确认退出均已实机验证，随后 Steam 冷启动也成功。selector 的通用 `navigate(quit)` 仍 unsupported |
| 返回主菜单/重开 | 普通选项对话框中的 Main Menu/Restart，按普通确认和保存路径进行 | 普通 Restart 已实机触发原生选项回调，确认 board 已重建、进入选卡且 tick 归零。冒险保存退出、Zen Garden 退出有部分流程验证；Tree mode 50 经普通路径退出主菜单已验证 |
| 档案查看/选择 | 查看用户列表并按已验证身份选择 | `profile_open`、`profile_select`、`profile_close`、异步 job 已通过完整生命周期实机验证；创建 ID 3 后选择该档案，再取消删除并确认删除。 |
| 新建档案 | 输入合法名字并通过游戏原 UI 创建 | `profile_create` 已实机创建本次测试档案 ID 3；只允许本桥接会话新建且追踪身份的档案 |
| 重命名档案 | 对本会话新建档案改名 | `profile_rename` 已实机重命名 ID 3；原档案受保护，名字受游戏规则及重复名检查 |
| 删除档案 | 选择本会话创建的目标并经独立确认删除 | `profile_delete` + `profile_delete_confirm` 已实机验证取消与确认两条路线；只允许删除本会话创建且身份核验通过的测试档案。档案测试结束时，原有 19 个存档文件与新鲜备份逐文件一致；这是该时点的比较，不代表后续花园护理和头像保存操作后文件仍相同。原用户档案仍不可删 |
| 存档备份/恢复 | 备份、恢复、列出版本、校验 | 连接流程曾创建本地备份；没有 Native/API 档案备份/恢复管理动作。恢复能力和恢复结果应独立验收 |
| Dave 商店 | 查看商品/库存、翻页、购买单件、关闭 | 源码实现；读取/翻页、预算拒绝、购买一次 Tree Food、关闭均实测。购买受库存/价格/金币和解锁限制 |
| Zen Garden 查看 | 读取当前花园及盆栽、需求、状态 | `garden.snapshot` 源码实现；主花园读取 4 盆植物实测 |
| 花园护理 | 浇水、施肥、喷药、音乐；需对应需求和库存 | `garden_care` 支持 `water`、`fertilizer`、`bug_spray`、`phonograph`；两盆植物浇水后需求均变为 none。施肥、喷药、音乐仍未实测，且受拥有的工具和库存限制 |
| 盆栽移动 | 手套拿起并放到空格 | `garden_move` 源码实现，需合法空格/当前花园；未实测 |
| 独轮车 | 放入盆栽、在有空位的兼容花园放置 | `garden_wheelbarrow_pick/place` 源码实现；库存、拥有权、花园类型和目标格均校验；未实测 |
| 出售盆栽 | 钱袋工具发起出售，再由用户确认或取消 | `garden_sell`、`garden_sale_confirm(confirm=true)`、`garden_sale_cancel` 源码实现并保护原档；只允许测试档案。未实测 |
| 蜗牛 Stinky | 对睡眠的 Stinky 喂巧克力，或不花巧克力直接唤醒 | `garden_feed_snail` 和 `garden_wake_snail` 均实现：前者需要巧克力，后者需空光标且成本为 0。仅主 Zen Garden；均未实测。精确版 readme 只记载 Stinky 收集金币及喂巧克力可维持活动；未证实有普通玩家“放出/收回”控件，故不列作已确认操作或缺失按钮 |
| 巧克力 | 喂花园植物或符合条件的蜗牛 | `garden_chocolate`、`garden_feed_snail` 源码实现并检查对象/库存；未实测 |
| 花园掉落物 | 收集花园金币、钻石等 | 没有单独的 `garden_collect`；通用 `action.collect` 不限战斗模式，可作用于当前有效掉落物。花园实际收集尚无实机证据，不应据此宣称花园全覆盖 |
| Next Garden / 智慧树 | 通过花园普通 Next Garden 控件按购买所有权轮转；树食物走正常 Tree Food 工具 | `garden_next` 已验证主花园进入已拥有的智慧树；`enter_garden(0)` 已验证返回主花园且报告 completed；智慧树直接退出主菜单也通过。树高保持 6、树粮为 0，`tree_feed` 未实测。蘑菇园、水族馆未购买，未验证其实际切换。花园轮转使用普通命中测试、悬停更新和正常换场过渡 |
| Zombatar 打开/协议 | 主菜单进入、接受条款 | `open_zombatar` 已实测。当前玩家此前已接受条款，本轮没有调用 `accept_terms`，该动作仅源码实现；没有专用拒绝条款动作 |
| Zombatar 类别/部件 | 按普通类别、调色、翻页和选择可见部件 | `skin`、`category`、`page`、`item` 及独立 palette 颜色选择均已实机验证：skin（enum 0）选 color/palette index 2 并提交，快照记录 `selected_colors_by_category[0]=2`；UI category index 3（enum 4）选第 1 项、翻至第 2 页后选 item 19；背景 category index 7（enum 8）选 item 1。未覆盖的类别及各项视觉语义仍不推断；此前单行背景测试 |
| Zombatar 保存/浏览 | 新建并保存头像；浏览已存头像；返回编辑器 | `save_new` 和 `back` 已通过实机流程。正常动画完成后保存生成桌面 JPG；图像已检查为有效 180×180 僵尸头像（带伤疤），文件 7,939 字节。`new_avatar` 与多个头像间的 `saved_avatar` 切换已实现，尚未单独实测 |
| Zombatar 删除 | 删除桥接会话创建并指纹记录的最新头像，要求显式确认 | `delete_new(confirm=true)` 已实机删除本次唯一创建的新头像（saved count 1→0），之后正常返回主菜单。不能删除已有头像或其他会话条目 |
| Zombatar 导出 | 保存头像时由游戏产生桌面 JPG；独立请求导出文件 | `save_new` 已实机生成并核验桌面 JPG 文件（7,939 字节、180×180，图像内容已检查）；独立 `export` 动作仍拒绝，未映射独立控件/回调 |
| 通用界面控件 | 当前作用域下可见/启用按钮激活、Slider 拖动 | `action.ui` 源码实现，检查 UI 树/作用域并延后重新验证。对档案、Zombatar、Restart 确认及 Garden/Tree 场景有保护性封锁；不支持输入文本、通用键盘/滚轮或鼠标消息脚本 |

## 状态用词

- **源码实现**：存在 native 语义入口和目标对象/状态校验，不代表游戏实测通过。
- **实机验证**：只限验证记录描述的特定 build、档案状态、场景和操作；不外推到所有模式或解锁状态。
- **受限制/拒绝**：API 明确检查可见启用、购买/库存、场景、用户档案保护或白名单；拒绝是预期行为。
- **未实现/未验证**：没有可用语义入口，或存在入口但没有对应实机证据。不要将菜单可见、模式可枚举、快照可读写成全覆盖。

## 尚未覆盖的重点

仍未覆盖或缺少实机证据的重点包括：玉米加农炮瞄准/发射/取消、Vasebreaker 可见罐操作及结果观察、I,Zombie 部署、Zombiquarium 喂鱼/敲击/收币、三消交换/老虎机点击、独立档案备份/恢复、种子选择取消均未完成逐项验证；这不排除 `game_input` 普通点击触发原生控件，但这些路径没有专项实测。已实现但未实测的操作包括通用 seed-chooser/普通 board 点击在各具体玩法中的效果、花园掉落物收集、未购买的蘑菇园/水族馆切换、花园盆栽移动/出售/喂树，以及 Zombatar 未覆盖类别的部件语义仍未逐项核实。已覆盖的 skin、palette、指定分类翻页和保存/删除流程。主花园与智慧树往返、成就滚动和返回已有验证；Credits 播放中 Replay/Main Menu 交互仍未通过验证；Quit 打开、取消和普通确认退出均已验证。这里列的是可操作 UI/接口缺口，不是要求自动制定或执行玩法策略。关卡目录及 host 合并规则见 [LEVEL_CATALOG.zh-CN.md](LEVEL_CATALOG.zh-CN.md)。
