# Image Build Pipeline

> How to create the pre-installed Bliss OS qcow2 base image that ships
> with DroidBox. This image has Android fully installed, setup wizard
> skipped, ADB enabled, and F-Droid pre-installed.

---

## Overview

The goal is a single `bliss-base.qcow2` file that:
- Boots straight to the Android home screen (no installer, no setup wizard)
- Has ADB enabled over TCP on port 5555
- Has F-Droid pre-installed as the app store
- Does NOT include Google Play / GApps
- Is used as a read-only backing file for per-instance overlays

---

## Method: Manual Installation + Configuration

Since automating the Bliss OS installer is fragile, the recommended
approach is to do a manual install once, configure it, then snapshot
the result.

Use QEMU's `-cpu max` model for Bliss OS. The `qemu64` compatibility model
does not expose SSE4.2, which Bliss OS 14.10.3 checks during boot. `max`
exposes the CPU features supported by the active accelerator. On the tested
WHPX host, Bliss OS 14.10.3 stalled while starting secondary CPUs, so use
`-smp 1` for this guest.

### Step 1: Download Bliss OS

Download the latest Bliss OS ISO (Android-x86 based):
- Website: https://blissos.org/
- Pick the `x86_64` ISO
- Choose a version without GApps (usually the default)

### Step 2: Create a Raw Disk Image

```powershell
# Create a 16 GB qcow2 disk (adjust size as needed)
qemu-img.exe create -f qcow2 bliss-install.qcow2 16G
```

### Step 3: Install Bliss OS

Boot QEMU from the ISO and install to the disk:

```powershell
qemu-system-x86_64.exe `
  -accel whpx,kernel-irqchip=off `
  -m 4096 `
  -smp 1 `
  -cpu max `
  -drive file=bliss-install.qcow2,format=qcow2,if=virtio `
  -cdrom BlissOS-x86_64-*.iso `
  -device usb-ehci `
  -device usb-tablet `
  -netdev user,id=net0 `
  -device virtio-net-pci,netdev=net0 `
  -vga none `
  -device virtio-vga-gl `
  -display sdl,gl=on `
  -boot d
```

Inside the Bliss OS installer:
1. Choose "Installation - Install Bliss-OS to hard disk"
2. Select the virtio disk (usually `vda`)
3. Create a new partition (GPT or MBR, use the whole disk)
4. Format as ext4
5. Install GRUB bootloader → Yes
6. Make /system read-write → Yes
7. Reboot (remove the `-cdrom` and `-boot d` args)

### Step 4: First Boot Configuration

Boot without the ISO:

```powershell
qemu-system-x86_64.exe `
  -accel whpx,kernel-irqchip=off `
  -m 4096 `
  -smp 1 `
  -cpu max `
  -drive file=bliss-install.qcow2,format=qcow2,if=virtio `
  -device usb-ehci `
  -device usb-tablet `
  -netdev user,id=net0,hostfwd=tcp::5555-:5555 `
  -device virtio-net-pci,netdev=net0 `
  -vga none `
  -device virtio-vga-gl `
  -display sdl,gl=on `
  -qmp tcp:localhost:4444,server,wait=off `
  -boot c
```

Complete the Android setup wizard manually:
1. Skip Wi-Fi (it will use the NAT network)
2. Skip Google account
3. Set up any basic settings
4. Reach the home screen

### Step 5: Configure for DroidBox

Once Android is running, connect via ADB:

```powershell
adb start-server
adb connect localhost:5555
```

An `offline` entry does not identify the cause. Check the guest itself:
`ip addr`, `getprop init.svc.adbd`, `getprop service.adb.tcp.port`, and
`ss -lnt`. The tested Bliss image had Ethernet address `10.0.2.15`, but
`adbd` was stopped, `persist.sys.usb.config=none`, and no TCP listener.

During image preparation, install [droidbox-adb.rc](droidbox-adb.rc) as
`/system/etc/init/droidbox-adb.rc`, owned by root with mode `0644` and
SELinux context `u:object_r:system_file:s0`. This starts TCP ADB after
`sys.boot_completed=1` without requiring a USB gadget. The helper also installs
[droidbox-first-boot.sh](droidbox-first-boot.sh), invoked by init as the shell UID
before TCP ADB starts. Starting adbd alone leaves the framework authorization
manager disabled when `adb_enabled=0`; a new host then stays unauthorized without
an RSA prompt. The script enables that manager with
`settings put global adb_enabled 1`. Keep `ro.adb.secure=1`.
Authorize the host through Android's RSA prompt, or provision that host's
public key into `/data/misc/adb/adb_keys` (owner `1000:2000`, mode `0640`,
context `u:object_r:adb_keys_file:s0`). Do not ship your personal key in a
generic base image. Offline disk edits must use a stopped VM and an overlay;
retain a backup and detach all mounts before QEMU starts.

For an already mounted writable Android system-image root, the Linux helper
applies the service and its ownership/security label:

