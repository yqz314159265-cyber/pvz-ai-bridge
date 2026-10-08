# PvZ AI 本地桥接器

PvZ AI v0.1.0 是面向早期使用者的 Windows 本地桥接预览。它把少量经过版本与状态检查的游戏操作，提供给桌面启动器、命令行、HTTP API 和 MCP 客户端。它是**控制桥接，不是完整的自动玩家**。

## 安装与支持范围

使用随发行版提供的 per-user 安装程序。它不需要另外安装 Python 或 C++ 编译器；游戏本身需由 Steam 单独安装。首次启动桌面启动器时先选择简体中文或 English，之后界面以所选的一种语言显示。游戏路径会优先从 Steam 库自动发现；如需指定，可设置 `PVZ_AI_GAME` 为确切的 `PlantsVsZombies.exe` 路径。

当前兼容目标是 Steam《植物大战僵尸 GOTY Edition》Windows 1.2.0.1096、x86。请在可见的 Windows 桌面会话中运行游戏。桥接调用游戏自己的处理函数和控件，不发送操作系统鼠标或键盘消息。其他版本或发行渠道未经确认。

**稳定性状态：** 截图/覆盖层回调调用约定已修正，并启用了重启后仍保留的软件渲染兼容方案。180 秒观察完成 174 次 health 读取，未见失败，最高延迟为 84.9 ms。长期稳定性仍未验证，也未证明此前挂起的根因；请先备份，使用可恢复的测试档案，并留意游戏窗口。

## 启动器与命令行

安装包提供 `pvz-ai.exe`，所有命令均可在 PowerShell 或 Windows Terminal 中调用：

```powershell
pvz-ai.exe launcher
pvz-ai.exe doctor
pvz-ai.exe health --pid <已接入游戏PID>
pvz-ai.exe http
pvz-ai.exe mcp
pvz-ai.exe guide
pvz-ai.exe guide --init [--path <文件>]
pvz-ai.exe install-skill [--destination <目录>]
```

- `launcher` 打开桌面启动器，供用户查看连接状态并选择操作；首次运行先选界面语言，此后界面保持单一语言。
- `doctor` 做只读的游戏路径、版本和原生文件检查；已接入游戏后，`health --pid <PID>` 发起新检查并报告响应情况与延迟。也可对 HTTP 主机请求 `GET /health`；未接入时裸用 CLI `health` 会报告未接入。
- `http` 启动本机 HTTP JSON 服务；主机启动时创建受当前用户保护的 Bearer token。启动器可复制连接到现有 HTTP 主机的 MCP 配置：`pvz-ai.exe mcp --api-url http://127.0.0.1:8765 --token-file <数据目录>\local-api\token`。普通 `pvz-ai.exe mcp` 是独立主机模式；同一游戏只连接一个控制主机，不要同时启动两种模式。
- `guide [--init] [--path <文件>]` 读取指南；`--init` 只在目标文件不存在时创建空白双语模板，不覆盖已有文件。
- `install-skill [--destination <目录>]` 安装 PvZ Play skill；遇到已有目标文件会拒绝覆盖。启动器也有显式安装按钮。此操作不会自动修改 Codex 或其他客户端配置。

AI 客户端也可通过需要 Bearer token 的 HTTP `GET /guide` 或本机 MCP `strategy_guide` 读取该指南。可编辑文件默认位于 `%LOCALAPPDATA%\PvZ AI Bridge\strategy.md`；`PVZ_AI_STRATEGY_FILE` 可指定其他位置。空模板只是个人填写表格，不会替用户编造策略。PvZ Play skill 应读取用户指南、遵守当前原生能力，并如实报告观察结果。

## 开发者：从源码构建

使用源码而不是发行安装器时，需要 Windows、Python 3.11+、Visual Studio C++ 工具和 CMake。项目根目录下执行 `py -3.11 -m venv .venv`、`.\.venv\Scripts\python.exe -m pip install -e ".[dev]"`，再运行 `scripts/build-native.ps1`。构建脚本输出 32 位 Release DLL 和 injector；该步骤只供开发者，普通安装用户无需 Python 或编译器。

## 数据、API 与备份

应用状态和用户指南保存在 `%LOCALAPPDATA%\PvZ AI Bridge`。游戏自动从 Steam 库发现；`PVZ_AI_GAME` 是可选的确切游戏程序覆盖值。

HTTP API 只监听回环地址（`127.0.0.1`）；运行中的 HTTP 主机会在 `%LOCALAPPDATA%\PvZ AI Bridge\local-api\token` 创建并保护 Bearer token，安装器本身不创建 token。只读 `GET /guide` 也需要 token，因为策略指南可能包含用户的私人批注；`GET /health` 用于实际响应检查。不要把 token 放进聊天、模型提示、截图、日志或共享配置。GET 状态/目录/指南接口只读；动作、连接、暂停和停止属于受保护的操作。

开始前请通过启动器提供的备份功能复制存档并生成 SHA-256 清单，备份应放在独立位置而不是游戏存档目录。桥接器不自动恢复存档；恢复结果不属于已验证功能。桥接动作遵循游戏库存、阳光、冷却、解锁与当前场景等正常检查。

## 能做什么，不能据此推断什么

实测覆盖了部分普通关卡、菜单、选项、图鉴、成就、档案、商店、花园和 Zombatar 操作。各操作的“源码存在”“已在该构建实测”和“仍未验证”有明确区别，详见[普通玩家操作矩阵](HUMAN_OPERATIONS.zh-CN.md)及其[English version](HUMAN_OPERATIONS.en.md)。关卡目录包含定义信息，但不代表已解锁、可进入或 AI 能获胜。

模板、AI 客户端或模型只能提出受限的动作建议。动作被接受不等于执行完成，状态变化也不自动等于胜利。目前没有所有模式的自动通关策略；不少小游戏操作尚未逐项验证。不要把演示模板当成保证获胜的方法。

## 卸载

从 Windows“已安装的应用”中卸载 per-user 安装的 PvZ AI。卸载前先关闭启动器和 HTTP/MCP 主机，并让游戏正常退出，以便释放仍加载在游戏进程中的 DLL。卸载不会删除 Steam 游戏或游戏存档。`%LOCALAPPDATA%\PvZ AI Bridge` 中的用户策略指南和用户数据可能保留；若要清除，请先备份并自行检查该目录，再手动删除。

## 进一步阅读

- [English setup and usage](README.en.md)
- [AI 使用指南 / AI guide](AI_GUIDE.md)
- [发布说明 / Release notes](RELEASE_NOTES.md)
- [验证摘要](../VALIDATION.md)
- [协议契约](../INTERFACE.md)
