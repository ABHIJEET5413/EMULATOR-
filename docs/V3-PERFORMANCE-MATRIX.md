# Sabi Version 3 Performance Matrix

**Status:** Template. No benchmark result is implied by this document.

Record each run on a supported Windows 10/11 x64 PC with the exact Sabi build, QEMU build, Windows build, display DPI, graphics driver, power mode, and base image identifier. Export **Save runtime report** after each launch and attach it locally to the test record; it is redacted but should still be reviewed before sharing.

| Scenario | Required observation | Result | Hardware / software record |
|---|---|---|---|
| Cold launch | launch → QMP ready and launch → Android ready | **NOT RUN** | |
| Warm launch | same timings after a clean prior shutdown | **NOT RUN** | |
| Idle | QEMU peak working set and CPU sample after Android settles | **NOT RUN** | |
| Active UI | CPU sample during normal app interaction; no FPS claim | **NOT RUN** | |
| Storage | overlay size before/after boot and a small app install | **NOT RUN** | |
| Shutdown | Android/QEMU exit time; recovery marker removed | **NOT RUN** | |
| Unclean exit | terminate only a disposable VM, restart Sabi, see warning/safe mode | **NOT RUN** | |
| Two instances | both launch, remain separately controllable, record combined host pressure | **NOT RUN** | |
| Input/DPI | 100%, 125%, 150%, 200%; mouse and touchpad enter/leave viewport correctly | **NOT RUN** | |

Do not compare numbers between different systems as a product guarantee. Do not report input latency unless a reproducible measurement method and its limits are recorded alongside the result.
