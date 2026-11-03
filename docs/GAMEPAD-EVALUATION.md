# Gamepad evaluation — Phase 10

## Decision

Phase 10 provides **detection only**. `GamepadProbe` dynamically checks the
standard Windows XInput libraries for connected controller indices 0–3. It
does not pass buttons, sticks, triggers, rumble, or controller audio to QEMU
or Android. No gamepad setting is presented as enabled.

## Why this is the correct boundary

Microsoft documents XInput state polling for up to four controllers and a
device-not-connected result for absent controllers. It is suitable for a local
capability check, but it is not proof that an Android guest mapping pipeline,
focus policy, controller disconnect behavior, or all HID devices work.

Raw Input can enumerate generic HID gamepads/joysticks, but interpreting each
device's reports and mapping them safely would be a separate compatibility
project. Sabi has not implemented that layer.

## Required evidence before guest mapping can be added

1. An explicit opt-in mapper with per-instance focus ownership.
2. Device-arrival/removal, unplug-during-hold, sleep/resume, and two-instance
   tests with no stuck Android action.
3. Dead-zone, trigger, button, and layout validation on real XInput hardware.
4. A separate feasibility decision for Raw Input/HID controllers.
5. Automated state-delta tests and manual guest-game acceptance evidence.

## Sources

- [Microsoft: Getting Started With XInput](https://learn.microsoft.com/windows/win32/xinput/getting-started-with-xinput)
- [Microsoft: XInputGetState](https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputgetstate)
- [Microsoft: Using Raw Input](https://learn.microsoft.com/en-us/windows/win32/inputdev/using-raw-input)
