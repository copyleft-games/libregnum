# Input Map

An `LrgInputMap` is a container for input actions with YAML serialization support. It manages all actions for your game and allows players to save and load their custom control schemes.

## Overview

An input map:

- Contains multiple named actions
- Can be saved to and loaded from YAML files
- Provides convenience methods to query actions by name
- Supports persistence for user control remapping

## Creating an Input Map

```c
g_autoptr(LrgInputMap) input_map = lrg_input_map_new();
```

## Adding Actions

```c
g_autoptr(LrgInputMap) input_map = lrg_input_map_new();

/* Create an action */
g_autoptr(LrgInputAction) jump = lrg_input_action_new("jump");

/* Add bindings to it */
LrgInputBinding *space = lrg_input_binding_new_keyboard(GRL_KEY_SPACE,
                                                        LRG_INPUT_MODIFIER_NONE);
lrg_input_action_add_binding(jump, space);
lrg_input_binding_free(space);

/* Add action to map (map takes ownership) */
lrg_input_map_add_action(input_map, jump);

/* g_autoptr will clean up jump when it goes out of scope */
```

## Querying Actions

### Existence Check

```c
if (lrg_input_map_has_action(input_map, "jump"))
{
    g_message("Jump action exists");
}
```

### Getting an Action

```c
LrgInputAction *jump = lrg_input_map_get_action(input_map, "jump");
if (jump != NULL)
{
    g_message("Found jump action");
}
else
{
    g_message("Jump action not found");
}
```

### Getting All Actions

```c
GList *actions = lrg_input_map_get_actions(input_map);
g_message("Map has %u actions", g_list_length(actions));

for (GList *iter = actions; iter; iter = iter->next)
{
    LrgInputAction *action = LRG_INPUT_ACTION(iter->data);
    g_message("  - %s", lrg_input_action_get_name(action));
}

g_list_free(actions);  /* Free the list, not the actions */
```

### Getting Action Count

```c
guint count = lrg_input_map_get_action_count(input_map);
g_message("Map contains %u actions", count);
```

## Removing Actions

```c
/* Remove specific action by name */
lrg_input_map_remove_action(input_map, "jump");

/* Clear all actions */
lrg_input_map_clear(input_map);
```

## Convenience State Queries

The map provides shortcut methods to query action states by name:

### Pressed

```c
if (lrg_input_map_is_pressed(input_map, "jump"))
{
    player_jump();
}
```

### Down

```c
if (lrg_input_map_is_down(input_map, "attack"))
{
    play_attack_animation();
}
```

### Released

```c
if (lrg_input_map_is_released(input_map, "attack"))
{
    stop_attack_animation();
}
```

### Value

```c
gfloat move_x = lrg_input_map_get_value(input_map, "move_right") -
                lrg_input_map_get_value(input_map, "move_left");

player_x += move_x * player_speed;
```

Note: If the action doesn't exist, these return FALSE or 0.0.

## Saving and Loading

### Saving to File

```c
g_autoptr(GError) error = NULL;

if (!lrg_input_map_save_to_file(input_map, "config/controls.yaml", &error))
{
    g_warning("Failed to save controls: %s", error->message);
}
```

### Loading from File

```c
g_autoptr(GError) error = NULL;
g_autoptr(LrgInputMap) loaded_map = lrg_input_map_new();

if (!lrg_input_map_load_from_file(loaded_map, "config/controls.yaml", &error))
{
    g_warning("Failed to load controls: %s", error->message);
    /* Use default map instead */
}
else
{
    g_message("Controls loaded successfully");
}
```

### Clear on Load

Loading a file **clears** any existing actions in the map:

```c
g_autoptr(LrgInputMap) map = lrg_input_map_new();

/* Add default actions */
g_autoptr(LrgInputAction) jump = lrg_input_action_new("jump");
/* ... add bindings ... */
lrg_input_map_add_action(map, jump);

g_message("Before: %u actions", lrg_input_map_get_action_count(map));

/* Load user's config - this clears the map first */
g_autoptr(GError) error = NULL;
lrg_input_map_load_from_file(map, "config/user_controls.yaml", &error);

g_message("After: %u actions", lrg_input_map_get_action_count(map));
/* The default actions are gone, replaced by loaded ones */
```

## YAML Format

Input maps serialize to human-readable YAML:

```yaml
actions:
  - name: jump
    bindings:
      - type: keyboard
        key: SPACE
        modifiers: NONE
      - type: gamepad_button
        gamepad: 0
        button: A

  - name: attack
    bindings:
      - type: keyboard
        key: Z
        modifiers: NONE
      - type: gamepad_button
        gamepad: 0
        button: X

  - name: move_right
    bindings:
      - type: keyboard
        key: D
        modifiers: NONE
      - type: gamepad_axis
        gamepad: 0
        axis: LEFT_X
        threshold: 0.2
        positive: true

  - name: move_left
    bindings:
      - type: keyboard
        key: A
        modifiers: NONE
      - type: gamepad_axis
        gamepad: 0
        axis: LEFT_X
        threshold: 0.2
        positive: false
```

