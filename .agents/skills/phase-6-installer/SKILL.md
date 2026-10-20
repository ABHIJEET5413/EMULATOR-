---
name: phase-6-installer
description: >
  Implement Phase 6 of DroidBox: Offline Packaging. Create an Inno Setup
  script producing a single setup.exe that bundles everything: host app,
  QEMU, Android disk image, ADB, runtimes. Includes WHPX check, shortcuts,
  .apk file association, and license notices.
---

# Phase 6: Offline Installer

## Overview

This phase produces the final deliverable: `DroidBoxSetup.exe`, a single
offline installer that bundles everything a user needs. Zero downloads at
install time or first launch.

## Before You Start

1. Read `docs/phase-guides/phase-6-installer.md` for the detailed guide.
2. Read `.agents/rules/project-rules.md` for coding standards.
3. Phase 5 must be complete and tested.
4. You need [Inno Setup 6.x](https://jrsoftware.org/isinfo.php) installed.

## What to Build

### Files to Create

| File | Purpose |
|---|---|
| `installer/droidbox.iss` | Inno Setup script |
| `installer/check_whpx.ps1` | WHPX check run during install |
| `installer/licenses/LICENSE.txt` | Combined license text |
| `installer/licenses/QEMU-LICENSE.txt` | QEMU GPLv2 full text |
| `installer/licenses/QT-LICENSE.txt` | Qt LGPL v3 full text |
| `installer/licenses/BLISS-LICENSE.txt` | Bliss OS Apache 2.0 text |
| `installer/licenses/FDROID-LICENSE.txt` | F-Droid GPLv3 text |
| `installer/resources/droidbox.ico` | Application icon |
| `installer/resources/installer-banner.bmp` | Installer side banner |
| `installer/scripts/post-install.ps1` | Post-install setup |
| `installer/BUILD.md` | How to build the installer |

### Bundle Contents

The installer must include ALL of these:

```
DroidBox/
├── DroidBox.exe                    # Host application
├── Qt6Core.dll                     # Qt runtime (dynamic)
├── Qt6Widgets.dll
├── Qt6Gui.dll
├── Qt6Network.dll
├── platforms/qwindows.dll          # Qt platform plugin
├── styles/qwindowsvistastyle.dll   # Qt style plugin
├── vm/
│   ├── qemu/
│   │   ├── qemu-system-x86_64.exe # QEMU binary
│   │   ├── *.dll                   # QEMU dependencies
│   │   └── share/                  # QEMU firmware (BIOS, etc.)
│   ├── images/
│   │   └── bliss-base.qcow2       # Pre-installed Android image
│   └── configs/
│       └── default-instance.json
├── adb/
│   ├── adb.exe                     # Android Debug Bridge
│   └── AdbWinApi.dll
├── instances/
│   └── default/
│       ├── config.json
│       └── overlay.qcow2           # Created by post-install script
├── keymaps/
│   ├── default.json
│   └── wasd-gaming.json
├── licenses/
│   ├── LICENSE.txt
│   ├── QEMU-LICENSE.txt
│   ├── QT-LICENSE.txt
│   ├── BLISS-LICENSE.txt
│   └── FDROID-LICENSE.txt
└── logs/                           # Created at runtime
```

### Inno Setup Script Structure

```pascal
[Setup]
AppName=DroidBox
AppVersion=1.0.0
AppPublisher=DroidBox
DefaultDirName={autopf}\DroidBox
DefaultGroupName=DroidBox
OutputBaseFilename=DroidBoxSetup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=licenses\LICENSE.txt
SetupIconFile=resources\droidbox.ico
WizardImageFile=resources\installer-banner.bmp
UninstallDisplayIcon={app}\DroidBox.exe
PrivilegesRequired=admin          ; Needed for WHPX feature enable

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Host application
Source: "..\build\Release\DroidBox.exe"; DestDir: "{app}"
Source: "..\build\Release\*.dll"; DestDir: "{app}"
Source: "..\build\Release\platforms\*"; DestDir: "{app}\platforms"
Source: "..\build\Release\styles\*"; DestDir: "{app}\styles"

; QEMU
Source: "bundle\qemu\*"; DestDir: "{app}\vm\qemu"; Flags: recursesubdirs

; Android base image (large file — use solid compression)
Source: "bundle\images\bliss-base.qcow2"; DestDir: "{app}\vm\images"

; ADB
Source: "bundle\adb\*"; DestDir: "{app}\adb"

; Configs and keymaps
Source: "bundle\configs\*"; DestDir: "{app}\vm\configs"
Source: "bundle\keymaps\*"; DestDir: "{app}\keymaps"

; Licenses
Source: "licenses\*"; DestDir: "{app}\licenses"

; MSVC Runtime (if not using static CRT)
Source: "bundle\vcredist\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\DroidBox"; Filename: "{app}\DroidBox.exe"
Name: "{group}\Uninstall DroidBox"; Filename: "{uninstallexe}"
Name: "{commondesktop}\DroidBox"; Filename: "{app}\DroidBox.exe"

[Registry]
; .apk file association
Root: HKCR; Subkey: ".apk"; ValueType: string; ValueData: "DroidBox.APK"
Root: HKCR; Subkey: "DroidBox.APK"; ValueType: string; ValueData: "Android Package"
Root: HKCR; Subkey: "DroidBox.APK\DefaultIcon"; ValueType: string; ValueData: "{app}\DroidBox.exe,0"
Root: HKCR; Subkey: "DroidBox.APK\shell\open\command"; ValueType: string; ValueData: """{app}\DroidBox.exe"" ""%1"""

[Run]
; Install MSVC Runtime
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Visual C++ Runtime..."
; Create default instance overlay
Filename: "{app}\vm\qemu\qemu-img.exe"; Parameters: "create -f qcow2 -b ""{app}\vm\images\bliss-base.qcow2"" -F qcow2 ""{app}\instances\default\overlay.qcow2"""; StatusMsg: "Creating default instance..."
; WHPX check (informational, don't block install)
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\check_whpx.ps1"""; Flags: runhidden

[Code]
// Pascal Script for WHPX detection during install
function IsHyperVEnabled(): Boolean;
var
  ResultCode: Integer;
begin
  // Check if Windows Hypervisor Platform is enabled
  Exec('powershell.exe',
    '-Command "(Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform).State -eq ''Enabled''"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := (ResultCode = 0);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpReady then
  begin
    if not IsHyperVEnabled() then
    begin
      if MsgBox(
        'Windows Hypervisor Platform is not enabled.' + #13#10 +
        'DroidBox requires this feature for Android VM acceleration.' + #13#10 + #13#10 +
        'Would you like the installer to enable it?' + #13#10 +
        '(A reboot will be required after installation)',
        mbConfirmation, MB_YESNO) = IDYES then
      begin
        Exec('powershell.exe',
          '-Command "Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform -NoRestart"',
          '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      end;
    end;
  end;
end;
```

### WHPX Check Script (Installer)

`installer/check_whpx.ps1`:
```powershell
# Check 1: CPU Virtualization
$virt = Get-CimInstance -ClassName Win32_Processor | Select -Expand VirtualizationFirmwareEnabled
if (-not $virt) {
    Write-Output "ERROR: CPU virtualization (VT-x/AMD-V) is DISABLED in BIOS."
    Write-Output "FIX: Restart your PC, enter BIOS setup, and enable:"
    Write-Output "  - Intel: 'Intel Virtualization Technology (VT-x)'"
    Write-Output "  - AMD: 'SVM Mode'"
    exit 1
}

# Check 2: Windows Hypervisor Platform
$whp = Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform
if ($whp.State -ne 'Enabled') {
    Write-Output "ERROR: Windows Hypervisor Platform is NOT enabled."
    Write-Output "FIX: Run this command in an admin PowerShell:"
    Write-Output "  Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform"
    Write-Output "Then reboot your PC."
    exit 1
}

Write-Output "OK: Virtualization and Windows Hypervisor Platform are enabled."
exit 0
```

### Post-Install Actions

1. Install MSVC runtime (silent)
2. Create default instance overlay from base image
3. Check WHPX status and offer to enable
4. Create Start Menu + Desktop shortcuts
5. Register .apk file association

### Uninstaller

Inno Setup handles this automatically, but ensure:
- Overlay images are deleted (user data)
- Registry entries for .apk association are cleaned up
- Ask "Delete instance data?" during uninstall

### Size Estimates

| Component | Estimated Size |
|---|---|
| Bliss OS qcow2 (compressed) | ~2-4 GB |
| QEMU + deps | ~100 MB |
| DroidBox.exe + Qt DLLs | ~30 MB |
| ADB + platform-tools | ~10 MB |
| MSVC runtime | ~25 MB |
| **Total setup.exe** | **~3-5 GB** |

### How to Build the Installer

Document this in `installer/BUILD.md`:

```markdown
# Building DroidBoxSetup.exe

## Prerequisites
1. Build DroidBox.exe (Release): `cmake --build build --config Release`
2. Download QEMU for Windows and extract to `installer/bundle/qemu/`
3. Download Android platform-tools and extract to `installer/bundle/adb/`
4. Place bliss-base.qcow2 in `installer/bundle/images/`
5. Place vc_redist.x64.exe in `installer/bundle/vcredist/`
6. Install Inno Setup 6.x

## Build
1. Open `installer/droidbox.iss` in Inno Setup Compiler
2. Click Compile (or run: `iscc.exe droidbox.iss`)
3. Output: `installer/Output/DroidBoxSetup.exe`
```

## Manual Test Checklist

- [ ] `DroidBoxSetup.exe` is produced as a single file
- [ ] Installer shows license agreement screen
- [ ] Installer detects if WHPX is not enabled and offers to fix
- [ ] Installation completes without errors
- [ ] Start Menu shortcut is created and works
- [ ] Desktop shortcut is created and works
- [ ] DroidBox launches from the installed location
- [ ] QEMU, ADB, and base image are in the correct directories
- [ ] Default instance overlay is created during install
- [ ] Double-clicking a .apk file opens DroidBox
- [ ] Uninstaller removes all files and registry entries
- [ ] Installer file size is reasonable (~3-5 GB)
- [ ] Can install from USB drive (offline, no internet)
- [ ] Fresh Windows 10 VM can install and run DroidBox
