; Omega CMI installer for Windows (Inno Setup 6). Built by .github/workflows/cmi-release.yml:
;   iscc /DBuildDir=<folder with Omega CMI.vst3 and Omega CMI.exe> /DAppVer=1.0.0 cmi.iss
#ifndef AppVer
  #define AppVer "1.0.0"
#endif
[Setup]
AppId={{B3E7C1D4-58A2-4F0E-9A6B-7C2D1E4F8A90}
AppName=Omega CMI
AppVersion={#AppVer}
AppPublisher=The Omega System
DefaultDirName={autopf}\Omega CMI
DefaultGroupName=Omega CMI
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=Omega-CMI-Setup-Windows
OutputDir=.
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Omega CMI
DisableProgramGroupPage=yes

[Types]
Name: "full"; Description: "The VST3 instrument and the standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Omega CMI VST3 instrument (into Common Files\VST3, where every DAW looks)"; Types: full custom
Name: "app"; Description: "Omega CMI standalone app"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for the standalone app"; Components: app

[Files]
Source: "{#BuildDir}\Omega CMI.vst3\*"; DestDir: "{commoncf64}\VST3\Omega CMI.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#BuildDir}\Omega CMI.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: app

[Icons]
Name: "{group}\Omega CMI"; Filename: "{app}\Omega CMI.exe"; Components: app
Name: "{group}\Uninstall Omega CMI"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Omega CMI"; Filename: "{app}\Omega CMI.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Omega CMI.exe"; Description: "Open Omega CMI"; Flags: nowait postinstall skipifsilent; Components: app
