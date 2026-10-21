# Camera support in Phase 5

## Bundled QEMU capability check

The installed Windows QEMU identifies as `11.1.0 (v11.1.0-12130-ge470268ff4)`.
Its `-device help` lists **usb-host**, and does not list usb-video or a virtual camera.
Therefore `camera.mode = "none"` is the default, and the Test Pattern settings option is disabled.
No synthetic video feed is advertised as working. These findings concern this exact binary;
they do not imply every QEMU fork or platform has the same device set.

QEMU documents forwarding a host USB device using `usb-host,vendorid=...,productid=...` in its
[USB device documentation](https://www.qemu.org/docs/master/system/devices/usb.html).
The presence of that device does not establish that a particular Windows webcam works inside Android.

## Experimental physical USB camera

The Settings dialog accepts four-digit hexadecimal vendor/product IDs, including `0x`, and the
controller appends `-device usb-host,vendorid=0x046d,productid=0x082d` (substituting the saved IDs).
It leaves the existing USB tablet connected. This is an experimental configuration path, not a verified webcam feature.

To evaluate a spare USB webcam:

1. Identify its exact VID/PID in Device Manager -> Properties -> Details -> Hardware IDs.
2. Record the installed driver so it can be restored. Close Windows camera applications.
3. If required by the QEMU/libusb build, use [Zadig](https://zadig.akeo.ie/) to bind the selected webcam
   to a generic driver such as WinUSB. Zadig's official site describes these driver choices.
   Replacing the webcam driver can make Windows camera applications lose access; select only the intended device.
4. Select USB webcam (experimental), enter its VID/PID, save, and restart the stopped instance.
5. Inspect QEMU errors and `adb shell dumpsys media.camera`; test the Android Camera app.
6. To return to Windows use, stop the VM and restore the manufacturer's/UVC driver through Device Manager.

This implementation does not install drivers or take over the host's integrated camera automatically.
Actual passthrough on this PC remains unverified.

## Virtual camera and ffmpeg approaches

There is no usb-video device in the bundled binary to receive ffmpeg frames, a named pipe, or a test pattern.
Linux v4l2 loopback cannot be assumed to provide a Windows QEMU device. Adding a custom UVC device,
camera HAL, or patched QEMU would require a separate implementation and guest compatibility testing.
Installing another camera app cannot create missing camera hardware.

## Guest behavior

On 2026-10-21, the repaired default Bliss guest reported `Number of camera devices: 0`.
`am start -W -a android.media.action.STILL_IMAGE_CAMERA` resolved to
`net.sourceforge.opencamera/.MainActivity` and returned `Status: ok`. After granting its requested
camera/audio permissions, Open Camera stayed the resumed activity and displayed a black preview with
"FAILED TO OPEN CAMERA". `adb logcat -d -b crash` was empty. This verifies graceful unavailable-camera
behavior for the installed Open Camera app, not a working preview or every possible Camera app.
