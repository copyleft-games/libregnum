/* lrg-gpu-particles.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGpuParticles: a particle emitter simulated on the GPU by transform
 * feedback (GL 3.3, no compute shaders): ping-pong vertex buffers, emit,
 * update and kill in a vertex shader, sort-free instanced billboards.
 * The same simulation runs on the CPU when there is no GL context or the
 * caller asks for it, bit-identical in its random numbers and equal to
 * float rounding in its motion.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../tween/lrg-keyframe-curve.h"
#include "../ui/lrg-gradient-edit.h"
#include "lrg-particle-system.h"
#include "lrg-particle-asset.h"

G_BEGIN_DECLS

/**
 * LRG_GPU_PARTICLES_FLOATS_PER_PARTICLE:
 *
 * The number of floats one particle takes in the simulation buffers (and
 * in lrg_gpu_particles_read_back()): position xyz, age, velocity xyz,
 * lifetime, rotation, spin, spawn index (its bits), and one spare.
 */
#define LRG_GPU_PARTICLES_FLOATS_PER_PARTICLE (12)

/**
 * LRG_GPU_PARTICLES_TABLE_SIZE:
 *
 * The number of evenly spaced samples the colour gradient and the size
 * curve are baked into for the GPU (the shader blends linearly between
 * neighbouring samples).
 */
#define LRG_GPU_PARTICLES_TABLE_SIZE (16)

/**
 * LRG_GPU_PARTICLES_ERROR:
 *
 * Error domain of #LrgGpuParticles.
 */
#define LRG_GPU_PARTICLES_ERROR (lrg_gpu_particles_error_quark ())

/**
 * lrg_gpu_particles_error_quark:
 *
 * Returns: the #GQuark of %LRG_GPU_PARTICLES_ERROR
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_gpu_particles_error_quark (void);

/**
 * LrgGpuParticlesError:
 * @LRG_GPU_PARTICLES_ERROR_UNSUPPORTED: the source uses a feature the GPU
 *   path cannot run (the message names it); run it on the CPU instead
 * @LRG_GPU_PARTICLES_ERROR_NO_GPU: no GL context, or the driver lacks a
 *   GL 3.3 entry point transform feedback needs
 * @LRG_GPU_PARTICLES_ERROR_SHADER: a simulation or render shader did not
 *   compile or link (the message carries the driver's log)
 *
 * Error codes of %LRG_GPU_PARTICLES_ERROR.
 */
typedef enum
{
	LRG_GPU_PARTICLES_ERROR_UNSUPPORTED,
	LRG_GPU_PARTICLES_ERROR_NO_GPU,
	LRG_GPU_PARTICLES_ERROR_SHADER
} LrgGpuParticlesError;

/**
 * lrg_gpu_particles_error_get_type:
 *
 * Returns: the #GType of #LrgGpuParticlesError
 */
LRG_AVAILABLE_IN_ALL
GType lrg_gpu_particles_error_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_GPU_PARTICLES_ERROR (lrg_gpu_particles_error_get_type ())

/**
 * LrgGpuParticlesShape:
 * @LRG_GPU_PARTICLES_SHAPE_POINT: spawn at the emitter position; velocity
 *   inside the cone around the direction
 * @LRG_GPU_PARTICLES_SHAPE_SPHERE: spawn inside a sphere of
 *   #LrgGpuParticles:shape-radius; velocity points away from the centre
 * @LRG_GPU_PARTICLES_SHAPE_BOX: spawn inside a box of #LrgGpuParticles:box-x
 *   by box-y by box-z centred on the emitter; velocity inside the cone
 * @LRG_GPU_PARTICLES_SHAPE_CONE: spawn on the cone's base disc of
 *   #LrgGpuParticles:shape-radius (perpendicular to the direction);
 *   velocity inside the cone
 *
 * Where new particles start.
 */
typedef enum
{
	LRG_GPU_PARTICLES_SHAPE_POINT,
	LRG_GPU_PARTICLES_SHAPE_SPHERE,
	LRG_GPU_PARTICLES_SHAPE_BOX,
	LRG_GPU_PARTICLES_SHAPE_CONE
} LrgGpuParticlesShape;

/**
 * lrg_gpu_particles_shape_get_type:
 *
 * Returns: the #GType of #LrgGpuParticlesShape
 */
LRG_AVAILABLE_IN_ALL
GType lrg_gpu_particles_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_GPU_PARTICLES_SHAPE (lrg_gpu_particles_shape_get_type ())

