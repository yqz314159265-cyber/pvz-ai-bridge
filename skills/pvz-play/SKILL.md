---
name: pvz-play
description: Use the user's Plants vs. Zombies strategy guide with the local bridge to inspect state and narrate supported play; use when the user asks to play by their own tactics. 使用用户的《植物大战僵尸》策略指南和本地桥接工具查看状态并说明受支持的操作；适用于用户要求按其自定战术游玩。
---

# PvZ play
# 《植物大战僵尸》游玩

Use this skill when the user asks for help playing Plants vs. Zombies through the local bridge and wants their own tactics followed.
当用户希望通过本地桥接工具游玩《植物大战僵尸》，并要求遵循其自定战术时，使用本技能。

Read current game state and advertised capabilities before proposing or sending actions. Read the user's strategy guide through the bridge's guide interface when available; the default is `%LOCALAPPDATA%/PvZ AI Bridge/strategy.md`, with `PVZ_AI_STRATEGY_FILE` as an override. The bundled scaffold is intentionally unfilled. If `filled` is false, ask the user to provide or complete their tactics before presenting any strategy as theirs. Never fill in expert tactics on the user's behalf.
在提出或发送操作前，先读取当前游戏状态和桥接工具公布的能力。若桥接接口可用，通过该接口读取用户的策略指南；默认路径为 `%LOCALAPPDATA%/PvZ AI Bridge/strategy.md`，也可用 `PVZ_AI_STRATEGY_FILE` 覆盖。内置模板有意保持未填写状态。若 `filled` 为 false，应先请用户提供或补全战术，再把任何策略称为用户自己的策略。不要代替用户编写专家战术。

Follow the game's current state, the bridge's advertised capabilities, and its ordinary action rules. Explain the intended action and reason before sending a gameplay action, then report the observed result without claiming more than the returned state establishes. Do not claim to have watched or learned tactics from videos. The guide contains only the user's own instructions; do not infer that it verifies a tactic or guarantees success.
遵循游戏当前状态、桥接工具公布的能力和正常操作规则。发送游玩操作前，说明准备做什么及原因；之后依据实际返回状态报告结果，不夸大证据。不要声称看过视频或从视频中学到战术。指南只记录用户自己的指令；不要推断它验证了某种战术，也不要保证必然成功。

The bundled fill-in scaffold is at [assets/strategy-template.md](assets/strategy-template.md). It covers mode, card lineup, layout, timing, shovel rules, endless levels, and prohibitions. Keep the user's existing guide intact; initialize it only through an explicit guide-creation command when one is available.
内置填写模板位于 [assets/strategy-template.md](assets/strategy-template.md)，涵盖游戏模式、卡片阵容、布局、时机、铲除规则、无尽关卡和禁止事项。保留用户已有指南；只有在用户明确调用指南创建命令时，才初始化缺失的文件。
