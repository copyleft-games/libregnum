# LrgAnimatorComponent

Animation controller component.

## Overview

`LrgAnimatorComponent` manages sprite animations for game objects. It supports multiple named animations with individual settings, and can drive animation playback for sprite components.

Animations are defined as frame ranges within a spritesheet, and can be configured with individual speed and loop settings. The animator drives frame updates that can be read by sprite components or used to update sprite source rectangles.

## Basic Usage

### Setting Up Animations

```c
#include <libregnum.h>

g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();

/* Load spritesheet and configure frame dimensions */
GrlTexture *texture = grl_texture_new_from_file ("assets/player.png", NULL);
lrg_animator_component_set_texture (animator, texture, 32, 32);

/* Define animations
   Frame layout: 32x32 frames in 256-wide sheet = 8 frames per row
   Frame indices are left-to-right, top-to-bottom
*/

/* Idle: frames 0-3 (first row) at 10 FPS, looping */
lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, TRUE);

/* Walk: frames 4-7 (second row) at 15 FPS, looping */
lrg_animator_component_add_animation (animator, "walk", 4, 4, 15.0f, TRUE);

/* Run: frames 8-11 (third row) at 20 FPS, looping */
lrg_animator_component_add_animation (animator, "run", 8, 4, 20.0f, TRUE);

/* Jump: frames 12-13 (special) at 12 FPS, NOT looping */
lrg_animator_component_add_animation (animator, "jump", 12, 2, 12.0f, FALSE);
```

### Playing Animations

```c
g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();
/* ... configure as above ... */

/* Play animation from start */
gboolean success = lrg_animator_component_play (animator, "walk");
if (!success)
{
    g_warning ("Animation 'walk' not found");
}

/* Check if playing */
if (lrg_animator_component_is_playing (animator))
{
    g_print ("Animation is running\n");
}

/* Get current animation name */
const gchar *current = lrg_animator_component_get_current_animation (animator);

/* Get current frame (absolute index in spritesheet) */
gint frame = lrg_animator_component_get_current_frame (animator);

/* Get source rectangle for rendering */
g_autoptr(GrlRectangle) frame_rect =
    lrg_animator_component_get_current_frame_rect (animator);
```

### Animation Control

```c
g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();
/* ... setup ... */

lrg_animator_component_play (animator, "walk");

/* Pause animation */
lrg_animator_component_pause (animator);
if (!lrg_animator_component_is_playing (animator))
{
    g_print ("Animation paused\n");
}

/* Resume from pause */
lrg_animator_component_resume (animator);

/* Stop and reset to first frame */
lrg_animator_component_stop (animator);
g_assert_cmpint (lrg_animator_component_get_current_frame (animator), ==, 0);
```

### Speed Control

```c
g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();
/* ... setup ... */

/* Default speed is 1.0 */
g_assert_cmpfloat (lrg_animator_component_get_speed (animator), ==, 1.0f);

/* Slow down animation */
lrg_animator_component_set_speed (animator, 0.5f);

/* Speed up animation */
lrg_animator_component_set_speed (animator, 2.0f);

/* Reverse animation */
lrg_animator_component_set_speed (animator, -1.0f);
```

### Transitions

```c
/* Only play if not already playing */
gboolean started = lrg_animator_component_play_if_different (animator, "walk");
if (started)
{
    g_print ("Started walk animation\n");
}
else
{
    g_print ("Walk already playing\n");
}

/* Auto-transition on animation finish */
lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, FALSE);
lrg_animator_component_add_animation (animator, "walk", 4, 4, 15.0f, FALSE);

/* Set idle as default - plays when walk finishes */
lrg_animator_component_set_default_animation (animator, "idle");

lrg_animator_component_play (animator, "walk");
/* When walk finishes, idle will start automatically */
```

## API Reference

### Construction

```c
LrgAnimatorComponent * lrg_animator_component_new (void);
```

Creates a new animator component.

Returns: (transfer full) A new `LrgAnimatorComponent`

```c
LrgAnimatorComponent * lrg_animator_component_new_with_texture (GrlTexture *texture,
                                                                gint        frame_width,
                                                                gint        frame_height);
```

Creates a new animator component with a spritesheet.

