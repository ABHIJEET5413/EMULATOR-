# Sabi 2.0.0 Release Candidate 1

**Published:** 31 October 2026<br>
**Git tag:** [`v2.0.0-rc1`](https://github.com/ABHIJEET5413/EMULATOR-/releases/tag/v2.0.0-rc1)<br>
**Release type:** prerelease / release candidate

## Download and integrity

Download `SabiSetup.exe` and `SabiSetup.exe.sha256` from the [GitHub
release](https://github.com/ABHIJEET5413/EMULATOR-/releases/tag/v2.0.0-rc1).

| File | Size | SHA-256 |
|---|---:|---|
| `SabiSetup.exe` | 1,810,483,824 bytes | `8186ca60d6de7fce7f3f3c526e66f5a177fc5556de75799bd85f33d010eafdb1` |

Before running the installer, verify it in PowerShell:

```powershell
Get-FileHash "$env:USERPROFILE\Downloads\SabiSetup.exe" -Algorithm SHA256
```

The hash must match exactly. Do not run an installer whose hash differs.

## What changed in Version 2

- Rebranded the desktop application, user data, executable, and installer from
  DroidBox to **Sabi**.
- Added persistent per-instance display profiles: Phone, Tablet, PC,
  Fullscreen, and Custom resolution/DPI settings.
- Improved host sizing and letterboxed embedded-display rendering to preserve
  guest aspect ratio.
- Updated the sidebar with a compact vector icon system and tooltips.
- Added and retained per-instance CPU, RAM, QMP, ADB, resolution, DPI, and
  experimental camera configuration.
- Added a one-time non-destructive migration path for compatible Version 1
  instance data.
- Fixed embedded guest pointer handling: touchpad/mouse cursor motion can cross
  into the embedded QEMU SDL display, while QMP supplies Android touch button
  transitions and host toolbar/window controls remain interactive.
- Added the major project report and repository map.
- Produced a new verified offline installer containing the Version 2
  release-candidate executable.

## Verification recorded for this release candidate

- CTest: **10/10 registered suites passed** on the release-candidate source.
- Installer staging: **3,399 payload paths and SHA-256 hashes verified**.
- The bundled `Sabi.exe` SHA-256 matched the tested Release executable.
- Android boot, ADB authorization, APK installation, file push/media scan,
  screenshots, navigation controls, and multi-instance behavior were previously
  exercised on the development PC.
- The touchpad cursor boundary fix was physically checked: the cursor entered
  Android, click/drag worked, and Sabi toolbar controls remained usable.

## Known limitations and remaining acceptance

This is intentionally a prerelease. It has not completed clean-PC/offline
acceptance, fresh-user Android RSA authorization, application-data A/B upgrade
retention, all WHPX error-state tests, or full uninstall acceptance. The camera
path remains experimental; Rotate and Shared Folder toolbar behavior have the
limitations described in the README and `docs/CAMERA.md`.

The repository's third-party corresponding-source/notices inventory remains
under review. This release is published for release-candidate testing, not as a
claim that all final distribution obligations have been closed.

## Test feedback

Use [CLEAN-PC-ACCEPTANCE.txt](CLEAN-PC-ACCEPTANCE.txt) on a clean Windows
10/11 x64 machine or clean Windows account. Report the first failure with the
installer hash, screenshots, and the relevant Sabi log from
`%LOCALAPPDATA%\Sabi\Sabi\sabi-*.log`.
