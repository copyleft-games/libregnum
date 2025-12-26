# Shapes System

The shapes system provides a set of drawable primitive shapes that wrap graylib's drawing functions. All shapes implement the `LrgDrawable` interface and can be rendered directly.

## Architecture

```
LrgShape (abstract, implements LrgDrawable)
    |
    +-- LrgShape2D (abstract, screen-space)
    |     +-- LrgText2D (final)
    |
    +-- LrgShape3D (abstract, world-space)
          +-- LrgSphere3D (final)
          +-- LrgCube3D (final)
          +-- LrgLine3D (final)
```

## Base Classes

### LrgShape

Abstract base class for all shapes. Implements the `LrgDrawable` interface.

**Properties:**
- `visible` (gboolean) - Whether the shape is rendered
- `color` (GrlColor*) - The shape's color
- `z-index` (gint) - Draw order for 2D layering

**Methods:**
```c
gboolean lrg_shape_get_visible    (LrgShape *self);
void     lrg_shape_set_visible    (LrgShape *self, gboolean visible);
GrlColor *lrg_shape_get_color     (LrgShape *self);
void     lrg_shape_set_color      (LrgShape *self, GrlColor *color);
gint     lrg_shape_get_z_index    (LrgShape *self);
void     lrg_shape_set_z_index    (LrgShape *self, gint z_index);
```

### LrgShape2D

Abstract base class for 2D shapes (screen-space coordinates).

**Properties:**
- `x` (gfloat) - Screen X position
- `y` (gfloat) - Screen Y position

**Methods:**
```c
gfloat lrg_shape2d_get_x        (LrgShape2D *self);
void   lrg_shape2d_set_x        (LrgShape2D *self, gfloat x);
gfloat lrg_shape2d_get_y        (LrgShape2D *self);
void   lrg_shape2d_set_y        (LrgShape2D *self, gfloat y);
void   lrg_shape2d_set_position (LrgShape2D *self, gfloat x, gfloat y);
void   lrg_shape2d_get_position (LrgShape2D *self, gfloat *x, gfloat *y);
```

### LrgShape3D

Abstract base class for 3D shapes (world-space coordinates).

**Properties:**
- `position` (GrlVector3*) - World position
- `wireframe` (gboolean) - Whether to render in wireframe mode

**Methods:**
```c
GrlVector3 *lrg_shape3d_get_position     (LrgShape3D *self);
void        lrg_shape3d_set_position     (LrgShape3D *self, GrlVector3 *pos);
void        lrg_shape3d_set_position_xyz (LrgShape3D *self, gfloat x, gfloat y, gfloat z);
gfloat      lrg_shape3d_get_x            (LrgShape3D *self);
gfloat      lrg_shape3d_get_y            (LrgShape3D *self);
gfloat      lrg_shape3d_get_z            (LrgShape3D *self);
gboolean    lrg_shape3d_get_wireframe    (LrgShape3D *self);
void        lrg_shape3d_set_wireframe    (LrgShape3D *self, gboolean wireframe);
```

## 3D Shapes

### LrgSphere3D

A 3D sphere shape.

**Properties:**
- `radius` (gfloat) - Sphere radius
- `rings` (gint) - Horizontal rings for tessellation (1-256, default 16)
- `slices` (gint) - Vertical slices for tessellation (2-256, default 16)

**Construction:**
```c
LrgSphere3D *lrg_sphere3d_new      (void);
LrgSphere3D *lrg_sphere3d_new_at   (gfloat x, gfloat y, gfloat z, gfloat radius);
LrgSphere3D *lrg_sphere3d_new_full (gfloat x, gfloat y, gfloat z, gfloat radius, GrlColor *color);
```

**Example:**
```c
g_autoptr(LrgSphere3D) sphere = lrg_sphere3d_new_full (0.0f, 1.0f, 0.0f, 0.5f, grl_color_red ());
lrg_drawable_draw (LRG_DRAWABLE (sphere), delta);
```

### LrgCube3D

A 3D cube/box shape with variable dimensions.

**Properties:**
- `width` (gfloat) - Width (X axis)
- `height` (gfloat) - Height (Y axis)
- `depth` (gfloat) - Depth (Z axis)

**Construction:**
```c
LrgCube3D *lrg_cube3d_new      (void);
LrgCube3D *lrg_cube3d_new_at   (gfloat x, gfloat y, gfloat z, gfloat w, gfloat h, gfloat d);
LrgCube3D *lrg_cube3d_new_full (gfloat x, gfloat y, gfloat z, gfloat w, gfloat h, gfloat d, GrlColor *color);
```

**Convenience Methods:**
```c
void lrg_cube3d_set_size         (LrgCube3D *self, gfloat w, gfloat h, gfloat d);
void lrg_cube3d_set_uniform_size (LrgCube3D *self, gfloat size);  /* True cube */
```

**Example:**
```c
g_autoptr(LrgCube3D) cube = lrg_cube3d_new_at (0.0f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f);
lrg_shape3d_set_wireframe (LRG_SHAPE3D (cube), TRUE);
lrg_drawable_draw (LRG_DRAWABLE (cube), delta);
```

### LrgLine3D

A 3D line segment from start to end point.

**Properties:**
- `end` (GrlVector3*) - End position (start is the inherited `position` property)