Parameters:
- `texture` - The spritesheet texture
- `frame_width` - Width of each frame
- `frame_height` - Height of each frame

Returns: (transfer full) A new `LrgAnimatorComponent`

### Spritesheet Configuration

```c
void lrg_animator_component_set_texture (LrgAnimatorComponent *self,
                                         GrlTexture           *texture,
                                         gint                  frame_width,
                                         gint                  frame_height);
```

Sets the spritesheet texture and frame dimensions.

Parameters:
- `self` - an `LrgAnimatorComponent`
- `texture` - The spritesheet texture
- `frame_width` - Width of each frame in pixels
- `frame_height` - Height of each frame in pixels

```c
GrlTexture * lrg_animator_component_get_texture (LrgAnimatorComponent *self);
```

Gets the spritesheet texture.

Returns: (transfer none) (nullable) The texture, or `NULL`

```c
gint lrg_animator_component_get_frame_width (LrgAnimatorComponent *self);
gint lrg_animator_component_get_frame_height (LrgAnimatorComponent *self);
```

Gets the frame width/height in pixels.

### Animation Definition

```c
gboolean lrg_animator_component_add_animation (LrgAnimatorComponent *self,
                                               const gchar          *name,
                                               gint                  start_frame,
                                               gint                  frame_count,
                                               gfloat                fps,
                                               gboolean              loop);
```

Adds a named animation with the given settings. Frame indices are based on left-to-right, top-to-bottom order in the spritesheet.

Parameters:
- `self` - an `LrgAnimatorComponent`
- `name` - Unique animation name
- `start_frame` - First frame index (0-based)
- `frame_count` - Number of frames in this animation
- `fps` - Frames per second
- `loop` - Whether to loop

Returns: `TRUE` if added successfully, `FALSE` if name already exists

```c
gboolean lrg_animator_component_remove_animation (LrgAnimatorComponent *self,
                                                  const gchar          *name);
```

Removes an animation by name.

Parameters:
- `self` - an `LrgAnimatorComponent`
- `name` - Animation name to remove

Returns: `TRUE` if removed, `FALSE` if not found

```c
gboolean lrg_animator_component_has_animation (LrgAnimatorComponent *self,
                                               const gchar          *name);
```

Checks if an animation exists.

```c
GList * lrg_animator_component_get_animation_names (LrgAnimatorComponent *self);
```

Gets a list of all animation names.

Returns: (transfer container) (element-type utf8) List of animation names

```c
void lrg_animator_component_clear_animations (LrgAnimatorComponent *self);
```

Removes all animations.

### Playback Control

```c
gboolean lrg_animator_component_play (LrgAnimatorComponent *self, const gchar *name);
```

Starts playing an animation from the beginning. Emits the "animation-started" signal.

Parameters:
- `self` - an `LrgAnimatorComponent`
- `name` - Animation name to play

Returns: `TRUE` if animation started, `FALSE` if not found

```c
gboolean lrg_animator_component_play_if_different (LrgAnimatorComponent *self,
                                                   const gchar          *name);
```

Starts playing an animation only if it's not already playing. This prevents restarting an animation when called repeatedly.

Parameters:
- `self` - an `LrgAnimatorComponent`
- `name` - Animation name to play

Returns: `TRUE` if animation started or already playing

```c
void lrg_animator_component_stop (LrgAnimatorComponent *self);
```

Stops the current animation and resets to the first frame.

```c
void lrg_animator_component_pause (LrgAnimatorComponent *self);
```

Pauses the current animation.

```c
void lrg_animator_component_resume (LrgAnimatorComponent *self);
```

Resumes a paused animation.

### State Queries

```c
const gchar * lrg_animator_component_get_current_animation (LrgAnimatorComponent *self);
```

Gets the name of the current animation.

Returns: (nullable) Current animation name, or `NULL` if none

```c
gboolean lrg_animator_component_is_playing (LrgAnimatorComponent *self);
```

Checks if an animation is currently playing (not paused).

```c
gboolean lrg_animator_component_is_finished (LrgAnimatorComponent *self);
```

Checks if a non-looping animation has finished.

```c
gint lrg_animator_component_get_current_frame (LrgAnimatorComponent *self);
```

Gets the current frame index (absolute index in spritesheet, not relative to animation).

