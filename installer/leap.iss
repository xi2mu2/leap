; ============================================================
;  leap 思维导图 —— Inno Setup 安装脚本
;  用法：先运行根目录的 build_release.bat 生成 release\leap-v<版本>-win64\，
;        再用 ISCC.exe 编译本文件；build_release.bat 会自动调用。
;  说明：免管理员（PrivilegesRequired=lowest），默认装到
;        %LOCALAPPDATA%\Programs\leap，用户可改路径。
; ============================================================

#define AppName "leap 思维导图"
#define AppShortName "leap"
#define AppVersion "2.14.10"
#define AppPublisher "刘羽林"
#define AppURL "https://github.com/xi2mu2/leap"
#define AppExeName "leap.exe"

[Setup]
AppId={{6F1C2B34-9A7E-4D52-B0C8-3E5A7D914F60}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={localappdata}\Programs\{#AppShortName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=no
PrivilegesRequired=lowest
OutputDir=..\release
OutputBaseFilename=leap-{#AppVersion}-setup
SetupIconFile=..\leap.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName} {#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; 大图仅在 classic 风格下显示；modern 风格使用右上角小图
WizardImageFile=assets\wizard-large.bmp
WizardSmallImageFile=assets\wizard-small.bmp
WizardImageStretch=yes
LicenseFile=..\LICENSE
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} 安装程序
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoCopyright=Copyright (C) {#AppPublisher}  |  GNU GPL v3.0

[Languages]
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\release\leap-v{#AppVersion}-win64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
