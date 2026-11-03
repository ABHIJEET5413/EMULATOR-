# Phase 8: Viewport, Cursor, and Input Quality

## Objective

Make the embedded Android viewport, cursor behavior, and QMP tablet input
reliable across letterboxing, resizing, fullscreen, and Windows DPI scaling.
QEMU remains a separate WHPX process and QMP remains Sabi's authoritative
input route.

## Implementation

- `GuestViewport` computes the sole logical and physical guest rectangle from
  the host widget, guest resolution, and device-pixel ratio.
- `DisplayEmbed` uses that model both to letterbox Qt content and to position
  and resize QEMU's native SDL child in physical Win32 pixels.
- `InputHandler` uses `GuestViewport` for synthetic Qt events and the actual
  QEMU child client rectangle for global native events. Letterbox margins are
  rejected; a drag that began inside the child still receives its matching
  release.
- `WM_MOUSEMOVE` is never consumed. Successful button transitions alone are
  consumed, preventing duplicate SDL/QMP touches without trapping the pointer.
- Focus loss, VM shutdown, and handler destruction release held keyboard and
  mouse input.
- `Ctrl+Shift+I` toggles a local, non-focusable, mouse-transparent input
  diagnostic overlay. It is disabled by default and reports only local geometry,
  focus state, and the latest QMP pointer position.

## Automated verification

`test_viewport_input` covers portrait and landscape letterboxing, physical
125% DPI geometry, margin rejection, QMP edge mapping, clamping, and one-pixel
client safety. Existing display/input and native-keyboard harnesses remain
required regression tests.

## Local verification

On 3 November 2026, the Release build completed and all 12 registered CTest
suites passed. Automated tests verify the coordinate model and native two-guest
input routing; the manual physical-pointer and high-DPI checklist remains
required before release acceptance.

## Manual acceptance checklist

- [ ] At 100%, 125%, 150%, and 200% Windows scaling, Android stays centered
  and aspect-correct for Phone, Tablet, Desktop, and Fullscreen profiles.
- [ ] Mouse and touchpad enter and leave Android naturally; no cursor sticks at
  its edge.
- [ ] Test click, drag outside Android then release, scroll, ordinary typing,
  mapped keys, resize, fullscreen, and return from fullscreen.
- [ ] Verify black margins, the tool rail, menus, dialogs, and title-bar
  controls never produce Android touches.
- [ ] With two instances, only the focused guest receives mouse/keyboard input.
- [ ] Toggle `Ctrl+Shift+I`; diagnostics must update yet never intercept input.
- [ ] Stop a VM during a held key/button and confirm the next boot has no stuck
  touch or modifier state.
