; The F.A.R.P. - Windows installer (Inno Setup)
#define AppVer GetEnv("FARP_VERSION")
#if AppVer == ""
  #define AppVer "1.0.0"
#endif

[Setup]
AppId={{6B3C9E52-5D1A-4F0E-9C77-3F2A8E1D4B90}
AppName=The F.A.R.P.
AppVersion={#AppVer}
AppPublisher=FARP
DefaultDirName={autopf}\The FARP
DefaultGroupName=The F.A.R.P.
DisableProgramGroupPage=yes
OutputDir=.
OutputBaseFilename=FARP-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=The F.A.R.P.

[Types]
Name: "full"; Description: "VST3 plugin and standalone app"
Name: "vst3"; Description: "VST3 plugin only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (Common Files\VST3)"; Types: full vst3 custom; Flags: fixed
Name: "app"; Description: "Standalone app"; Types: full

[Dirs]
Name: "{userdocs}\FARP ROMs"; Flags: uninsneveruninstall
Name: "{userdocs}\FARP Samples"; Flags: uninsneveruninstall

[Files]
Source: "package\FARP.vst3\*"; DestDir: "{commoncf64}\VST3\FARP.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "package\FARP Standalone.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\The F.A.R.P."; Filename: "{app}\FARP Standalone.exe"; Components: app
Name: "{group}\Read me"; Filename: "{app}\INSTALL.txt"
Name: "{group}\Uninstall The F.A.R.P."; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\FARP Standalone.exe"; Description: "Open the standalone app now"; Components: app; Flags: postinstall nowait skipifsilent unchecked

[Messages]
FinishedLabel=The F.A.R.P. is installed. Rescan plugins in your music software: it appears as an instrument called "The F.A.R.P.". Put your ROM zips in Documents\FARP ROMs and your drum samples in Documents\FARP Samples (or pick other folders inside the plugin).
