# Game Feel / Juice System

The template system provides built-in "game feel" effects that make games more satisfying and impactful. These effects are available on all templates through `LrgGameTemplate`.

## Effects Available

| Effect | Purpose |
|--------|---------|
| Hit Stop | Brief freeze on impact |
| Screen Shake | Camera trauma for explosions/hits |
| Time Scale | Slow-motion and speed-up |
| Audio Pitch Variation | Prevent repetitive sounds |

---

## Hit Stop (Freeze Frame)

Hit stop is a brief pause in game time on impact, making attacks feel more powerful. The technique is used extensively in fighting games and action games.

### Basic Usage

```c
static void
on_enemy_hit (MyGame *game, Enemy *enemy, gfloat damage)
{
    LrgGameTemplate *template = LRG_GAME_TEMPLATE (game);

    /* Apply damage */
    enemy_take_damage (enemy, damage);

    /* Freeze for 50ms on hit */
    lrg_game_template_hit_stop (template, 0.05);

    /* Play hit sound */
    lrg_game_template_play_sound (template, "hit_impact");

    /* Add screen shake */
    lrg_game_template_shake (template, 0.3f);
}
```

### Scaling Hit Stop

Different actions benefit from different freeze durations:

```c
static void
apply_hit_effect (MyGame *game, HitType type)
{
    LrgGameTemplate *template = LRG_GAME_TEMPLATE (game);

    gdouble duration;
    gfloat shake_trauma;

    switch (type)
    {
        case HIT_LIGHT:
            duration = 0.02;      /* 20ms - subtle */
            shake_trauma = 0.1f;
            break;

        case HIT_MEDIUM:
            duration = 0.05;      /* 50ms - noticeable */
            shake_trauma = 0.3f;
            break;

        case HIT_HEAVY:
            duration = 0.08;      /* 80ms - impactful */
            shake_trauma = 0.5f;
            break;

        case HIT_CRITICAL:
            duration = 0.12;      /* 120ms - dramatic */
            shake_trauma = 0.8f;
            break;

        case HIT_FINISHING:
            duration = 0.2;       /* 200ms - epic */
            shake_trauma = 1.0f;
            break;
    }

    lrg_game_template_hit_stop (template, duration);
    lrg_game_template_shake (template, shake_trauma);
}
```

### Hit Stop During Update

Hit stop pauses game time but not real time. Your update loop receives `delta = 0` during the freeze:

```c
static void
my_game_update (LrgGameTemplate *template, gdouble delta)
{
    MyGame *self = MY_GAME (template);

    /* During hit stop, delta is 0 - game world freezes */
    update_player (self, delta);
    update_enemies (self, delta);
    update_projectiles (self, delta);

    /* UI and particles can still animate using real time */
    gdouble real_delta = lrg_game_template_get_real_delta (template);
    update_ui_animations (self, real_delta);
    update_hit_flash (self, real_delta);
}
```

---

## Screen Shake

Screen shake uses a "trauma" system where shake intensity is the square of trauma, creating natural falloff. Trauma decays over time.

### Basic Shake

```c
/* Small hit */
lrg_game_template_shake (template, 0.3f);

/* Medium explosion */
lrg_game_template_shake (template, 0.6f);

/* Massive impact */
lrg_game_template_shake (template, 1.0f);
```

### Custom Shake Parameters

```c
/* Custom decay and frequency */
lrg_game_template_shake_with_params (template,
                                      0.5f,   /* trauma */
                                      0.8f,   /* decay per second */
                                      30.0f); /* frequency in Hz */

/* Slow, heavy shake */
lrg_game_template_shake_with_params (template, 0.6f, 0.3f, 8.0f);

/* Fast, jittery shake */
lrg_game_template_shake_with_params (template, 0.4f, 1.5f, 60.0f);
```

### Trauma Values Guide

