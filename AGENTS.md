# AGENTS.md — DroidBox Project Instructions

> Codex: Read this file first. It tells you how to build, test, and
> navigate the DroidBox Android emulator project.

## Project Overview
DroidBox is a native Windows desktop Android emulator (C++17 / Qt 6).
It runs Bliss OS (Android-x86) inside QEMU with WHPX hardware acceleration.
Ships as a single offline `setup.exe`.

## Build Commands
```bash
# Configure (adjust Qt path to your install)
cmake -B build -S host-app -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/msvc2019_64"
# Build
cmake --build build --config Release
# WHPX check
powershell -ExecutionPolicy Bypass -File vm/scripts/check-whpx.ps1
```

## Coding Standards
- C++17, MSVC 2022. `PascalCase` classes, `camelCase` methods, `m_` members.
- Use `#pragma once`. Qt headers first, then project, then std.
- Smart pointers or Qt parent-child ownership — never raw new/delete.
- Log via `qCritical/qWarning/qInfo/qDebug`. User errors get `QMessageBox`.
- Qt linked **dynamically** (LGPL). QEMU is a separate process (GPLv2).
- Full rules: `.agents/rules/project-rules.md`

## Repo Layout
```
host-app/src/, include/   — C++ source and headers
host-app/CMakeLists.txt   — Build config
vm/configs/               — Instance JSON configs
vm/scripts/               — PowerShell helpers (WHPX check)
image-build/              — qcow2 disk image build pipeline
installer/                — Inno Setup script
docs/ARCHITECTURE.md      — Module descriptions (keep updated)
docs/ATTRIBUTION.md       — Third-party licenses
docs/phase-guides/        — Detailed per-phase implementation guides
tests/                    — CLI test harnesses
```

## Phase-Based Development (STRICT)
1→VM Control, 2→Display & Input, 3→Keymap Engine,
4→ADB Integration, 5→Multi-Instance & Camera, 6→Installer.
**Never skip a phase. Never start N+1 before N is user-approved.**

## How to Implement a Phase
When told "implement phase N":
1. Read the skill file: `docs/phase-guides/phase-N-*.md`
2. Read project rules: `.agents/rules/project-rules.md`
3. Read current arch: `docs/ARCHITECTURE.md`
4. Write a short plan, then implement
5. Update `docs/ARCHITECTURE.md` with new modules
6. Write/update tests in `tests/`
7. Provide: build commands, manual test checklist, hardware-specific test commands
8. **STOP** and wait for user to test

## Things to NEVER Do
- Create a web app or localhost server
- Use HAXM acceleration
- Bundle Google Play / GApps
- Statically link Qt
- Skip documentation updates
- Pass off stubs as working features
