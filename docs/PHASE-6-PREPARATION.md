# Phase 6 prerequisite fixes

Prerequisite verification recorded on 2026-10-21. Installer implementation now
lives in `installer/`; see [build instructions](../installer/BUILD.md) and the
[Phase 6 acceptance checklist](PHASE-6-TESTING.md) for its current scope.

## Clean guest authorization

The former init fragment started TCP `adbd` while Android's global
`adb_enabled` setting remained `0`. Already-authorized development keys worked,
but new hosts received AUTH packets without a framework RSA approval dialog.

`droidbox-first-boot.sh`, installed by `provision-adb-image.sh`, now enables the
framework's ADB authorization manager before init starts TCP ADB. Authentication
remains enabled (`ro.adb.secure=1`); no host key is inserted. The first-boot script
also records setup completion and selects Bliss Launcher once per writable
overlay, avoiding the first-run launcher chooser. Later boots preserve launcher
choices. Generic image preparation rejects inherited nonempty `adb_keys` files.

The host bridge now distinguishes authorization from an offline guest. While
awaiting approval, it emits `authorizationRequired`, updates the window status,
and observes only `adb get-state` on the existing transport for up to three
minutes. A timeout or lost transport stops the wait with an actionable message.
It does not repeatedly run `adb connect` or disconnect a pending authorization.

### Live verification

- Created `vm/images/bliss-distribution-v2.qcow2` without supplying a host key.
- The detached qcow2 passed integrity checking. Read-only inspection found an
  empty data directory, no authorized keys, `ro.adb.secure=1`, and correctly owned
  and SELinux-labeled startup files.
- Booted two separate fresh overlays. Both reached Bliss Launcher without a
  chooser and displayed the RSA prompt when an unrecognized host connected.
- In the standalone guest, selecting Allow produced `device`, boot completed
  `1`, global `adb_enabled=1`, and `ro.adb.secure=1`.
- Rebuilt the host in `cmake-build-phase6-fixes` and launched the isolated
  `phase6-auth-acceptance` instance. The embedded guest showed the RSA dialog and
  the host showed approval instructions. Selecting Allow changed the host to
  **Status: Ready**, without restarting it.
- That host log recorded exactly **one** `adb connect localhost:5568` and **zero**
  `adb disconnect localhost:5568` calls while waiting. The connection continued
  to boot polling and display configuration after approval.
- Approval was exercised without persistent key storage in the acceptance
  overlay. The immutable clean base remained key-free.
- Existing default instance data and its running QEMU process were preserved.

Clean base SHA-256:
`f46bc32872303d8fa8abd1fc81d4558b7996fb23d4e49d19afe3ce2e838a090d`.

## QEMU source provenance

The installed Windows executable reports
`11.1.0 (v11.1.0-12130-ge470268ff4)`. Its source is the Windows builder's
`https://repo.or.cz/qemu/ar7.git`, at the full commit
`e470268ff446ec56728215a987ed166d5ed8fb64` (merge of `v11.1.0`).
The older GitHub mirror branch explains the unsuccessful hash lookups there.

Retrieved the exact commit through Git using HTTP/1.1 from WSL, exported its
source tree including `make-installers-all`, and validated the compressed archive.
Local input:
`_downloads/phase6/sources/qemu-ar7-e470268ff446ec56728215a987ed166d5ed8fb64.verified.tar.gz`.
Archive SHA-256:
`d917de0eab28e34c1e281a789b3a4c9878219bb54d7343cb4a07d33dbc79e660`.

`docs/ATTRIBUTION.md` now records the actual Qt/QEMU/Bliss versions, the installed
Neo Store client, and the microG compatibility components. The invalid placeholder
source offer and inaccurate Apache-only description of Bliss were removed.

## Build and manual review

```powershell
cmake -B cmake-build-phase6-fixes -S host-app -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64" -DBUILD_TESTING=OFF
cmake --build cmake-build-phase6-fixes --config Release --target Sabi
```

The Release build and the live checks above passed. The automated regression
suites were not rerun in this prerequisite-fix task.

- Create a new overlay from the clean image and confirm it reaches Home.
- Confirm an unknown host receives an RSA dialog and the desktop displays approval instructions.
- Select Allow and confirm Ready appears without reconnecting or restarting the host.
- Review the optional Always allow choice, later boots, denial, and the three-minute timeout.
- Confirm a factory-reset overlay starts without previously authorized host keys.

## Handoff to installer implementation

The installer now stages the clean image under the installed instance manager's
`bliss-provisioned.qcow2` name and implements the approved packaging flow.
Complete the dependency/submodule/firmware and guest license/source review before
public distribution, and run the clean-machine install/uninstall checklist before
acceptance. Identifying the exact QEMU source tree alone does not complete that
redistribution inventory.
