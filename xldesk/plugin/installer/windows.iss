; XL-Desk installer for Windows (Inno Setup 6). Built by CI:  iscc /DVERSION=1.1.0 /DSRC=<artefacts dir> windows.iss
#ifndef VERSION
  #define VERSION "1.1.0"
#endif
#ifndef SRC
  #define SRC "..\..\..\build\XLDesk_artefacts\Release"
#endif
[Setup]
AppId={{5E3C1F7A-8B2D-4C1E-9F3A-XLDESK000001}
AppName=XL-Desk
AppVersion={#VERSION}
AppPublisher=Absolute Future Studios
AppPublisherURL=https://soursnake71.github.io/The-System/xldesk/
DefaultDirName={autopf}\Absolute Future Studios\XL-Desk
DefaultGroupName=XL-Desk
DisableProgramGroupPage=yes
OutputBaseFilename=XL-Desk-{#VERSION}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayName=XL-Desk

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full custom
#ifdef AAX
Name: "aax"; Description: "AAX plugin (Pro Tools)"; Types: full custom
#endif

[Files]
Source: "{#SRC}\VST3\XL-Desk.vst3\*"; DestDir: "{commoncf64}\VST3\XL-Desk.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SRC}\Standalone\XL-Desk.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
#ifdef AAX
Source: "{#SRC}\AAX\XL-Desk.aaxplugin\*"; DestDir: "{commoncf64}\Avid\Audio\Plug-Ins\XL-Desk.aaxplugin"; Components: aax; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[Icons]
Name: "{group}\XL-Desk"; Filename: "{app}\XL-Desk.exe"; Components: standalone
Name: "{autodesktop}\XL-Desk"; Filename: "{app}\XL-Desk.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for the Standalone app"; Components: standalone; Flags: unchecked

[Run]
Filename: "{app}\XL-Desk.exe"; Description: "Open XL-Desk now"; Components: standalone; Flags: nowait postinstall skipifsilent
