#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{43FEA266-EB75-48F3-906D-C7D671ECFEA8}
AppName=DroidBox
AppVersion={#AppVersion}
AppPublisher=DroidBox
AppPublisherURL=https://github.com/HEARTLESS5413/EMULATOR-
DefaultDirName={autopf}\DroidBox
DefaultGroupName=DroidBox
OutputDir=Output
OutputBaseFilename=DroidBoxSetup
Compression=lzma2/ultra64
SolidCompression=yes
DiskSpanning=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
WizardStyle=modern
LicenseFile=licenses\LICENSE.txt
SetupIconFile=resources\droidbox.ico
WizardImageFile=resources\installer-banner.bmp
UninstallDisplayIcon={app}\resources\droidbox.ico
ChangesAssociations=yes
CloseApplications=no
RestartApplications=no
RestartIfNeededByRun=no
ExtraDiskSpaceRequired=4294967296
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a Desktop shortcut"; Flags: checkedonce
Name: "apkassociation"; Description: "Associate .apk files with DroidBox"; Flags: checkedonce

[Files]
; These must precede the solid-compressed image for fast prerequisite extraction.
Source: "check_whpx.ps1"; Flags: dontcopy
Source: "bundle\vcredist\vc_redist.x64.exe"; Flags: dontcopy
Source: "bundle\app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "bundle\qemu\*"; DestDir: "{app}\vm\qemu"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "bundle\adb\*"; DestDir: "{app}\vm\adb"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "bundle\images\bliss-provisioned.qcow2"; DestDir: "{app}\vm\images"; Flags: ignoreversion
Source: "bundle\configs\default-instance.json"; DestDir: "{app}\vm\configs"; Flags: ignoreversion
Source: "bundle\sources\*"; DestDir: "{app}\sources"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "bundle\notices\*"; DestDir: "{app}\licenses\upstream"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "bundle\manifest.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "resources\droidbox.ico"; DestDir: "{app}\resources"; Flags: ignoreversion
Source: "check_whpx.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "scripts\post-install.ps1"; DestDir: "{app}\installer-scripts"; Flags: ignoreversion
Source: "scripts\uninstall-data.ps1"; DestDir: "{app}\installer-scripts"; Flags: ignoreversion

[Icons]
Name: "{group}\DroidBox"; Filename: "{app}\DroidBox.exe"; WorkingDir: "{app}"; IconFilename: "{app}\resources\droidbox.ico"
Name: "{group}\Uninstall DroidBox"; Filename: "{uninstallexe}"
Name: "{commondesktop}\DroidBox"; Filename: "{app}\DroidBox.exe"; WorkingDir: "{app}"; IconFilename: "{app}\resources\droidbox.ico"; Tasks: desktopicon

[Registry]
Root: HKLM; Subkey: "Software\Classes\.apk"; ValueType: none; Flags: uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\.apk\OpenWithProgids"; ValueType: string; ValueName: "DroidBox.APK"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\Classes\DroidBox.APK"; ValueType: string; ValueData: "Android Application Package"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Classes\DroidBox.APK\DefaultIcon"; ValueType: string; ValueData: """{app}\resources\droidbox.ico"""
Root: HKLM; Subkey: "Software\Classes\DroidBox.APK\shell\open\command"; ValueType: string; ValueData: """{app}\DroidBox.exe"" ""%1"""

[Run]
Filename: "{app}\DroidBox.exe"; Description: "Launch DroidBox"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent runasoriginaluser; Check: CanLaunch

[Code]
var
  EnableRequested, RestartRequired, WhpxReady, WhpxChecked: Boolean;
  LastWhpxMessage: String;

function PowerShell: String;
begin
  Result := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
end;

function CheckWhpx(const ScriptPath, Options: String): Integer;
var Code, Index: Integer; ResultFile: String; Lines: TArrayOfString;
begin
  Result := 20;
  ResultFile := ExpandConstant('{tmp}\droidbox-whpx.json');
  DeleteFile(ResultFile + '.txt');
  LastWhpxMessage := '';
  if Exec(PowerShell, '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + ScriptPath + '" ' + Options + ' -ResultPath "' + ResultFile + '"',
      '', SW_HIDE, ewWaitUntilTerminated, Code) then Result := Code;
  Log('WHPX check exit code: ' + IntToStr(Result));
  if LoadStringsFromFile(ResultFile + '.txt', Lines) then
    for Index := 0 to GetArrayLength(Lines) - 1 do begin
      LastWhpxMessage := LastWhpxMessage + Lines[Index] + #13#10;
      Log('WHPX: ' + Lines[Index]);
    end;
end;

procedure CurPageChanged(PageID: Integer);
var Code: Integer;
begin
  if (PageID = wpReady) and not WhpxChecked then begin
    WhpxChecked := True;
    ExtractTemporaryFile('check_whpx.ps1');
    Code := CheckWhpx(ExpandConstant('{tmp}\check_whpx.ps1'), '');
    WhpxReady := Code = 0;
    if Code = 10 then
      EnableRequested := SuppressibleMsgBox('Windows Hypervisor Platform is disabled. Enable it during installation?' + #13#10 +
        'A Windows restart may be required. No downloads will be performed.', mbConfirmation, MB_YESNO, IDNO) = IDYES
    else if Code = 11 then begin
      RestartRequired := True;
      SuppressibleMsgBox('Restart Windows after installation before starting Android.', mbInformation, MB_OK, IDOK);
    end else if Code = 12 then
      SuppressibleMsgBox('Enable Intel VT-x or AMD SVM in BIOS/UEFI and restart Windows before starting Android.', mbInformation, MB_OK, IDOK)
    else if Code <> 0 then
      SuppressibleMsgBox('WHPX status could not be verified: ' + LastWhpxMessage + 'Run check_whpx.ps1 as Administrator afterwards.', mbInformation, MB_OK, IDOK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Code: Integer;
begin
  Result := '';
  if not WhpxChecked then begin
    ExtractTemporaryFile('check_whpx.ps1');
    Code := CheckWhpx(ExpandConstant('{tmp}\check_whpx.ps1'), '');
    WhpxReady := Code = 0;
    RestartRequired := (Code = 11) or RestartRequired;
    WhpxChecked := True;
  end;
  if EnableRequested then begin
    Code := CheckWhpx(ExpandConstant('{tmp}\check_whpx.ps1'), '-Enable');
    RestartRequired := (Code = 11) or RestartRequired;
    WhpxReady := Code = 0;
    if (Code <> 0) and (Code <> 11) then
      SuppressibleMsgBox('WHPX could not be made ready: ' + LastWhpxMessage + 'Check Windows component files or BIOS settings. No automatic restart will occur.', mbInformation, MB_OK, IDOK);
    EnableRequested := False;
  end;
end;

function CanLaunch: Boolean;
begin
  Result := WhpxReady and not RestartRequired;
end;

function NeedRestart: Boolean;
begin
  Result := RestartRequired;
end;

procedure CurStepChanged(Step: TSetupStep);
var Code: Integer; Previous: String;
begin
  if Step <> ssPostInstall then Exit;
  WizardForm.StatusLabel.Caption := 'Installing the Visual C++ runtime...';
  ExtractTemporaryFile('vc_redist.x64.exe');
  if not Exec(ExpandConstant('{tmp}\vc_redist.x64.exe'), '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, Code) then
    RaiseException('Could not start the bundled Visual C++ runtime installer.');
  if Code = 3010 then RestartRequired := True
  else if (Code <> 0) and (Code <> 1638) and (Code <> -2147023258) then
    RaiseException('Visual C++ runtime installation failed. Exit code: ' + IntToStr(Code));

  WizardForm.StatusLabel.Caption := 'Preparing your default Android instance...';
  if not ExecAsOriginalUser(PowerShell, '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
      ExpandConstant('{app}\installer-scripts\post-install.ps1') + '" -InstallRoot "' + ExpandConstant('{app}') + '"',
      ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code) then
    RaiseException('Could not initialize the original user''s default instance. Run Setup from a signed-in Windows account.');
  if Code <> 0 then RaiseException('Default instance creation failed. Check free disk space and the DroidBox log in your LocalAppData folder.');

  if WizardIsTaskSelected('apkassociation') then begin
    // Preserve the previous machine default. Windows UserChoice is never edited.
    if not RegValueExists(HKLM, 'Software\DroidBox\Installer', 'PreviousApk') then begin
      Previous := '';
      RegQueryStringValue(HKLM, 'Software\Classes\.apk', '', Previous);
      if Previous = 'DroidBox.APK' then Previous := '';
      RegWriteStringValue(HKLM, 'Software\DroidBox\Installer', 'PreviousApk', Previous);
    end;
    RegWriteStringValue(HKLM, 'Software\Classes\.apk', '', 'DroidBox.APK');
  end;

  if not RestartRequired then begin
    Code := CheckWhpx(ExpandConstant('{app}\check_whpx.ps1'), '-Probe');
    WhpxReady := Code = 0;
    if Code = 11 then RestartRequired := True;
    if not WhpxReady then SuppressibleMsgBox('Android acceleration is not ready: ' + LastWhpxMessage + 'Run the installed WHPX check before launching Android.', mbInformation, MB_OK, IDOK);
  end;
end;

procedure CurUninstallStepChanged(Step: TUninstallStep);
var Code: Integer; Current, Previous: String;
begin
  if Step <> usUninstall then Exit;
  if RegQueryStringValue(HKLM, 'Software\Classes\.apk', '', Current) and (Current = 'DroidBox.APK') then begin
    Previous := '';
    RegQueryStringValue(HKLM, 'Software\DroidBox\Installer', 'PreviousApk', Previous);
    if Previous <> '' then RegWriteStringValue(HKLM, 'Software\Classes\.apk', '', Previous)
    else RegDeleteValue(HKLM, 'Software\Classes\.apk', '');
  end;
  RegDeleteKeyIncludingSubkeys(HKLM, 'Software\DroidBox\Installer');
  // No unconditional UninstallDelete entry: Keep really preserves overlays AND bases.
  if not UninstallSilent and (MsgBox('Delete Android instance data for the Windows account running this uninstaller?' + #13#10 +
      'Choose No to keep apps and data. Data belonging to other Windows accounts is retained.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then begin
    if not Exec(PowerShell, '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
        ExpandConstant('{app}\installer-scripts\uninstall-data.ps1') + '" -Delete', '', SW_HIDE, ewWaitUntilTerminated, Code) then
      MsgBox('Could not start data cleanup. Your Android data was retained.', mbInformation, MB_OK)
    else if Code <> 0 then MsgBox('Data cleanup could not finish. Close DroidBox and QEMU, then remove your LocalAppData\DroidBox\DroidBox directory manually after preserving anything you need.', mbInformation, MB_OK);
  end;
end;