| Trauma | Use Case |
|--------|----------|
| 0.1 - 0.2 | Footsteps, small bumps |
| 0.2 - 0.4 | Taking damage, small explosions |
| 0.4 - 0.6 | Heavy attacks, medium explosions |
| 0.6 - 0.8 | Boss attacks, large explosions |
| 0.8 - 1.0 | Massive impacts, screen-filling explosions |

### Applying Shake to Camera

The template tracks shake offset internally. Apply it to your camera:

```c
static void
my_game_draw (LrgGameTemplate *template)
{
    MyGame *self = MY_GAME (template);

    /* Get current shake offset */
    gfloat shake_x, shake_y;
    lrg_game_template_get_shake_offset (template, &shake_x, &shake_y);

    /* Apply to 2D camera */
    LrgCamera2D *camera = lrg_game_2d_template_get_camera (
        LRG_GAME_2D_TEMPLATE (template));

    gfloat cam_x = self->camera_target_x + shake_x;
    gfloat cam_y = self->camera_target_y + shake_y;
    lrg_camera_2d_set_target (camera, cam_x, cam_y);

    /* Begin drawing */
    lrg_camera_2d_begin (camera);
    draw_world (self);
    lrg_camera_2d_end (camera);
}
```

### Additive Trauma

Trauma is additive - multiple hits stack:

```c
static void
on_multi_hit (MyGame *game, guint hit_count)
{
    LrgGameTemplate *template = LRG_GAME_TEMPLATE (game);

    /* Each hit adds trauma (capped at 1.0 internally) */
    for (guint i = 0; i < hit_count; i++)
    {
        lrg_game_template_shake (template, 0.2f);
    }
    /* 5 hits = 1.0 trauma (capped), not 1.0 per hit */
}
```

---

## Time Scale

Time scale affects game delta time, enabling slow-motion and speed-up effects.

### Basic Usage

```c
/* Slow motion (50% speed) */
lrg_game_template_set_time_scale (template, 0.5);

/* Super slow-mo for dramatic moments */
lrg_game_template_set_time_scale (template, 0.1);

/* Normal speed */
lrg_game_template_set_time_scale (template, 1.0);

/* Fast forward */
lrg_game_template_set_time_scale (template, 2.0);
```

### Bullet Time Effect

```c
static void
activate_bullet_time (MyGame *game)
{
    LrgGameTemplate *template = LRG_GAME_TEMPLATE (game);

    /* Enable slow motion */
    lrg_game_template_set_time_scale (template, 0.2);

    /* Schedule return to normal after 2 real seconds */
    lrg_game_template_set_timer (template, 2.0,
                                  deactivate_bullet_time_cb, game);
}

static void
deactivate_bullet_time_cb (gpointer user_data)
{
    MyGame *game = user_data;
    LrgGameTemplate *template = LRG_GAME_TEMPLATE (game);

    /* Smoothly return to normal */
    lrg_game_template_set_time_scale (template, 1.0);
}
```

### Time Scale with Hit Stop

Combine for maximum impact:

```c
static void
on_finisher_move (MyGame *game)
{
    LrgGameTemplate *template = LRG_GAME_TEMPLATE (game);

    /* Slow everything down */
    lrg_game_template_set_time_scale (template, 0.3);

    /* Freeze on impact */
    lrg_game_template_hit_stop (template, 0.15);

    /* Big shake */
    lrg_game_template_shake (template, 1.0f);

    /* Return to normal after 1 real second */
    lrg_game_template_set_timer (template, 1.0,
                                  restore_time_scale_cb, game);
}
```

### Query Time Scale

```c
gdouble scale = lrg_game_template_get_time_scale (template);

if (scale < 1.0)
{
    /* We're in slow motion - maybe show visual effect */
    draw_slow_motion_overlay ();
}
```

---

## Audio Pitch Variation

Prevent sounds from becoming repetitive by adding slight pitch variation.

### Play with Random Pitch

```c
/* Play sound with random pitch variation */
lrg_game_template_play_sound_pitched (template, "footstep", 0.9f, 1.1f);

/* Play with fixed pitch */
lrg_game_template_play_sound_pitch (template, "explosion", 0.8f);
```

