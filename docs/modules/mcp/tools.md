# MCP Tools Reference

This document provides a complete reference for all MCP tools available in libregnum.

## Input Tools (`LrgMcpInputTools`)

Input tools inject keyboard, mouse, and gamepad input into the game via `LrgInputSoftware`.

### lrg_input_press_key

Press and hold a keyboard key.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| key | string | Yes | Key name (see Key Names table) |

**Example:**
```json
{
  "tool": "lrg_input_press_key",
  "arguments": {
    "key": "space"
  }
}
```

### lrg_input_release_key

Release a held keyboard key.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| key | string | Yes | Key name (see Key Names table) |

### lrg_input_tap_key

Press and immediately release a key.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| key | string | Yes | Key name (see Key Names table) |

### lrg_input_press_mouse_button

Press and hold a mouse button.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| button | string | Yes | Button name: "left", "right", "middle" |

### lrg_input_release_mouse_button

Release a held mouse button.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| button | string | Yes | Button name: "left", "right", "middle" |

### lrg_input_move_mouse_to

Move mouse to an absolute screen position.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| x | number | Yes | X coordinate |
| y | number | Yes | Y coordinate |

### lrg_input_move_mouse_by

Move mouse by a relative amount.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| dx | number | Yes | X delta |
| dy | number | Yes | Y delta |

### lrg_input_press_gamepad_button

Press a gamepad button.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| gamepad | number | No | Gamepad index (default: 0) |
| button | string | Yes | Button name (see Gamepad Buttons table) |

### lrg_input_release_gamepad_button

Release a gamepad button.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| gamepad | number | No | Gamepad index (default: 0) |
| button | string | Yes | Button name (see Gamepad Buttons table) |

### lrg_input_set_gamepad_axis

Set a gamepad axis value.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| gamepad | number | No | Gamepad index (default: 0) |
| axis | string | Yes | Axis name (see Gamepad Axes table) |
| value | number | Yes | Value from -1.0 to 1.0 |

### lrg_input_clear_all

Release all held inputs (keys, buttons, axes).

No parameters.

### lrg_input_get_state

Get current input state summary.

No parameters.

**Returns:**
```json
{
  "pressed_keys": ["w", "shift"],
  "mouse_position": {"x": 640, "y": 480},
  "mouse_buttons": ["left"],
  "gamepads": [
    {
      "index": 0,
      "buttons": ["a"],
      "axes": {"left_x": 0.5, "left_y": -0.3}
    }
  ]
}
```

### Key Names

| Key Name | Description |
|----------|-------------|
| `a`-`z` | Letter keys |
| `0`-`9` | Number keys |
| `f1`-`f12` | Function keys |
| `space` | Spacebar |
| `enter`, `return` | Enter key |
| `escape`, `esc` | Escape key |
| `tab` | Tab key |
| `backspace` | Backspace |
| `delete` | Delete key |
| `insert` | Insert key |
| `home`, `end` | Home/End keys |
| `pageup`, `pagedown` | Page Up/Down |
| `up`, `down`, `left`, `right` | Arrow keys |
| `shift`, `lshift`, `rshift` | Shift keys |
| `ctrl`, `lctrl`, `rctrl` | Control keys |
| `alt`, `lalt`, `ralt` | Alt keys |
| `capslock` | Caps Lock |
| `numlock` | Num Lock |
| `scrolllock` | Scroll Lock |
| `kp0`-`kp9` | Keypad numbers |
| `kpadd`, `kpsub`, `kpmul`, `kpdiv` | Keypad operators |
| `kpenter` | Keypad Enter |
| `kpdot` | Keypad decimal |

### Gamepad Buttons

| Button Name | Description |
|-------------|-------------|
| `a`, `south` | A/Cross button |
| `b`, `east` | B/Circle button |
| `x`, `west` | X/Square button |
| `y`, `north` | Y/Triangle button |
| `lb`, `left_shoulder` | Left bumper |
| `rb`, `right_shoulder` | Right bumper |
| `lt`, `left_trigger` | Left trigger (digital) |
| `rt`, `right_trigger` | Right trigger (digital) |
| `back`, `select` | Back/Select button |
| `start` | Start button |
| `guide`, `home` | Guide/Home button |
| `ls`, `left_stick` | Left stick click |
| `rs`, `right_stick` | Right stick click |
| `dpad_up` | D-pad up |
| `dpad_down` | D-pad down |
| `dpad_left` | D-pad left |
| `dpad_right` | D-pad right |

### Gamepad Axes

| Axis Name | Range | Description |
|-----------|-------|-------------|
| `left_x` | -1.0 to 1.0 | Left stick horizontal |
| `left_y` | -1.0 to 1.0 | Left stick vertical |
| `right_x` | -1.0 to 1.0 | Right stick horizontal |
| `right_y` | -1.0 to 1.0 | Right stick vertical |
| `left_trigger` | 0.0 to 1.0 | Left trigger analog |
| `right_trigger` | 0.0 to 1.0 | Right trigger analog |

## Screenshot Tools (`LrgMcpScreenshotTools`)

Screenshot tools capture the current frame as PNG images.

### lrg_screenshot_capture

Capture the full screen.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| scale | number | No | Scale factor (0.0-1.0, default: 1.0) |

**Returns:** Base64-encoded PNG image.

### lrg_screenshot_region

Capture a rectangular region.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| x | number | Yes | Left coordinate |
| y | number | Yes | Top coordinate |
| width | number | Yes | Region width |
| height | number | Yes | Region height |
| scale | number | No | Scale factor (0.0-1.0, default: 1.0) |

