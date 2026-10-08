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
