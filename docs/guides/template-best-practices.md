# Best Practices for Game Templates

This guide covers best practices for using `LrgGameTemplate` and its subclasses (`LrgGame2DTemplate`, `LrgGame3DTemplate`, and genre-specific templates). These practices are derived from real-world production use.

## Coordinate Spaces in 2D Templates

Understanding coordinate spaces is critical for correct UI and entity positioning.

### The Two Coordinate Spaces

When using `LrgGame2DTemplate` (or any 2D template), there are two distinct coordinate spaces:

| Coordinate Space | Description | Use For |
|------------------|-------------|---------|
| **Virtual Resolution** | The render target size you design at | UI positioning, entity positions, all drawing |
| **Window Size** | The actual window dimensions on screen | Window management only |

**Key insight**: All drawing happens in virtual resolution space. The template scales this to fit the window automatically.

### Common Mistake: Using Window Size for UI

```c
/* WRONG - UI will be mispositioned during window resize */
static void
my_game_draw_ui (LrgGame2DTemplate *template)
{
    gint screen_w, screen_h;

    /* Don't use this for UI positioning! */
    lrg_game_template_get_window_size (LRG_GAME_TEMPLATE (template),
                                        &screen_w, &screen_h);

    /* UI positioned at wrong coordinates */
    draw_button (screen_w - 100, screen_h - 50);
}
```

```c
/* CORRECT - UI always positioned correctly */
static void
my_game_draw_ui (LrgGame2DTemplate *template)
{
    gint screen_w, screen_h;

    /* Use virtual resolution for UI positioning */
    screen_w = lrg_game_2d_template_get_virtual_width (template);
    screen_h = lrg_game_2d_template_get_virtual_height (template);

    /* UI positioned correctly in virtual space */
    draw_button (screen_w - 100, screen_h - 50);
}
```

### When to Use Each

| Function | When to Use |
|----------|-------------|
| `lrg_game_2d_template_get_virtual_width/height()` | **Always** for drawing, UI, entity positions |
| `lrg_game_template_get_window_size()` | Only for window management tasks |
| `lrg_game_template_set_window_size()` | Changing actual window dimensions |

## Asynchronous Window Managers

Modern Linux display servers (Wayland, some X11 compositors) handle window resize requests asynchronously.

### The Race Condition Problem

When you call `lrg_game_template_set_window_size()`:

1. Request is sent to the window manager
2. Window manager processes request (may take several frames)
3. Actual window size eventually matches requested size

During frames 1 and 2, there's a mismatch between what you requested and what the window reports.

| Frame | Requested | Actual (from WM) | Problem |
|-------|-----------|------------------|---------|
| 0     | 1920x1080 | 1280x720         | Request sent |
| 1     | -         | 1280x720         | Still old size |
| 2+    | -         | 1920x1080        | Finally updated |

### How Libregnum Handles This

The template system tracks pending resize requests internally. While a resize is pending:
- Scaling calculations use the **requested** size, not the stale actual size
- This prevents UI "jumping" during the transition

You don't need to do anything special - just use the virtual resolution functions and it works correctly.

### Best Practice: Immediate Visual Feedback

When changing resolution in settings menus:

```c
static void
on_resolution_changed (MyGame *self, gint width, gint height)
{
    LrgGame2DTemplate *template = LRG_GAME_2D_TEMPLATE (self);

    /* Request the new window size */
    lrg_game_template_set_window_size (LRG_GAME_TEMPLATE (template),
                                        width, height);

    /*
     * Don't worry about the race condition - the template handles it.
     * The next frame will render correctly at the new resolution.
     */
}
```

## Virtual Resolution Runtime Changes

Virtual resolution can be changed at runtime for features like:
- Quality settings (render at lower resolution)
- Dynamic resolution scaling
- Different gameplay modes

### Scaling Updates Automatically

```c
/* Change virtual resolution at runtime - scaling updates immediately */
lrg_game_2d_template_set_virtual_resolution (template, 1920, 1080);

/* Individual setters also update scaling */
lrg_game_2d_template_set_virtual_width (template, 640);
lrg_game_2d_template_set_virtual_height (template, 360);
```

### UI Must Adapt to Virtual Resolution Changes

If you store UI positions as absolute values, recalculate them when virtual resolution changes:

