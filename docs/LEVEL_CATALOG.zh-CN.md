# 当前 `/levels` 原生目录和 host 合并规则

`native/src/game.cpp::level_catalog()` 当前构造 73 项：Adventure mode 0 加目标二进制的 72 条 ChallengeDefinitions。定义表证据来自 Steam GOTY Windows 1.2.0.1096 本机代码数据：`artifacts/runtime-code/data.bin` 映射基址 `0x71F000`，表起始 `0x72A370`、stride `0x18`；记录首 DWORD 是 mode ID，`+0x08` 是 ChallengeScreen page，末 DWORD 指向 `rdata.bin` 中的本地化 token。按钮回调索引为 `200 + mode_id - 1`。它是目录映射，不是存档解锁状态，也不是 AI 通关能力。

普通挑战入口白名单与源码一致：page 0 → selector ID 110；page 1 → ID 101；page 3 → ID 102；只有当前按钮可见且启用时才会执行。page 2 均标注 `hidden_selector_page=true`；mode 43 另走 Zen Garden selector ID 109，mode 50 走 `garden_next` 且要求拥有智慧树。其它 page 2 项的 `entry_action` 为 null、`entry_implemented=false`。没有 Limbo/隐藏关卡的直入路线。

下方 JSON 是 native `/levels` 返回形状。mode 0、13、43 的 `entry_live_verified` 为 true；其它入口为 false。`ai_win_verified` 全部 false。Adventure/13/43 的标记只表示入口验证，不表示策略、完整关卡或模式胜利已验证。

