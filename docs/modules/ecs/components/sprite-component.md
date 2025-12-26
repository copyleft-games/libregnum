# LrgSpriteComponent

Sprite rendering component.

## Overview

`LrgSpriteComponent` renders a texture at the game object's position. It supports sprite sheets (via source rectangle), tinting, and flipping.

The component handles:
- Texture rendering at entity position
- Source rectangle for sprite sheet animation frames
- Color tinting for visual effects
- Horizontal and vertical flipping
- Integration with entity transform system

## Basic Usage

### Creating Sprites

```c
#include <libregnum.h>

/* Create empty sprite */
g_autoptr(LrgSpriteComponent) sprite = lrg_sprite_component_new ();

/* Create with texture */
GrlTexture *texture = grl_texture_new_from_file ("assets/player.png", NULL);
g_autoptr(LrgSpriteComponent) sprite_with_texture =
    lrg_sprite_component_new_with_texture (texture);

/* Add to game object */
g_autoptr(LrgGameObject) player = lrg_game_object_new ();
lrg_game_object_add_component (player, LRG_COMPONENT (sprite));
```

### Sprite Sheets

```c
g_autoptr(LrgSpriteComponent) sprite = lrg_sprite_component_new ();
GrlTexture *texture = grl_texture_new_from_file ("assets/spritesheet.png", NULL);

lrg_sprite_component_set_texture (sprite, texture);

/* Set source rectangle for specific frame (32x32 at position 64, 0) */
lrg_sprite_component_set_source (sprite, 64, 0, 32, 32);

/* Frame numbering (for 32x32 frames in 256-wide sheet):
   Frame 0: (0, 0)
   Frame 1: (32, 0)
   Frame 2: (64, 0)
   Frame 3: (96, 0)
   Frame 4: (0, 32)
   ... etc
*/

/* Calculate source rect for frame N */
gint frame_width = 32;
gint frame_height = 32;
gint sheet_width = 256;  /* pixels */
gint frame_n = 5;

gint cols = sheet_width / frame_width;
gint src_x = (frame_n % cols) * frame_width;
gint src_y = (frame_n / cols) * frame_height;

lrg_sprite_component_set_source (sprite, src_x, src_y, frame_width, frame_height);
```

### Tinting and Flipping

```c
g_autoptr(LrgSpriteComponent) sprite = lrg_sprite_component_new ();

/* Set color tint (white = no tint) */
g_autoptr(GrlColor) red = grl_color_new (255, 0, 0, 255);
lrg_sprite_component_set_tint (sprite, red);

/* Get current tint */
g_autoptr(GrlColor) tint = lrg_sprite_component_get_tint (sprite);

/* Flip horizontally (facing left instead of right) */
lrg_sprite_component_set_flip_h (sprite, TRUE);

/* Flip vertically */
lrg_sprite_component_set_flip_v (sprite, TRUE);

/* Check flip state */
gboolean flipped_h = lrg_sprite_component_get_flip_h (sprite);
```

## API Reference

### Construction

```c
LrgSpriteComponent * lrg_sprite_component_new (void);
```

Creates a new sprite component with no texture.

Returns: (transfer full) A new `LrgSpriteComponent`

```c
LrgSpriteComponent * lrg_sprite_component_new_with_texture (GrlTexture *texture);
```

Creates a new sprite component with the specified texture.

Parameters:
- `texture` - (nullable) initial texture

Returns: (transfer full) A new `LrgSpriteComponent`

### Texture Management

```c
void lrg_sprite_component_set_texture (LrgSpriteComponent *self, GrlTexture *texture);
```

Sets the texture to render.

Parameters:
- `self` - an `LrgSpriteComponent`
- `texture` - (nullable) the texture to use

```c
GrlTexture * lrg_sprite_component_get_texture (LrgSpriteComponent *self);
```

Gets the current texture.

Returns: (transfer none) (nullable) The texture, or `NULL`

### Source Rectangle (for Sprite Sheets)

```c
void lrg_sprite_component_set_source (LrgSpriteComponent *self,
                                      gint                x,
                                      gint                y,
                                      gint                width,
                                      gint                height);
```

Sets the source rectangle for sprite sheet rendering. If not set, the entire texture is rendered.

Parameters:
- `self` - an `LrgSpriteComponent`
- `x` - source X coordinate in pixels
- `y` - source Y coordinate in pixels
- `width` - source width in pixels
- `height` - source height in pixels

```c
GrlRectangle * lrg_sprite_component_get_source (LrgSpriteComponent *self);
```

Gets the source rectangle.

Returns: (transfer full) (nullable) The source rectangle, or `NULL` if using full texture

```c
void lrg_sprite_component_clear_source (LrgSpriteComponent *self);
```

Clears the source rectangle, causing the full texture to be rendered.

### Tint Color

```c
void lrg_sprite_component_set_tint (LrgSpriteComponent *self, GrlColor *color);
```

Sets the tint color applied to the texture. White (255, 255, 255, 255) means no tinting.

