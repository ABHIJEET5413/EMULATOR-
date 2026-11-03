---
name: sabi-performance-reliability
description: Add or assess Sabi performance controls, diagnostics, multi-instance scheduling, startup reliability, or release-readiness evidence. Use for measurable behavior rather than cosmetic-only UI work.
---

# Sabi Performance and Reliability

Make performance claims only from repeatable measurements on supported
hardware; graceful behavior under pressure is more important than an arbitrary
benchmark number.

## Workflow

1. Establish a baseline: cold launch time, Android-ready time, resident RAM,
   CPU while idle and active, guest frame pacing where observable, and process
   exit/restart behavior.
2. Add an explicit per-instance resource policy with clear names and reversible
   settings. A control must report what it can actually do with QEMU/WHPX;
   never label a setting "FPS" or "Eco" unless it genuinely changes the
   measured policy.
3. Keep diagnostics local by default. Provide a redacted support bundle only
   after showing the user what it contains and obtaining their action.
4. Test failure paths: disabled WHPX, unavailable QEMU, corrupted/missing
   overlay, blocked ADB authorization, storage exhaustion, an unclean QEMU
   exit, and two concurrently running instances.

## Completion evidence

Add headless tests for state/configuration logic, document the manual hardware
matrix, publish before/after measurements with machine details, and update
release/architecture docs. Do not add telemetry, cloud accounts, GApps, or a
localhost service without separate user approval.