```json
[
  {
    "mode_id": 0,
    "name": "Adventure",
    "name_token": "ADVENTURE",
    "entry_action": "enter_level",
    "entry_implemented": true,
    "entry_live_verified": true,
    "ai_win_verified": false
  },
  {
    "mode_id": 1,
    "name": "[SURVIVAL_DAY_NORMAL]",
    "name_token": "[SURVIVAL_DAY_NORMAL]",
    "definition_page": 0,
    "page_position": {
      "row": 0,
      "column": 0
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 2,
    "name": "[SURVIVAL_NIGHT_NORMAL]",
    "name_token": "[SURVIVAL_NIGHT_NORMAL]",
    "definition_page": 0,
    "page_position": {
      "row": 0,
      "column": 1
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 3,
    "name": "[SURVIVAL_POOL_NORMAL]",
    "name_token": "[SURVIVAL_POOL_NORMAL]",
    "definition_page": 0,
    "page_position": {
      "row": 0,
      "column": 2
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 4,
    "name": "[SURVIVAL_FOG_NORMAL]",
    "name_token": "[SURVIVAL_FOG_NORMAL]",
    "definition_page": 0,
    "page_position": {
      "row": 0,
      "column": 3
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 5,
    "name": "[SURVIVAL_ROOF_NORMAL]",
    "name_token": "[SURVIVAL_ROOF_NORMAL]",
    "definition_page": 0,
    "page_position": {
      "row": 0,
      "column": 4
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 6,
    "name": "[SURVIVAL_DAY_HARD]",
    "name_token": "[SURVIVAL_DAY_HARD]",
    "definition_page": 0,
    "page_position": {
      "row": 1,
      "column": 0
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 7,
    "name": "[SURVIVAL_NIGHT_HARD]",
    "name_token": "[SURVIVAL_NIGHT_HARD]",
    "definition_page": 0,
    "page_position": {
      "row": 1,
      "column": 1
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 8,
    "name": "[SURVIVAL_POOL_HARD]",
    "name_token": "[SURVIVAL_POOL_HARD]",
    "definition_page": 0,
    "page_position": {
      "row": 1,
      "column": 2
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 9,
    "name": "[SURVIVAL_FOG_HARD]",
    "name_token": "[SURVIVAL_FOG_HARD]",
    "definition_page": 0,
    "page_position": {
      "row": 1,
      "column": 3
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 10,
    "name": "[SURVIVAL_ROOF_HARD]",
    "name_token": "[SURVIVAL_ROOF_HARD]",
    "definition_page": 0,
    "page_position": {
      "row": 1,
      "column": 4
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 11,
    "name": "[SURVIVAL_DAY_ENDLESS]",
    "name_token": "[SURVIVAL_DAY_ENDLESS]",
    "definition_page": 2,
    "page_position": {
      "row": 3,
      "column": 0
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 12,
    "name": "[SURVIVAL_NIGHT_ENDLESS]",
    "name_token": "[SURVIVAL_NIGHT_ENDLESS]",
    "definition_page": 2,
    "page_position": {
      "row": 3,
      "column": 1
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 13,
    "name": "[SURVIVAL_POOL_ENDLESS]",
    "name_token": "[SURVIVAL_POOL_ENDLESS]",
    "definition_page": 0,
    "page_position": {
      "row": 2,
      "column": 2
    },
    "selector_button_id": 110,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": true,
    "ai_win_verified": false
  },
  {
    "mode_id": 14,
    "name": "[SURVIVAL_FOG_ENDLESS]",
    "name_token": "[SURVIVAL_FOG_ENDLESS]",
    "definition_page": 2,
    "page_position": {
      "row": 3,
      "column": 2
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 15,
    "name": "[SURVIVAL_ROOF_ENDLESS]",
    "name_token": "[SURVIVAL_ROOF_ENDLESS]",
    "definition_page": 2,
    "page_position": {
      "row": 3,
      "column": 3
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 16,
    "name": "[WAR_AND_PEAS]",
    "name_token": "[WAR_AND_PEAS]",
    "definition_page": 1,
    "page_position": {
      "row": 0,
      "column": 0
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 17,
    "name": "[WALL_NUT_BOWLING]",
    "name_token": "[WALL_NUT_BOWLING]",
    "definition_page": 1,
    "page_position": {
      "row": 0,
      "column": 1
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 18,
    "name": "[SLOT_MACHINE]",
    "name_token": "[SLOT_MACHINE]",
    "definition_page": 1,
    "page_position": {
      "row": 0,
      "column": 2
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 19,
    "name": "[ITS_RAINING_SEEDS]",
    "name_token": "[ITS_RAINING_SEEDS]",
    "definition_page": 1,
    "page_position": {
      "row": 0,
      "column": 3
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 20,
    "name": "[BEGHOULED]",
    "name_token": "[BEGHOULED]",
    "definition_page": 1,
    "page_position": {
      "row": 0,
      "column": 4
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 21,
    "name": "[INVISIGHOUL]",
    "name_token": "[INVISIGHOUL]",
    "definition_page": 1,
    "page_position": {
      "row": 1,
      "column": 0
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 22,
    "name": "[SEEING_STARS]",
    "name_token": "[SEEING_STARS]",
    "definition_page": 1,
    "page_position": {
      "row": 1,
      "column": 1
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 23,
    "name": "[ZOMBIQUARIUM]",
    "name_token": "[ZOMBIQUARIUM]",
    "definition_page": 1,
    "page_position": {
      "row": 1,
      "column": 2
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 24,
    "name": "[BEGHOULED_TWIST]",
    "name_token": "[BEGHOULED_TWIST]",
    "definition_page": 1,
    "page_position": {
      "row": 1,
      "column": 3
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 25,
    "name": "[LITTLE_TROUBLE]",
    "name_token": "[LITTLE_TROUBLE]",
    "definition_page": 1,
    "page_position": {
      "row": 1,
      "column": 4
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 26,
    "name": "[PORTAL_COMBAT]",
    "name_token": "[PORTAL_COMBAT]",
    "definition_page": 1,
    "page_position": {
      "row": 2,
      "column": 0
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 27,
    "name": "[COLUMN_AS_YOU_SEE_EM]",
    "name_token": "[COLUMN_AS_YOU_SEE_EM]",
    "definition_page": 1,
    "page_position": {
      "row": 2,
      "column": 1
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 28,
    "name": "[BOBSLED_BONANZA]",
    "name_token": "[BOBSLED_BONANZA]",
    "definition_page": 1,
    "page_position": {
      "row": 2,
      "column": 2
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 29,
    "name": "[ZOMBIES_ON_SPEED]",
    "name_token": "[ZOMBIES_ON_SPEED]",
    "definition_page": 1,
    "page_position": {
      "row": 2,
      "column": 3
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 30,
    "name": "[WHACK_A_ZOMBIE]",
    "name_token": "[WHACK_A_ZOMBIE]",
    "definition_page": 1,
    "page_position": {
      "row": 2,
      "column": 4
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 31,
    "name": "[LAST_STAND]",
    "name_token": "[LAST_STAND]",
    "definition_page": 1,
    "page_position": {
      "row": 3,
      "column": 0
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 32,
    "name": "[WAR_AND_PEAS_2]",
    "name_token": "[WAR_AND_PEAS_2]",
    "definition_page": 1,
    "page_position": {
      "row": 3,
      "column": 1
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 33,
    "name": "[WALL_NUT_BOWLING_EXTREME]",
    "name_token": "[WALL_NUT_BOWLING_EXTREME]",
    "definition_page": 1,
    "page_position": {
      "row": 3,
      "column": 2
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 34,
    "name": "[POGO_PARTY]",
    "name_token": "[POGO_PARTY]",
    "definition_page": 1,
    "page_position": {
      "row": 3,
      "column": 3
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 35,
    "name": "[FINAL_BOSS]",
    "name_token": "[FINAL_BOSS]",
    "definition_page": 1,
    "page_position": {
      "row": 3,
      "column": 4
    },
    "selector_button_id": 101,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 36,
    "name": "[ART_CHALLENGE_WALL_NUT]",
    "name_token": "[ART_CHALLENGE_WALL_NUT]",
    "definition_page": 2,
    "page_position": {
      "row": 0,
      "column": 0
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 37,
    "name": "[SUNNY_DAY]",
    "name_token": "[SUNNY_DAY]",
    "definition_page": 2,
    "page_position": {
      "row": 0,
      "column": 1
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 38,
    "name": "[UNSODDED]",
    "name_token": "[UNSODDED]",
    "definition_page": 2,
    "page_position": {
      "row": 0,
      "column": 2
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 39,
    "name": "[BIG_TIME]",
    "name_token": "[BIG_TIME]",
    "definition_page": 2,
    "page_position": {
      "row": 0,
      "column": 3
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 40,
    "name": "[ART_CHALLENGE_SUNFLOWER]",
    "name_token": "[ART_CHALLENGE_SUNFLOWER]",
    "definition_page": 2,
    "page_position": {
      "row": 0,
      "column": 4
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 41,
    "name": "[AIR_RAID]",
    "name_token": "[AIR_RAID]",
    "definition_page": 2,
    "page_position": {
      "row": 1,
      "column": 0
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 42,
    "name": "[ICE_LEVEL]",
    "name_token": "[ICE_LEVEL]",
    "definition_page": 2,
    "page_position": {
      "row": 1,
      "column": 1
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 43,
    "name": "[ZEN_GARDEN]",
    "name_token": "[ZEN_GARDEN]",
    "definition_page": 2,
    "page_position": {
      "row": 1,
      "column": 2
    },
    "selector_button_id": 109,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": true,
    "ai_win_verified": false
  },
  {
    "mode_id": 44,
    "name": "[HIGH_GRAVITY]",
    "name_token": "[HIGH_GRAVITY]",
    "definition_page": 2,
    "page_position": {
      "row": 1,
      "column": 3
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 45,
    "name": "[GRAVE_DANGER]",
    "name_token": "[GRAVE_DANGER]",
    "definition_page": 2,
    "page_position": {
      "row": 1,
      "column": 4
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 46,
    "name": "[CAN_YOU_DIG_IT]",
    "name_token": "[CAN_YOU_DIG_IT]",
    "definition_page": 2,
    "page_position": {
      "row": 2,
      "column": 0
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 47,
    "name": "[DARK_STORMY_NIGHT]",
    "name_token": "[DARK_STORMY_NIGHT]",
    "definition_page": 2,
    "page_position": {
      "row": 2,
      "column": 1
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 48,
    "name": "[BUNGEE_BLITZ]",
    "name_token": "[BUNGEE_BLITZ]",
    "definition_page": 2,
    "page_position": {
      "row": 2,
      "column": 2
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 49,
    "name": "[SQUIRREL]",
    "name_token": "[SQUIRREL]",
    "definition_page": 2,
    "page_position": {
      "row": 2,
      "column": 3
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 50,
    "name": "[TREE_OF_WISDOM]",
    "name_token": "[TREE_OF_WISDOM]",
    "definition_page": 2,
    "page_position": {
      "row": 2,
      "column": 4
    },
    "selector_button_id": null,
    "entry_action": "garden_next",
    "entry_implemented": true,
    "requires_owned_tree": true,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 51,
    "name": "[SCARY_POTTER_1]",
    "name_token": "[SCARY_POTTER_1]",
    "definition_page": 3,
    "page_position": {
      "row": 0,
      "column": 0
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 52,
    "name": "[SCARY_POTTER_2]",
    "name_token": "[SCARY_POTTER_2]",
    "definition_page": 3,
    "page_position": {
      "row": 0,
      "column": 1
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 53,
    "name": "[SCARY_POTTER_3]",
    "name_token": "[SCARY_POTTER_3]",
    "definition_page": 3,
    "page_position": {
      "row": 0,
      "column": 2
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 54,
    "name": "[SCARY_POTTER_4]",
    "name_token": "[SCARY_POTTER_4]",
    "definition_page": 3,
    "page_position": {
      "row": 0,
      "column": 3
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 55,
    "name": "[SCARY_POTTER_5]",
    "name_token": "[SCARY_POTTER_5]",
    "definition_page": 3,
    "page_position": {
      "row": 0,
      "column": 4
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 56,
    "name": "[SCARY_POTTER_6]",
    "name_token": "[SCARY_POTTER_6]",
    "definition_page": 3,
    "page_position": {
      "row": 1,
      "column": 0
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 57,
    "name": "[SCARY_POTTER_7]",
    "name_token": "[SCARY_POTTER_7]",
    "definition_page": 3,
    "page_position": {
      "row": 1,
      "column": 1
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 58,
    "name": "[SCARY_POTTER_8]",
    "name_token": "[SCARY_POTTER_8]",
    "definition_page": 3,
    "page_position": {
      "row": 1,
      "column": 2
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 59,
    "name": "[SCARY_POTTER_9]",
    "name_token": "[SCARY_POTTER_9]",
    "definition_page": 3,
    "page_position": {
      "row": 1,
      "column": 3
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 60,
    "name": "[SCARY_POTTER_ENDLESS]",
    "name_token": "[SCARY_POTTER_ENDLESS]",
    "definition_page": 3,
    "page_position": {
      "row": 1,
      "column": 4
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 61,
    "name": "[I_ZOMBIE_1]",
    "name_token": "[I_ZOMBIE_1]",
    "definition_page": 3,
    "page_position": {
      "row": 2,
      "column": 0
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 62,
    "name": "[I_ZOMBIE_2]",
    "name_token": "[I_ZOMBIE_2]",
    "definition_page": 3,
    "page_position": {
      "row": 2,
      "column": 1
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 63,
    "name": "[I_ZOMBIE_3]",
    "name_token": "[I_ZOMBIE_3]",
    "definition_page": 3,
    "page_position": {
      "row": 2,
      "column": 2
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 64,
    "name": "[I_ZOMBIE_4]",
    "name_token": "[I_ZOMBIE_4]",
    "definition_page": 3,
    "page_position": {
      "row": 2,
      "column": 3
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 65,
    "name": "[I_ZOMBIE_5]",
    "name_token": "[I_ZOMBIE_5]",
    "definition_page": 3,
    "page_position": {
      "row": 2,
      "column": 4
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 66,
    "name": "[I_ZOMBIE_6]",
    "name_token": "[I_ZOMBIE_6]",
    "definition_page": 3,
    "page_position": {
      "row": 3,
      "column": 0
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 67,
    "name": "[I_ZOMBIE_7]",
    "name_token": "[I_ZOMBIE_7]",
    "definition_page": 3,
    "page_position": {
      "row": 3,
      "column": 1
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 68,
    "name": "[I_ZOMBIE_8]",
    "name_token": "[I_ZOMBIE_8]",
    "definition_page": 3,
    "page_position": {
      "row": 3,
      "column": 2
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 69,
    "name": "[I_ZOMBIE_9]",
    "name_token": "[I_ZOMBIE_9]",
    "definition_page": 3,
    "page_position": {
      "row": 3,
      "column": 3
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 70,
    "name": "[I_ZOMBIE_ENDLESS]",
    "name_token": "[I_ZOMBIE_ENDLESS]",
    "definition_page": 3,
    "page_position": {
      "row": 3,
      "column": 4
    },
    "selector_button_id": 102,
    "entry_action": "enter_level",
    "entry_implemented": true,
    "requires_owned_tree": false,
    "hidden_selector_page": false,
    "requires_visible_enabled_button": true,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 71,
    "name": "[UPSELL]",
    "name_token": "[UPSELL]",
    "definition_page": 2,
    "page_position": {
      "row": 3,
      "column": 4
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  },
  {
    "mode_id": 72,
    "name": "[INTRO]",
    "name_token": "[INTRO]",
    "definition_page": 2,
    "page_position": {
      "row": 2,
      "column": 3
    },
    "selector_button_id": null,
    "entry_action": null,
    "entry_implemented": false,
    "requires_owned_tree": false,
    "hidden_selector_page": true,
    "requires_visible_enabled_button": false,
    "entry_live_verified": false,
    "ai_win_verified": false
  }
]
```

Python host 的 `Host._merge_levels()` 将 native 项按已知 mode ID 合并进 `src/pvz_ai/catalog.py` 的规范目录。host 静态名称、类别、隐藏标记和 action profile 保持权威；native 数据只有通过严格白名单/类型范围校验后，才补充 `entry_implemented`、`entry_action`、`selector_button_id`、定义页/位置、entry-live、AI-win、隐藏页、owned-tree 等验证元数据。未知/重复 mode ID 会被忽略。`unlocked` 仍为 `None`，不会从目录推断当前档案解锁状态；调用方应查看实时能力和当前页面按钮状态。

官方/玩法版本来源：本机 Steam 安装说明 `...\Plants Vs Zombies\readme.html`（Windows 1.2.0.1096）；[Steam 商店页](https://store.steampowered.com/app/3590/Plants_vs_Zombies_GOTY_Edition/)；EA 可访问的 [Mac 1.0.40 readme](https://akamai.cdn.ea.com/eadownloads/u/f/manuals/GAME-PVZ/en_US_readme.html) 仅作为相关玩法参考，不作为 Windows 1096 入口/偏移证据。