```c
static void
my_game_on_resolution_changed (LrgGame2DTemplate *template,
                                gint               width,
                                gint               height)
{
    MyGame *self = MY_GAME (template);

    /*
     * This is called after WINDOW size changes, not virtual resolution.
     * If you're also changing virtual resolution, handle UI layout here.
     */

    recalculate_ui_layout (self);
}

static void
recalculate_ui_layout (MyGame *self)
{
    gint vw = lrg_game_2d_template_get_virtual_width (
        LRG_GAME_2D_TEMPLATE (self));
    gint vh = lrg_game_2d_template_get_virtual_height (
        LRG_GAME_2D_TEMPLATE (self));

    /* Position UI elements relative to virtual resolution */
    self->pause_button_x = vw - 80;
    self->pause_button_y = 20;

    self->score_label_x = vw / 2;
    self->score_label_y = 20;
}
```

## Rendering Pipeline Order

Understanding the rendering order prevents common layering bugs.

### 2D Template Render Order

```
1. Begin render to virtual resolution texture
2. Clear with background color
3. draw_background() - Static backgrounds, parallax (NO camera transform)
4. Begin camera transform
5. draw_world() - Game entities, tilemaps (WITH camera transform)
6. End camera transform
7. draw_ui() - HUD, menus (screen-space, NO camera transform)
8. End render to texture
9. Scale texture to window with letterboxing
```

### What Goes Where

| Layer | Camera Applied | Example Content |
|-------|----------------|-----------------|
| `draw_background()` | No | Parallax layers, skybox |
| `draw_world()` | Yes | Player, enemies, tilemap, projectiles |
| `draw_ui()` | No | Health bar, score, menus, buttons |

### Common Mistake: Drawing UI in World Space

```c
/* WRONG - UI moves with camera */
static void
my_game_draw_world (LrgGame2DTemplate *template)
{
    MyGame *self = MY_GAME (template);

    draw_tilemap (self->tilemap);
    draw_player (self);

    /* Health bar will scroll with the level! */
    draw_health_bar (10, 10);  /* Wrong place! */
}

/* CORRECT - UI stays in screen position */
static void
my_game_draw_ui (LrgGame2DTemplate *template)
{
    MyGame *self = MY_GAME (template);

    /* Health bar stays fixed at top-left */
    draw_health_bar (10, 10);
}
```

## Input Handling Best Practices

### Mouse Position in 2D Games

When handling mouse input, convert to the appropriate coordinate space:

```c
static void
my_game_handle_click (MyGame *self, gfloat mouse_x, gfloat mouse_y)
{
    LrgGame2DTemplate *template = LRG_GAME_2D_TEMPLATE (self);

    /*
     * For UI clicks - convert screen to virtual coordinates.
     * Use this when checking if mouse is over a UI button.
     */
    gfloat ui_x, ui_y;
    lrg_game_2d_template_screen_to_virtual (template,
                                             mouse_x, mouse_y,
                                             &ui_x, &ui_y);

    if (button_contains_point (self->pause_button, ui_x, ui_y))
    {
        pause_game (self);
        return;
    }

    /*
     * For world clicks - convert screen to world coordinates.
     * Use this when selecting entities or placing objects.
     */
    gfloat world_x, world_y;
    lrg_game_2d_template_screen_to_world (template,
                                           mouse_x, mouse_y,
                                           &world_x, &world_y);

    select_entity_at (self, world_x, world_y);
}
```

## Camera Best Practices

### Smooth Camera Follow

```c
static void
my_game_update (LrgGameTemplate *template, gdouble delta)
{
    MyGame *self = MY_GAME (template);

    /* Update player position */
    update_player (self, delta);

    /* Set camera target to follow player */
    lrg_game_2d_template_set_camera_target (LRG_GAME_2D_TEMPLATE (template),
                                             self->player.x,
                                             self->player.y);
}
```

### Camera Bounds for Levels

```c
static void
my_game_load_level (MyGame *self, const gchar *level_path)
{
    LrgGame2DTemplate *template = LRG_GAME_2D_TEMPLATE (self);

    /* Load level data */
    self->level = load_level (level_path);

    /* Constrain camera to level boundaries */
    lrg_game_2d_template_set_camera_bounds (template,
                                             0.0f, 0.0f,
                                             self->level->width,
                                             self->level->height);
}
```

## State Management Pattern

For games with multiple states (menus, gameplay, pause), consider this pattern:

