# Sabi Development Phases

> Quick reference for all phases. Each phase has a detailed guide in
> `phase-guides/` and a matching AI skill in `.agents/skills/`.

---

## Phase Summary

| Phase | Name | Key Deliverables | Dependencies |
|---|---|---|---|
| 1 | VM Control | QemuController, QmpClient, CLI test | None |
| 2 | Display & Input | MainWindow, DisplayEmbed, InputHandler | Phase 1 |
| 3 | Keymap Engine | KeymapEngine, JSON profiles | Phase 2 |
| 4 | ADB Integration | AdbBridge, APK install, nav buttons | Phase 3 |
| 5 | Multi-Instance | InstanceManager, SettingsDialog, Camera | Phase 4 |
| 6 | Installer | Inno Setup script, setup.exe | Phase 5 |

---

## Phase 1: VM Control
**Files**: `qemu_controller.{h,cpp}`, `qmp_client.{h,cpp}`, `main.cpp`
**Guide**: [phase-1-vm-control.md](phase-guides/phase-1-vm-control.md)
**Skill**: `.agents/skills/phase-1-vm-control/SKILL.md`

Launch QEMU with WHPX, connect QMP, pause/resume/snapshot.
CLI test harness only — no GUI.

---

## Phase 2: Display Embedding & Input
**Files**: `main_window.{h,cpp}`, `display_embed.{h,cpp}`, `input_handler.{h,cpp}`
**Guide**: [phase-2-display-input.md](phase-guides/phase-2-display-input.md)
**Skill**: `.agents/skills/phase-2-display-input/SKILL.md`

Embed QEMU window inside Qt app (SetParent or VNC fallback).
Mouse/keyboard forwarding. Side toolbar with stub buttons.

---

## Phase 3: Keymap Engine
**Files**: `keymap_engine.{h,cpp}`, `keymap_profile.{h,cpp}`, JSON profiles
**Guide**: [phase-3-keymap-engine.md](phase-guides/phase-3-keymap-engine.md)
**Skill**: `.agents/skills/phase-3-keymap-engine/SKILL.md`

Map keyboard keys to screen taps/swipes. Loadable JSON profiles.
Gaming support (WASD, mouse aim).

---

## Phase 4: ADB Integration
**Files**: `adb_bridge.{h,cpp}`, toolbar button wiring
**Guide**: [phase-4-adb-integration.md](phase-guides/phase-4-adb-integration.md)
**Skill**: `.agents/skills/phase-4-adb-integration/SKILL.md`

ADB auto-connect, APK install (drag-drop + button), file push,
media scan, screenshot, navigation buttons.

---

## Phase 5: Multi-Instance, Settings & Camera
**Files**: `instance_manager.{h,cpp}`, `settings_dialog.{h,cpp}`, `CAMERA.md`
**Guide**: [phase-5-multi-instance.md](phase-guides/phase-5-multi-instance.md)
**Skill**: `.agents/skills/phase-5-multi-instance/SKILL.md`

Create/clone/delete instances. Settings UI. Camera evaluation.

---

## Phase 6: Offline Installer
**Files**: `installer/sabi.iss`, license files, build scripts
**Guide**: [phase-6-installer.md](phase-guides/phase-6-installer.md)
**Skill**: `.agents/skills/phase-6-installer/SKILL.md`

Inno Setup producing `SabiSetup.exe`. WHPX check, shortcuts,
.apk file association, license notices.

---

## Stretch Goals (After Phase 6)

| Goal | Description |
|---|---|
| VirGL GPU | Hardware-accelerated OpenGL via VirGL |
| Macro Recorder | Record and replay touch sequences |
| Shared Clipboard | Copy/paste between host and guest |
| Game Profiles | Pre-made keymaps for popular games |
| Auto-Update | Check for updates on launch |
