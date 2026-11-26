; RX5 installer for Windows (Inno Setup 6). Built by .github/workflows/five-release.yml:
;   iscc /DBuildDir=<folder with the built RX5.vst3 bundle and RX5.exe> /DAppVer=1.0.0 five.iss
; (not Ver or Src: Ver is the preprocessor's own version number and overriding it breaks its built-ins)
#ifndef AppVer
  #define AppVer "1.0.0"
#endif
[Setup]
AppId={{25BB120A-6D96-4CE1-BA69-58BC55ADA851}
AppName=RX5
AppVersion={#AppVer}
AppPublisher=The Omega System
DefaultDirName={autopf}\RX5
DefaultGroupName=RX5
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=RX5-Setup-Windows
OutputDir=.
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=RX5
DisableProgramGroupPage=yes

[InstallDelete]
; the plugin was called Omega Five before: remove the old bundle (same plugin ID, a DAW would list it twice) and app
Type: filesandordirs; Name: "{commoncf64}\VST3\Omega Five.vst3"
Type: files; Name: "{app}\Omega Five.exe"
Type: files; Name: "{group}\Omega Five.lnk"
Type: files; Name: "{group}\Uninstall Omega Five.lnk"
Type: files; Name: "{autodesktop}\Omega Five.lnk"

[Types]
Name: "full"; Description: "Plugin and the standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin: RX5 (drum machine)"; Types: full custom
Name: "app"; Description: "RX5 standalone app"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for the standalone app"; Components: app

[Files]
Source: "{#BuildDir}\RX5.vst3\*"; DestDir: "{commoncf64}\VST3\RX5.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#BuildDir}\RX5.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app

[Icons]
Name: "{group}\RX5"; Filename: "{app}\RX5.exe"; Components: app
Name: "{group}\Uninstall RX5"; Filename: "{uninstallexe}"
Name: "{autodesktop}\RX5"; Filename: "{app}\RX5.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\RX5.exe"; Description: "Open RX5"; Flags: nowait postinstall skipifsilent; Components: app
