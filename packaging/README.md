# Windows release packaging

`scripts/build-release.ps1` makes a Windows x64 onedir distribution and, when
Inno Setup is available, a per-user installer. The build uses the project
`.venv`, the native outputs from `scripts/build-native.ps1`, and an explicit
allowlist of user documentation, templates, and the `pvz-play` skill.

The installer defaults to `%LOCALAPPDATA%\Programs\PvZ AI Bridge` and creates
shortcuts in the current user's Start Menu. The application stores mutable
state under `%LOCALAPPDATA%\PvZ AI Bridge`, outside the install directory;
uninstall removes the application tree and leaves that state and any save
backups intact. The installer presents an English or 简体中文 language choice
and uses only the selected language in its own text and shortcuts.

The launcher asks the user to select a language before showing its matching
interface. The bundle contains an explicit allowlist of user-facing documents:
the root README, interface and validation guides; English and Chinese user
guides; AI guide; human operations guides; release notes; Chinese templates and
level catalog; entity layout; source references; and third-party notices.
Research notes and live-validation evidence are excluded.

Do not add game assets, dumps, saves, logs, API tokens, `.venv`, `.git`, or build
inputs to the release staging tree. After building, inspect the external
`release-manifest.json` and `package-tree.txt` beside the installer.

## Tools

- Python dependencies and PyInstaller are installed only in the project venv.
- `packaging/requirements-build.lock` records the exact package versions from
  the successful Python 3.14.4 x64 build environment. Recreate it with:

  ```powershell
  py -3.14 -m venv .venv
  .\.venv\Scripts\python.exe -m pip install -r packaging\requirements-build.lock
  ```

- Native builds use the Visual Studio CMake path configured by
  `scripts/build-native.ps1`.
- Inno Setup is a separate packaging tool and is never included in the app.
  Use Inno Setup 7.1.0 or compatible and pass `-InnoCompilerPath` for a local
  `ISCC.exe` when it is not found in the standard locations.
- Run `scripts\build-release.ps1` from PowerShell to rebuild native binaries,
  create the onedir executable and compile the per-user installer. The output
  is unsigned; no publisher-signing identity is configured.
- The script writes `release-manifest.json` and `package-tree.txt` beside the
  installer. Review those before distributing the package.
