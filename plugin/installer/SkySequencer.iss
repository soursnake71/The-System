; Windows installer for Sky Sequencer (Inno Setup 6). Built by .github/workflows/sky-plugin.yml:
;   iscc /DAppVersion=1.0.0 /DBuildDir=<cmake build dir> plugin\installer\SkySequencer.iss
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build"
#endif
#define Art BuildDir + "\SkySequencer_artefacts\Release"

[Setup]
AppId={{6C3E2A91-5B7D-4F0E-9A1C-5B1E7D2F8A40}
AppName=Sky Sequencer
AppVersion={#AppVersion}
AppPublisher=The Snake Device
AppPublisherURL=https://github.com/soursnake71/The-System
DefaultDirName={autopf}\Sky Sequencer
DefaultGroupName=Sky Sequencer
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\..\dist
OutputBaseFilename=SkySequencer-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Sky Sequencer
CloseApplications=yes

[Types]
Name: "full"; Description: "VST3 plugin and standalone app"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (installs to Common Files\VST3, where every DAW looks)"; Types: full custom; Flags: fixed
Name: "app"; Description: "Standalone app (runs without a DAW)"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for the standalone app"; Components: app; Flags: unchecked

[Files]
Source: "{#Art}\VST3\Sky Sequencer.vst3\*"; DestDir: "{commoncf64}\VST3\Sky Sequencer.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Art}\Standalone\Sky Sequencer.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\Sky Sequencer"; Filename: "{app}\Sky Sequencer.exe"; Components: app
Name: "{group}\Uninstall Sky Sequencer"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Sky Sequencer"; Filename: "{app}\Sky Sequencer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Sky Sequencer.exe"; Description: "Open the standalone app now"; Components: app; Flags: nowait postinstall skipifsilent unchecked

[Messages]
FinishedLabel=Sky Sequencer is installed.%n%nIn your DAW, rescan plugins and load "Sky Sequencer" as an instrument on a track.