```c
typedef enum {
    GAME_STATE_MENU,
    GAME_STATE_PLAYING,
    GAME_STATE_PAUSED,
    GAME_STATE_GAME_OVER
} GameState;

struct _MyGame
{
    LrgGame2DTemplate parent_instance;

    GameState state;
    /* ... */
};

static void
my_game_update (LrgGameTemplate *template, gdouble delta)
{
    MyGame *self = MY_GAME (template);

    switch (self->state)
    {
    case GAME_STATE_MENU:
        update_menu (self, delta);
        break;

    case GAME_STATE_PLAYING:
        update_gameplay (self, delta);
        break;

    case GAME_STATE_PAUSED:
        /* No updates while paused */
        break;

    case GAME_STATE_GAME_OVER:
        update_game_over (self, delta);
        break;
    }
}

static void
my_game_draw_ui (LrgGame2DTemplate *template)
{
    MyGame *self = MY_GAME (template);
    gint vw, vh;

    vw = lrg_game_2d_template_get_virtual_width (template);
    vh = lrg_game_2d_template_get_virtual_height (template);

    switch (self->state)
    {
    case GAME_STATE_MENU:
        draw_main_menu (self, vw, vh);
        break;

    case GAME_STATE_PLAYING:
        draw_hud (self, vw, vh);
        break;

    case GAME_STATE_PAUSED:
        draw_hud (self, vw, vh);
        draw_pause_overlay (self, vw, vh);
        break;

    case GAME_STATE_GAME_OVER:
        draw_game_over_screen (self, vw, vh);
        break;
    }
}
```

## Memory Patterns for Game Objects

### Using g_autoptr for Temporary Objects

```c
static void
spawn_particle_burst (MyGame *self, gfloat x, gfloat y)
{
    /* g_autoptr handles cleanup automatically */
    g_autoptr(GrlColor) color = grl_color_new (255, 200, 50, 255);

    for (int i = 0; i < 10; i++)
    {
        spawn_particle (self, x, y, color);
    }
    /* color is freed automatically when scope exits */
}
```

### GBoxed vs GObject (Critical!)

graylib uses GBoxed types for lightweight value types. Using the wrong free function causes crashes.

```c
/* CORRECT - GBoxed types use *_free() */
GrlColor *color = grl_color_new (255, 100, 100, 255);
grl_color_free (color);

/* CORRECT - or use g_autoptr */
g_autoptr(GrlColor) color = grl_color_new (255, 100, 100, 255);

/* WRONG - GrlColor is NOT a GObject! */
GrlColor *color = grl_color_new (255, 100, 100, 255);
g_object_unref (color);  /* CRASH! */
```

See `CLAUDE.md` for the complete list of GBoxed vs GObject types.

## Debugging Tips

### Enable Debug Output

```bash
# Enable all libregnum debug messages
G_MESSAGES_DEBUG=Libregnum ./my-game

# Enable specific domain
G_MESSAGES_DEBUG=Libregnum-Template ./my-game
```

### Check Virtual vs Window Size Mismatch

If UI appears in wrong positions, add this debug code:

```c
static void
my_game_draw_ui (LrgGame2DTemplate *template)
{
    gint vw, vh, ww, wh;

    vw = lrg_game_2d_template_get_virtual_width (template);
    vh = lrg_game_2d_template_get_virtual_height (template);
    lrg_game_template_get_window_size (LRG_GAME_TEMPLATE (template), &ww, &wh);

    g_debug ("Virtual: %dx%d, Window: %dx%d", vw, vh, ww, wh);

    /* If these are different, make sure you're using virtual for positioning */
    /* ... */
}
```

## Summary: Key Rules

1. **Use virtual resolution for all drawing and positioning**
2. **Use window size only for window management**
3. **Trust the template to handle async window resize**
4. **Put UI in `draw_ui()`, world entities in `draw_world()`**
5. **Convert mouse coordinates to appropriate space**
6. **Set camera bounds to prevent showing outside the level**
7. **Use GBoxed free functions for graylib value types**

## See Also

- [LrgGame2DTemplate Documentation](../modules/template/templates/game-2d-template.md)
- [LrgGameTemplate Documentation](../modules/template/templates/game-template.md)
- [Coordinate Transformation Functions](../modules/template/templates/game-2d-template.md#coordinate-transformation)