/**
 * LrgGpuParticlesRenderMode:
 * @LRG_GPU_PARTICLES_RENDER_BILLBOARD: camera-facing quads turned by each
 *   particle's rotation
 * @LRG_GPU_PARTICLES_RENDER_STRETCHED: quads stretched along the
 *   on-screen velocity by #LrgGpuParticles:stretch seconds of travel
 *   (rain, sparks); the head stays on the particle
 *
 * How particles are drawn.
 */
typedef enum
{
	LRG_GPU_PARTICLES_RENDER_BILLBOARD,
	LRG_GPU_PARTICLES_RENDER_STRETCHED
} LrgGpuParticlesRenderMode;

/**
 * lrg_gpu_particles_render_mode_get_type:
 *
 * Returns: the #GType of #LrgGpuParticlesRenderMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_gpu_particles_render_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_GPU_PARTICLES_RENDER_MODE (lrg_gpu_particles_render_mode_get_type ())

#define LRG_TYPE_GPU_PARTICLES (lrg_gpu_particles_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGpuParticles, lrg_gpu_particles, LRG, GPU_PARTICLES, GObject)

/* ==========================================================================
 * Construction and backend
 * ========================================================================== */

/**
 * lrg_gpu_particles_new:
 * @capacity: the number of particle slots (1 .. 4194304); the oldest
 *   particle is replaced when more are emitted than fit
 *
 * Creates an emitter with the default parameters (no gravity, 100
 * particles per second, white fading to transparent). Nothing touches GL
 * until the first update or lrg_gpu_particles_prepare().
 *
 * Returns: (transfer full): a new #LrgGpuParticles
 */
LRG_AVAILABLE_IN_ALL
LrgGpuParticles * lrg_gpu_particles_new (guint capacity);

/**
 * lrg_gpu_particles_new_from_system:
 * @system: an #LrgParticleSystem with one emitter
 * @capacity: slots for the GPU copy, 0 for the system's max particles
 * @error: return location for a #GError, or %NULL
 *
 * Copies a CPU particle system's first emitter and its forces into a GPU
 * emitter: point, rectangle (box) and cone shapes, speed, lifetime and
 * size ranges, start and end colours, gravity and plain wind (summed into
 * #LrgGpuParticles:gravity-x/y/z), one attractor and one turbulence force.
 * A circle shape becomes a sphere of the same radius. Fails with
 * %LRG_GPU_PARTICLES_ERROR_UNSUPPORTED naming the first thing it cannot
 * run on the GPU (mesh shapes, several emitters, gusty wind, a second
 * attractor, custom force classes): keep running @system on the CPU then.
 *
 * Returns: (transfer full) (nullable): a new #LrgGpuParticles, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgGpuParticles * lrg_gpu_particles_new_from_system (LrgParticleSystem  *system,
                                                     guint               capacity,
                                                     GError            **error);

/**
 * lrg_gpu_particles_check_emitter:
 * @asset: the #LrgParticleAsset owning @emitter (seed, duration, loop)
 * @emitter: one of @asset's emitters
 * @error: return location for a #GError, or %NULL
 *
 * Validates that the GPU path can run an `.rparticles` emitter: every
 * enabled module must be GPU capable (lrg_particle_module_get_gpu_capable())
 * and inside the subset documented in docs/modules/particles/gpu-particles.org
 * (one spawn-rate without a rate curve or per-distance emission, bursts
 * with a fixed count, point/sphere/cone/box shapes, constant or gradient
 * colour, size/alpha/colour over life, gravity, wind or drag, one
 * turbulence, a vertical vortex around the emitter, one attractor, a
 * horizontal collision plane; billboard renderer). Fails with
 * %LRG_GPU_PARTICLES_ERROR_UNSUPPORTED naming the module and parameter:
 * play the emitter on the CPU (lrg_particle_asset_instantiate()) then.
 *
 * Returns: %TRUE when lrg_gpu_particles_new_from_asset() will succeed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gpu_particles_check_emitter (LrgParticleAsset         *asset,
                                          LrgParticleAssetEmitter  *emitter,
                                          GError                  **error);

/**
 * lrg_gpu_particles_new_from_asset:
 * @asset: an #LrgParticleAsset
 * @emitter: an emitter index of @asset
 * @capacity: particle slots, 0 for the emitter's max-particles
 * @error: return location for a #GError, or %NULL
 *
 * Builds a GPU emitter from an `.rparticles` emitter (see
 * lrg_gpu_particles_check_emitter() for the subset). The asset seed
 * becomes #LrgGpuParticles:seed (low 32 bits); the renderer texture is
 * not loaded (set it with lrg_gpu_particles_set_texture()).
 *
 * Returns: (transfer full) (nullable): a new #LrgGpuParticles, or %NULL
 *   with %LRG_GPU_PARTICLES_ERROR_UNSUPPORTED
 */
