# LrgParticleSystem

`LrgParticleSystem` is the main coordinator for particle effects. It manages emitters, forces, particle pools, and rendering.

## Type

```c
#define LRG_TYPE_PARTICLE_SYSTEM (lrg_particle_system_get_type ())
G_DECLARE_DERIVABLE_TYPE (LrgParticleSystem, lrg_particle_system, LRG, PARTICLE_SYSTEM, GObject)
```

This is a derivable type, allowing subclasses to override behavior.

## Functions

### Creation

```c
LrgParticleSystem *lrg_particle_system_new (guint max_particles);
```

Creates a particle system with the specified maximum capacity. The internal pool is sized to this limit.

### Update Loop

```c
void lrg_particle_system_update (LrgParticleSystem *self, gfloat delta_time);
void lrg_particle_system_draw (LrgParticleSystem *self);
```

Call `update` each frame to advance particle simulation, then `draw` to render.

### Emitter Management

```c
void lrg_particle_system_add_emitter (LrgParticleSystem *self, LrgParticleEmitter *emitter);
void lrg_particle_system_remove_emitter (LrgParticleSystem *self, LrgParticleEmitter *emitter);
GList *lrg_particle_system_get_emitters (LrgParticleSystem *self);
void lrg_particle_system_clear_emitters (LrgParticleSystem *self);
```

### Force Management

```c
void lrg_particle_system_add_force (LrgParticleSystem *self, LrgParticleForce *force);
void lrg_particle_system_remove_force (LrgParticleSystem *self, LrgParticleForce *force);
GList *lrg_particle_system_get_forces (LrgParticleSystem *self);
void lrg_particle_system_clear_forces (LrgParticleSystem *self);
```

### Manual Emission

```c
guint lrg_particle_system_emit (LrgParticleSystem *self, guint count);
guint lrg_particle_system_emit_at (LrgParticleSystem *self, gfloat x, gfloat y, gfloat z, guint count);
void lrg_particle_system_clear (LrgParticleSystem *self);
```

Emit particles on demand or clear all active particles.

### Playback Control

```c
void lrg_particle_system_play (LrgParticleSystem *self);
void lrg_particle_system_pause (LrgParticleSystem *self);
void lrg_particle_system_stop (LrgParticleSystem *self);
gboolean lrg_particle_system_is_playing (LrgParticleSystem *self);
gboolean lrg_particle_system_is_alive (LrgParticleSystem *self);
```

### Properties

```c
guint lrg_particle_system_get_active_count (LrgParticleSystem *self);
guint lrg_particle_system_get_max_particles (LrgParticleSystem *self);

void lrg_particle_system_get_position (LrgParticleSystem *self, gfloat *x, gfloat *y, gfloat *z);
void lrg_particle_system_set_position (LrgParticleSystem *self, gfloat x, gfloat y, gfloat z);

gboolean lrg_particle_system_get_loop (LrgParticleSystem *self);
void lrg_particle_system_set_loop (LrgParticleSystem *self, gboolean loop);

gfloat lrg_particle_system_get_duration (LrgParticleSystem *self);
void lrg_particle_system_set_duration (LrgParticleSystem *self, gfloat duration);

gfloat lrg_particle_system_get_time_scale (LrgParticleSystem *self);
void lrg_particle_system_set_time_scale (LrgParticleSystem *self, gfloat scale);
```

## Render Modes

```c
typedef enum {
    LRG_PARTICLE_RENDER_BILLBOARD,          /* Camera-facing quad */
    LRG_PARTICLE_RENDER_STRETCHED_BILLBOARD, /* Stretched along velocity */
    LRG_PARTICLE_RENDER_TRAIL,              /* Trail/ribbon rendering */
} LrgParticleRenderMode;

LrgParticleRenderMode lrg_particle_system_get_render_mode (LrgParticleSystem *self);
void lrg_particle_system_set_render_mode (LrgParticleSystem *self, LrgParticleRenderMode mode);
```

## Blend Modes

```c
typedef enum {
    LRG_PARTICLE_BLEND_ADDITIVE,  /* Fire, glow effects */
    LRG_PARTICLE_BLEND_ALPHA,     /* Standard transparency */
    LRG_PARTICLE_BLEND_MULTIPLY,  /* Shadows, darkening */
} LrgParticleBlendMode;

LrgParticleBlendMode lrg_particle_system_get_blend_mode (LrgParticleSystem *self);
void lrg_particle_system_set_blend_mode (LrgParticleSystem *self, LrgParticleBlendMode mode);
```

