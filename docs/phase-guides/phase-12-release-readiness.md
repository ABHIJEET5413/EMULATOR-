# Phase 12 — Reliability, Performance, and Version 3 Release Readiness

## Outcome

Phase 12 makes failure state visible and measurement repeatable. It does not turn unmeasured hardware claims into release claims.

## Runtime recovery

`RuntimeRecovery` writes a small atomic marker under the local instance root only after QEMU and QMP are up. It contains an instance name, QEMU PID, and UTC start time; it contains no guest data, ADB keys, or host paths. A marker is removed after a clean QEMU stop. A marker whose recorded process is absent is shown as an unclean previous session on the next Sabi start.

- The warning does not repair, validate, reset, or delete an overlay.
- `Sabi.exe --safe-mode` opens only the instance workspace. It does not forward a launch to another Sabi process, auto-start QEMU, or change an instance configuration/disk.
- Starting an instance normally replaces its old marker; a later clean stop removes it.

## Local measurements

`PerformanceMonitor` records one process session: launch time, QMP-ready time, Android-ready time, exit time, QEMU peak working set, a recent CPU sample, and overlay file sizes at process attach/exit. The rail action **Save runtime report** writes a report only to a path selected by the user.

These are local point-in-time observations, not a benchmark, an FPS meter, or a promise of performance. CPU output is a sample of the QEMU process; it must be interpreted with the machine and workload recorded in the matrix.

## Automated checks

- `test_runtime_recovery`: atomic marker lifecycle, stale-marker recognition, and redacted performance report contents using temporary data only.
- `test_release_readiness.ps1`: static inventory of the Phase 12 files and safe-mode/recovery evidence. It explicitly cannot prove installer behavior, hardware acceleration, input, or a clean-machine result.

## Manual release gates

Use [V3 release acceptance](../V3-RELEASE-ACCEPTANCE.md) and the [performance matrix](../V3-PERFORMANCE-MATRIX.md). Leave a row as **NOT RUN** until a tester records the actual result, hardware, Windows build, DPI, QEMU, and graphics driver.

Required gates before a Version 3 public release include: a clean-PC offline installer run, upgrade retention, uninstall keep/delete choices, WHPX disabled and enabled states, Android boot and ADB authorization, normal/touchpad input at 100/125/150/200% DPI, two instances, and keyboard-only review.
