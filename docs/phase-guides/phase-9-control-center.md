# Phase 9 — Professional Control Center and Settings Safety

## Outcome

Sabi provides a clear, per-instance control center without making unverified
emulator capabilities appear available. It replaces the former dense settings
form with accessible pages and retains `InstanceManager` as the sole authority
for saving a configuration.

## Implemented pages

1. **General** identifies the instance and makes its per-instance scope clear.
   Renaming and destructive disk operations remain in the Instances window.
2. **Display** has Phone, Tablet, Desktop, Fullscreen, and Custom profile
   cards, plus resolution, DPI, and custom dimensions. Android can receive a
   live `wm size`/`wm density` update after boot; QEMU's initial raster size
   still requires the next VM start.
3. **Performance** exposes real per-instance QEMU CPU/RAM launch limits. It
   does not claim an FPS, booster, or eco mode.
4. **Controls** truthfully states that JSON keymaps exist and that the visual
   editor belongs to Phase 10.
5. **Storage** reports base/overlay filenames and opens the instance folder.
   Reset/delete stay in the confirmed Instances workflow.
6. **Network / ADB** edits QMP and ADB ports only while stopped. They must be
   different; `InstanceManager` additionally checks availability/conflicts at
   save time.
7. **Advanced** labels USB webcam passthrough as experimental and keeps the
   unavailable test-pattern option disabled.
8. **Diagnostics** opens local logs, copies a redacted summary, or saves a
   user-selected plain-text report.

## Data and privacy contract

`DiagnosticsService` never uploads anything and does not create a network
listener. A report includes only timestamp, OS label, instance resource/display
metadata, ports, camera mode, base/overlay **filenames**, and the symbolic
`<Sabi local data>` log location. It excludes ADB keys, guest files, APK
contents, log contents, and complete user paths. Saving is explicitly initiated
by the user.

## Safety behavior

- Save is blocked when QMP and ADB use the same port; cancellation changes
  nothing.
- Restore configurable defaults only resets CPU, RAM, display, and camera. It
  preserves name, disk paths, and ports.
- Port widgets remain disabled for a running VM; the manager still validates a
  configuration before writing its JSON.
- Each page uses standard keyboard-accessible Qt controls and the shared Sabi
  high-DPI theme.

## Automated verification

`test_control_center` verifies page navigation, profile persistence, default
scope, local validation, and redacted report saving. `test_phase5_ui` remains a
regression test for settings persistence through the instance manager.

## Manual acceptance checklist

- [ ] Open Settings from a stopped instance; move through every page using
  mouse and keyboard only (Tab, arrows, Space, Enter).
- [ ] Choose Phone, Tablet, Desktop, Fullscreen, and Custom; save each one,
  restart the VM, and confirm its saved display values are used.
- [ ] With Android ready, change display/DPI, save, and verify the guest
  changes without stretching or input drift. Confirm the next boot uses the
  selected initial raster size.
- [ ] Start the instance; confirm QMP/ADB port fields are visibly unavailable.
- [ ] Attempt equal QMP/ADB ports on a stopped instance; confirm Save explains
  the issue and does not modify `config.json`.
- [ ] Change CPU, RAM, display, camera, and ports; select Restore configurable
  defaults; verify only the documented fields reset.
- [ ] Use Open instance folder and Open local logs. Copy and save diagnostics;
  inspect the report to confirm it contains no full host path, APK, guest file,
  ADB key, or log content.
- [ ] At Windows 100%, 125%, 150%, and 200% scaling, confirm navigation,
  cards, text, buttons, and dialogs remain readable and usable.
