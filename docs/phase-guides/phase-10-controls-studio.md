# Phase 10 — Controls Studio and Gaming Input Evaluation

## Outcome

Sabi adds a local, visual editor for the Phase 3 JSON keymap format. The editor
is modeless and has an explicit placement mode: normal guest input is released
before placement begins, and the editor-only overlay is removed before normal
guest input resumes.

## Supported keymap actions

Controls Studio reads and writes the existing validated schema without a
second format:

- `tap`
- `hold`
- `swipe`
- `passthrough`
- `repeat_tap`

The editor supports add/update, remove, unique-key conflict detection,
undo/redo, import, export, and explicit save. Profiles retain their reference
resolution and are scaled by `KeymapEngine` to the active Android guest.

## Placement and safety contract

- Marker positions use the profile's reference pixels, not host/DPI pixels.
- `KeymapOverlay` aligns to `GuestViewport::logicalContentRect()`, so its
  markers exclude letterbox margins.
- Entering placement mode calls `InputHandler::releaseAllInput()` and blocks
  Sabi's QMP forwarding paths. The overlay is hidden and this block is removed
  when placement mode ends or the studio closes.
- This is deliberately not a macro system: no scripts, background automation,
  synchronization, or remote control are added.
- Saving a profile is a user action. The selected profile path is persisted in
  that instance's configuration and reloaded at launch. Bundled profiles are
  never overwritten; first save asks for a user-local path.

## Gamepad decision

`GamepadProbe` only checks connected XInput slots and reports that mapping is
not enabled. It emits no guest events, has no rumble support, and does not
claim Raw Input/HID compatibility. See `docs/GAMEPAD-EVALUATION.md`.

## Automated verification

`test_keymap_studio` exercises duplicate-key rejection, action edits,
undo/redo, profile parser round-trip, invalid-profile rejection, and the
detection-only gamepad boundary. Existing `test_keymap`, `test_display_input`,
`test_viewport_input`, and `test_native_keyboard` remain required regressions.

## Manual acceptance checklist

- [ ] Start Android, open **Controls Studio** from the tool rail, and verify
  keyboard-only navigation through bindings, fields, buttons, and file dialogs.
- [ ] Add each action type, use the placement mode, save it to a user-selected
  local JSON file, close the studio, and reopen it.
- [ ] For a swipe, click two guest points; confirm start and end markers are
  distinct and the saved JSON has the matching `start*`/`end*` fields.
- [ ] With the editor open, activate placement mode and confirm Android does
  not receive the placement click or a stuck touch/key. Close placement mode
  and confirm ordinary mouse, touchpad, keyboard, and keymap input recover.
- [ ] Resize Sabi and test Windows 100%, 125%, 150%, and 200% scaling. Markers
  must stay inside Android's content area and must not appear in black margins.
- [ ] Save a profile, restart only that instance, and confirm the selected
  profile reloads. Confirm another instance retains its own profile choice.
- [ ] Connect/disconnect an XInput controller and confirm Controls Studio does
  not send Android input or imply gamepad mapping is available.