### Pitch for Intensity

```c
static void
play_hit_sound (MyGame *game, gfloat damage)
{
    LrgGameTemplate *template = LRG_GAME_TEMPLATE (game);

    /* Higher damage = lower pitch for heavier sound */
    gfloat pitch = 1.2f - (damage / 100.0f * 0.4f);  /* 1.2 to 0.8 */
    pitch = CLAMP (pitch, 0.8f, 1.2f);

    lrg_game_template_play_sound_pitch (template, "hit", pitch);
}
```

---

## Combining Effects

### Combat Impact

```c
static void
apply_combat_impact (MyGame       *game,
                     CombatResult *result)
{
    LrgGameTemplate *template = LRG_GAME_TEMPLATE (game);

    /* Calculate intensity from damage */
    gfloat intensity = CLAMP (result->damage / 50.0f, 0.1f, 1.0f);

    /* Hit stop - longer for criticals */
    gdouble freeze = result->is_critical ? 0.1 : 0.05;
    lrg_game_template_hit_stop (template, freeze);

    /* Screen shake proportional to damage */
    lrg_game_template_shake (template, intensity * 0.6f);

    /* Sound with pitch based on attack type */
    gfloat pitch = result->is_critical ? 0.8f : 1.0f;
    lrg_game_template_play_sound_pitch (template, "hit_flesh", pitch);

    /* Brief slow-mo for critical hits */
    if (result->is_critical)
    {
        lrg_game_template_set_time_scale (template, 0.5);
        lrg_game_template_set_timer (template, 0.3,
                                      restore_time_scale_cb, game);
    }
}
```

### Death Sequence

```c
static void
on_player_death (MyGame *game)
{
    LrgGameTemplate *template = LRG_GAME_TEMPLATE (game);

    /* Dramatic slow-motion */
    lrg_game_template_set_time_scale (template, 0.2);

    /* Heavy shake */
    lrg_game_template_shake (template, 0.8f);

    /* Long freeze */
    lrg_game_template_hit_stop (template, 0.3);

    /* Deep sound */
    lrg_game_template_play_sound_pitch (template, "death", 0.6f);

    /* Gradually speed up over 2 seconds */
    animate_time_scale_to (game, 1.0, 2.0);
}
```

### Boss Entry

```c
static void
on_boss_appear (MyGame *game, Boss *boss)
{
    LrgGameTemplate *template = LRG_GAME_TEMPLATE (game);

    /* Ground pound effect */
    lrg_game_template_shake_with_params (template,
                                          1.0f,  /* max trauma */
                                          0.5f,  /* slow decay */
                                          15.0f);/* low frequency */

    /* Brief pause for dramatic effect */
    lrg_game_template_hit_stop (template, 0.2);

    /* Deep rumble sound */
    lrg_game_template_play_sound_pitch (template, "boss_roar", 0.7f);
}
```

---

## Best Practices

1. **Less is more**: Overusing effects makes them lose impact. Save dramatic effects for significant moments.

2. **Stack effects**: Combine hit stop, shake, and audio for cohesive feedback.

3. **Match intensity**: Effect intensity should match the in-game action's importance.

4. **Consider accessibility**: Provide options to reduce or disable screen shake.

5. **Test on devices**: Effects that feel good on desktop may be too intense on mobile.

## Accessibility

```c
/* Respect user preferences */
LrgSettings *settings = lrg_engine_get_settings (engine);
gfloat shake_intensity = lrg_settings_get_float (settings, "shake_intensity");

if (shake_intensity > 0)
{
    gfloat adjusted_trauma = trauma * shake_intensity;
    lrg_game_template_shake (template, adjusted_trauma);
}
```

## Related Documentation

- [LrgGameTemplate](../templates/game-template.md) - Base template with juice methods
- [Audio System](../../audio/index.md) - Sound playback
- [Camera System](../../graphics/camera.md) - Camera shake application