LRG_AVAILABLE_IN_ALL
LrgGpuParticles * lrg_gpu_particles_new_from_asset (LrgParticleAsset  *asset,
                                                    guint              emitter,
                                                    guint              capacity,
                                                    GError           **error);

/**
 * lrg_gpu_particles_is_gpu_available:
 *
 * Whether a window (GL context) is open and the driver has every GL 3.3
 * entry point the transform-feedback path needs. Does not compile the
 * shaders; lrg_gpu_particles_prepare() does.
 *
 * Returns: %TRUE when the GPU path can run
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gpu_particles_is_gpu_available (void);

/**
 * lrg_gpu_particles_prepare:
 * @self: an #LrgGpuParticles
 * @error: return location for a #GError, or %NULL
 *
 * Picks the backend now instead of at the first update: the GPU when
 * #LrgGpuParticles:prefer-gpu is set and lrg_gpu_particles_is_gpu_available(),
 * with the shaders compiled and the buffers allocated; the CPU otherwise.
 * Safe to call again. When the GPU was wanted but failed, @error says why
 * and the emitter runs on the CPU (the return is still %TRUE: the
 * emitter works); %FALSE only when it cannot run at all (never today).
 *
 * Returns: %TRUE when the emitter is ready
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gpu_particles_prepare (LrgGpuParticles  *self,
                                    GError          **error);

/**
 * lrg_gpu_particles_get_backend:
 * @self: an #LrgGpuParticles
 *
 * Returns: where the simulation runs; %LRG_PARTICLE_BACKEND_CPU before
 *   the backend is chosen
 */
