#define ProductVersion "0.1.1"
#define ProductExe "pvz-ai.exe"
#define StageDir "..\build\release\dist\pvz-ai"
#define ReleaseDir "..\build\release"

[Setup]
AppId={{A017AF5C-C864-4B18-AE72-95CE185FD0E2}
AppName={cm:AppName}
AppVersion={#ProductVersion}
AppVerName={cm:AppName} {#ProductVersion}
AppPublisher=PvZ AI Bridge
VersionInfoDescription=PvZ AI Bridge Installer
VersionInfoProductName=PvZ AI Bridge
DefaultDirName={localappdata}\Programs\PvZ AI Bridge
DefaultGroupName={cm:ProgramGroup}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
Uninstallable=yes
UninstallDisplayName={cm:AppName}
OutputDir={#ReleaseDir}
OutputBaseFilename=pvz-ai-setup-{#ProductVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupLogging=yes
CloseApplications=no
ChangesAssociations=no
CreateUninstallRegKey=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[CustomMessages]
english.AppName=PvZ AI Bridge
chinesesimplified.AppName=植物大战僵尸 AI 桥接
english.ProgramGroup=PvZ AI Bridge
chinesesimplified.ProgramGroup=植物大战僵尸 AI 桥接
english.LauncherLink=Open control panel
chinesesimplified.LauncherLink=打开控制面板
english.ReadmeLink=English guide
chinesesimplified.ReadmeLink=中文使用指南
english.CliLink=Command-line guide
chinesesimplified.CliLink=命令行指南
english.UninstallLink=Uninstall PvZ AI Bridge
chinesesimplified.UninstallLink=卸载植物大战僵尸 AI 桥接

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{cm:LauncherLink}"; Filename: "{app}\{#ProductExe}"; Parameters: "launcher"; WorkingDir: "{app}"
Name: "{autoprograms}\{cm:ReadmeLink}"; Filename: "{app}\docs\README.en.md"; Languages: english
Name: "{autoprograms}\{cm:ReadmeLink}"; Filename: "{app}\docs\README.zh-CN.md"; Languages: chinesesimplified
Name: "{autoprograms}\{cm:CliLink}"; Filename: "{app}\docs\AI_GUIDE.md"
Name: "{autoprograms}\{cm:UninstallLink}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\{#ProductExe}"; Parameters: "launcher"; Description: "{cm:LauncherLink}"; Flags: postinstall nowait skipifsilent
