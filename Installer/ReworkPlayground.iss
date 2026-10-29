; HEM Rework Playground - Inno Setup 6 script
;
; Build:  iscc /DBuildDir="<path to ReworkPlayground_artefacts\Release>" /DAppVersion=0.1.0 Installer\ReworkPlayground.iss
;
; A separate app from Hybrid Studio: its own AppId, install folder, settings and file type, so the
; two never touch each other.
;
; Upgrade safety:
;   - AppId is fixed forever -> new versions install over the old one in place.
;   - Settings live in %APPDATA%\Hybrid Elite Mafia\Rework Playground.settings and user presets in
;     %APPDATA%\Hybrid Elite Mafia\Rework Playground\Presets (written by the app, never listed here,
;     so upgrades and uninstall leave them alone).
;   - Songs / exports live in Documents\Hybrid Elite Mafia\Rework Playground; created with
;     uninsneveruninstall so your work survives uninstall and upgrades.
;   - Faceplate art, fonts and icon are inside the exe; static MSVC runtime -> no VC++ redistributable.
;   - /DSoundPackDir=<folder> bundles the HEM Sound Pack (Band Forge's recorded instruments, built by
;     Tools/build_sound_pack.py) into {app}\Sounds\HEM Sound Pack, replaced on every upgrade.

#ifndef AppVersion
  #define AppVersion "0.6.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\ReworkPlayground_artefacts\Release"
#endif

#define AppName      "HEM Rework Playground"
#define AppPublisher "Hybrid Elite Mafia"
#define AppExe       "HEM Rework Playground.exe"
#define ProgId       "HybridEliteMafia.HemPlaySession"

[Setup]
AppId={{D191F83F-FC9D-457D-9619-62DA702D0FB6}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppPublisher}
UsePreviousAppDir=yes
UsePreviousGroup=yes
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=HEM-Rework-Playground-Setup-{#AppVersion}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
SetupIconFile=HEM-Playground.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
VersionInfoVersion={#AppVersion}
VersionInfoCopyright=Copyright (c) 2026 Steven Reliford / Hybrid Elite Mafia
AppCopyright=Copyright (c) 2026 Steven Reliford / Hybrid Elite Mafia. Free software under the GNU GPL v3.
LicenseFile=EULA.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "fileassoc"; Description: "Open .hemplay session files with {#AppName}"; GroupDescription: "File associations:"

[Dirs]
Name: "{userdocs}\Hybrid Elite Mafia\Rework Playground"; Flags: uninsneveruninstall
Name: "{userdocs}\Hybrid Elite Mafia\Rework Playground\Exports"; Flags: uninsneveruninstall
Name: "{userdocs}\Hybrid Elite Mafia\Rework Playground\Song Forge"; Flags: uninsneveruninstall
Name: "{userdocs}\Hybrid Elite Mafia\Rework Playground\Band Forge"; Flags: uninsneveruninstall
Name: "{userdocs}\Hybrid Elite Mafia\Rework Playground\Library"; Flags: uninsneveruninstall

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "EULA.txt"; DestDir: "{app}"; DestName: "License.txt"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "GPL-3.0.txt"; Flags: ignoreversion
#ifdef SoundPackDir
Source: "{#SoundPackDir}\*"; DestDir: "{app}\Sounds\HEM Sound Pack"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; a pack from an older version is replaced as a whole (no stale samples left behind)
Type: filesandordirs; Name: "{app}\Sounds\HEM Sound Pack"
#endif

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\{#AppName} License"; Filename: "{app}\License.txt"
Name: "{group}\Rework Playground Songs"; Filename: "{userdocs}\Hybrid Elite Mafia\Rework Playground"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.hemplay"; ValueType: string; ValueName: ""; ValueData: "{#ProgId}"; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "Rework Playground Session"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: fileassoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