LRG_AVAILABLE_IN_ALL
LrgParticleBackendType lrg_gpu_particles_get_backend (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_get_prefer_gpu:
 * @self: an #LrgGpuParticles
 *
 * Returns: whether the GPU path is used when available
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gpu_particles_get_prefer_gpu (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_set_prefer_gpu:
 * @self: an #LrgGpuParticles
 * @prefer_gpu: whether to simulate on the GPU when available
 *
 * Switches the backend. Live particles move with it: the GPU state is
 * read back into the CPU arrays (or the CPU arrays uploaded), so the
 * effect continues where it was.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_set_prefer_gpu (LrgGpuParticles *self,
                                       gboolean         prefer_gpu);

/**
 * lrg_gpu_particles_get_capacity:
 * @self: an #LrgGpuParticles
 *
 * Returns: the number of particle slots
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gpu_particles_get_capacity (LrgGpuParticles *self);

/* ==========================================================================
 * Simulation
 * ========================================================================== */

/**
 * lrg_gpu_particles_update:
 * @self: an #LrgGpuParticles
 * @delta_time: seconds since the last update (scaled by
 *   #LrgGpuParticles:time-scale; negative values count as 0)
 *
 * Advances the emitter clock, emits (rate, bursts and pending
 * lrg_gpu_particles_emit() requests) and steps every particle. On the GPU
 * this is one transform-feedback draw with rasterisation off; it needs
 * the GL context current (the main thread of a raylib game).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_update (LrgGpuParticles *self,
                               gfloat           delta_time);

/**
 * lrg_gpu_particles_emit:
 * @self: an #LrgGpuParticles
 * @count: particles to emit
 *
 * Queues a burst of @count particles for the next update, independent of
 * #LrgGpuParticles:emitting.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_emit (LrgGpuParticles *self,
                             guint            count);

/**
 * lrg_gpu_particles_clear:
 * @self: an #LrgGpuParticles
 *
 * Kills every particle and rewinds the emitter clock, the burst schedule
 * and the spawn counter (the next run repeats the same random numbers).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_clear (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_add_burst:
 * @self: an #LrgGpuParticles
 * @time: seconds into each #LrgGpuParticles:duration cycle
 * @count: particles emitted when the clock passes @time
 *
 * Adds a timed burst. With #LrgGpuParticles:loop the burst repeats every
 * cycle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_add_burst (LrgGpuParticles *self,
                                  gfloat           time,
                                  guint            count);

/**
 * lrg_gpu_particles_clear_bursts:
 * @self: an #LrgGpuParticles
 *
 * Removes every timed burst.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_clear_bursts (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_get_burst_count:
 * @self: an #LrgGpuParticles
 *
 * Returns: the number of timed bursts
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gpu_particles_get_burst_count (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_get_burst:
 * @self: an #LrgGpuParticles
 * @index: a burst index
 * @time: (out) (optional): its time in the cycle
 * @count: (out) (optional): its particle count
 *
 * Returns: %FALSE when @index is out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gpu_particles_get_burst (LrgGpuParticles *self,
                                      guint            index,
                                      gfloat          *time,
                                      guint           *count);

/**
 * lrg_gpu_particles_get_time:
 * @self: an #LrgGpuParticles
 *
 * Returns: the simulated seconds since creation or the last clear
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_gpu_particles_get_time (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_get_emitted_total:
 * @self: an #LrgGpuParticles
 *
 * The spawn counter: every particle ever emitted (since the last clear).
 * Particle n of a run always gets the same random numbers.
 *
 * Returns: the number of particles emitted
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_gpu_particles_get_emitted_total (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_count_alive:
 * @self: an #LrgGpuParticles
 *
 * Counts the live particles. On the GPU this reads the whole buffer back
 * (a pipeline stall: tests, tools and debug overlays, not every frame).
 *
 * Returns: the number of live particles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gpu_particles_count_alive (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_read_back:
 * @self: an #LrgGpuParticles
 * @n_floats: (out): the length of the returned array
 *
 * Copies the simulation state, %LRG_GPU_PARTICLES_FLOATS_PER_PARTICLE
 * floats per slot in slot order (see lrg_gpu_particles_get_particle()
 * for the layout). On the GPU this reads the buffer back.
 *
 * Returns: (transfer full) (array length=n_floats): the state
 */
LRG_AVAILABLE_IN_ALL
gfloat * lrg_gpu_particles_read_back (LrgGpuParticles *self,
                                      guint           *n_floats);

/**
 * lrg_gpu_particles_get_particle:
 * @self: an #LrgGpuParticles
 * @slot: a slot index below the capacity
 * @position: (out caller-allocates) (optional): world position
 * @velocity: (out caller-allocates) (optional): velocity in units/s
 * @age: (out) (optional): seconds since it spawned
 * @lifetime: (out) (optional): its lifetime in seconds
 *
 * Reads one slot (on the GPU: a small buffer read). A slot is live while
 * @age is below @lifetime; a slot never used has lifetime 0.
 *
 * Returns: %TRUE when the slot holds a live particle
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gpu_particles_get_particle (LrgGpuParticles *self,
                                         guint            slot,
                                         GrlVector3      *position,
                                         GrlVector3      *velocity,
                                         gfloat          *age,
                                         gfloat          *lifetime);

/* ==========================================================================
 * Rendering
 * ========================================================================== */

/**
 * lrg_gpu_particles_draw:
 * @self: an #LrgGpuParticles
 *
 * Draws every live particle as one instanced draw, inside BeginMode3D()
 * after the opaque geometry: depth test on, depth writes off, the
 * #LrgGpuParticles:blend-mode. Flushes rlgl's batch before and restores
 * raylib's blend and depth state after. The CPU backend uploads its state
 * first. Does nothing without a window or when
 * #LrgGpuParticles:layer is outside the active cull mask.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_draw (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_set_texture:
 * @self: an #LrgGpuParticles
 * @texture: (nullable): a sprite multiplied with the particle colour, or
 *   %NULL for a soft round dot
 *
 * Sets the particle sprite.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_set_texture (LrgGpuParticles *self,
                                    GrlTexture      *texture);

/**
 * lrg_gpu_particles_get_texture:
 * @self: an #LrgGpuParticles
 *
 * Returns: (transfer none) (nullable): the sprite
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_gpu_particles_get_texture (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_get_draw_count:
 * @self: an #LrgGpuParticles
 *
 * Returns: the instanced draw calls issued so far (0 or 1 per
 *   lrg_gpu_particles_draw(): tests and the editor's stats)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gpu_particles_get_draw_count (LrgGpuParticles *self);

/* ==========================================================================
 * Parameters (each is also a property; see the documentation)
 * ========================================================================== */

/**
 * lrg_gpu_particles_set_position:
 * @self: an #LrgGpuParticles
 * @x: world X
 * @y: world Y
 * @z: world Z
 *
 * Moves the emitter. Particles spawned during the next update are spread
 * along the way from the old position, so a moving emitter leaves a
 * continuous trail; live particles stay where they are (world space).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_set_position (LrgGpuParticles *self,
                                     gfloat           x,
                                     gfloat           y,
                                     gfloat           z);

/**
 * lrg_gpu_particles_get_position:
 * @self: an #LrgGpuParticles
 * @x: (out) (optional): world X
 * @y: (out) (optional): world Y
 * @z: (out) (optional): world Z
 *
 * Gets the emitter position.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_get_position (LrgGpuParticles *self,
                                     gfloat          *x,
                                     gfloat          *y,
                                     gfloat          *z);

/**
 * lrg_gpu_particles_set_direction:
 * @self: an #LrgGpuParticles
 * @x: X component
 * @y: Y component
 * @z: Z component
 *
 * Sets the cone axis new particles fly along (normalised when used; a
 * zero vector means up).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_set_direction (LrgGpuParticles *self,
                                      gfloat           x,
                                      gfloat           y,
                                      gfloat           z);

/**
 * lrg_gpu_particles_set_gravity:
 * @self: an #LrgGpuParticles
 * @x: acceleration X (units/s²)
 * @y: acceleration Y
 * @z: acceleration Z
 *
 * Sets the constant acceleration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_set_gravity (LrgGpuParticles *self,
                                    gfloat           x,
                                    gfloat           y,
                                    gfloat           z);

/**
 * lrg_gpu_particles_set_wind:
 * @self: an #LrgGpuParticles
 * @x: air velocity X (units/s)
 * @y: air velocity Y
 * @z: air velocity Z
 *
 * Sets the air velocity #LrgGpuParticles:drag pulls particles toward.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_set_wind (LrgGpuParticles *self,
                                 gfloat           x,
                                 gfloat           y,
                                 gfloat           z);

/**
 * lrg_gpu_particles_set_colors:
 * @self: an #LrgGpuParticles
 * @start: colour at birth
 * @end: colour at death
 *
 * Sets a two-stop colour ramp (clears #LrgGpuParticles:color-gradient).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_set_colors (LrgGpuParticles *self,
                                   const GrlColor  *start,
                                   const GrlColor  *end);

/**
 * lrg_gpu_particles_set_color_gradient:
 * @self: an #LrgGpuParticles
 * @gradient: (nullable): colour over the particle's life, or %NULL for
 *   the start-color to end-color ramp
 *
 * Sets the colour over life; it is baked into
 * %LRG_GPU_PARTICLES_TABLE_SIZE samples.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_set_color_gradient (LrgGpuParticles        *self,
                                           const LrgGradientStops *gradient);

/**
 * lrg_gpu_particles_get_color_gradient:
 * @self: an #LrgGpuParticles
 *
 * Returns: (transfer none) (nullable): the colour over life
 */
LRG_AVAILABLE_IN_ALL
const LrgGradientStops * lrg_gpu_particles_get_color_gradient (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_set_size_curve:
 * @self: an #LrgGpuParticles
 * @curve: (nullable): a multiplier of #LrgGpuParticles:size-start over
 *   the particle's life (t from 0 to 1), or %NULL for the linear
 *   size-start to size-end ramp
 *
 * Sets the size over life; it is sampled into
 * %LRG_GPU_PARTICLES_TABLE_SIZE values when set (change the curve
 * afterwards and set it again).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_set_size_curve (LrgGpuParticles  *self,
                                       LrgKeyframeCurve *curve);

/**
 * lrg_gpu_particles_get_size_curve:
 * @self: an #LrgGpuParticles
 *
 * Returns: (transfer none) (nullable): the size multiplier curve
 */
LRG_AVAILABLE_IN_ALL
LrgKeyframeCurve * lrg_gpu_particles_get_size_curve (LrgGpuParticles *self);

/**
 * lrg_gpu_particles_sample_color:
 * @self: an #LrgGpuParticles
 * @t: life fraction, clamped to 0..1
 * @color: (out caller-allocates): the colour a particle has at @t
 *
 * Evaluates the baked colour table exactly as the render shader does.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_sample_color (LrgGpuParticles *self,
                                     gfloat           t,
                                     GrlColor        *color);

/**
 * lrg_gpu_particles_sample_size:
 * @self: an #LrgGpuParticles
 * @t: life fraction, clamped to 0..1
 *
 * Evaluates the baked size table exactly as the render shader does.
 *
 * Returns: the particle size at @t in world units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_gpu_particles_sample_size (LrgGpuParticles *self,
                                      gfloat           t);

/**
 * lrg_gpu_particles_list_parameters:
 *
 * The property names that make up an emitter's description (everything
 * an asset or an inspector sets; not #LrgGpuParticles:capacity nor the
 * runtime switches), in display order.
 *
 * Returns: (transfer none) (array zero-terminated=1): the names
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_gpu_particles_list_parameters (void);

/**
 * lrg_gpu_particles_copy_parameters:
 * @self: an #LrgGpuParticles
 * @source: another #LrgGpuParticles
 *
 * Copies every description parameter, the bursts, the gradient, the size
 * curve and the texture from @source (not the capacity nor the particles).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_particles_copy_parameters (LrgGpuParticles *self,
                                        LrgGpuParticles *source);

G_END_DECLS
