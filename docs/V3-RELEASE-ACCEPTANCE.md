# Sabi Version 3 Release Acceptance

**Current status: NOT RELEASE READY.** Automated checks can establish code regressions; they cannot substitute for the manual gates below.

| Gate | Evidence required | Status |
|---|---|---|
| Full automated regression | 17/17 CTest tests passed locally on 2026-11-03 | PASS (local build) |
| Static release audit | `test_release_readiness.ps1` passed locally on 2026-11-03 | PASS (static audit) |
| Installer build/inventory | `prepare-bundle` + `build-installer` result and source/notices review | **NOT RUN** |
| Clean-PC offline install | Windows 10/11 PC with no development tree; Setup, initial instance, and launch | **NOT RUN** |
| Upgrade retention | install newer build over old build; apps/overlay survive | **NOT RUN** |
| Uninstall data choices | verify both retain and confirmed delete on disposable data | **NOT RUN** |
| WHPX states | disabled/enable-needed, restart-needed, and ready check behavior | **NOT RUN** |
| Guest boot / ADB | Android home screen, RSA authorization, APK install/push/screenshot | **NOT RUN** |
| Input and display | mouse, touchpad, keyboard, resize/fullscreen, 100/125/150/200% DPI | **NOT RUN** |
| Multi-instance | two overlays, independent input/ADB, workspace operations | **NOT RUN** |
| Accessibility | keyboard-only rail/settings/workspace and readable focus/contrast | **NOT RUN** |
| Privacy review | runtime/support reports checked for expected redaction and no upload | **NOT RUN** |
| Performance matrix | all required rows completed with hardware/context | **NOT RUN** |

## Recovery test

On a disposable instance, terminate only the QEMU process after it has reached the home screen. Start `Sabi.exe --safe-mode`; confirm the workspace opens and does not start Android or change the instance. Start Sabi normally and confirm the previous-session warning identifies the instance. Do **not** use recovery testing on the only copy of important guest data.

## Sign-off rule

Only change this document to **RELEASE READY** when every table row has a real pass/fail result and an accountable tester/date. A pass from static scripts is not a substitute for the clean-PC or hardware rows.
