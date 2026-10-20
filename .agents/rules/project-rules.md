# DroidBox — Agent Rules

> These rules apply to ALL phases of DroidBox development.
> Read and follow them before writing any code.

---

## Project Identity

- **Name**: DroidBox
- **Type**: Native Windows desktop Android emulator
- **Stack**: C++17, Qt 6 (LGPL, dynamic linking), QEMU 8.x, ADB
- **Target OS**: Windows 10/11 x64
- **Build system**: CMake ≥ 3.21

---

## Code Standards

### C++ Style
- **Standard**: C++17 (MSVC 2022 compatible)
- **Naming**: `PascalCase` for classes, `camelCase` for methods/variables, `UPPER_SNAKE` for constants, `m_` prefix for member variables
- **Headers**: Use `#pragma once`, not include guards
- **Includes**: Qt headers first, then project headers, then std library
- **Smart pointers**: Use `std::unique_ptr` / `std::shared_ptr`, never raw `new`/`delete` for ownership
- **Qt memory**: Use Qt parent-child ownership where appropriate

### Documentation
- Every `.h` file gets a file-level doc comment explaining the class purpose
- Every public method gets a brief doc comment
- Non-obvious logic gets inline comments
- **ARCHITECTURE.md must be updated** when adding/modifying modules
- **ATTRIBUTION.md must be updated** when adding third-party dependencies

### Error Handling
- All errors must be **human-readable** (no raw error codes without explanation)
- Use `qCritical()`, `qWarning()`, `qInfo()`, `qDebug()` for logging
- Log to file via `QFile` logger installed at startup
- Show `QMessageBox` for user-facing errors
- QEMU/ADB failures must suggest specific fixes

### File Organization
```
host-app/
├── CMakeLists.txt          # Build configuration
├── src/
│   ├── main.cpp            # Entry point, logging setup
│   ├── qemu_controller.cpp # VM lifecycle management
│   ├── qmp_client.cpp      # QMP protocol client
│   ├── display_embed.cpp   # Guest display embedding
│   ├── input_handler.cpp   # Mouse/keyboard translation
│   ├── keymap_engine.cpp   # Key→tap/swipe mapping
│   ├── adb_bridge.cpp      # ADB command interface
│   ├── instance_manager.cpp# Multi-instance management
│   ├── settings_manager.cpp# JSON settings I/O
│   └── main_window.cpp     # Qt main window + toolbar
├── include/
│   ├── qemu_controller.h
│   ├── qmp_client.h
│   ├── display_embed.h
│   ├── input_handler.h
│   ├── keymap_engine.h
│   ├── adb_bridge.h
│   ├── instance_manager.h
│   ├── settings_manager.h
│   └── main_window.h
└── resources/
    ├── icons/              # Toolbar icons
    └── keymaps/            # Default keymap profiles
```

---

## Build Rules

- Always use CMake as the build system
- Qt must be linked **dynamically** (LGPL compliance)
- QEMU is a **separate executable** — never link QEMU code into the host app
- ADB is a **separate executable** — invoke via `QProcess`
- Target architecture: x64 only
- Build types: Debug (for development), Release (for installer)

---

## Testing Rules

- Every phase must include a **CLI test harness** in `tests/`
- Test harnesses are standalone executables that exercise the phase's functionality
- Tests must be runnable without a GUI where possible
- Each phase ends with a **manual test checklist** in the phase guide
- Never claim something works unless it was actually built and tested
- For hardware-specific features (WHPX, webcam), provide exact commands to test and what output to expect

---

## VM Rules

- QEMU acceleration: **WHPX only** (not HAXM, not TCG)
- Disk images: qcow2 format, always use overlays on a read-only base
- Networking: QEMU user-mode NAT (`-netdev user`)
- ADB: TCP connection via QEMU host port forward (`hostfwd=tcp::5555-:5555`)
- Input device: `-device usb-tablet` (absolute positioning)
- Display: `-display sdl` (for SetParent approach) or `-vnc :0` (for VNC approach)

---

## Phase Workflow

For each phase:
1. **Read the phase skill** (`.agents/skills/phase-N-*/SKILL.md`)
2. **Read the phase guide** (`docs/phase-guides/phase-N-*.md`)
3. **Write a short plan** before coding
4. **Implement** following the guide's class-by-class breakdown
5. **Update** `docs/ARCHITECTURE.md` with any new/changed modules
6. **Write tests** in `tests/`
7. **Provide build instructions** and a manual test checklist
8. **STOP and wait** for the user to test before starting next phase

---

## Forbidden Actions

- ❌ Do NOT create a web application or localhost server
- ❌ Do NOT use HAXM acceleration
- ❌ Do NOT bundle Google Play / GApps
- ❌ Do NOT statically link Qt
- ❌ Do NOT link QEMU source code into the host app
- ❌ Do NOT skip writing documentation
- ❌ Do NOT use placeholder/stub implementations passed off as features
- ❌ Do NOT start Phase N+1 before Phase N is tested and approved
- ❌ Do NOT use `new`/`delete` for memory management (use smart pointers or Qt parent ownership)
- ❌ Do NOT hardcode paths — use `QStandardPaths` and relative paths from the executable