```sh
sudo sh image-build/provision-adb-image.sh /mnt/android-system-root
# Optional: authorize this development host in this instance's data directory.
sudo sh image-build/provision-adb-image.sh /mnt/android-system-root /mnt/android-data /path/to/adbkey.pub
```

The helper requires `attr` (`setfattr`) and accepts mounted directories; it
does not attach disks or stop QEMU. Run it only during offline image preparation.

After repairing the service, make one connection attempt and verify
`adb -s localhost:5555 get-state` prints `device`.

After the connection shows `device`, persist remaining settings:

```powershell
adb -s localhost:5555 shell setprop persist.adb.tcp.port 5555
adb -s localhost:5555 shell setprop service.adb.tcp.port 5555

# Set a fixed screen density (optional)
adb -s localhost:5555 shell wm density 240

# Set a fixed resolution (optional)
adb -s localhost:5555 shell wm size 1080x1920

# Disable the setup wizard so it doesn't show again
adb -s localhost:5555 shell settings put global device_provisioned 1
adb -s localhost:5555 shell settings put secure user_setup_complete 1

# Install F-Droid
adb -s localhost:5555 install F-Droid.apk

# Optional: Install Open Camera (from F-Droid APK)
# adb -s localhost:5555 install open-camera.apk

# Optional: Set default launcher if needed
# adb -s localhost:5555 shell cmd package set-home-activity <component>
```

### Step 6: Clean Up and Compact

```powershell
# Inside the guest, zero free space for better compression
adb shell dd if=/dev/zero of=/sdcard/zero.fill bs=4096 || true
adb shell rm /sdcard/zero.fill

# Shut down the guest
adb shell reboot -p

# Wait for QEMU to exit, then compact the image
qemu-img convert -O qcow2 -c bliss-install.qcow2 bliss-base.qcow2
```

The compacted `bliss-base.qcow2` is your base image.

### Step 7: Test the Base Image

```powershell
# Create a test overlay
qemu-img create -f qcow2 -b bliss-base.qcow2 -F qcow2 test-overlay.qcow2

# Boot from the overlay
qemu-system-x86_64.exe `
  -accel whpx,kernel-irqchip=off `
  -m 4096 -smp 1 -cpu max `
  -drive file=test-overlay.qcow2,format=qcow2,if=virtio `
  -device usb-ehci -device usb-tablet `
  -netdev user,id=net0,hostfwd=tcp::5555-:5555 `
  -device virtio-net-pci,netdev=net0 `
  -vga none -device virtio-vga-gl `
  -display sdl,gl=on -boot c

# Verify:
# - Boots straight to home screen (no setup wizard)
# - adb connect localhost:5555 works
# - F-Droid is installed
```

---

## Automating the Process (Future)

For reproducible builds, consider:
1. Using `virt-customize` (from libguestfs) to modify the image offline
2. Creating an auto-answer file for the Bliss installer
3. Writing a script that drives QEMU via QMP + ADB to perform all setup steps

This is complex and not needed for the initial release.

---

## Image Size

| Stage | Estimated Size |
|---|---|
| After install (raw) | ~4-6 GB |
| After compaction + compression | ~2-3 GB |
| Inno Setup further compresses | ~1.5-2.5 GB |

---

## Important: Base Image Path

The base image must be placed at:
```
vm/images/bliss-base.qcow2
```

Instance overlays reference it with a relative path via qcow2 backing file.
If the base image moves, overlays will break.

## Phase 5 provisioned base

Fresh instances use `vm/images/bliss-provisioned.qcow2`, prepared with
`prepare-instance-base.sh`. This adds the Phase 4 ADB startup repair to a new
image without changing `bliss-base.qcow2` or an existing instance's backing chain.
See [Phase 5 image preparation and tests](../docs/PHASE-5-TESTING.md).
Never distribute a local development base that authorizes a personal ADB public key.

## Clean distribution base

Build a new image without a host-key argument, preserving all existing bases:

```powershell
wsl -u root --exec sh image-build/prepare-instance-base.sh vm/images/bliss-base.qcow2 vm/images/bliss-distribution-v2.qcow2
```

The helper refuses to overwrite output. The clean image's data directory starts
empty. On its first boot the shell script enables ADB authorization, sets the
setup-completed flags, and chooses Bliss Launcher. It writes a marker in that
instance's overlay so subsequent boots preserve the user's launcher choice.
`ro.adb.secure=1` remains enabled; a new computer must be approved in Android's
RSA dialog. The optional "Always allow" choice stores a public key only in that
instance's overlay. No host private key belongs in a guest image.

The desktop shows authorization instructions and observes the existing ADB
transport until approval; it does not repeatedly reconnect while waiting.
Package this clean image under the filename expected by the installed instance
manager (`bliss-provisioned.qcow2`), with matching installed configuration.
Never replace a development base while an existing overlay still backs onto it.
