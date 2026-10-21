# Build DroidBoxSetup.exe

Run in **Windows PowerShell**, from the repository root. Installation and first
launch use bundled dependencies and never download them. Examples use Qt 6.12.0,
MSVC 2022, and Inno Setup 6.7.3.

## 1. Inputs

- Visual Studio 2022 C++ x64, CMake, Git, dynamic Qt with `windeployqt`.
- Inno Setup 6.x; installed here at `%LOCALAPPDATA%\Programs\InnoSetup6\ISCC.exe`.
- `vm/qemu/`: tested Windows ar7 QEMU, `qemu-img.exe`, DLLs and firmware.
- `vm/adb/`: platform-tools 37.0.1, both Windows DLLs, NOTICE.txt and source.properties.
- `vm/images/bliss-distribution-v2.qcow2`: the clean, key-free image. Do not use
  the personal-key-authorized development `bliss-provisioned.qcow2`.
- `_downloads/phase6/vc_redist.x64.exe`: signed offline Microsoft runtime.
- The three source archives in `_downloads/phase6/sources`, named and hashed in
  `installer/source-lock.json`. Exact origins are recorded there and in
  [ATTRIBUTION](../docs/ATTRIBUTION.md).

If rebuilding the clean image, choose a new output filename:

```powershell
wsl -u root --exec sh image-build/prepare-instance-base.sh vm/images/bliss-base.qcow2 vm/images/bliss-distribution-new.qcow2
```

Review its fresh-guest RSA flow and update the reviewed image hash in the source
lock before staging it. Never overwrite a backing base used by an instance.
This command has no host-key argument.

## 2. Build Release

```powershell
cmake -B cmake-build-phase6-fixes -S host-app -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64" -DBUILD_TESTING=OFF
cmake --build cmake-build-phase6-fixes --config Release --target DroidBox
```

The separate build directory avoids replacing a running development executable.
The installer invokes `--initialize-only` to create data without launching a GUI
or VM. Close an executable before rebuilding its directory.

## 3. Branding (already committed)

Only needed after changing/recreating assets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer/scripts/make-resources.ps1
```

## 4. Prepare the offline bundle

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer/scripts/prepare-bundle.ps1 -AppDirectory "$PWD/cmake-build-phase6-fixes/Release" -QtRoot "C:/Qt/6.12.0/msvc2022_64"
```

Overrides: `-QemuRoot`, `-AdbRoot`, `-BaseImage`, `-RuntimeInstaller`,
`-SourceDirectory`. Versions/hashes must match the reviewed source lock.
The script never downloads anything. It generates `installer/bundle/` containing
the host, dynamic Qt/plugins, QEMU/DLLs/firmware, ADB, standalone base, configs,
runtime, sources, notices and a SHA-256 manifest.

The base is installed as `vm/images/bliss-provisioned.qcow2`, keymaps at
`resources/keymaps`, and ADB at `vm/adb`, matching application lookups.
Staging uses a hard link for the image where possible; **never edit or boot the
staged image**. `-CopyImage` forces an independent copy. Restaging replaces only
the generated bundle and refuses junctions/links; input files are retained.

Allow approximately 6 GB for staging, 4 GB for compiler output/workspace and
8 GB for installation plus the user's first base copy. Retained base versions
need additional space and must stay while any overlay references them.

## 5. Compile the single EXE

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer/scripts/build-installer.ps1
```

Output: **`installer/Output/DroidBoxSetup.exe`**, followed by its SHA-256.
No external `.bin` files. Compression may take several minutes.
Use `-IsccPath "C:/path/to/ISCC.exe"` for another compiler location.

Direct compiler equivalent after staging:

```powershell
& "$env:LOCALAPPDATA/Programs/InnoSetup6/ISCC.exe" installer/droidbox.iss
```

Prefer the wrapper for payload integrity and distribution review checks.
If the single-EXE limit is exceeded, compact the clean image and review unused
dependencies/firmware. Do not enable disk spanning or add installation downloads.

## Public-distribution source review

Default builds are development/review installers. The full requested GPLv2,
LGPLv3, Apache 2.0 and GPLv3 texts, source-offer text and pinned core sources are
included. These do not complete the source/notices inventory of every DLL,
firmware, kernel and guest application.

Before publishing, collect additional corresponding sources/notices into
`bundle/sources` and `bundle/notices`. Create a reviewed
`installer/release-inventory.json` containing:

- `complete`: true only after completing the actual inventory;
- `reviewedBy`: accountable distributor/reviewer;
- `sourceOfferContact`: `https://github.com/HEARTLESS5413/EMULATOR-/issues`, which
  must be accessible to recipients and match SOURCE-OFFER.txt;
- `sourceArtifacts`: a nonempty array of records with `path` (under `sources/`)
  and the actual `sha256` for each reviewed source delivery artifact.

Do not insert placeholders or claim completeness merely to pass the gate. Then:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer/scripts/build-installer.ps1 -ForDistribution
```

This validates the inventory and artifacts, includes its receipt, and records
`releaseReady=true`. It is a recorded distributor review, not automatic legal
certification. Keep matching complete sources for the period in SOURCE-OFFER.txt
and supply them alongside binaries or fulfill source requests as applicable.

## Installed behavior

- Launch Setup normally from the intended signed-in account and accept UAC.
  `ExecAsOriginalUser` initializes that account's default instance during install.
  Explicitly starting Setup as another account uses that account's profile.
  SYSTEM/service installation cannot initialize personal instance data.
- Application files go to Program Files. Writable data is under
  `%LOCALAPPDATA%\DroidBox\DroidBox`: `instances`, `bases/<SHA-256>`, logs.
  A verified, atomic user base copy keeps overlays usable when uninstall retains
  data. New releases retain old base versions. Installed instances resolve QEMU
  from the current installation, including after reinstalling elsewhere.
- WHPX uses explicit exit codes and a diskless QEMU probe. Enablement uses
  `-NoRestart -LimitAccess`: no Windows Update and no forced reboot. Missing
  Windows component files produce instructions. Pending states suppress launch.
- APK Open With registration is installed. The optional association task sets
  the machine default while saving its previous value. Windows UserChoice is
  untouched; uninstall restores a default only if it still points to DroidBox.
- Uninstall asks about the current uninstaller account's data. No and silent
  uninstall preserve both overlays and bases. Other accounts' data is retained.
  Cleanup refuses junctions/open disk writers; no unconditional instance erasure.

## Manual acceptance / WHPX

See [Phase 6 checklist](../docs/PHASE-6-TESTING.md). A compiler run does not imply
a clean-machine install or boot has passed.

```powershell
# Elevated PowerShell after installation:
powershell -NoProfile -ExecutionPolicy Bypass -File 'C:/Program Files/DroidBox/check_whpx.ps1' -Probe
Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform
# Only if necessary; restart afterwards:
Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform -NoRestart -LimitAccess
```
