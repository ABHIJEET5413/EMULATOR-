# DroidBox — Windows Desktop Android Emulator

> A native Windows desktop application that runs a full Android environment
> (Bliss OS) inside a QEMU virtual machine with WHPX hardware acceleration.
> Ships as a single offline `setup.exe`.

---

## Key Features

| Feature | Implementation |
|---|---|
| **Android VM** | Bliss OS (Android-x86) on QEMU + WHPX |
| **Host App** | C++ / Qt 6 desktop application |
| **Input** | Mouse→touch, full keyboard, keymap engine |
| **APK Install** | Drag-drop, button, `.apk` file association |
| **Networking** | QEMU user-mode NAT (internet) + ADB port-forward |
| **Camera** | USB webcam passthrough or test-pattern fallback |
| **App Store** | F-Droid (no Google Play / GApps) |
| **Multi-Instance** | qcow2 overlays on a read-only base image |
| **Installer** | Single offline `setup.exe` via Inno Setup |

---

## Repository Layout

```
emulator/
├── host-app/          # Qt 6 / C++ desktop application
│   ├── src/           #   Source files (.cpp)
│   ├── include/       #   Header files (.h)
│   └── CMakeLists.txt
├── vm/                # QEMU launch configs and helper scripts
│   ├── configs/       #   Instance JSON configs
│   └── scripts/       #   PowerShell helpers (WHPX check, etc.)
├── image-build/       # Pipeline to produce the pre-installed qcow2
├── installer/         # Inno Setup script for setup.exe
├── docs/              # Architecture, attribution, phase guides
│   └── phase-guides/  #   Detailed per-phase implementation docs
├── tests/             # CLI test harnesses, integration tests
├── .agents/           # AI agent skills & rules (phase prompts)
│   ├── skills/        #   One skill per phase
│   └── rules/         #   Project-wide coding rules
└── README.md          # ← you are here
```

---

## Prerequisites (Development)

| Tool | Version | Purpose |
|---|---|---|
| Visual Studio 2022 | 17.x | MSVC C++ compiler |
| CMake | ≥ 3.21 | Build system |
| Qt 6 | 6.5+ | UI framework (LGPL, dynamic linking) |
| QEMU | 8.x+ (Windows) | Virtual machine engine |
| Android platform-tools | latest | ADB for guest control |
| Inno Setup | 6.x | Offline installer builder |
| Python 3 | 3.10+ | Image-build scripts |

### Runtime Prerequisites (End User)

- Windows 10/11 (64-bit)
- CPU with VT-x / AMD-V enabled in BIOS
- Windows Hypervisor Platform feature enabled
- ≥ 8 GB RAM (4 GB for VM)
- ≥ 15 GB free disk space

---

## Build & Run (Development)

```powershell
# 1. Configure
cmake -B build -S host-app -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/msvc2019_64"

# 2. Build
cmake --build build --config Release

# 3. Run (with QEMU + image already in vm/)
.\build\Release\DroidBox.exe
```

---

## Development Phases

| # | Phase | Description |
|---|---|---|
| 1 | VM Control | Launch/stop QEMU+WHPX, QMP, pause/resume/snapshot |
| 2 | Display & Input | Embed guest display in Qt window, mouse/keyboard |
| 3 | Keymap Engine | Keyboard → screen taps/swipes with JSON profiles |
| 4 | ADB Integration | APK install, file push, nav buttons, screenshot |
| 5 | Multi-Instance | Instance manager, settings UI, camera |
| 6 | Installer | Inno Setup `setup.exe` with everything bundled |

Each phase has a detailed guide in `docs/phase-guides/` and a matching
AI skill in `.agents/skills/`.

---

## License

- **DroidBox host app**: MIT License
- **QEMU**: GPLv2 — linked dynamically, source offer included
- **Qt 6**: LGPL v3 — linked dynamically, source offer included
- **Bliss OS**: Apache 2.0
- **F-Droid**: GPLv3

See [docs/ATTRIBUTION.md](docs/ATTRIBUTION.md) for full details.
