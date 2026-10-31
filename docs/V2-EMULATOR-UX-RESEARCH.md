# Sabi v2 emulator UX research

## Sources reviewed

- [LDPlayer multi-instance manager](https://www.ldplayer.net/support/introduction-to-ldmultiplayer.html)
- [LDPlayer synchronizer](https://www.ldplayer.net/support/introduction-to-synchronizer.html)
- [LDPlayer CPU and RAM guidance](https://www.ldplayer.net/support/ways-to-configure-ram-and-cpu-assignment.html)
- [MEmu interface overview](https://www.memuplay.com/blog/memu-play-interface-overview-for-beginners.html)
- [MEmu resolution settings](https://www.memuplay.com/blog/wp-content/uploads/2016/02/MEmu_Manual_January_2016.pdf)
- [MEmu command/configuration reference](https://www.memuplay.com/blog/en/memucommand-reference-manual.html)

## What mature emulator interfaces provide

| User need | LDPlayer / MEmu pattern | Sabi status | v2 direction |
| --- | --- | --- | --- |
| Start quickly | Instance launcher, direct instance shortcuts | Partly present | Keep direct launch; add recent-instance selection and window-state persistence. |
| Instance lifecycle | Create, clone, start, close, remove, backup/restore | Create/clone/delete/reset present | Add explicit backup/restore and instance shortcuts. |
| Per-instance resources | CPU, RAM, resolution, DPI | Present | Preserve profiles and add a clear restart-required indicator. |
| Display selection | Common presets and custom width/height/DPI | Present in Sabi v2 | Phone, Tablet, PC, Fullscreen, and Custom profiles are stored per instance. |
| Sidebar workflow | Compact icon actions with tooltips | Present in Sabi v2 | Keep navigation, APK, screenshot, rotation, fullscreen, settings, instances. |
| Input | Absolute pointer mapping, keyboard map, game controls | QMP tablet and keymaps present | Validate physical-DPI coordinate mapping in a real guest before adding advanced game controls. |
| Multi-instance efficiency | FPS, audio, memory and window arrangement controls | Limited | Add measured FPS first, then safe FPS/audio/window layout controls. |
| Batch/synchronizer | Parallel input broadcast with equal display settings | Not implemented | Future feature: only after stable pointer events and per-instance profile compatibility checks. |
| Shared files | A documented host/guest exchange workflow | ADB file drop exists | Keep file drag/drop; do not label it Shared Folder until a verified transport exists. |

## Architecture decisions

Sabi remains a native Qt host that launches QEMU as a separate WHPX process.
It does not use or reverse-engineer LDPlayer/MEmu components. Their public UI
documentation is used only as product research.

The reliable v2 startup flow is:

1. Read the selected instance configuration and acquire its qcow2 lock.
2. Start QEMU with WHPX, virtio-vga-gl, SDL/OpenGL, QMP, and `usb-tablet`.
3. Embed the SDL child window with `SetParent`.
4. Connect ADB only after the guest has a live ADB protocol endpoint.
5. Apply saved Android display dimensions/DPI through ADB after boot.
6. Release the instance lock only after QEMU exits.

## Profile contract

| Profile | Android size | DPI | Intended use |
| --- | ---: | ---: | --- |
| Phone | 1080 x 2400 | 420 | Portrait mobile apps and games |
| Tablet | 1600 x 2560 | 280 | Portrait tablet apps |
| PC | 1920 x 1080 | 160 | Desktop-style landscape apps |
| Fullscreen | Active monitor pixel size | 160 | Monitor-sized landscape view |
| Custom | 320-4096 per edge | 80-640 | App-specific testing |

CPU/RAM and QEMU raster changes require a QEMU restart. Android `wm size` and
`wm density` changes can be sent once ADB reports boot complete, but visual
quality and input alignment must be checked after the guest has redrawn.

New instances default to 4 CPU cores and 4096 MB RAM, subject to Sabi's host
core and half-host-memory limits. This matches the common 4-core/4096 MB
single-instance guidance documented by LDPlayer and MEmu, while users running
multiple instances should lower allocations to keep the total below available
host resources.

## Cursor and rendering acceptance gate

Before claiming parity with mature emulators, test an actual guest at Windows
100%, 125%, 150%, and 200% display scaling. At each scale, click all four
corners and the centre of the guest image. The guest target must match the
host pointer. Test both a letterboxed window and fullscreen. Capture a
screenshot for every failure and record the embedded SDL client rectangle,
Windows scale, configured guest resolution, and actual Android `wm size`.

The current QMP path uses absolute `usb-tablet` events and maps from the real
Win32 SDL client rectangle. SDL must not also receive a second mouse stream;
otherwise the two cursor/input paths can diverge.

The release-candidate input path uses a scoped low-level mouse hook only while
the pointer is inside the embedded QEMU child rectangle. It forwards absolute
coordinates and button transitions through QMP. `WM_MOUSEMOVE` is never
suppressed, so a touchpad or mouse cursor can cross into the foreign SDL child;
only successful button transitions are consumed to avoid duplicate SDL/QMP
touches. Toolbar, menu, title-bar, and Windows caption-control input remain
ordinary host UI input. This behavior was physically checked on the development
PC, but still needs clean-PC/display-scale acceptance coverage.

## Toolbar visual system

The vertical toolbar now draws a purpose-built 24px vector icon family with
one stroke weight, rounded joins, consistent padding, visible tooltips, and
clear hover/pressed states. The visual direction was evaluated against the
current Material Symbols guidance. Sabi draws its own vectors at runtime, so
it ships no copied third-party icon asset or icon-font runtime.

## Deferred features

The following require a separate design and manual acceptance run before they
can be called implemented: synchronizer/broadcast input, batch start/close,
backup/restore UI, FPS limiter/measurement, audio toggle, window grid manager,
hardware render-mode selection, and a true shared folder. A Bliss OS upgrade
also requires independent boot, ADB, license, image, and installer validation.
