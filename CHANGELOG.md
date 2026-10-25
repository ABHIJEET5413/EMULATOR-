# Changelog

## 2.0.0 — Sabi v2 (in development)

- Renamed the desktop application and installer from DroidBox to Sabi.
- Renamed the executable to `Sabi.exe`, the installer script to `sabi.iss`,
  and installer output to `SabiSetup.exe`.
- Added a one-time non-destructive migration of a v1 per-user instance root
  into Sabi's data root. The migration moves the instance directory only when
  the new root does not exist; it does not alter qcow2 overlays or backing
  files.
- Changed new runtime logs to `sabi-<pid>.log`.

## 0.1.0 — DroidBox v1

- Initial Windows Android emulator release candidate.
