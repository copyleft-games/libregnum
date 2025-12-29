# LrgParticleEmitter

`LrgParticleEmitter` spawns particles with configurable emission shapes, rates, and initial properties.

## Type

```c
#define LRG_TYPE_PARTICLE_EMITTER (lrg_particle_emitter_get_type ())
G_DECLARE_FINAL_TYPE (LrgParticleEmitter, lrg_particle_emitter, LRG, PARTICLE_EMITTER, GObject)
```

## Emission Shapes

```c
typedef enum {
    LRG_EMITTER_SHAPE_POINT,      /* Single point */
    LRG_EMITTER_SHAPE_CIRCLE,     /* 2D circle */
    LRG_EMITTER_SHAPE_RECTANGLE,  /* 2D rectangle */
    LRG_EMITTER_SHAPE_SPHERE,     /* 3D sphere */
    LRG_EMITTER_SHAPE_CONE,       /* Directional cone */
} LrgEmitterShape;
```

## Functions

### Creation

```c
LrgParticleEmitter *lrg_particle_emitter_new (void);
```

### Position and Shape

```c
void lrg_particle_emitter_set_position (LrgParticleEmitter *emitter,
                                        gfloat x, gfloat y, gfloat z);
void lrg_particle_emitter_get_position (LrgParticleEmitter *emitter,
                                        gfloat *x, gfloat *y, gfloat *z);

void lrg_particle_emitter_set_shape (LrgParticleEmitter *emitter,
                                     LrgEmitterShape shape);
LrgEmitterShape lrg_particle_emitter_get_shape (LrgParticleEmitter *emitter);

/* Shape-specific parameters */
void lrg_particle_emitter_set_radius (LrgParticleEmitter *emitter, gfloat radius);
void lrg_particle_emitter_set_size (LrgParticleEmitter *emitter,
                                    gfloat width, gfloat height);
void lrg_particle_emitter_set_cone_angle (LrgParticleEmitter *emitter, gfloat degrees);
```

### Emission Rate

```c
void lrg_particle_emitter_set_rate (LrgParticleEmitter *emitter, gfloat particles_per_second);
gfloat lrg_particle_emitter_get_rate (LrgParticleEmitter *emitter);

/* Burst mode - emit many at once */
void lrg_particle_emitter_burst (LrgParticleEmitter *emitter, guint count);
```

### Particle Properties

```c
/* Lifetime */
void lrg_particle_emitter_set_lifetime (LrgParticleEmitter *emitter, gfloat seconds);
void lrg_particle_emitter_set_lifetime_variance (LrgParticleEmitter *emitter, gfloat variance);

/* Initial velocity */
void lrg_particle_emitter_set_initial_velocity (LrgParticleEmitter *emitter,
                                                gfloat vx, gfloat vy, gfloat vz);
void lrg_particle_emitter_set_velocity_variance (LrgParticleEmitter *emitter, gfloat variance);

/* Size */
void lrg_particle_emitter_set_particle_size (LrgParticleEmitter *emitter, gfloat size);
void lrg_particle_emitter_set_size_variance (LrgParticleEmitter *emitter, gfloat variance);

/* Color (start and end for interpolation) */
void lrg_particle_emitter_set_start_color (LrgParticleEmitter *emitter,
                                           guint8 r, guint8 g, guint8 b, guint8 a);
void lrg_particle_emitter_set_end_color (LrgParticleEmitter *emitter,
                                         guint8 r, guint8 g, guint8 b, guint8 a);
```

### Control

```c
void lrg_particle_emitter_start (LrgParticleEmitter *emitter);
void lrg_particle_emitter_stop (LrgParticleEmitter *emitter);
void lrg_particle_emitter_pause (LrgParticleEmitter *emitter);
gboolean lrg_particle_emitter_is_emitting (LrgParticleEmitter *emitter);
```

## Usage Examples

### Fire Effect

```c
g_autoptr(LrgParticleEmitter) fire = lrg_particle_emitter_new ();
lrg_particle_emitter_set_position (fire, 400.0f, 500.0f, 0.0f);
lrg_particle_emitter_set_shape (fire, LRG_EMITTER_SHAPE_CIRCLE);
lrg_particle_emitter_set_radius (fire, 20.0f);
lrg_particle_emitter_set_rate (fire, 100.0f);
lrg_particle_emitter_set_lifetime (fire, 1.5f);
lrg_particle_emitter_set_initial_velocity (fire, 0.0f, -80.0f, 0.0f);
lrg_particle_emitter_set_start_color (fire, 255, 200, 50, 255);
lrg_particle_emitter_set_end_color (fire, 255, 50, 0, 0);
lrg_particle_emitter_start (fire);
```

### Explosion Burst

```c
g_autoptr(LrgParticleEmitter) explosion = lrg_particle_emitter_new ();
lrg_particle_emitter_set_position (explosion, x, y, 0.0f);
lrg_particle_emitter_set_shape (explosion, LRG_EMITTER_SHAPE_SPHERE);
lrg_particle_emitter_set_radius (explosion, 5.0f);
lrg_particle_emitter_set_velocity_variance (explosion, 200.0f);
lrg_particle_emitter_set_lifetime (explosion, 0.8f);

/* Burst 100 particles at once */
lrg_particle_emitter_burst (explosion, 100);
```

### Directional Spray

```c
g_autoptr(LrgParticleEmitter) spray = lrg_particle_emitter_new ();
lrg_particle_emitter_set_shape (spray, LRG_EMITTER_SHAPE_CONE);
lrg_particle_emitter_set_cone_angle (spray, 30.0f);  /* 30 degree spread */
lrg_particle_emitter_set_initial_velocity (spray, 0.0f, -100.0f, 0.0f);
lrg_particle_emitter_set_rate (spray, 50.0f);
```
