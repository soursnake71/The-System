; Omega Five installer for Windows (Inno Setup 6). Built by .github/workflows/five-release.yml:
;   iscc /DBuildDir=<folder with the built Omega Five.vst3 bundle and Omega Five.exe> /DAppVer=1.0.0 five.iss
; (not Ver or Src: Ver is the preprocessor's own version number and overriding it breaks its built-ins)
#ifndef AppVer
  #define AppVer "1.0.0"
#endif
[Setup]
AppId={{25BB120A-6D96-4CE1-BA69-58BC55ADA851}
AppName=Omega Five
AppVersion={#AppVer}
AppPublisher=The Omega System
DefaultDirName={autopf}\Omega Five
DefaultGroupName=Omega Five
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=Omega-Five-Setup-Windows
OutputDir=.
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Omega Five
DisableProgramGroupPage=yes

[Types]
Name: "full"; Description: "Plugin and the standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin: Omega Five (drum machine)"; Types: full custom
Name: "app"; Description: "Omega Five standalone app"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for the standalone app"; Components: app

[Files]
Source: "{#BuildDir}\Omega Five.vst3\*"; DestDir: "{commoncf64}\VST3\Omega Five.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#BuildDir}\Omega Five.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app

[Icons]
Name: "{group}\Omega Five"; Filename: "{app}\Omega Five.exe"; Components: app
Name: "{group}\Uninstall Omega Five"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Omega Five"; Filename: "{app}\Omega Five.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Omega Five.exe"; Description: "Open Omega Five"; Flags: nowait postinstall skipifsilent; Components: app
