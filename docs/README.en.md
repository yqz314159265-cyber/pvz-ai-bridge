# PvZ AI Bridge — Setup and Use

PvZ AI v0.1.1 (unreleased working copy) is an early Windows local-bridge preview. It exposes a limited set of version- and state-checked game actions through a desktop launcher, CLI, HTTP API, and MCP client. It is a **control bridge, not a complete autonomous player**.

## Install and supported build

Use the per-user installer distributed with the release. It bundles the runtime and does not require a separate Python or C++ compiler installation. Plants vs. Zombies itself must be installed separately through Steam. On first launch, choose Simplified Chinese or English; the launcher then displays one language at a time. Open PvZ AI from the Windows Start menu. The bridge discovers the Steam library automatically. If needed, set `PVZ_AI_GAME` to the exact `PlantsVsZombies.exe` path.

The launcher remembers its language in the user data directory's `language.json`; the installer's language choice is separate. To choose again, close the launcher, remove that preference file, and reopen it. Missing or invalid preferences bring back the picker.

The supported target is Steam Plants vs. Zombies GOTY Edition for Windows, version 1.2.0.1096, x86. Run it in a visible Windows desktop session. The bridge calls game callbacks and controls on the game thread; it does not synthesize OS mouse or keyboard input. Other builds and distributions are unverified.

**Stability status:** the screenshot/overlay callback calling convention was corrected, and a software-rendering compatibility workaround is enabled and persists across restart. A 180-second observation recorded 174 health reads, no failures, and a maximum latency of 84.9 ms. Long-term stability remains untested, and this does not prove the cause of the earlier hang. Back up saves, use a recoverable test profile, and keep an eye on the game window.

## Launcher and commands

The installer provides `pvz-ai.exe`; commands can be run from PowerShell or Windows Terminal:

```powershell
pvz-ai.exe launcher
pvz-ai.exe doctor
pvz-ai.exe health --pid <PID>
pvz-ai.exe http
pvz-ai.exe mcp
pvz-ai.exe guide
pvz-ai.exe guide --init [--path PATH]
pvz-ai.exe install-skill [--destination PATH]
```

- `launcher` opens the desktop launcher for connection status and user-selected operations. The first run asks for Simplified Chinese or English; the UI uses the selected language.
- `doctor` performs read-only checks of the game path, version, and native files. For an already attached game, `health --pid <PID>` makes a fresh request and reports responsiveness and latency. You can also request HTTP `GET /health`; the CLI reports “not attached” when used without an attached process.
- `http` starts the local HTTP JSON service and creates a current-user-protected Bearer token. The launcher can copy an MCP configuration that connects to this existing host: `pvz-ai.exe mcp --api-url http://127.0.0.1:8765 --token-file <data-directory>\local-api\token`. Plain `pvz-ai.exe mcp` runs a standalone host; do not run both modes against the same game because only one control host can attach.
- `guide [--init] [--path PATH]` reads the guide. `--init` creates a blank bilingual scaffold only when the target file is absent; it never overwrites an existing file.
- `install-skill [--destination PATH]` installs the PvZ Play skill and refuses to overwrite existing target files. The launcher also has an explicit install button. This does not automatically edit Codex or other client configuration.

An AI client can also read the guide with authenticated HTTP `GET /guide` (Bearer token required even though it is read-only) or the local MCP `strategy_guide` tool. The editable guide defaults to `%LOCALAPPDATA%\PvZ AI Bridge\strategy.md`; `PVZ_AI_STRATEGY_FILE` overrides that location. The blank scaffold is a form for the user, not a strategy generated on the user's behalf. The PvZ Play skill should follow the user's guide, respect current native capabilities, and report observed results without overclaiming.

## Developers: build from source

If using a source checkout instead of the release installer, use Windows, Python 3.11+, Visual Studio C++ tools, and CMake. From the project root, create a virtual environment with `py -3.11 -m venv .venv`, install development dependencies with `.\.venv\Scripts\python.exe -m pip install -e ".[dev]"`, and run `scripts/build-native.ps1`. The script builds the 32-bit Release DLL and injector. This is only for developers; installer users do not need Python or a compiler.

## Data, API, and backups

Application state and the user guide live under `%LOCALAPPDATA%\PvZ AI Bridge`. Steam game discovery is automatic; `PVZ_AI_GAME` is an optional exact executable override.

If a packaged install reports `LoadLibraryW failed` while loading the bridge, one observed case had an encrypted staged DLL; using a separate data folder allowed loading. This does not establish encryption as the cause in every failure. If you encounter it, you can set `PVZ_AI_DATA_DIR` for this launch to a folder you have confirmed is writable and not encrypted:

```powershell
$env:PVZ_AI_DATA_DIR = Join-Path $env:USERPROFILE 'Documents\PvZ-AI-Data'
& "${env:LOCALAPPDATA}\Programs\PvZ AI Bridge\pvz-ai.exe" launcher
```

This changes where this process stores the guide, API token, and backups; it does not move or remove data in the original folder. The normal language picker still appears when the launcher starts.

The HTTP API binds only to loopback (`127.0.0.1`). The running HTTP host creates the Bearer token at `%LOCALAPPDATA%\PvZ AI Bridge\local-api\token` and protects it for the current user; the installer does not create it. Read-only `GET /guide` also requires the token because the guide can contain private user notes. `GET /health` performs an actual responsiveness check. Never put the token in chat, model prompts, screenshots, logs, or shared configuration. GET state/catalog/guide routes are read-only; actions, connection, pause, and stop are protected.

Before experiments, use the launcher backup function to copy saves and create a SHA-256 manifest. Keep backups outside the game save directory. Automatic restore is not provided, and restore results are not verified. Game actions remain subject to normal inventory, sun, cooldown, unlock, and current-scene checks.

## Coverage and limits

Some ordinary gameplay, menu, options, almanac, achievements, profile, shop, garden, and Zombatar operations have been tested. The [human operation matrix](HUMAN_OPERATIONS.en.md) distinguishes source implementation, live-tested behavior, and gaps. A listed mode does not prove it is unlocked, enterable, or winnable by AI.

Templates, AI clients, and models can only propose bounded actions. Acceptance does not prove execution, and a state change does not automatically prove a win. There is no strategy that completes all modes automatically; many minigame interactions have not been individually verified. Example templates are not guaranteed winning plans.

## Uninstall

Uninstall PvZ AI from Windows **Installed apps**. Close the launcher and HTTP/MCP host first, then exit the game normally so it releases the DLL still loaded in its process. Uninstalling the bridge does not remove the Steam game or saves. User data and the strategy guide under `%LOCALAPPDATA%\PvZ AI Bridge` may remain; back them up and inspect that folder before manually deleting it.

## More

- [中文安装与使用说明](README.zh-CN.md)
- [AI guide](AI_GUIDE.md)
- [Release notes](RELEASE_NOTES.md)
- [Validation summary](../VALIDATION.md)
- [Interface contract](../INTERFACE.md)
