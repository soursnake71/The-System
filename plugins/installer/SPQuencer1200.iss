; Windows installer for the SPQuencer1200 VST3 plugin (Inno Setup 6).
; CI builds it: iscc /DSrcDir=<folder holding SPQuencer1200.vst3> /DAppVersion=1.0.0 SPQuencer1200.iss
#ifndef SrcDir
  #define SrcDir "..\..\out"
#endif
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{6F1D2C4B-8A3E-4F57-9B21-5C0E7A1D3F12}
AppName=SPQuencer1200
AppVersion={#AppVersion}
AppPublisher=The Omega System
AppComments=24 PPQ sequencer for the E-mu and Rossum SP-1200, as a VST3 plugin
; the uninstaller lives here; the plugin itself goes to the VST3 folder (see [Files]), so nothing extra sits in its bundle
DefaultDirName={commonpf64}\SPQuencer1200
DisableDirPage=yes
; don't reuse the 1.0.0 test installers' folder, which was inside the plugin bundle
UsePreviousAppDir=no
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=SPQuencer1200-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=SPQuencer1200 VST3
MinVersion=10.0

[Files]
; the whole .vst3 bundle folder goes into the standard VST3 folder, C:\Program Files\Common Files\VST3
Source: "{#SrcDir}\SPQuencer1200.vst3\*"; DestDir: "{commoncf64}\VST3\SPQuencer1200.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; an uninstaller left inside the bundle by an earlier installer
Type: files; Name: "{commoncf64}\VST3\SPQuencer1200.vst3\unins000.*"

[UninstallDelete]
Type: dirifempty; Name: "{commoncf64}\VST3\SPQuencer1200.vst3"

[Messages]
FinishedLabel=SPQuencer1200 is installed in C:\Program Files\Common Files\VST3.%n%nIn Ableton Live, rescan plugins (Settings > Plug-Ins > Rescan) and find SPQuencer1200 under Plug-Ins > VST3 > The Omega System.

[Code]
// the plugin's editor is a WebView2 page; Windows 11 and up-to-date Windows 10 already have the runtime
function WebView2Installed(): Boolean;
var v: String;
begin
  Result :=
    RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', v) or
    RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', v) or
    RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}', 'pv', v);
  if Result then Result := (v <> '') and (v <> '0.0.0.0');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var code: Integer;
begin
  if (CurStep = ssPostInstall) and not WebView2Installed() then
    // SuppressibleMsgBox: a silent install (/SUPPRESSMSGBOXES) answers No instead of waiting for a click
    if SuppressibleMsgBox('SPQuencer1200''s window needs the Microsoft Edge WebView2 Runtime, which is not on this PC.' + #13#10 +
              'Open the download page now?', mbConfirmation, MB_YESNO, IDNO) = IDYES then
      ShellExec('open', 'https://developer.microsoft.com/microsoft-edge/webview2/', '', '', SW_SHOWNORMAL, ewNoWait, code);
end;
