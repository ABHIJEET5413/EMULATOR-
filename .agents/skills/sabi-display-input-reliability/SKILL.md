---
name: sabi-display-input-reliability
description: Improve Sabi's embedded QEMU viewport, DPI-aware coordinate mapping, cursor behavior, or guest input forwarding. Use whenever Qt/Win32/QMP display input could regress.
---

# Sabi Display and Input Reliability

Protect correct interaction with the embedded QEMU SDL child while improving
the display experience.

## Invariants

- QEMU remains a separate WHPX process and QMP remains the authoritative
  absolute-tablet input channel.
- Convert from the actual physical Win32 guest viewport rectangle, not a Qt
  layout rectangle or the whole main window. Account for letterboxing, window
  frame offsets, and per-monitor DPI.
- Never consume `WM_MOUSEMOVE`; Windows must be able to move a physical mouse
  or touchpad pointer across the SDL child boundary.
- Consume a mouse button event only after the corresponding QMP transition was
  accepted. Release held guest buttons on focus loss, guest shutdown, or
  viewport destruction.
- Toolbar, menus, caption buttons, dialogs, and letterbox margins must retain
  normal Windows interaction. Do not install a global hook that captures them.

## Required validation

Test physical mouse and touchpad entry/exit, click, drag, scrolling, keyboard
typing, all four display profiles, fullscreen, resize, and 100/125/150/200%
DPI. Exercise a second instance to prove input is routed only to its focused
guest. Record QMP failures in the existing log with actionable messages.
