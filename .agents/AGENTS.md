# DroidBox Agent Instructions

> Master instructions for AI agents working on DroidBox.
> Read `.agents/rules/project-rules.md` for coding standards.
> Read the relevant phase skill before implementing each phase.

## Project Summary

DroidBox is a native Windows desktop Android emulator. It runs Bliss OS
(Android-x86) inside QEMU with WHPX hardware acceleration. The host app
is built with C++ and Qt 6. Everything ships as a single offline
`setup.exe`.

## How to Implement a Phase

When the user says "implement phase N":

1. **Read the skill**: `.agents/skills/phase-N-*/SKILL.md`
2. **Read the guide**: `docs/phase-guides/phase-N-*.md`
3. **Read the rules**: `.agents/rules/project-rules.md`
4. **Read current architecture**: `docs/ARCHITECTURE.md`
5. **Write a short plan** as a message to the user
6. **Implement** the code following the guide
7. **Update** `docs/ARCHITECTURE.md`
8. **Write/update tests** in `tests/`
9. **Provide**:
   - Build instructions (exact commands)
   - Manual test checklist
   - Any hardware-specific test commands the user should run
10. **STOP** and wait for the user to test

## Phase Order (Strict)

1. VM Control → 2. Display & Input → 3. Keymap Engine →
4. ADB Integration → 5. Multi-Instance & Camera → 6. Installer

Never skip a phase. Never start N+1 before N is approved.

## Key File Locations

| What | Where |
|---|---|
| Host app source | `host-app/src/`, `host-app/include/` |
| CMake config | `host-app/CMakeLists.txt` |
| VM scripts | `vm/scripts/` |
| Instance configs | `vm/configs/` |
| Image build | `image-build/` |
| Installer | `installer/` |
| Phase guides | `docs/phase-guides/` |
| Architecture | `docs/ARCHITECTURE.md` |
| Attribution | `docs/ATTRIBUTION.md` |
| Tests | `tests/` |