**Construction:**
```c
LrgLine3D *lrg_line3d_new           (void);
LrgLine3D *lrg_line3d_new_from_to   (gfloat sx, gfloat sy, gfloat sz, gfloat ex, gfloat ey, gfloat ez);
LrgLine3D *lrg_line3d_new_from_to_v (GrlVector3 *start, GrlVector3 *end);
LrgLine3D *lrg_line3d_new_full      (gfloat sx, gfloat sy, gfloat sz, gfloat ex, gfloat ey, gfloat ez, GrlColor *color);
```

**End Point Accessors:**
```c
GrlVector3 *lrg_line3d_get_end     (LrgLine3D *self);
void        lrg_line3d_set_end     (LrgLine3D *self, GrlVector3 *end);
void        lrg_line3d_set_end_xyz (LrgLine3D *self, gfloat x, gfloat y, gfloat z);
gfloat      lrg_line3d_get_end_x   (LrgLine3D *self);
gfloat      lrg_line3d_get_end_y   (LrgLine3D *self);
gfloat      lrg_line3d_get_end_z   (LrgLine3D *self);
void        lrg_line3d_set_points  (LrgLine3D *self, gfloat sx, gfloat sy, gfloat sz, gfloat ex, gfloat ey, gfloat ez);
```

**Example:**
```c
/* Draw a direction indicator */
g_autoptr(LrgLine3D) direction = lrg_line3d_new_full (
    player_x, player_y, player_z,
    player_x + dir_x, player_y, player_z + dir_z,
    grl_color_green ()
);
lrg_drawable_draw (LRG_DRAWABLE (direction), delta);
```

## 2D Shapes

### LrgText2D

A 2D text label.

**Properties:**
- `text` (gchar*) - The text string to display
- `font-size` (gfloat) - Font size in pixels (default 20)
- `spacing` (gfloat) - Character spacing (default 1)
- `font` (GrlFont*, nullable) - Custom font, or NULL for default

**Construction:**
```c
LrgText2D *lrg_text2d_new           (void);
LrgText2D *lrg_text2d_new_with_text (const gchar *text);
LrgText2D *lrg_text2d_new_at        (gfloat x, gfloat y, const gchar *text);
LrgText2D *lrg_text2d_new_full      (gfloat x, gfloat y, const gchar *text, gfloat size, GrlColor *color);
```

**Example:**
```c
g_autoptr(LrgText2D) fps_text = lrg_text2d_new_full (10.0f, 10.0f, "FPS: 60", 20.0f, grl_color_white ());
lrg_drawable_draw (LRG_DRAWABLE (fps_text), delta);

/* Update text dynamically */
g_autofree gchar *new_text = g_strdup_printf ("FPS: %d", current_fps);
lrg_text2d_set_text (fps_text, new_text);
```

## Usage with LrgDrawable

All shapes implement `LrgDrawable`, so they can be drawn using the standard interface:

```c
void
draw_scene (gfloat delta)
{
    g_autoptr(LrgSphere3D) player = lrg_sphere3d_new_full (
        player_x, player_y, player_z, 0.4f, player_color
    );

    g_autoptr(LrgLine3D) direction = lrg_line3d_new_full (
        player_x, player_y, player_z,
        player_x + dir_x * 0.6f, player_y, player_z + dir_z * 0.6f,
        grl_color_green ()
    );

    g_autoptr(LrgText2D) score = lrg_text2d_new_full (
        10.0f, 10.0f, "Score: 0", 20.0f, grl_color_white ()
    );

    /* Draw all shapes */
    lrg_drawable_draw (LRG_DRAWABLE (player), delta);
    lrg_drawable_draw (LRG_DRAWABLE (direction), delta);
    lrg_drawable_draw (LRG_DRAWABLE (score), delta);
}
```

## Creating Custom Shapes

To create a custom shape, subclass `LrgShape2D` or `LrgShape3D` and implement the `draw` virtual method:

```c
struct _MyCustomShape
{
    LrgShape3D parent_instance;
    /* custom properties */
};

G_DEFINE_FINAL_TYPE (MyCustomShape, my_custom_shape, LRG_TYPE_SHAPE3D)

static void
my_custom_shape_draw (LrgShape *shape, gfloat delta)
{
    MyCustomShape *self = MY_CUSTOM_SHAPE (shape);
    GrlVector3 *pos = lrg_shape3d_get_position (LRG_SHAPE3D (self));
    GrlColor *color = lrg_shape_get_color (shape);

    /* Custom drawing code using graylib functions */
    grl_draw_my_primitive (pos, color);
}

static void
my_custom_shape_class_init (MyCustomShapeClass *klass)
{
    LrgShapeClass *shape_class = LRG_SHAPE_CLASS (klass);
    shape_class->draw = my_custom_shape_draw;
}
```

## Best Practices

1. **Use autoptr for automatic cleanup:**
   ```c
   g_autoptr(LrgSphere3D) sphere = lrg_sphere3d_new ();
   ```

2. **Reuse shapes when possible** - Creating shapes has overhead. For static geometry, create once and reuse.

3. **Use wireframe mode for debugging:**
   ```c
   lrg_shape3d_set_wireframe (LRG_SHAPE3D (shape), TRUE);
   ```

4. **z-index for 2D layering** - Higher z-index renders on top:
   ```c
   lrg_shape_set_z_index (LRG_SHAPE (background), 0);
   lrg_shape_set_z_index (LRG_SHAPE (foreground), 10);
   ```

5. **visibility toggle** - Hide shapes without destroying them:
   ```c
   lrg_shape_set_visible (LRG_SHAPE (shape), FALSE);
   ```
