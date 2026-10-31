# Attribution & Licenses

This document tracks dependencies and planned redistribution notices.
The Phase 6 installer scripts and full license bundle are implemented. Unchecked
source/notices inventory items below remain required before public distribution;
development builds do not claim that review is complete.

---

## Original Work (Sabi; formerly DroidBox)

| Component | License | Notes |
|---|---|---|
| Sabi host application | MIT | All code in `host-app/` |
| Build scripts & tooling | MIT | `image-build/`, `vm/scripts/` |
| Installer script | MIT | `installer/` |
| Documentation | MIT | `docs/` |

---

## Third-Party Components — Bundled in Installer

| Component | Version | License | How Used | Compliance |
|---|---|---|---|---|
| **QEMU** | 11.1.0, Windows ar7 fork `e470268ff446ec56728215a987ed166d5ed8fb64` | **GPLv2**, with component-specific notices | Separate VM executable | Exact [builder source commit](https://repo.or.cz/qemu/ar7.git/commit/e470268ff446ec56728215a987ed166d5ed8fb64). A stock upstream 11.1.0 archive is not a substitute for this fork. Final source/dependency bundle remains a packaging requirement. |
| **Qt 6** | 6.12.0 | **LGPL v3**, plus dependency licenses | UI framework; replaceable DLLs | Dynamically linked. Installed SBOMs identify qtbase `025bdad181de241e81bf853c8a2d7bf3d19261a9` and qtsvg `883a58532078d003cefef0a549bfe9f885ce41f5`; matching source archives collected locally. |
| **Bliss OS** | 14.10.3 | **Mixed**: AOSP Apache 2.0, Linux GPLv2, and individual app/library licenses | Android-x86 distribution; qcow2 image | Preserve the distribution's notices and identify matching kernel/Android sources. It is not licensed solely under Apache 2.0. Local init/first-boot modifications are in `image-build/`. |
| **Android platform-tools** (ADB) | 37.0.1 | **Apache 2.0** | `vm/adb/adb.exe` for guest communication | Upstream `vm/adb/NOTICE.txt` included. |
| **Neo Store** | 1.0.5 (`com.machiav3lli.fdroid`) | **GPLv3+** | Pre-installed F-Droid repository client | Actual client identified in the guest; [source project](https://github.com/NeoApplications/Neo-Store). Do not label it as the official F-Droid client. |
| **microG GmsCore / Companion / GsfProxy** | 0.3.3.240913 / 0.3.3.40226 / v0.1.0 | **Apache 2.0**, plus bundled dependency notices | Free Android compatibility implementations already in the FOSS image | Guest components use `org.microg.*` classes. [GmsCore sources](https://github.com/microg/GmsCore), [GsfProxy sources](https://github.com/microg/GsfProxy). Shared package identifiers alone do not establish that proprietary Google apps are present. |
| **MSVC Runtime** | v14; collected package 14.51.36247.0 | **Microsoft EULA** | C++ runtime redistributable | Retain package terms and verify redistribution rights for the installed Visual Studio license. |

---

## Third-Party Components — Build-Time Only (Not Bundled)

| Component | License | Purpose |
|---|---|---|
| CMake | BSD 3-Clause | Build system |
| Inno Setup 6.7.3 | Inno Setup License | Installer compiler; verify applicable commercial-use terms |
| Python 3 | PSF License | Image build scripts |
| Visual Studio 2022 | Microsoft EULA | C++ compiler |

---

## License Compliance Checklist

### QEMU (GPLv2)
- [x] QEMU is a **separate executable** — Sabi does NOT link to QEMU code
- [x] Sabi communicates with QEMU only via IPC (QMP socket, process launch)
- [x] Full GPLv2 license text included in `installer/licenses/QEMU-LICENSE.txt`
- [x] Source-offer text linked from the installer license screen
- [x] Exact Windows fork commit identified (not the older GitHub mirror tip)
- [ ] Matching QEMU, dependency, firmware sources/notices included in final distribution

### Qt 6 (LGPL v3)
- [x] Qt libraries are **dynamically linked** (`.dll` files)
- [x] User can replace Qt DLLs with their own build
- [x] Full LGPL v3 text included in `installer/licenses/QT-LICENSE.txt`
- [x] Pinned Qt Base/SVG sources and source-request information staged for installer
- [x] Sabi's own source is available (MIT license)

### Bliss OS (mixed licenses)
- [x] Apache 2.0 text included in `installer/licenses/BLISS-LICENSE.txt`
- [x] Local startup/provisioning changes retained in `image-build/`
- [ ] Original kernel/Android build provenance and full third-party notices included
- [ ] Inspect the exact FOSS image for third-party firmware, codecs, drivers, and preinstalled apps and verify their individual redistribution terms. The [Bliss OS licensing page](https://blissos.org/licensing.html) says FOSS/Vanilla images may still include proprietary redistributables; the variant label alone does not complete that check.

### Neo Store (GPLv3+)
- [x] Pre-installed `com.machiav3lli.fdroid` 1.0.5 identified
- [x] GPLv3 text included in `installer/licenses/FDROID-LICENSE.txt`
- [ ] Matching Neo Store source release and build/dependency information included

---

## Source distribution status

The installer includes `SOURCE-OFFER.txt` using the actual project issue tracker
as the source-request channel, and `SOURCES.md` identifies the collected primary
sources. The offer text is for distributor-approved releases; the build wrapper
requires a complete reviewed release inventory for `-ForDistribution`.
The distributor must provide the applicable complete corresponding sources and
notices for the exact redistributed binaries, including required build files,
local changes, submodules, and dependency components. A project homepage or an
unrelated upstream release does not establish correspondence. Do not mark the
unchecked packaging items complete until the final payload has been inspected.

`installer/source-lock.json` pins the clean image and core source hashes.
Bundle preparation copies the original QEMU/ADB notices, Qt SBOMs, current host
and image-build source, and exact Qt/QEMU archives into the offline payload.
Microsoft's published Visual C++ 2015-2022 runtime terms are retained as DOCX and
plain text in `installer/licenses`; the exact bundled package's applicable terms
remain part of the distributor's final inventory review.

The Windows builder identifies its source repository as
`https://repo.or.cz/qemu/ar7.git`, whose verified master ref was
`e470268ff446ec56728215a987ed166d5ed8fb64` on 2026-10-21. This commit merges
`v11.1.0` and matches the installed binary's `-ge470268ff4` suffix. The linked
GitHub mirror's ar7 branch was older (`54e84cdc7af22e1b6f3f309fea25404fac17beec`),
which explains the failed commit lookups there.

The exact fork source tree, including `make-installers-all`, was exported through
Git and validated locally as
`qemu-ar7-e470268ff446ec56728215a987ed166d5ed8fb64.verified.tar.gz`.
Its SHA-256 is
`d917de0eab28e34c1e281a789b3a4c9878219bb54d7343cb4a07d33dbc79e660`.
This identifies the QEMU source tree; required submodule/dependency source
archives and notices still belong in the final installer release inventory.

---

## How to Verify Dynamic Linking

```powershell
# Verify Sabi.exe does not statically link Qt:
dumpbin /dependents Sabi.exe
# Should show Qt6Core.dll, Qt6Widgets.dll, Qt6Network.dll, etc.

# Verify QEMU is a separate process:
tasklist | findstr qemu
# Should show qemu-system-x86_64.exe as a separate PID
```
