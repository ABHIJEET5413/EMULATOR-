# Phase 4 audit and verification

Audit scope: tracked C++ modules, headers, CMake targets, existing tests,
image preparation instructions, configuration, and documentation. Phase 5
and Phase 6 remain planned work.

## Verified fixes

- **Guest ADB startup:** the guest had `10.0.2.15/24`, completed boot, and
  `persist.sys.usb.config=none`, but `init.svc.adbd=stopped` and no TCP listener.
  `image-build/droidbox-adb.rc` explicitly starts authenticated TCP ADB on
  port 5555 after boot. The development instance retains its authorized
  public key in `/data/misc/adb/adb_keys`; no private keys are stored here.
  Experimental initramfs/system-root keys and diagnostic scripts were removed.
- **Shared ADB server:** the app no longer kills the system ADB server on startup.
- **Transport failure:** offline and unauthorized responses stop retries with
  guest diagnostic guidance; real ADB reports these failures on stderr.
- **Boot race:** a TCP accept from QEMU before Android boot no longer triggers
  `adb connect`. The bridge waits for an actual CNXN/AUTH protocol packet
  from `adbd`, then connects with the normal ADB client.
- **Stale callbacks:** completed commands from a previous connection cannot
  trigger install success, media scan, or screenshot callbacks on a new guest.
- **Screenshots:** truncated PNG data is rejected before atomic file saving.
- **Input:** extended keys use QMP XT numeric codes rather than Linux keycodes.
- **Keymaps:** punctuation and page keys survive saving/reloading; swipe step
  calculation no longer overflows at the largest accepted duration.
- **Display:** initial native child size uses Windows DPI scaling consistently.
- **VM lifecycle:** startup is blocked while a disk snapshot operation runs;
  stderr history is bounded like stdout history.
- **Configuration:** CMake regenerates the staged default config, and runtime
  lookup supports launching from the build directory. A stale staged config
  previously selected a different development overlay.
- **Repository hygiene/docs:** downloaded ADB binaries and backup disks are
  ignored; packaging claims and outdated module descriptions were corrected.

## Automated checks

Release build and four CTest suites pass. Regressions cover stderr offline
errors, explicit reconnect after repair, preservation of the shared server,
stale install callbacks, malformed PNG output, punctuation profile roundtrip,
and extended navigation key mapping.

The WHPX startup probe and Phase 1 pause/resume harness also passed. Firmware
and Windows optional-feature metadata could not be fully queried without an
elevated shell, but QEMU actually initialized WHPX and returned its QMP greeting.

`test_adb_live` is an explicit guest test, excluded from CTest. It passed
against the repaired Bliss guest: authenticated connection and boot query,
real APK reinstall, PNG file push and media scan, valid screenshot, and Home,
Back, Recents, and volume commands. The APK used was the F-Droid client already
present in this image, pulled and reinstalled with its existing signature.

After removing the experimental image changes, a clean GUI boot selected the
correct default overlay, made one `adb connect` call after listener readiness,
reported `Status: Ready`, and started the APK supplied on the command line.
Windows UI Automation invoked Home, Back, Recents, both volume controls, and
Fullscreen. The real Install APK dialog submitted an APK installation, and
the Screenshot dialog saved a valid PNG in the Windows Pictures directory.
The live harness passed again against this clean boot.

## Build and repeat

```powershell
cmake -B build -S host-app -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64"
cmake --build build --config Release
$env:PATH = "C:\Qt\6.12.0\msvc2022_64\bin;$env:PATH"
ctest --test-dir build -C Release --output-on-failure

# Start the GUI; it auto-connects to the provisioned instance.
.\build\Release\DroidBox.exe

# Once Ready, this test installs/reinstalls the supplied APK.
.\build\Release\test_adb_live.exe --adb vm/adb/adb.exe --apk C:/path/to/app.apk
```

The guest disk is intentionally ignored by Git. A new checkout needs a Bliss
image prepared as described in `image-build/README.md`; pushing source does
not distribute this machine's repaired qcow2 overlay.

## Guest diagnostic commands

```powershell
.\vm\adb\adb.exe connect localhost:5555
.\vm\adb\adb.exe -s localhost:5555 get-state
.\vm\adb\adb.exe -s localhost:5555 shell getprop init.svc.adbd
.\vm\adb\adb.exe -s localhost:5555 shell getprop service.adb.tcp.port
.\vm\adb\adb.exe -s localhost:5555 shell getprop sys.boot_completed
.\vm\adb\adb.exe -s localhost:5555 shell input keyevent 3

# Binary-safe screenshot capture, including Windows PowerShell 5.1.
.\vm\adb\adb.exe -s localhost:5555 shell screencap -p /sdcard/Download/phase4.png
.\vm\adb\adb.exe -s localhost:5555 pull /sdcard/Download/phase4.png build/phase4.png
```

Expected results: `device`, `running`, `5555`, and `1`. If a connection fails,
inspect the guest listener and authorization before reconnecting. An open
Windows port 5555 proves only that QEMU owns its forwarding socket.

## Remaining visual acceptance checklist

- Confirm DroidBox displays Ready and its guest is usable.
- Click Home, Back, Recents, Volume, and Power; confirm the guest response.
- Install an APK through the button, drag and drop, and a command-line path.
- Drag an image onto the guest area and check it appears in Gallery/Downloads.
- Save a screenshot with the toolbar dialog and compare it with the guest.
- Toggle Fullscreen, then close DroidBox and confirm QEMU exits.

The toolbar dialogs and command-line install path were exercised as described
above. Native OLE drag automation could not be completed reliably in this
session, so drag and drop remains a manual acceptance check. Complete the
visual checklist before final user acceptance; no audit guarantees that a
program has zero bugs. Phase 5 has not been started.