## Usage Examples

### Basic Fire Effect

```c
/* Create system */
g_autoptr(LrgParticleSystem) fire = lrg_particle_system_new (500);
lrg_particle_system_set_position (fire, 400.0f, 500.0f, 0.0f);
lrg_particle_system_set_blend_mode (fire, LRG_PARTICLE_BLEND_ADDITIVE);

/* Add emitter */
g_autoptr(LrgParticleEmitter) emitter = lrg_particle_emitter_new ();
lrg_particle_emitter_set_rate (emitter, 100.0f);
lrg_particle_emitter_set_lifetime (emitter, 1.5f);
lrg_particle_emitter_set_initial_velocity (emitter, 0.0f, -80.0f, 0.0f);
lrg_particle_emitter_set_start_color (emitter, 255, 200, 50, 255);
lrg_particle_emitter_set_end_color (emitter, 255, 50, 0, 0);
lrg_particle_system_add_emitter (fire, emitter);

/* Add forces */
lrg_particle_system_add_force (fire, lrg_particle_force_wind_new (15.0f, 0.0f, 0.0f));
lrg_particle_system_add_force (fire, lrg_particle_force_turbulence_new (20.0f));

/* Start */
lrg_particle_system_play (fire);
```

### One-Shot Explosion

```c
g_autoptr(LrgParticleSystem) explosion = lrg_particle_system_new (200);
lrg_particle_system_set_loop (explosion, FALSE);
lrg_particle_system_set_blend_mode (explosion, LRG_PARTICLE_BLEND_ADDITIVE);

/* Configure emitter for burst */
g_autoptr(LrgParticleEmitter) burst = lrg_particle_emitter_new ();
lrg_particle_emitter_set_shape (burst, LRG_EMITTER_SHAPE_SPHERE);
lrg_particle_emitter_set_velocity_variance (burst, 300.0f);
lrg_particle_emitter_set_lifetime (burst, 0.8f);
lrg_particle_system_add_emitter (explosion, burst);

/* Add gravity */
lrg_particle_system_add_force (explosion, lrg_particle_force_gravity_new (0.0f, 200.0f, 0.0f));

/* Trigger at position */
lrg_particle_system_set_position (explosion, hit_x, hit_y, 0.0f);
lrg_particle_emitter_burst (burst, 150);
lrg_particle_system_play (explosion);
```

### Game Loop Integration

```c
/* In game update */
void
game_update (gfloat delta_time)
{
    /* Update all particle systems */
    lrg_particle_system_update (fire_system, delta_time);
    lrg_particle_system_update (smoke_system, delta_time);
    lrg_particle_system_update (sparkles_system, delta_time);

    /* Remove finished one-shot effects */
    if (!lrg_particle_system_is_alive (explosion))
    {
        g_clear_object (&explosion);
    }
}

/* In game render */
void
game_render (void)
{
    /* Draw particles (usually after scene, before UI) */
    lrg_particle_system_draw (fire_system);
    lrg_particle_system_draw (smoke_system);
    lrg_particle_system_draw (sparkles_system);
}
```

### Slow Motion Effect

```c
/* Normal speed */
lrg_particle_system_set_time_scale (system, 1.0f);

/* Slow motion (50% speed) */
lrg_particle_system_set_time_scale (system, 0.5f);

/* Fast forward (2x speed) */
lrg_particle_system_set_time_scale (system, 2.0f);
```

## Subclassing

Override virtual methods for custom behavior:

```c
struct _MyParticleSystem
{
    LrgParticleSystem parent;
    /* Custom fields */
};

static void
my_particle_system_on_particle_spawn (LrgParticleSystem *system,
                                       LrgParticle *particle)
{
    /* Custom spawn logic (e.g., play sound, trigger sub-effect) */
}

static void
my_particle_system_on_particle_death (LrgParticleSystem *system,
                                       LrgParticle *particle)
{
    /* Custom death logic (e.g., spawn secondary particles) */
}

static void
my_particle_system_class_init (MyParticleSystemClass *klass)
{
    LrgParticleSystemClass *system_class = LRG_PARTICLE_SYSTEM_CLASS (klass);
    system_class->on_particle_spawn = my_particle_system_on_particle_spawn;
    system_class->on_particle_death = my_particle_system_on_particle_death;
}
```
