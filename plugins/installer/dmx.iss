; Omega DMX installer for Windows (Inno Setup 6). Built by .github/workflows/dmx-release.yml:
;   iscc /DBuildDir=<folder with "Omega DMX.vst3" and "Omega DMX.exe"> /DAppVer=1.0.0 dmx.iss
#ifndef AppVer
  #define AppVer "1.0.0"
#endif
[Setup]
AppId={{9B2E6D41-7C3A-4F18-A5D2-3E8C1B0F7A64}
AppName=Omega DMX
AppVersion={#AppVer}
AppPublisher=The Omega System
DefaultDirName={autopf}\Omega DMX
DefaultGroupName=Omega DMX
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=Omega-DMX-Setup-Windows
OutputDir=.
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Omega DMX
DisableProgramGroupPage=yes

[Types]
Name: "full"; Description: "Plugin and the standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin: Omega DMX (instrument)"; Types: full custom
Name: "app"; Description: "Omega DMX standalone app"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for the standalone app"; Components: app

[Files]
Source: "{#BuildDir}\Omega DMX.vst3\*"; DestDir: "{commoncf64}\VST3\Omega DMX.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#BuildDir}\Omega DMX.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app

[Icons]
Name: "{group}\Omega DMX"; Filename: "{app}\Omega DMX.exe"; Components: app
Name: "{group}\Uninstall Omega DMX"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Omega DMX"; Filename: "{app}\Omega DMX.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Omega DMX.exe"; Description: "Open Omega DMX"; Flags: nowait postinstall skipifsilent; Components: app
