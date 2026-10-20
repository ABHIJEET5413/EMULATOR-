---
name: phase-3-keymap-engine
description: >
  Implement Phase 3 of DroidBox: Keymap Engine. Map physical keyboard keys
  to screen taps, swipes, and key passthrough with JSON profile support.
---

# Phase 3: Keymap Engine

## Overview

This phase adds a keymap engine that lets users map keyboard keys to
screen taps, swipes, and other touch gestures. This is essential for
gaming (WASD → directional swipes, mouse click → screen tap at position).

## Before You Start

1. Read `docs/phase-guides/phase-3-keymap-engine.md` for the detailed guide.
2. Read `.agents/rules/project-rules.md` for coding standards.
3. Phase 2 must be complete and tested.

## What to Build

### Files to Create / Modify

| File | Purpose |
|---|---|
| `host-app/include/keymap_engine.h` | KeymapEngine class declaration |
| `host-app/src/keymap_engine.cpp` | Keymap engine implementation |
| `host-app/include/keymap_profile.h` | KeymapProfile data model |
| `host-app/src/keymap_profile.cpp` | JSON parsing for profiles |
| `host-app/resources/keymaps/default.json` | Default keymap profile |
| `host-app/resources/keymaps/wasd-gaming.json` | Gaming keymap example |
| `host-app/src/input_handler.cpp` | **Modify**: integrate keymap engine |
| `tests/test_keymap.cpp` | Unit tests for keymap parsing + dispatch |

### Keymap Profile JSON Schema

```json
{
  "name": "WASD Gaming",
  "description": "Standard WASD movement + mouse aim",
  "version": 1,
  "resolution": { "width": 1080, "height": 1920 },
  "bindings": [
    {
      "key": "W",
      "action": "swipe",
      "params": {
        "startX": 200, "startY": 1400,
        "endX": 200, "endY": 1200,
        "durationMs": 100
      },
      "description": "Move forward"
    },
    {
      "key": "Space",
      "action": "tap",
      "params": {
        "x": 900, "y": 1600
      },
      "description": "Jump"
    },
    {
      "key": "F",
      "action": "tap",
      "params": {
        "x": 800, "y": 1200
      },
      "description": "Interact"
    },
    {
      "key": "Escape",
      "action": "passthrough",
      "description": "Pass to Android as-is"
    }
  ],
  "mousemap": {
    "enabled": true,
    "mode": "aim",
    "sensitivity": 1.0,
    "centerX": 700,
    "centerY": 900,
    "description": "Mouse controls camera/aiming"
  }
}
```

### Action Types

| Action | What It Does | Parameters |
|---|---|---|
| `tap` | Single touch at a point | `x`, `y` |
| `hold` | Touch-and-hold while key is held | `x`, `y` |
| `swipe` | Swipe from A to B | `startX/Y`, `endX/Y`, `durationMs` |
| `passthrough` | Forward the key to Android as a keyboard key | (none) |
| `repeat_tap` | Tap repeatedly while held | `x`, `y`, `intervalMs` |

### Classes

#### `KeymapProfile`
- Data class representing a parsed keymap JSON
- Stores a map of `Qt::Key → KeyAction`
- Stores mouse mapping config
- Method: `static loadFromFile(path) → KeymapProfile`
- Method: `actionForKey(Qt::Key) → optional<KeyAction>`

#### `KeyAction` (struct/variant)
```cpp
struct KeyAction {
    enum Type { Tap, Hold, Swipe, Passthrough, RepeatTap };
    Type type;
    // Tap/Hold/RepeatTap
    int x = 0, y = 0;
    // Swipe
    int startX = 0, startY = 0, endX = 0, endY = 0;
    int durationMs = 100;
    // RepeatTap
    int intervalMs = 50;
    // Metadata
    QString description;
};
```

#### `KeymapEngine`
- Owns the active `KeymapProfile`
- Methods:
  - `loadProfile(path)` — load a JSON profile
  - `setEnabled(bool)` — toggle keymap on/off
  - `handleKeyPress(Qt::Key)` — resolve to action, execute
  - `handleKeyRelease(Qt::Key)` — stop holds/swipes
  - `listProfiles(directory)` — scan for available profiles
- Uses `InputHandler` or `QmpClient` to send the actual touch events
- Must handle coordinate scaling if guest resolution ≠ profile resolution

### Integration with InputHandler

```
InputHandler::keyPressEvent(event):
    if keymapEngine.isEnabled():
        action = keymapEngine.handleKeyPress(event.key())
        if action is not passthrough:
            return  // consumed by keymap
    // else: send as keyboard event to guest
```

### Coordinate Scaling

Keymap profiles define coordinates for a specific resolution.
If the actual guest resolution differs, coordinates must be scaled:

```
scaledX = profileX * (actualWidth / profileWidth)
scaledY = profileY * (actualHeight / profileHeight)
```

## Manual Test Checklist

- [ ] Default keymap profile loads without errors
- [ ] Gaming keymap profile loads without errors
- [ ] Invalid JSON is handled gracefully (error message, not crash)
- [ ] With keymap enabled: pressing mapped keys produces touch events
- [ ] With keymap disabled: all keys pass through as keyboard input
- [ ] Tap action: single touch at correct coordinates
- [ ] Hold action: touch-and-hold while key pressed, release on key-up
- [ ] Swipe action: smooth swipe from start to end
- [ ] Passthrough action: key reaches Android as a keyboard event
- [ ] Coordinate scaling works for different resolutions
- [ ] Multiple profiles can be switched at runtime
- [ ] Unit tests pass for JSON parsing and action dispatch
