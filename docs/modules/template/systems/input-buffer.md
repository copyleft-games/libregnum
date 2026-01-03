# LrgInputBuffer

`LrgInputBuffer` provides frame-perfect input buffering for action games. It allows inputs to be recorded slightly before they can be executed and consumed when the action becomes available, creating more responsive controls.

## Overview

Input buffering solves the common problem in action games where a player presses a button slightly too early (e.g., pressing jump before landing) and the input is lost. The buffer stores recent inputs and allows the game to consume them within a configurable time window.

## Features

- Configurable buffer duration per action
- Context-aware buffering (e.g., ground vs air actions)
- Record/consume pattern for clean input handling
- Automatic expiration of stale inputs
- Multiple simultaneous buffered actions

## Quick Start

```c
/* Create input buffer */
LrgInputBuffer *buffer = lrg_input_buffer_new ();

/* Configure buffer windows */
lrg_input_buffer_set_buffer_frames (buffer, "jump", 6);    /* 6 frames = 100ms at 60fps */
lrg_input_buffer_set_buffer_frames (buffer, "attack", 4);  /* 4 frames = 66ms */
lrg_input_buffer_set_buffer_frames (buffer, "dodge", 8);   /* 8 frames = 133ms */

/* In update loop - record inputs */
if (lrg_input_action_just_pressed (jump_action))
    lrg_input_buffer_record (buffer, "jump");

if (lrg_input_action_just_pressed (attack_action))
    lrg_input_buffer_record (buffer, "attack");

/* When action becomes available - consume buffered input */
if (player_is_grounded && lrg_input_buffer_consume (buffer, "jump"))
{
    player_jump ();
}

/* Tick the buffer each frame to expire old inputs */
lrg_input_buffer_tick (buffer);
```

## API Reference

### Construction

```c
/* Create a new input buffer */
LrgInputBuffer *buffer = lrg_input_buffer_new ();
```

### Configuration

```c
/* Set buffer duration in frames for an action */
lrg_input_buffer_set_buffer_frames (buffer, "jump", 6);

/* Get buffer duration for an action */
guint frames = lrg_input_buffer_get_buffer_frames (buffer, "jump");

/* Set default buffer frames for unregistered actions */
lrg_input_buffer_set_default_frames (buffer, 4);
guint default_frames = lrg_input_buffer_get_default_frames (buffer);
```

### Recording Inputs

```c
/* Record an input (starts the buffer timer) */
lrg_input_buffer_record (buffer, "jump");

/* Record with context (only consumable in matching context) */
lrg_input_buffer_record_with_context (buffer, "wall_jump", "touching_wall");
```

### Consuming Inputs

```c
/* Consume a buffered input (returns TRUE if input was buffered and not expired) */
if (lrg_input_buffer_consume (buffer, "jump"))
{
    /* Input was buffered and is now consumed */
    perform_jump ();
}

/* Consume with context requirement */
if (lrg_input_buffer_consume_with_context (buffer, "wall_jump", "touching_wall"))
{
    perform_wall_jump ();
}

/* Check if input is buffered without consuming */
if (lrg_input_buffer_is_buffered (buffer, "attack"))
{
    /* Attack is buffered, but we're not consuming it yet */
}
```

### Frame Management

```c
/* Tick the buffer (call once per frame) */
lrg_input_buffer_tick (buffer);

/* Clear all buffered inputs */
lrg_input_buffer_clear (buffer);

/* Clear specific action */
lrg_input_buffer_clear_action (buffer, "jump");
```

## Context-Aware Buffering

Contexts allow the same action to have different meanings depending on player state:

```c
static void
update_player (Player *self, gfloat delta)
{
    LrgInputBuffer *buffer = self->input_buffer;
    const gchar *context = NULL;

    /* Determine current context */
    if (self->is_grounded)
        context = "ground";
    else if (self->is_touching_wall)
        context = "wall";
    else
        context = "air";

    /* Record inputs with context */
    if (lrg_input_action_just_pressed (self->jump_action))
        lrg_input_buffer_record_with_context (buffer, "jump", context);

    /* Consume based on current state */
    if (self->is_grounded)
    {
        if (lrg_input_buffer_consume_with_context (buffer, "jump", "ground"))
            player_ground_jump (self);
    }
    else if (self->is_touching_wall)
    {
        if (lrg_input_buffer_consume_with_context (buffer, "jump", "wall"))
            player_wall_jump (self);
    }

    lrg_input_buffer_tick (buffer);
}
```

## Integration with Templates

Templates that use input buffering automatically create and manage the buffer:

```c
/* LrgPlatformerTemplate provides built-in buffering */
static void
my_platformer_configure (LrgGameTemplate *template)
{
    LrgPlatformerTemplate *platformer = LRG_PLATFORMER_TEMPLATE (template);

    /* Configure jump buffering */
    lrg_platformer_template_set_jump_buffer_frames (platformer, 6);

    /* Configure coyote time (related concept) */
    lrg_platformer_template_set_coyote_frames (platformer, 6);
}
```

## Coyote Time vs Input Buffering

These are complementary but distinct concepts:

| Concept | Purpose | When It Helps |
|---------|---------|---------------|
| Input Buffering | Store early inputs | Player presses jump before landing |
| Coyote Time | Allow late inputs | Player presses jump after walking off ledge |

Both together create the most responsive platformer controls:

```c
static void
update_jump (Player *self)
{
    gboolean can_jump = FALSE;

    /* Coyote time - can still jump briefly after leaving ground */
    if (self->is_grounded || self->coyote_timer > 0)
        can_jump = TRUE;

    /* Input buffer - consume jump pressed slightly early */
    if (can_jump && lrg_input_buffer_consume (self->buffer, "jump"))
    {
        player_jump (self);
        self->coyote_timer = 0;  /* Reset coyote time on jump */
    }

    /* Decrement coyote timer */
    if (!self->is_grounded && self->coyote_timer > 0)
        self->coyote_timer--;
}
```

## Common Buffer Windows

Recommended buffer durations for different game types:

| Game Type | Action | Frames (60fps) | Milliseconds |
|-----------|--------|----------------|--------------|
| Platformer | Jump | 4-8 | 66-133ms |
| Platformer | Wall Jump | 6-10 | 100-166ms |
| Fighting | Attack | 3-6 | 50-100ms |
| Fighting | Block | 4-8 | 66-133ms |
| Action RPG | Dodge | 6-10 | 100-166ms |
| Action RPG | Parry | 2-4 | 33-66ms |

Shorter windows feel more precise but less forgiving. Longer windows feel more forgiving but can cause unwanted actions.

## Best Practices

1. **Tick every frame**: Always call `lrg_input_buffer_tick()` every frame, even when paused if inputs should still expire.

2. **Clear on state changes**: Clear relevant buffers when changing game states to prevent stale inputs from triggering.

3. **Use contexts for disambiguation**: When the same button does different things in different states, use contexts.

4. **Tune per-action**: Different actions benefit from different buffer windows. Quick reactions (parry) should be shorter than forgiving ones (jump).

5. **Don't over-buffer**: Too generous buffering can make controls feel imprecise. Start small and increase based on playtesting.

## Related Documentation

- [LrgPlatformerTemplate](../templates/platformer-template.md) - Built-in jump buffering
- [Game Feel](game-feel.md) - Other responsiveness techniques
- [Input System](../../input/index.md) - Core input handling
