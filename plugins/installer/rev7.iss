; Omega REV7 installer for Windows (Inno Setup 6). Built by .github/workflows/rev7-release.yml:
;   iscc /DBuildDir=<folder with the built "Omega REV7.vst3"> /DAppVer=1.0.0 rev7.iss
#ifndef AppVer
  #define AppVer "1.0.0"
#endif
[Setup]
AppId={{B7E2D4A1-5C3F-4A8E-9D61-2F0C7B3E9A54}
AppName=Omega REV7
AppVersion={#AppVer}
AppPublisher=The Omega System
DefaultDirName={commoncf64}\VST3\Omega REV7.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=Omega-REV7-Setup-Windows
OutputDir=.
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Omega REV7 (VST3)
UninstallFilesDir={commonappdata}\Omega REV7

[Files]
Source: "{#BuildDir}\Omega REV7.vst3\*"; DestDir: "{commoncf64}\VST3\Omega REV7.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion

[Messages]
FinishedLabel=Omega REV7 is installed in C:\Program Files\Common Files\VST3. Rescan plugins in your DAW to find it.
