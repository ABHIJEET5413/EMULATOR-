# Phase 6: Offline Installer — Detailed Implementation Guide

## Goal

Produce a single `DroidBoxSetup.exe` that bundles everything: host app,
QEMU, Android disk image, ADB, runtimes. The installer checks for WHPX,
creates shortcuts, registers `.apk` file association, and includes
proper license notices.

---

## Prerequisites

- [ ] Phase 5 complete and tested
- [ ] Inno Setup 6.x installed: https://jrsoftware.org/isinfo.php
- [ ] All runtime components collected in `installer/bundle/`
- [ ] License text files prepared

---

## Step-by-Step Implementation

### Step 1: Collect Bundle Components

Create this directory structure under `installer/bundle/`:

```
installer/bundle/
├── qemu/
│   ├── qemu-system-x86_64.exe
│   ├── qemu-img.exe
│   ├── *.dll (SDL2, glib, etc.)
│   └── share/
│       ├── bios-256k.bin
│       ├── efi-e1000.rom
│       ├── kvmvapic.bin
│       └── ... (other firmware)
├── adb/
│   ├── adb.exe
│   ├── AdbWinApi.dll
│   └── AdbWinUsbApi.dll
├── images/
│   └── bliss-base.qcow2
├── configs/
│   └── default-instance.json
├── keymaps/
│   ├── default.json
│   └── wasd-gaming.json
└── vcredist/
    └── vc_redist.x64.exe
```

#### Finding QEMU Dependencies

QEMU for Windows needs several DLLs. To find them all:

```powershell
# List all DLLs that qemu-system-x86_64.exe depends on
dumpbin /dependents "C:\path\to\qemu\qemu-system-x86_64.exe"

# Copy all non-system DLLs alongside the exe
# Common ones: SDL2.dll, libglib-2.0-0.dll, libintl-8.dll, etc.
```

#### Qt Dependencies

Use Qt's `windeployqt` tool to gather Qt DLLs:

```powershell
C:\Qt\6.5.3\msvc2019_64\bin\windeployqt.exe `
    --release `
    --no-translations `
    --no-opengl-sw `
    build\Release\DroidBox.exe
```

This copies the needed Qt DLLs and creates `platforms/` and `styles/`.

---

### Step 2: Inno Setup Script

The main `installer/droidbox.iss` file. See the SKILL.md for the
detailed script template.

Key Inno Setup concepts:
- `[Setup]`: App metadata, compression, architecture
- `[Files]`: What to copy and where
- `[Icons]`: Start Menu and Desktop shortcuts
- `[Registry]`: .apk file association
- `[Run]`: Post-install actions (MSVC runtime, overlay creation)
- `[Code]`: Pascal Script for custom logic (WHPX check)

### Step 3: WHPX Detection During Install

