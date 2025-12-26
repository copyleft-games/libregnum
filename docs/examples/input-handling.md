# Input Handling Example

A complete example showing how to set up and use the Input module for game controls.

## Setup

```c
#include <libregnum.h>

typedef struct
{
    LrgEngine *engine;
    LrgInputMap *input_map;
    gfloat player_x;
    gfloat player_y;
    gfloat player_speed;
} Game;

Game *game_new(void)
{
    Game *game = g_new0(Game, 1);
    game->engine = lrg_engine_get_default();
    game->input_map = lrg_input_map_new();
    game->player_x = 400;
    game->player_y = 300;
    game->player_speed = 200;  /* pixels per second */
    return game;
}
```

## Creating Input Actions

```c
void setup_game_input(Game *game)
{
    LrgInputMap *map = game->input_map;

    /* Jump action */
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

    /* Movement actions */
    const gchar *movement_names[] = {
        "move_left", "move_right", "move_up", "move_down"
    };

    GrlKey keys[] = {
        GRL_KEY_A, GRL_KEY_D, GRL_KEY_W, GRL_KEY_S
    };

    GrlKey arrow_keys[] = {
        GRL_KEY_LEFT, GRL_KEY_RIGHT, GRL_KEY_UP, GRL_KEY_DOWN
    };

    GrlGamepadAxis axes[] = {
        GRL_GAMEPAD_AXIS_LEFT_X,  /* left */
        GRL_GAMEPAD_AXIS_LEFT_X,  /* right */
        GRL_GAMEPAD_AXIS_LEFT_Y,  /* up */
        GRL_GAMEPAD_AXIS_LEFT_Y   /* down */
    };

    gboolean axis_positive[] = {
        FALSE, TRUE, FALSE, TRUE
    };

    for (guint i = 0; i < 4; i++)
    {
        g_autoptr(LrgInputAction) action = lrg_input_action_new(
            movement_names[i]
        );

        /* WASD binding */
        LrgInputBinding *key_binding = lrg_input_binding_new_keyboard(
            keys[i],
            LRG_INPUT_MODIFIER_NONE
        );
        lrg_input_action_add_binding(action, key_binding);

        /* Arrow key binding */
        LrgInputBinding *arrow_binding = lrg_input_binding_new_keyboard(
            arrow_keys[i],
            LRG_INPUT_MODIFIER_NONE
        );
        lrg_input_action_add_binding(action, arrow_binding);

        /* Gamepad analog stick binding */
        LrgInputBinding *axis_binding = lrg_input_binding_new_gamepad_axis(
            0,
            axes[i],
            0.2f,  /* Dead zone */
            axis_positive[i]
        );
        lrg_input_action_add_binding(action, axis_binding);

        lrg_input_binding_free(key_binding);
        lrg_input_binding_free(arrow_binding);
        lrg_input_binding_free(axis_binding);

        lrg_input_map_add_action(map, action);
    }

    /* Attack action */
    g_autoptr(LrgInputAction) attack = lrg_input_action_new("attack");

    LrgInputBinding *attack_key = lrg_input_binding_new_keyboard(
        GRL_KEY_Z,
        LRG_INPUT_MODIFIER_NONE
    );
    lrg_input_action_add_binding(attack, attack_key);

    LrgInputBinding *attack_gamepad = lrg_input_binding_new_gamepad_button(
        0,
        GRL_GAMEPAD_BUTTON_RIGHT_FACE_LEFT  /* X / Square */
    );
    lrg_input_action_add_binding(attack, attack_gamepad);

    lrg_input_binding_free(attack_key);
    lrg_input_binding_free(attack_gamepad);

    lrg_input_map_add_action(map, attack);
}
```

## Loading/Saving Controls

```c
void load_or_create_input_config(Game *game)
{
    g_autoptr(GError) error = NULL;
    const gchar *config_path = "config/controls.yaml";

    if (!lrg_input_map_load_from_file(game->input_map, config_path, &error))
    {
        if (error != NULL)
            g_message("No saved controls, creating defaults: %s", error->message);

        setup_game_input(game);

        /* Save defaults for future use */
        if (!lrg_input_map_save_to_file(game->input_map, config_path, &error))
        {
            g_warning("Failed to save controls: %s", error->message);
        }
    }
    else
    {
        g_message("Controls loaded from %s", config_path);
    }
}
```

## Game Update