## Complete Example

```c
void setup_input_map(LrgInputMap *map)
{
    /* Create and bind jump action */
    g_autoptr(LrgInputAction) jump = lrg_input_action_new("jump");

    LrgInputBinding *jump_space = lrg_input_binding_new_keyboard(
        GRL_KEY_SPACE,
        LRG_INPUT_MODIFIER_NONE
    );
    lrg_input_action_add_binding(jump, jump_space);

    LrgInputBinding *jump_gamepad = lrg_input_binding_new_gamepad_button(
        0,
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN
    );
    lrg_input_action_add_binding(jump, jump_gamepad);

    lrg_input_binding_free(jump_space);
    lrg_input_binding_free(jump_gamepad);

    lrg_input_map_add_action(map, jump);

    /* Create movement actions */
    const gchar *movement_keys[] = {
        "move_left",  "move_right",  "move_up",  "move_down"
    };
    GrlKey keys[] = {
        GRL_KEY_A,     GRL_KEY_D,      GRL_KEY_W,  GRL_KEY_S
    };
    GrlGamepadAxis axes[] = {
        GRL_GAMEPAD_AXIS_LEFT_X,   /* left negative */
        GRL_GAMEPAD_AXIS_LEFT_X,   /* right positive */
        GRL_GAMEPAD_AXIS_LEFT_Y,   /* up negative */
        GRL_GAMEPAD_AXIS_LEFT_Y,   /* down positive */
    };
    gboolean positives[] = {
        FALSE,  TRUE,  FALSE,  TRUE
    };

    for (guint i = 0; i < G_N_ELEMENTS(movement_keys); i++)
    {
        g_autoptr(LrgInputAction) action = lrg_input_action_new(movement_keys[i]);

        LrgInputBinding *key_binding = lrg_input_binding_new_keyboard(
            keys[i],
            LRG_INPUT_MODIFIER_NONE
        );
        lrg_input_action_add_binding(action, key_binding);

        LrgInputBinding *axis_binding = lrg_input_binding_new_gamepad_axis(
            0,
            axes[i],
            0.2f,
            positives[i]
        );
        lrg_input_action_add_binding(action, axis_binding);

        lrg_input_binding_free(key_binding);
        lrg_input_binding_free(axis_binding);

        lrg_input_map_add_action(map, action);
    }
}

void game_init(void)
{
    g_autoptr(LrgInputMap) input_map = lrg_input_map_new();
    g_autoptr(GError) error = NULL;

    /* Try to load user's config */
    if (!lrg_input_map_load_from_file(input_map,
                                      "config/controls.yaml", &error))
    {
        g_message("No saved controls, using defaults");
        setup_input_map(input_map);

        /* Save defaults for next time */
        lrg_input_map_save_to_file(input_map, "config/controls.yaml", &error);
    }

    /* Use the map in game */
    game->input_map = g_steal_pointer(&input_map);
}

void game_update(void)
{
    /* Query input using the map */
    if (lrg_input_map_is_pressed(game->input_map, "jump"))
    {
        player_jump();
    }

    gfloat move_x = lrg_input_map_get_value(game->input_map, "move_right") -
                    lrg_input_map_get_value(game->input_map, "move_left");

    player_move(move_x * player_speed);
}
```

## API Reference

### Construction

- `lrg_input_map_new()` - Create a new input map

### Action Management

- `lrg_input_map_add_action(LrgInputMap *self, LrgInputAction *action)` - Add action
- `lrg_input_map_remove_action(LrgInputMap *self, const gchar *name)` - Remove by name
- `lrg_input_map_get_action(LrgInputMap *self, const gchar *name)` - Get by name
- `lrg_input_map_has_action(LrgInputMap *self, const gchar *name)` - Check existence
- `lrg_input_map_get_actions(LrgInputMap *self)` - Get all actions
- `lrg_input_map_get_action_count(LrgInputMap *self)` - Get action count
- `lrg_input_map_clear(LrgInputMap *self)` - Remove all actions

### Convenience State Queries

- `lrg_input_map_is_pressed(LrgInputMap *self, const gchar *action_name)`
- `lrg_input_map_is_down(LrgInputMap *self, const gchar *action_name)`
- `lrg_input_map_is_released(LrgInputMap *self, const gchar *action_name)`
- `lrg_input_map_get_value(LrgInputMap *self, const gchar *action_name)`

### Serialization

- `lrg_input_map_save_to_file(LrgInputMap *self, const gchar *path, GError **error)`
- `lrg_input_map_load_from_file(LrgInputMap *self, const gchar *path, GError **error)`

## Notes

- Input maps are reference-counted GObjects
- Loading clears existing actions
- File paths are relative to the current working directory (usually the game directory)
- YAML files are human-editable for manual control customization
- Convenience queries return safe defaults if action doesn't exist

## Related

- [Input Action](input-action.md) - Individual action with bindings
- [Input Binding](input-binding.md) - Single physical input
- [Input Module Overview](index.md) - Complete module documentation