```c
GrlRectangle * lrg_animator_component_get_current_frame_rect (LrgAnimatorComponent *self);
```

Gets the source rectangle for the current frame.

Returns: (transfer full) (nullable) Current frame rectangle, or `NULL`

### Speed Control

```c
gfloat lrg_animator_component_get_speed (LrgAnimatorComponent *self);
```

Gets the playback speed multiplier.

Returns: Speed multiplier (1.0 = normal)

```c
void lrg_animator_component_set_speed (LrgAnimatorComponent *self, gfloat speed);
```

Sets the playback speed multiplier.

Parameters:
- `self` - an `LrgAnimatorComponent`
- `speed` - Speed multiplier (1.0 = normal, 2.0 = double, 0.5 = half)
- Negative values play backwards

### Transition Helpers

```c
void lrg_animator_component_set_default_animation (LrgAnimatorComponent *self,
                                                   const gchar          *name);
```

Sets the default animation to play when current animation finishes. If set, finished non-looping animations will transition to this.

Parameters:
- `self` - an `LrgAnimatorComponent`
- `name` - (nullable) Default animation name, or `NULL` for none

```c
const gchar * lrg_animator_component_get_default_animation (LrgAnimatorComponent *self);
```

Gets the default animation name.

Returns: (nullable) Default animation name, or `NULL`

## Signals

```c
void animation-started (LrgAnimatorComponent *self, const gchar *animation_name)
```

Emitted when an animation starts playing.

```c
void animation-finished (LrgAnimatorComponent *self, const gchar *animation_name)
```

Emitted when a non-looping animation finishes.

```c
void animation-looped (LrgAnimatorComponent *self, const gchar *animation_name)
```

Emitted each time a looping animation loops back to the beginning.

## Properties

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| `texture` | `GrlTexture*` | RW | `NULL` | Spritesheet texture |
| `frame-width` | `gint` | RO | 0 | Frame width in pixels |
| `frame-height` | `gint` | RO | 0 | Frame height in pixels |
| `current-animation` | `gchar*` | RO | `NULL` | Current animation name |
| `playing` | `gboolean` | RO | `FALSE` | Whether playing |
| `current-frame` | `gint` | RO | 0 | Current frame index |
| `speed` | `gfloat` | RW | 1.0 | Playback speed |
| `default-animation` | `gchar*` | RW | `NULL` | Default animation |

## Frame Layout Example

For a 32x32 sprite sheet that is 256 pixels wide (8 frames per row):

```
Frame numbering:
0  1  2  3  4  5  6  7
8  9  10 11 12 13 14 15
16 17 18 19 20 21 22 23

Location calculation for frame N:
col = N % 8
row = N / 8
x = col * 32
y = row * 32
```

## Example: Character State Machine

```c
typedef struct
{
    LrgGameObject *obj;
    LrgAnimatorComponent *animator;
    const gchar *current_state;
} PlayerState;

void
player_update_animation (PlayerState *player, gfloat velocity_x)
{
    if (velocity_x != 0.0f)
    {
        /* Moving */
        if (ABS(velocity_x) > 200.0f)
        {
            player->current_state = "run";
        }
        else
        {
            player->current_state = "walk";
        }
    }
    else
    {
        /* Idle */
        player->current_state = "idle";
    }

    /* Only play if different from current */
    lrg_animator_component_play_if_different (player->animator,
                                              player->current_state);

    /* Face direction */
    if (velocity_x < 0.0f)
    {
        LrgSpriteComponent *sprite =
            lrg_game_object_get_component_of_type (player->obj,
                                                   LrgSpriteComponent,
                                                   LRG_TYPE_SPRITE_COMPONENT);
        lrg_sprite_component_set_flip_h (sprite, TRUE);
    }
}
```

## Related Types

- [LrgComponent](../component.md) - Base component class
- [LrgGameObject](../game-object.md) - Entity container
- [LrgSpriteComponent](sprite-component.md) - Rendering integration
- `GrlTexture` - Texture from graylib
- `GrlRectangle` - Rectangle from graylib

## See Also

- [ECS Overview](../index.md) - Module overview
- [LrgSpriteComponent](sprite-component.md) - Sprite rendering
- [ECS Examples](../../examples/ecs-basics.md) - Comprehensive examples