```c
void game_update(Game *game, gfloat delta_time)
{
    LrgInputMap *map = game->input_map;

    /* Jump (discrete action) */
    if (lrg_input_map_is_pressed(map, "jump"))
    {
        g_message("Player jumped!");
        player_jump(game);
    }

    /* Movement (analog input) */
    gfloat move_x = 0.0f;
    gfloat move_y = 0.0f;

    if (lrg_input_map_is_down(map, "move_right"))
        move_x += lrg_input_map_get_value(map, "move_right");

    if (lrg_input_map_is_down(map, "move_left"))
        move_x -= lrg_input_map_get_value(map, "move_left");

    if (lrg_input_map_is_down(map, "move_down"))
        move_y += lrg_input_map_get_value(map, "move_down");

    if (lrg_input_map_is_down(map, "move_up"))
        move_y -= lrg_input_map_get_value(map, "move_up");

    /* Normalize diagonal movement */
    gfloat magnitude = sqrtf(move_x*move_x + move_y*move_y);
    if (magnitude > 1.0f)
    {
        move_x /= magnitude;
        move_y /= magnitude;
    }

    /* Apply movement */
    game->player_x += move_x * game->player_speed * delta_time;
    game->player_y += move_y * game->player_speed * delta_time;

    /* Attack (continuous while held) */
    if (lrg_input_map_is_down(map, "attack"))
    {
        player_attack(game);
    }

    if (lrg_input_map_is_released(map, "attack"))
    {
        player_stop_attack(game);
    }
}
```

## Control Remapping Menu

```c
void show_rebind_menu(Game *game, const gchar *action_name)
{
    g_message("Press a key to rebind '%s'", action_name);
    g_message("Current bindings:");

    LrgInputAction *action = lrg_input_map_get_action(
        game->input_map,
        action_name
    );

    if (action == NULL)
    {
        g_warning("Action '%s' not found", action_name);
        return;
    }

    guint count = lrg_input_action_get_binding_count(action);
    for (guint i = 0; i < count; i++)
    {
        const LrgInputBinding *binding = lrg_input_action_get_binding(
            action,
            i
        );
        g_autofree gchar *str = lrg_input_binding_to_string(binding);
        g_message("  [%u] %s", i, str);
    }
}

void rebind_action_key(Game *game, const gchar *action_name, GrlKey key)
{
    LrgInputAction *action = lrg_input_map_get_action(
        game->input_map,
        action_name
    );

    if (action == NULL)
        return;

    /* Clear old bindings and add new key binding */
    lrg_input_action_clear_bindings(action);

    LrgInputBinding *new_binding = lrg_input_binding_new_keyboard(
        key,
        LRG_INPUT_MODIFIER_NONE
    );
    lrg_input_action_add_binding(action, new_binding);
    lrg_input_binding_free(new_binding);

    /* Save updated controls */
    g_autoptr(GError) error = NULL;
    if (!lrg_input_map_save_to_file(game->input_map,
                                     "config/controls.yaml", &error))
    {
        g_warning("Failed to save rebind: %s", error->message);
    }
    else
    {
        g_autofree gchar *str = lrg_input_binding_to_string(new_binding);
        g_message("Rebound '%s' to %s", action_name, str);
    }
}
```

## YAML Configuration Example

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

  - name: move_left
    bindings:
      - type: keyboard
        key: A
        modifiers: NONE
      - type: keyboard
        key: LEFT
        modifiers: NONE
      - type: gamepad_axis
        gamepad: 0
        axis: LEFT_X
        threshold: 0.2
        positive: false

  - name: move_right
    bindings:
      - type: keyboard
        key: D
        modifiers: NONE
      - type: keyboard
        key: RIGHT
        modifiers: NONE
      - type: gamepad_axis
        gamepad: 0
        axis: LEFT_X
        threshold: 0.2
        positive: true

  - name: move_up
    bindings:
      - type: keyboard
        key: W
        modifiers: NONE
      - type: keyboard
        key: UP
        modifiers: NONE
      - type: gamepad_axis
        gamepad: 0
        axis: LEFT_Y
        threshold: 0.2
        positive: false

  - name: move_down
    bindings:
      - type: keyboard
        key: S
        modifiers: NONE
      - type: keyboard
        key: DOWN
        modifiers: NONE
      - type: gamepad_axis
        gamepad: 0
        axis: LEFT_Y
        threshold: 0.2
        positive: true

  - name: attack
    bindings:
      - type: keyboard
        key: Z
        modifiers: NONE
      - type: gamepad_button
        gamepad: 0
        button: X
```

## Complete Game Loop Example

```c
int main(void)
{
    Game *game = game_new();
    load_or_create_input_config(game);

    gboolean running = TRUE;
    GTimer *timer = g_timer_new();

    while (running)
    {
        gfloat delta_time = g_timer_elapsed(timer, NULL);
        g_timer_reset(timer);

        /* Update game with input */
        game_update(game, delta_time);

        /* Render */
        /* ... graphics code ... */

        /* Check for quit */
        if (lrg_input_map_is_pressed(game->input_map, "quit"))
            running = FALSE;
    }

    g_timer_destroy(timer);
    g_object_unref(game->input_map);
    g_free(game);

    return 0;
}
```

## Key Concepts Demonstrated

1. **Action-based input** - Query actions, not raw input
2. **Multiple bindings** - One action, multiple input methods
3. **Keyboard + Gamepad** - Same action works with both
4. **Analog input** - Stick axes with dead zones
5. **YAML persistence** - Load/save control configuration
6. **Control remapping** - Let players rebind keys
7. **Normalized movement** - Prevent faster diagonal movement

## Next Steps

- See [Input Module Overview](../modules/input/index.md) for more details
- See [UI Basics Example](ui-basics.md) for UI integration