Parameters:
- `self` - an `LrgSpriteComponent`
- `color` - the tint color

```c
GrlColor * lrg_sprite_component_get_tint (LrgSpriteComponent *self);
```

Gets the current tint color.

Returns: (transfer full) The tint color

### Flip

```c
void lrg_sprite_component_set_flip_h (LrgSpriteComponent *self, gboolean flip);
```

Sets whether the sprite is flipped horizontally.

```c
gboolean lrg_sprite_component_get_flip_h (LrgSpriteComponent *self);
```

Gets whether the sprite is flipped horizontally.

Returns: `TRUE` if flipped horizontally

```c
void lrg_sprite_component_set_flip_v (LrgSpriteComponent *self, gboolean flip);
```

Sets whether the sprite is flipped vertically.

```c
gboolean lrg_sprite_component_get_flip_v (LrgSpriteComponent *self);
```

Gets whether the sprite is flipped vertically.

Returns: `TRUE` if flipped vertically

### Drawing

```c
void lrg_sprite_component_draw (LrgSpriteComponent *self);
```

Draws the sprite at the owning game object's position. This is typically called by the game object during its draw phase. The component must be attached to a game object for this to work.

## Properties

| Property | Type | Access | Default | Description |
|----------|------|--------|---------|-------------|
| `texture` | `GrlTexture*` | RW | `NULL` | Texture to render |
| `source` | `GrlRectangle*` | RW | `NULL` | Source rectangle for sprite sheets |
| `tint` | `GrlColor*` | RW | White | Color tint |
| `flip-h` | `gboolean` | RW | `FALSE` | Flip horizontally |
| `flip-v` | `gboolean` | RW | `FALSE` | Flip vertically |

## Color Tinting

Tinting multiplies the texture by a color. Common uses:

```c
/* Damage flash: briefly tint red */
g_autoptr(GrlColor) red = grl_color_new (255, 100, 100, 255);
lrg_sprite_component_set_tint (sprite, red);

/* Fade out: reduce alpha */
g_autoptr(GrlColor) fade = grl_color_new (255, 255, 255, 128);
lrg_sprite_component_set_tint (sprite, fade);

/* Darkening: reduce all channels */
g_autoptr(GrlColor) dark = grl_color_new (100, 100, 100, 255);
lrg_sprite_component_set_tint (sprite, dark);

/* Reset to white (no tint) */
g_autoptr(GrlColor) white = grl_color_new (255, 255, 255, 255);
lrg_sprite_component_set_tint (sprite, white);
```

## Sprite Sheet Layout

Frames are ordered left-to-right, top-to-bottom:

```
Frame Layout (32x32 frames, 256 width):

Frame 0  Frame 1  Frame 2  Frame 3
(0,0)    (32,0)   (64,0)   (96,0)

Frame 4  Frame 5  Frame 6  Frame 7
(0,32)   (32,32)  (64,32)  (96,32)
```

Helper to convert frame number to source coordinates:

```c
void
frame_to_source (gint frame_n,
                 gint sheet_width,
                 gint frame_width,
                 gint frame_height,
                 gint *out_x,
                 gint *out_y)
{
    gint cols = sheet_width / frame_width;
    *out_x = (frame_n % cols) * frame_width;
    *out_y = (frame_n / cols) * frame_height;
}
```

## Example: Animated Character

```c
/* Create game object */
g_autoptr(LrgGameObject) player = lrg_game_object_new_at (100.0f, 150.0f);

/* Create sprite */
g_autoptr(LrgSpriteComponent) sprite = lrg_sprite_component_new ();
GrlTexture *texture = grl_texture_new_from_file ("assets/player.png", NULL);
lrg_sprite_component_set_texture (sprite, texture);

/* Set up idle animation frame (frame 0 of 32x32 sheet) */
lrg_sprite_component_set_source (sprite, 0, 0, 32, 32);

/* Add to object */
lrg_game_object_add_component (player, LRG_COMPONENT (sprite));

/* Create animator to cycle frames */
g_autoptr(LrgAnimatorComponent) animator = lrg_animator_component_new ();
lrg_animator_component_set_texture (animator, texture, 32, 32);
lrg_animator_component_add_animation (animator, "idle", 0, 4, 10.0f, TRUE);
lrg_game_object_add_component (player, LRG_COMPONENT (animator));

/* Start animation */
lrg_animator_component_play (animator, "idle");

/* In game loop:
   - Animator updates sprite's source rectangle
   - Sprite renders the current frame
*/
```

## Related Types

- [LrgComponent](../component.md) - Base component class
- [LrgGameObject](../game-object.md) - Entity container
- [LrgAnimatorComponent](animator-component.md) - Animation playback
- `GrlTexture` - Texture from graylib
- `GrlColor` - Color from graylib
- `GrlRectangle` - Rectangle from graylib

## See Also

- [ECS Overview](../index.md) - Module overview
- [LrgAnimatorComponent](animator-component.md) - Animation integration
- [ECS Examples](../../examples/ecs-basics.md) - Comprehensive examples
