---
name: sabi-desktop-ux
description: Design or implement Sabi's user-facing Qt desktop UI, including the application shell, toolbar, dialogs, settings, icons, and accessibility. Use for deliberate UX work, not isolated text changes.
---

# Sabi Desktop UX

Create a cohesive, responsive Windows desktop experience without imitating a
competitor's branding or shipping unverified controls.

## Before changing UI

1. Read `docs/V3-PRODUCT-PLAN.md`, `.agents/rules/project-rules.md`, and the
   relevant sections of `docs/ARCHITECTURE.md`.
2. Identify the actual state, persistence, and error path behind every visible
   control. Do not present a disabled or placeholder feature as functional.
3. Preserve native caption controls unless an approved custom title-bar design
   has equivalent keyboard, DPI, accessibility, and window-management behavior.

## Design requirements

- Use a small design-token layer (colors, spacing, typography, radii, focus,
  and motion) rather than unrelated per-widget stylesheets.
- Build a compact tool rail from named SVG icons with labels/tooltips,
  keyboard-accessible focus, and checked/disabled states. Do not copy product
  logos, artwork, layouts, or icons from LDPlayer, MEmu, or BlueStacks.
- Keep the guest viewport visually primary. Letterbox deliberately; never
  stretch Android content or obscure it with persistent decorative UI.
- Use scalable vector/high-DPI assets and verify 100%, 125%, 150%, and 200%
  Windows scaling, narrow windows, fullscreen, and both light/dark Windows
  accessibility settings where supported.
- Prefer `QStyle`/`QProxyStyle` or focused widget subclasses for production
  behavior; do not grow a global stylesheet into an unmaintainable theme.

## Completion evidence

Provide screenshots at normal and high DPI, keyboard-only navigation results,
and the automated layout/unit tests added for the changed behavior. Update
architecture and user documentation when UI actions or saved settings change.