The installer should:
1. Check if VT-x/AMD-V is enabled in BIOS
2. Check if Windows Hypervisor Platform feature is enabled
3. If not enabled: offer to enable it (requires admin + reboot)
4. If VT-x is off: show instructions (can't fix from software)

```pascal
// In [Code] section of .iss file

function CheckWHPX(): Boolean;
var
  ResultCode: Integer;
  Output: AnsiString;
begin
  // Run PowerShell to check
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -Command "if ((Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform).State -eq ''Enabled'') { exit 0 } else { exit 1 }"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);

  Result := (ResultCode = 0);
end;

function EnableWHPX(): Boolean;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -Command "Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform -NoRestart"',
    '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := (ResultCode = 0);
end;
```

### Step 4: .apk File Association

```pascal
[Registry]
; Create .apk file type
Root: HKCR; Subkey: ".apk"; ValueType: string; ValueName: ""; ValueData: "DroidBox.APK"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "DroidBox.APK"; ValueType: string; ValueName: ""; ValueData: "Android Application Package"; Flags: uninsdeletekey
Root: HKCR; Subkey: "DroidBox.APK\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\DroidBox.exe,0"
Root: HKCR; Subkey: "DroidBox.APK\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\DroidBox.exe"" ""%1"""
```

### Step 5: Uninstaller

Inno Setup creates an uninstaller automatically. Add custom cleanup:

```pascal
[UninstallDelete]
; Clean up runtime files
Type: filesandordirs; Name: "{app}\instances"
Type: filesandordirs; Name: "{app}\logs"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    // Ask about user data
    if MsgBox('Delete all Android instance data? This cannot be undone.',
              mbConfirmation, MB_YESNO) = IDNO then
    begin
      // Keep instances directory
      // Inno Setup will still remove app files
    end;
  end;
end;
```

### Step 6: License Screen

Create `installer/licenses/LICENSE.txt` combining all licenses:

```
DroidBox License Agreement
===========================

DroidBox is provided under the MIT License.

This software bundles the following third-party components:

1. QEMU - GPLv2
   Source code: https://www.qemu.org/download/
   Full license: see QEMU-LICENSE.txt

2. Qt 6 - LGPL v3
   Source code: https://www.qt.io/download-open-source
   Full license: see QT-LICENSE.txt
   Qt libraries are dynamically linked. You may replace them with
   your own build.

3. Bliss OS - Apache 2.0
   Source code: https://blissos.org/
   Full license: see BLISS-LICENSE.txt

4. F-Droid - GPLv3
   Source code: https://f-droid.org/
   Full license: see FDROID-LICENSE.txt

Source Code Offer:
You may obtain the complete source code for QEMU and Qt by visiting
the URLs above, or by contacting [email]. This offer is valid for
three years from the date of distribution.
```

### Step 7: Compression

The base qcow2 image will be 2-4 GB. Use aggressive compression:

```pascal
[Setup]
Compression=lzma2/ultra64
SolidCompression=yes
; This will take longer to compress but produce a smaller installer
```

Expected sizes:
- Uncompressed total: ~5-6 GB
- Compressed setup.exe: ~3-4 GB (depends on qcow2 content)

### Step 8: Building the Installer

Document in `installer/BUILD.md`:

```powershell
# 1. Build DroidBox (Release)
cmake -B build -S host-app -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.5.3\msvc2019_64"
cmake --build build --config Release

# 2. Deploy Qt DLLs
C:\Qt\6.5.3\msvc2019_64\bin\windeployqt.exe --release build\Release\DroidBox.exe

# 3. Collect bundle components
#    (QEMU, ADB, base image, etc. — see bundle directory structure)

# 4. Compile installer
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\droidbox.iss

# Output: installer\Output\DroidBoxSetup.exe
```

---

## Testing the Installer

### Test on a Clean Machine

The gold-standard test is installing on a clean Windows 10/11 VM:

1. Create a Windows 10/11 VM in Hyper-V or VirtualBox
2. Copy `DroidBoxSetup.exe` to the VM
3. Run the installer
4. Verify:
   - Installation completes without errors
   - WHPX check runs and shows appropriate messages
   - Shortcuts are created
   - DroidBox launches
   - Android boots
   - .apk association works
   - Uninstaller works

### Quick Test (Same Machine)

If you can't test on a clean VM:
1. Install to a non-default directory
2. Verify all files are present
3. Launch from the Start Menu shortcut
4. Test .apk double-click
5. Uninstall and verify cleanup

---

## Size Optimization

If the installer is too large (>5 GB), consider:

1. **Compress the qcow2 before bundling:**
   ```
   qemu-img convert -O qcow2 -c base.qcow2 base-compressed.qcow2
   ```

2. **Use sparse qcow2:**
   The qcow2 format is already sparse, but after installation/setup
   it may contain empty blocks. Use `qemu-img` to compact it.

3. **Strip unnecessary QEMU firmware:**
   The `share/` directory contains firmware for many architectures.
   Only include what's needed for x86_64.

4. **Exclude unused Qt modules:**
   `windeployqt` may copy more than needed. Manually remove unused
   plugins and translations.