**Returns:** Base64-encoded PNG image.

## Engine Tools (`LrgMcpEngineTools`)

Engine tools query and control the game engine.

### lrg_engine_get_info

Get current engine state.

No parameters.

**Returns:**
```json
{
  "fps": 60.0,
  "delta_time": 0.0166,
  "running": true,
  "paused": false,
  "version": "1.0.0"
}
```

### lrg_engine_pause

Pause the engine (stops update loop).

No parameters.

### lrg_engine_resume

Resume the engine after pausing.

No parameters.

### lrg_engine_step_frame

Advance exactly one frame while paused.

No parameters.

## ECS Tools (`LrgMcpEcsTools`)

ECS tools query and manipulate the Entity-Component-System.

### lrg_ecs_list_worlds

List all active game worlds.

No parameters.

**Returns:**
```json
{
  "worlds": [
    {"name": "main", "object_count": 150, "active": true},
    {"name": "ui", "object_count": 25, "active": true}
  ]
}
```

### lrg_ecs_list_game_objects

List game objects in a world.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| world | string | No | World name (default: first active world) |
| limit | number | No | Max objects to return (default: 100) |

**Returns:**
```json
{
  "world": "main",
  "objects": [
    {"id": "player-1", "name": "Player", "active": true},
    {"id": "enemy-1", "name": "Goblin", "active": true}
  ]
}
```

### lrg_ecs_get_game_object

Get detailed information about a game object.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| id | string | Yes | Object ID |

**Returns:**
```json
{
  "id": "player-1",
  "name": "Player",
  "active": true,
  "transform": {
    "x": 100.0,
    "y": 200.0,
    "rotation": 0.0,
    "scale_x": 1.0,
    "scale_y": 1.0
  },
  "components": [
    "LrgTransformComponent",
    "LrgSpriteComponent",
    "LrgColliderComponent"
  ]
}
```

### lrg_ecs_get_component

Get component data from an object.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| object_id | string | Yes | Object ID |
| type | string | Yes | Component type name |

### lrg_ecs_set_component_property

Modify a component property.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| object_id | string | Yes | Object ID |
| type | string | Yes | Component type name |
| property | string | Yes | Property name |
| value | any | Yes | New value |

### lrg_ecs_get_transform

Get object transform data.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| object_id | string | Yes | Object ID |

**Returns:**
```json
{
  "x": 100.0,
  "y": 200.0,
  "rotation": 45.0,
  "scale_x": 1.0,
  "scale_y": 1.0
}
```

### lrg_ecs_set_transform

Set object transform.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| object_id | string | Yes | Object ID |
| x | number | No | X position |
| y | number | No | Y position |
| rotation | number | No | Rotation in degrees |
| scale_x | number | No | X scale |
| scale_y | number | No | Y scale |

### lrg_ecs_spawn_object

Spawn a new object from a registered type.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| type | string | Yes | Registered type name |
| x | number | No | Initial X position (default: 0) |
| y | number | No | Initial Y position (default: 0) |
| world | string | No | Target world (default: first active) |

**Returns:**
```json
{
  "id": "spawned-123",
  "type": "enemy",
  "success": true
}
```

### lrg_ecs_destroy_object

Destroy a game object.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| id | string | Yes | Object ID to destroy |

## Save Tools (`LrgMcpSaveTools`)

Save tools manage game saves.

### lrg_save_list_slots

List available save slots.

No parameters.

**Returns:**
```json
{
  "slots": [
    {"name": "slot1", "has_save": true, "timestamp": "2025-01-15T10:30:00Z"},
    {"name": "slot2", "has_save": false},
    {"name": "autosave", "has_save": true, "timestamp": "2025-01-15T11:00:00Z"}
  ]
}
```

### lrg_save_get_info

Get metadata for a save slot.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| slot | string | Yes | Slot name |

**Returns:**
```json
{
  "slot": "slot1",
  "timestamp": "2025-01-15T10:30:00Z",
  "description": "Before boss fight",
  "play_time": 3600.0,
  "version": "1.0.0"
}
```

### lrg_save_create

Create a new save.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| slot | string | Yes | Slot name |
| description | string | No | Save description |

### lrg_save_load

Load a save.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| slot | string | Yes | Slot name |

### lrg_save_delete

Delete a save slot.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| slot | string | Yes | Slot name |

### lrg_save_quick_save

Trigger a quick save.

No parameters.

### lrg_save_quick_load

Trigger a quick load.

No parameters.

## Debug Tools (`LrgMcpDebugTools`)

Debug tools for logging and profiling.

### lrg_debug_log

Log a message.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| message | string | Yes | Log message |
| level | string | No | Level: "debug", "info", "warning", "error" (default: "info") |

### lrg_debug_get_fps

Get detailed FPS statistics.

No parameters.

**Returns:**
```json
{
  "current_fps": 60.0,
  "average_fps": 59.8,
  "min_fps": 55.0,
  "max_fps": 62.0,
  "frame_time_ms": 16.6,
  "frame_count": 10000
}
```

### lrg_debug_profiler_start

Start a profiler section.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| name | string | Yes | Section name |

### lrg_debug_profiler_stop

Stop a profiler section.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| name | string | Yes | Section name |

### lrg_debug_profiler_report

Get profiler report.

No parameters.

**Returns:**
```json
{
  "sections": [
    {"name": "update", "total_ms": 5.2, "calls": 1000, "avg_ms": 0.0052},
    {"name": "render", "total_ms": 12.8, "calls": 1000, "avg_ms": 0.0128}
  ]
}
```
