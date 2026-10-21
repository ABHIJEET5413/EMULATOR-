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
adb connect localhost:5555
adb wait-for-device

# Enable ADB over TCP permanently (so it survives reboot)
adb shell setprop persist.adb.tcp.port 5555

# Set a fixed screen density (optional)
adb shell wm density 240

# Set a fixed resolution (optional)
adb shell wm size 1080x1920

# Disable the setup wizard so it doesn't show again
adb shell settings put global device_provisioned 1
adb shell settings put secure user_setup_complete 1

# Install F-Droid
adb install F-Droid.apk

# Optional: Install Open Camera (from F-Droid APK)
# adb install open-camera.apk

# Optional: Set default launcher if needed
# adb shell cmd package set-home-activity <component>
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
