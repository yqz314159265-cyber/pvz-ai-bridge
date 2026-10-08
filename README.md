# PvZ AI / 植物大战僵尸 AI 桥接器

[Download v0.1.0 / 下载首版安装器](https://github.com/yqz314159265-cyber/pvz-ai-bridge/releases/tag/v0.1.0)

## English

PvZ AI v0.1.0 is a Windows control-bridge preview for Steam Plants vs. Zombies GOTY 1.2.0.1096 (x86). The per-user installer bundles the runtime and exposes a desktop launcher, CLI, loopback HTTP API, and stdio MCP commands. On first launch, choose Simplified Chinese or English; the UI then uses one language at a time. The game must be installed separately through Steam and run in a visible desktop session.

This is a constrained control bridge, **not a complete autonomous player**. Only native capabilities advertised for the current state may be used. Many minigame actions are unverified, and no all-mode automatic-win strategy is included. The screenshot/overlay callback calling convention was corrected, and a software-rendering compatibility workaround is enabled and persists across restart. A 180-second observation recorded 174 health reads, no failures, and a maximum latency of 84.9 ms. Longer-term stability remains untested; this does not establish the root cause of the earlier hang.

Start here:

- [English setup and usage](docs/README.en.md)
- [AI usage guide (English and 中文)](docs/AI_GUIDE.md)
- [Release notes (English and 中文)](docs/RELEASE_NOTES.md)
- [Human operation matrix — English](docs/HUMAN_OPERATIONS.en.md)
- [Human operation matrix — 中文](docs/HUMAN_OPERATIONS.zh-CN.md)

## 中文

PvZ AI v0.1.0 是面向 Steam《植物大战僵尸 GOTY》Windows 1.2.0.1096（x86）的本地控制桥接预览。per-user 安装器内含运行时，并提供桌面启动器、CLI、仅本机 HTTP API 和 stdio MCP。首次启动先选择简体中文或 English，之后界面只显示所选语言。游戏需另外通过 Steam 安装，并在可见桌面会话中运行。

这是受限的控制桥接，**不是完整自动玩家**。只能使用当前状态下 native 明确公布的能力；许多小游戏操作仍未验证，也没有覆盖所有模式的自动获胜策略。截图/覆盖层回调调用约定已修正，并启用了重启后仍保留的软件渲染兼容方案。180 秒观察期间完成 174 次 health 读取，未见失败，最高延迟为 84.9 ms。长期稳定性仍未验证；这并不能证明此前挂起的根因。

从这里开始：

- [中文安装与使用](docs/README.zh-CN.md)
- [AI 使用指南（English + 中文）](docs/AI_GUIDE.md)
- [发布说明（English + 中文）](docs/RELEASE_NOTES.md)
- [普通玩家操作矩阵（中文）](docs/HUMAN_OPERATIONS.zh-CN.md)
- [普通玩家操作矩阵（English）](docs/HUMAN_OPERATIONS.en.md)

## License / 许可证

A license for the project's original code has not yet been selected. Public
availability does not add a license grant. Third-party components retain their
own licenses; see [notices](THIRD_PARTY_NOTICES.md).

项目原创代码的许可证尚未选定，公开展示不额外授予许可；第三方组件沿用各自许可证，
见[第三方声明](THIRD_PARTY_NOTICES.md)。
