; Omega Twelve installer for Windows (Inno Setup 6). Built by .github/workflows/twelve-release.yml:
;   iscc /DSrc=<folder with the built .vst3 bundles and Omega Twelve.exe> /DVer=1.0.0 twelve.iss
#ifndef Ver
  #define Ver "1.0.0"
#endif
[Setup]
AppId={{6F1C2A9E-3B7D-4E21-9C55-0D1E7A4B12F0}
AppName=Omega Twelve
AppVersion={#Ver}
AppPublisher=The Omega System
DefaultDirName={autopf}\Omega Twelve
DefaultGroupName=Omega Twelve
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=Omega-Twelve-Setup-Windows
OutputDir=.
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Omega Twelve
DisableProgramGroupPage=yes

[Types]
Name: "full"; Description: "Plugins and the standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugins: Omega Twelve (effect) and Omega Twelve Instrument"; Types: full custom
Name: "app"; Description: "Omega Twelve standalone app"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for the standalone app"; Components: app

[Files]
Source: "{#Src}\Omega Twelve.vst3\*"; DestDir: "{commoncf64}\VST3\Omega Twelve.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#Src}\Omega Twelve Instrument.vst3\*"; DestDir: "{commoncf64}\VST3\Omega Twelve Instrument.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#Src}\Omega Twelve.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app

[Icons]
Name: "{group}\Omega Twelve"; Filename: "{app}\Omega Twelve.exe"; Components: app
Name: "{group}\Uninstall Omega Twelve"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Omega Twelve"; Filename: "{app}\Omega Twelve.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Omega Twelve.exe"; Description: "Open Omega Twelve"; Flags: nowait postinstall skipifsilent; Components: app
