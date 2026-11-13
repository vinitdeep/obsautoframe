; V Media AI AutoFrame - Windows installer (Inno Setup 6)
; Build with scripts\package.ps1, or:  ISCC.exe /DAppVersion=1.0.0 /DStageDir=<stage> installer.iss
;
; Installs into the OBS per-machine plugin folder:
;   C:\ProgramData\obs-studio\plugins\vmedia-ai-autoframe\bin\64bit\vmedia-ai-autoframe.dll
;   C:\ProgramData\obs-studio\plugins\vmedia-ai-autoframe\data\...
; OBS 28 - 33 load plugins from there. Nothing inside the OBS program folder is touched:
; no OBS DLL and no shared dependency is ever overwritten, so OBS updates cannot break
; the plugin and the plugin cannot break OBS. ONNX Runtime + DirectML live in the plugin's
; own data\runtime folder and are loaded by full path.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\build\stage\vmedia-ai-autoframe"
#endif
#ifndef OutputDir
  #define OutputDir "..\release"
#endif

[Setup]
AppId={{8C1B5E02-5A7D-4E8B-9C47-2F6D0B9A1E73}
AppName=V Media AI AutoFrame for OBS Studio
AppVersion={#AppVersion}
AppVerName=V Media AI AutoFrame {#AppVersion}
AppPublisher=V Media Production
AppPublisherURL=https://vmediaproduction.com
AppSupportURL=https://vmediaproduction.com
AppCopyright=Copyright (C) 2026 V Media Production
VersionInfoVersion={#AppVersion}
VersionInfoCompany=V Media Production
VersionInfoDescription=V Media AI AutoFrame installer
DefaultDirName={commonappdata}\obs-studio\plugins\vmedia-ai-autoframe
DisableDirPage=no
DirExistsWarning=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=VMedia-AI-AutoFrame-Setup-x64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
InfoBeforeFile=installer-info.txt
UninstallDisplayName=V Media AI AutoFrame (OBS plugin)
UninstallDisplayIcon={app}\bin\64bit\vmedia-ai-autoframe.dll
; refuse to install / uninstall while OBS is running (it would hold the DLL open)
AppMutex=OBSStudioCore,OBSStudioPortable
CloseApplications=no

[Messages]
SetupAppRunningError=OBS Studio is running.%n%nPlease close OBS Studio, then click OK to continue.
UninstallAppRunningError=OBS Studio is running.%n%nPlease close OBS Studio, then click OK to continue.
SelectDirDesc=Where should the plugin be installed?
SelectDirLabel3=The recommended location is the OBS plugin folder in ProgramData, which OBS Studio 28 and newer loads automatically and which OBS updates never touch. Keep the default unless you use a portable OBS.

[Files]
Source: "{#StageDir}\bin\64bit\vmedia-ai-autoframe.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#StageDir}\bin\64bit\vmedia-ai-autoframe.pdb"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
Source: "{#StageDir}\data\*"; DestDir: "{app}\data"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; files from older versions that are no longer shipped
Type: filesandordirs; Name: "{app}\data\runtime\*.tmp"

[UninstallDelete]
Type: dirifempty; Name: "{app}\data"
Type: dirifempty; Name: "{app}"

[Code]
function ObsInstallDir(): String;
begin
  Result := '';
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Result) then
    RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', Result);
end;

function InitializeSetup(): Boolean;
var
  Dir: String;
begin
  Result := True;
  Dir := ObsInstallDir();
  if (Dir = '') or (not FileExists(Dir + '\bin\64bit\obs64.exe')) then
  begin
    Result := MsgBox('OBS Studio does not seem to be installed on this computer.' + #13#10 + #13#10 +
      'V Media AI AutoFrame is a plugin for OBS Studio 30 or newer (64-bit). Install OBS Studio from obsproject.com first.' + #13#10 + #13#10 +
      'Continue installing anyway?', mbConfirmation, MB_YESNO) = IDYES;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Legacy: String;
begin
  if CurStep = ssPostInstall then
  begin
    // A copy placed manually inside the OBS program folder would be loaded instead of this one.
    Legacy := ObsInstallDir() + '\obs-plugins\64bit\vmedia-ai-autoframe.dll';
    if FileExists(Legacy) then
      MsgBox('An older, manually copied version of V Media AI AutoFrame was found in' + #13#10 + Legacy + #13#10 + #13#10 +
        'Please delete it (and the folder data\obs-plugins\vmedia-ai-autoframe next to it) so OBS loads this version.',
        mbInformation, MB_OK);
  end;
end;
