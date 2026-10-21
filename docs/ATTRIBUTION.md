# Attribution & Licenses

> This document lists every third-party component bundled with or used by
> DroidBox, its license, and what DroidBox itself contributes.

---

## Original Work (DroidBox)

| Component | License | Notes |
|---|---|---|
| DroidBox host application | MIT | All code in `host-app/` |
| Build scripts & tooling | MIT | `image-build/`, `vm/scripts/` |
| Installer script | MIT | `installer/` |
| Documentation | MIT | `docs/` |

---

## Third-Party Components — Bundled in Installer

| Component | Version | License | How Used | Compliance |
|---|---|---|---|---|
| **QEMU** | 8.x | **GPLv2** | VM engine; `qemu-system-x86_64.exe` bundled | Dynamically linked. Full source code available at [qemu.org/download](https://www.qemu.org/download/). Source offer included in installer's license dialog. |
| **Qt 6** | 6.5+ | **LGPL v3** | UI framework; DLLs bundled alongside DroidBox.exe | **Dynamically linked** (user can replace Qt DLLs). Qt source available at [qt.io](https://www.qt.io/download-open-source). Source offer in installer. |
| **Bliss OS** | latest | **Apache 2.0** | Android-x86 distribution; pre-installed qcow2 image | Apache 2.0 notice included. Bliss OS source at [blissos.org](https://blissos.org/). |
| **Android platform-tools** (ADB) | 37.0.1 | **Apache 2.0** | `vm/adb/adb.exe` for guest communication | Upstream `vm/adb/NOTICE.txt` included. |
| **F-Droid** | latest | **GPLv3** | Pre-installed app store in guest image | GPLv3 text included. Source at [f-droid.org](https://f-droid.org/). |
| **MSVC Runtime** | 2022 | **Microsoft EULA** | C++ runtime redistributable | Distributed per Microsoft redistribution terms. |

---

## Third-Party Components — Build-Time Only (Not Bundled)

| Component | License | Purpose |
|---|---|---|
| CMake | BSD 3-Clause | Build system |
| Inno Setup | Inno Setup License (freeware) | Installer compiler |
| Python 3 | PSF License | Image build scripts |
| Visual Studio 2022 | Microsoft EULA | C++ compiler |

---

## License Compliance Checklist

### QEMU (GPLv2)
- [x] QEMU is a **separate executable** — DroidBox does NOT link to QEMU code
- [x] DroidBox communicates with QEMU only via IPC (QMP socket, process launch)
- [x] Full GPLv2 license text included in `installer/licenses/QEMU-LICENSE.txt`
- [x] Source code offer included in installer's license screen
- [x] QEMU modifications (if any) documented and source provided

### Qt 6 (LGPL v3)
- [x] Qt libraries are **dynamically linked** (`.dll` files)
- [x] User can replace Qt DLLs with their own build
- [x] Full LGPL v3 text included in `installer/licenses/QT-LICENSE.txt`
- [x] Source code offer for Qt included in installer
- [x] DroidBox's own source is available (MIT license)

### Bliss OS (Apache 2.0)
- [x] Apache 2.0 notice included
- [x] No modifications to Bliss OS source (using pre-built images)
- [x] If modified: changes documented

### F-Droid (GPLv3)
- [x] Pre-installed as an APK in the guest image
- [x] GPLv3 text included
- [x] Source available at f-droid.org

---

## Source Offer Statement

> DroidBox includes components licensed under GPLv2 (QEMU) and LGPL v3
> (Qt 6). You may obtain the complete corresponding source code for these
> components by visiting the URLs listed above, or by contacting
> [your-email@example.com] for a physical copy (cost of distribution
> applies). This offer is valid for three years from the date of
> distribution.

---

## How to Verify Dynamic Linking

```powershell
# Verify DroidBox.exe does not statically link Qt:
dumpbin /dependents DroidBox.exe
# Should show Qt6Core.dll, Qt6Widgets.dll, Qt6Network.dll, etc.

# Verify QEMU is a separate process:
tasklist | findstr qemu
# Should show qemu-system-x86_64.exe as a separate PID
```
