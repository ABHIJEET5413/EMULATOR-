# Phase 7: Design System and Application Shell

## Objective

Give Sabi a consistent, high-DPI native Windows interface while retaining the
verified QEMU, ADB, display-embedding, and input behavior from Versions 1–2.
This phase changes the host shell, not Android, QEMU command lines, or VM data.

## Scope

- Install a single Sabi palette and `QProxyStyle` at application startup.
- Use original bundled SVG icons for all current toolbar actions.
- Add a compact context bar for product identity, active instance, and Android
  state; retain the native Windows title bar.
- Keep the viewport central and use the existing right-hand control rail.
- Replace the profile combo box with a keyboard-accessible icon menu.
- Apply the shared theme to the existing instance launcher and settings dialog.

## Non-goals

- No new VM/ADB/QMP functionality.
- No custom title bar, copied competitor design, Google Play/GApps, cloud,
  telemetry, macros, or claims of an unimplemented feature.
- No changes to the physical-pixel input mapping; Phase 8 owns that work.

## Automated checks

`test_phase7_ui` runs offscreen and verifies the application palette, resource
icons, semantic colors, and accessibility metadata. Existing input, keymap,
ADB, instance, and installer tests remain mandatory regression checks.

## Local verification

On 1 November 2026, the Release build completed and all 11 registered CTest
suites passed after deploying Qt's required runtime and offscreen test plugin
into the disposable build directory. This verifies host UI resources and
regressions; the physical-DPI and touchpad checklist below still requires user
acceptance on the target display.

## Manual acceptance checklist

- [ ] At 100%, 125%, 150%, and 200% Windows scaling, toolbar and context icons
  are crisp and no text is clipped.
- [ ] Tab and Shift+Tab reach the profile button and all enabled actions;
  tooltips describe every icon-only button.
- [ ] Home, Back, Recents, volume, Power, Rotate, Screenshot, Install APK,
  Fullscreen, Settings, and Instances retain their prior behavior.
- [ ] Phone, Tablet, Desktop, and Fullscreen profile selections persist and
  update Android as before.
- [ ] A physical mouse and touchpad still enter the embedded Android display,
  click/drag correctly, and can return to the host rail.
- [ ] Resize, fullscreen, narrow windows, and instance switching retain an
  aspect-correct viewport and working native caption controls.
