# LrgParticle

`LrgParticle` is a GBoxed type representing a single particle's state including position, velocity, lifetime, color, and size.

## Structure

```c
typedef struct _LrgParticle LrgParticle;

struct _LrgParticle
{
    /* Position */
    gfloat x, y, z;

    /* Velocity */
    gfloat vx, vy, vz;

    /* Lifetime */
    gfloat life;         /* Current life remaining */
    gfloat max_life;     /* Total lifetime */

    /* Appearance */
    guint8 r, g, b, a;   /* Color */
    gfloat size;         /* Size multiplier */
    gfloat rotation;     /* Rotation in radians */
    gfloat angular_vel;  /* Angular velocity */
};
```

## Functions

### Creation

```c
LrgParticle *lrg_particle_new (void);
LrgParticle *lrg_particle_copy (const LrgParticle *particle);
void lrg_particle_free (LrgParticle *particle);
```

### State Management

```c
void lrg_particle_reset (LrgParticle *particle);
gboolean lrg_particle_is_alive (const LrgParticle *particle);
gfloat lrg_particle_get_normalized_life (const LrgParticle *particle);
```

### Properties

```c
/* Position */
void lrg_particle_get_position (const LrgParticle *particle,
                                gfloat *x, gfloat *y, gfloat *z);
void lrg_particle_set_position (LrgParticle *particle,
                                gfloat x, gfloat y, gfloat z);

/* Velocity */
void lrg_particle_get_velocity (const LrgParticle *particle,
                                gfloat *vx, gfloat *vy, gfloat *vz);
void lrg_particle_set_velocity (LrgParticle *particle,
                                gfloat vx, gfloat vy, gfloat vz);

/* Color */
void lrg_particle_get_color (const LrgParticle *particle,
                             guint8 *r, guint8 *g, guint8 *b, guint8 *a);
void lrg_particle_set_color (LrgParticle *particle,
                             guint8 r, guint8 g, guint8 b, guint8 a);

/* Size */
gfloat lrg_particle_get_size (const LrgParticle *particle);
void lrg_particle_set_size (LrgParticle *particle, gfloat size);

/* Lifetime */
void lrg_particle_set_lifetime (LrgParticle *particle, gfloat lifetime);
gfloat lrg_particle_get_life_remaining (const LrgParticle *particle);
```

## Usage

```c
/* Manual particle creation (usually done by emitter) */
LrgParticle *p = lrg_particle_new ();
lrg_particle_set_position (p, 100.0f, 200.0f, 0.0f);
lrg_particle_set_velocity (p, 10.0f, -50.0f, 0.0f);
lrg_particle_set_color (p, 255, 128, 0, 255);
lrg_particle_set_lifetime (p, 2.0f);

/* Check life progress (0.0 = born, 1.0 = dead) */
gfloat progress = lrg_particle_get_normalized_life (p);

/* Fade out based on life */
guint8 alpha = (guint8)(255 * (1.0f - progress));
lrg_particle_set_color (p, 255, 128, 0, alpha);

lrg_particle_free (p);
```

## Memory Management

Use `g_autoptr` for automatic cleanup:

```c
g_autoptr(LrgParticle) particle = lrg_particle_new ();
/* Automatically freed at end of scope */
```
