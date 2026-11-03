# Sabi Version 3 Product Modernization Plan

> **Implementation note (Phase 12):** Phases 7–12 have now been implemented.
> Version 3 is still **not release ready** until the real installer, clean-PC,
> hardware, input, and performance gates in `V3-RELEASE-ACCEPTANCE.md` pass.

**Status:** planning only — no Version 3 product code is authorized yet.  
**Prepared:** 1 November 2026.  
**Implementation:** Phases 7–11 implemented and locally regression-tested; Phase 12 remains planning only.

## Goal

Make Sabi feel like a polished native Windows application while retaining its
current fundamentals: offline installation, Qt dynamically linked, QEMU as a
separate WHPX process, Bliss OS without GApps, qcow2 overlays, and truthful
feature claims. Version 3 is not a visual copy of LDPlayer, MEmu, or
BlueStacks; it adopts proven interaction patterns using Sabi's own visual
language and implementation.

The reference image is strong because it has one obvious guest viewport, a
quiet top context bar, evenly spaced icon controls, consistent dark surfaces,
and no competing visual styles. Sabi should reach that clarity before adding
many new buttons.

## Research conclusions

Competitor research suggests four priorities:

1. A concise side toolbar and a clear settings surface make frequent actions
   discoverable. BlueStacks describes this one-click side-toolbar pattern and
   grouped settings UI. [BlueStacks feature overview](https://support.bluestacks.com/hc/en-us/articles/360055360872-All-you-need-to-know-about-BlueStacks-5)
2. Multi-instance management is a core workflow: create, clone, search, sort,
   start/stop, arrange, and apply resource choices. LDPlayer documents those
   capabilities, including window arrangement and per-instance configuration.
   [LDPlayer multi-instance manager](https://www.ldplayer.net/support/introduction-to-ldmultiplayer.html)
3. Key-map editing and gamepad support are valuable only after reliable input
   and an understandable editor exist. BlueStacks exposes a controls editor;
   Sabi already has a JSON keymap engine to build on. [BlueStacks controls and
   gamepad overview](https://support.bluestacks.com/hc/en-us/articles/360055360872-All-you-need-to-know-about-BlueStacks-5)
4. Resource-saving modes must be genuine, per-instance policies. BlueStacks'
   Eco Mode couples reduced resource use with optional audio and a chosen frame
   rate; it is a product requirement to measure equivalent behavior before
   exposing it in Sabi. [Eco Mode details](https://support.bluestacks.com/hc/en-us/articles/360052834772-How-to-run-multiple-instances-of-BlueStacks-5-more-efficiently-using-Eco-mode)

For the host UI, Qt automatically supports high-DPI displays when the app uses
high-resolution assets, while Qt recommends production styling through a
`QStyle`/`QProxyStyle` approach rather than an ever-growing stylesheet.
[Qt high-DPI guidance](https://doc.qt.io/qt-6/highdpi.html) and [Qt styling
guidance](https://doc.qt.io/qt-6/stylesheet.html).

## Feature decisions

### Commit to Version 3

- A cohesive native desktop shell: Sabi identity, instance context, native
  caption controls, refined tool rail, status/notification area, shortcuts,
  and a high-DPI SVG icon set created for Sabi.
- A proper display viewport contract: aspect-correct rendering, cursor/focus
  states, DPI-aware physical coordinate mapping, input diagnostics, and
  no regressions to the verified touchpad/mouse behavior.
- A tabbed control center with clear restart/live-apply state, validation,
  defaults, import/export of settings, log/support-bundle access, and
  safer storage/instance operations.
- An improved instance manager: search, rename, clone, start/stop, safe
  delete, sorting, window layout presets, and per-instance resource profiles.
- A visual keymap editor using the existing JSON schema, plus scoped research
  and a proof-of-concept for gamepad support.
- Startup/performance diagnostics, local measurements, safe-mode recovery,
  and stronger automated/manual quality gates.

### Explicitly deferred or conditional

- **Google Play Store/GApps:** prohibited by the project rules and not part of
  Version 3.
- **Virtual GPS, root toggles, scripts/macros, instance synchronisation,
  video recording, and a shared-folder protocol:** investigate only after the
  core UI/reliability work. Each requires its own privacy, guest, filesystem,
  or anti-abuse design; none will be presented as working early.
- **A completely new display backend:** evaluate only if measurement shows the
  embedded SDL/QMP architecture cannot meet responsiveness/stability targets.
  A rewrite to VNC, Qt Quick, or a custom renderer is not assumed.
- **Cloud accounts, telemetry, advertising, or online stores:** out of scope.

## Phased roadmap

Each phase requires a guide, implementation plan, tests, documentation update,
build verification, and your approval before the next phase begins.

### Phase 7 — Design system and application shell

**Outcome:** a coherent, high-DPI Sabi shell around the existing guest.

- Audit every current screen and action; remove visual duplication and identify
  missing state/error/loading UX.
- Introduce design tokens, named icon assets, focus/hover/disabled states,
  spacing and typography rules, and accessible tooltips.
- Refactor `MainWindow` into a stable top context bar, primary display area,
  compact right tool rail, and non-intrusive status/notification area.
- Keep the standard Windows frame initially; use a custom title bar only after
  it has a complete accessibility/window-management design.
- Test at 100/125/150/200% DPI, normal/narrow/fullscreen windows, and screen
  reader/keyboard navigation.

**Likely modules:** `ui_theme`, `icon_factory`, a revised `MainWindow`, and
Qt resource files. No VM behavior changes.

### Phase 8 — Viewport, cursor, and interaction quality

**Outcome:** the Android display feels intentional and input stays correct.

- Make a single `GuestViewport`/display geometry authority for Qt layout,
  letterboxing, Win32 physical pixels, cursor state, and QMP mapping.
- Add input focus/capture indicators and a bounded diagnostic overlay that is
  off by default.
- Preserve the current rule: never consume `WM_MOUSEMOVE`; button events are
  consumed only after the QMP send succeeds.
- Add resize/DPI/input regression tests and physical touchpad + mouse tests.

**Exit condition:** no cursor drift, no cursor boundary trap, no duplicate
clicks, and no host-toolbar interaction loss across display profiles.

### Phase 9 — Professional control center and settings safety

**Outcome:** settings are understandable, grouped, and safe to change.

- Replace the single dense dialog with pages: General, Display, Performance,
  Controls, Storage, Network/ADB, and Advanced.
- Show live versus restart-required values, validation before save, exact
  explanations for unavailable options, restore defaults, and per-instance
  versus global scope.
- Add display-profile cards (Phone, Tablet, Desktop, Fullscreen, Custom) with
  correct persisted values and preview/confirmation where necessary.
- Add local log viewer, copy diagnostics, and a user-controlled redacted
  support-bundle workflow.

### Phase 10 — Controls studio and gaming input

**Outcome:** users can understand, select, create, test, and share local
keymaps without manually editing JSON.

- Build an in-guest overlay editor over the existing validated keymap schema:
  tap, hold, swipe, repeat-tap, passthrough, undo/redo, conflict detection,
  test mode, and import/export.
- Add gamepad feasibility work first (Windows XInput/Raw Input evaluation),
  then implement it only with device-loss, focus, and mapping tests.
- Add key-prompt visibility and per-game profile selection. Macro scripting is
  deferred until its security and reliability design is approved.

### Phase 11 — Multi-instance workspace and resource policies

**Outcome:** managing several overlays is organized and honest about hardware
limits.

- Rebuild the launcher as an instance workspace with search, sorting, labels,
  rename, clone, backup/export, safe deletion, bulk start/stop, and grid/cascade
  window arrangement.
- Add resource profiles and a measured low-resource policy (for example,
  paused/background policy and audio mute) only for controls QEMU can enforce.
- Provide instance health, disk usage, backing-image compatibility, and clear
  non-destructive recovery paths.
- Treat synchronized input as a separate opt-in proposal, requiring matched
  resolution/DPI and strong guardrails before any implementation.

### Phase 12 — Reliability, performance, and Version 3 release readiness

**Outcome:** Sabi has evidence behind its performance and installer claims.

- Establish a reproducible hardware/DPI/graphics-driver/manual test matrix.
- Measure cold boot, Android-ready time, idle/active CPU, RAM, storage growth,
  input latency where measurable, shutdown, and two-instance behavior.
- Implement startup recovery, crash/unclean-exit messaging, overlay checks,
  diagnostics, and safe mode.
- Run full automated tests, a clean-PC installer acceptance, upgrade retention,
  uninstallation, WHPX states, and a UI accessibility review before declaring
  Version 3 releasable.

## Skills added for this roadmap

- `.agents/skills/sabi-desktop-ux/` — design-system, layout, high-DPI, icon,
  and accessibility guardrails.
- `.agents/skills/sabi-display-input-reliability/` — protects the embedded SDL
  viewport and the verified QMP/Windows input behavior.
- `.agents/skills/sabi-performance-reliability/` — measurement, diagnostics,
  performance-policy, and release-readiness constraints.

## Before implementation begins

1. Approve this roadmap and select **Phase 7** as the first Version 3 phase.
2. Confirm Sabi's desired visual direction: dark professional desktop utility
   is assumed; no competitor branding or copied graphics will be used.
3. Capture a baseline on the development PC (DPI, display resolution, cold
   launch, Android-ready time, idle RAM/CPU, and current screenshots).
4. Create the Phase 7 implementation guide and a small visual reference board
   made from original Sabi tokens/assets before changing `MainWindow`.
