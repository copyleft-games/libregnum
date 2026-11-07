/* lrg-particle-system.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Complete particle system with emitters, forces, and rendering.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../graphics/lrg-drawable.h"
#include "lrg-particle.h"
#include "lrg-particle-pool.h"
#include "lrg-particle-emitter.h"
#include "lrg-particle-force.h"

G_BEGIN_DECLS

/**
 * LrgParticleFunc:
 * @particle: (not nullable): an active particle (read only)
 * @user_data: data passed to lrg_particle_system_foreach_active()
 *
 * Callback receiving each active particle of a #LrgParticleSystem.
 *
 * Since: 1.0
 */
typedef void (*LrgParticleFunc) (const LrgParticle *particle,
                                 gpointer           user_data);

#define LRG_TYPE_PARTICLE_SYSTEM (lrg_particle_system_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgParticleSystem, lrg_particle_system, LRG, PARTICLE_SYSTEM, GObject)

/**
 * LrgParticleSystemClass:
 * @parent_class: Parent class
 * @update: Virtual method called each frame
 * @draw: Virtual method to render particles
 * @on_particle_spawn: Called when a particle is spawned
 * @on_particle_death: Called when a particle dies
 *
 * The class structure for #LrgParticleSystem.
 */
struct _LrgParticleSystemClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgParticleSystemClass::update:
     * @self: A #LrgParticleSystem
     * @delta_time: Time since last frame
     *
     * Updates all particles, emitters, and forces.
     */
    void    (*update)               (LrgParticleSystem   *self,
                                     gfloat               delta_time);

    /**
     * LrgParticleSystemClass::draw:
     * @self: A #LrgParticleSystem
     *
     * Renders all active particles.
     */
    void    (*draw)                 (LrgParticleSystem   *self);

    /**
     * LrgParticleSystemClass::on_particle_spawn:
     * @self: A #LrgParticleSystem
     * @particle: The spawned particle
     *
     * Called when a particle is spawned (for custom initialization).
     */
    void    (*on_particle_spawn)    (LrgParticleSystem   *self,
                                     LrgParticle         *particle);

    /**
     * LrgParticleSystemClass::on_particle_death:
     * @self: A #LrgParticleSystem
     * @particle: The dying particle
     *
     * Called when a particle dies (for effects like sub-emitters).
     */
    void    (*on_particle_death)    (LrgParticleSystem   *self,
                                     LrgParticle         *particle);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * LrgParticleRng (0.9.0)
 * ========================================================================== */

typedef struct _LrgParticleRng LrgParticleRng;

/**
 * LrgParticleRng:
 * @state: generator state
 * @inc: stream selector (always odd)
 *
 * A small, fast, seedable random number generator (PCG32, XSH-RR
 * output): the same seed gives the same sequence on every platform.
 * Every #LrgParticleSystem owns one (lrg_particle_system_get_rng()); the
 * module stack draws all its randomness from it, which makes a particle
 * effect reproducible from its seed.  A plain boxed struct: copy it by
 * value to snapshot a sequence.
 */
struct _LrgParticleRng
{
	guint64 state;
	guint64 inc;
};

#define LRG_TYPE_PARTICLE_RNG (lrg_particle_rng_get_type ())

/**
 * lrg_particle_rng_get_type:
 *
 * Returns: the boxed #GType of #LrgParticleRng
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_rng_get_type (void) G_GNUC_CONST;

/**
 * lrg_particle_rng_new:
 * @seed: the seed
 *
 * Returns: (transfer full): a generator seeded with @seed
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRng * lrg_particle_rng_new (guint64 seed);

/**
 * lrg_particle_rng_copy:
 * @self: an #LrgParticleRng
 *
 * Returns: (transfer full): a copy in the same state
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRng * lrg_particle_rng_copy (const LrgParticleRng *self);

/**
 * lrg_particle_rng_free:
 * @self: (nullable): an #LrgParticleRng
 *
 * Frees a generator from lrg_particle_rng_new() or lrg_particle_rng_copy().
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_rng_free (LrgParticleRng *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgParticleRng, lrg_particle_rng_free)

/**
 * lrg_particle_rng_seed:
 * @self: an #LrgParticleRng
 * @seed: the seed
 *
 * Restarts the sequence from @seed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_rng_seed (LrgParticleRng *self,
                            guint64         seed);

/**
 * lrg_particle_rng_next_u32:
 * @self: an #LrgParticleRng
 *
 * Returns: the next 32 random bits
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_particle_rng_next_u32 (LrgParticleRng *self);

/**
 * lrg_particle_rng_next_float:
 * @self: an #LrgParticleRng
 *
 * Returns: the next number in [0, 1) (24 random bits)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_rng_next_float (LrgParticleRng *self);

/**
 * lrg_particle_rng_range:
 * @self: an #LrgParticleRng
 * @min: lower bound
 * @max: upper bound
 *
 * Returns: a number in [@min, @max) (exactly @min when they are equal;
 *   no number is drawn then, so constant ranges do not shift the
 *   sequence)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_rng_range (LrgParticleRng *self,
                               gfloat          min,
                               gfloat          max);

/**
 * lrg_particle_rng_unit_vector:
 * @self: an #LrgParticleRng
 * @out: (out caller-allocates) (array fixed-size=3): a uniformly
 *   distributed unit vector
 *
 * Draws a direction uniformly over the sphere.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_rng_unit_vector (LrgParticleRng *self,
                                   gfloat         *out);

/* ==========================================================================
 * Module stack (0.9.0)
 * ========================================================================== */

/**
 * LrgParticleModuleStage:
 * @LRG_PARTICLE_MODULE_STAGE_SPAWN: decides how many particles to emit
 *   each step (rate, bursts): the spawn_count() vfunc
 * @LRG_PARTICLE_MODULE_STAGE_SHAPE: places a new particle and sets the
 *   spawn direction (shapes): init_particle()
 * @LRG_PARTICLE_MODULE_STAGE_INIT: sets a new particle's initial values
 *   (velocity, lifetime, size, colour, rotation): init_particle()
 * @LRG_PARTICLE_MODULE_STAGE_FORCE: changes velocities before the
 *   integration step (gravity, wind, drag, turbulence, ...):
 *   update_particle()
 * @LRG_PARTICLE_MODULE_STAGE_COLLISION: after integration, tests the
 *   step's segment and responds (bounce, kill): update_particle()
 * @LRG_PARTICLE_MODULE_STAGE_OVER_LIFE: sets values from the normalized
 *   age (size, colour, alpha, rotation by curve or gradient):
 *   update_particle()
 * @LRG_PARTICLE_MODULE_STAGE_EVENT: reacts to birth, death and collision
 *   events (sub-emitters): particle_event()
 *
 * When in a step a module runs.  The system runs the stages in this
 * order, and the modules of one stage in stack order, so the stack order
 * only matters inside a stage (two force modules, two collision planes).
 */
typedef enum
{
	LRG_PARTICLE_MODULE_STAGE_SPAWN,
	LRG_PARTICLE_MODULE_STAGE_SHAPE,
	LRG_PARTICLE_MODULE_STAGE_INIT,
	LRG_PARTICLE_MODULE_STAGE_FORCE,
	LRG_PARTICLE_MODULE_STAGE_COLLISION,
	LRG_PARTICLE_MODULE_STAGE_OVER_LIFE,
	LRG_PARTICLE_MODULE_STAGE_EVENT
} LrgParticleModuleStage;

/**
 * lrg_particle_module_stage_get_type:
 *
 * Returns: the #GType of #LrgParticleModuleStage
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_module_stage_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_MODULE_STAGE (lrg_particle_module_stage_get_type ())

/**
 * LrgParticleEvent:
 * @LRG_PARTICLE_EVENT_BIRTH: a particle was spawned
 * @LRG_PARTICLE_EVENT_DEATH: a particle's lifetime ran out or a module
 *   killed it
 * @LRG_PARTICLE_EVENT_COLLISION: a collision module reported a hit
 *
 * Particle events delivered to %LRG_PARTICLE_MODULE_STAGE_EVENT modules.
 */
typedef enum
{
	LRG_PARTICLE_EVENT_BIRTH,
	LRG_PARTICLE_EVENT_DEATH,
	LRG_PARTICLE_EVENT_COLLISION
} LrgParticleEvent;

/**
 * lrg_particle_event_get_type:
 *
 * Returns: the #GType of #LrgParticleEvent
 */
LRG_AVAILABLE_IN_ALL
GType lrg_particle_event_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PARTICLE_EVENT (lrg_particle_event_get_type ())

typedef struct _LrgParticleModuleContext LrgParticleModuleContext;

/**
 * LrgParticleModuleContext:
 * @system: the system being stepped
 * @rng: the system's generator: draw every random number from it
 * @dt: the step in seconds (time scale applied)
 * @time: seconds since the system started playing (start delay included)
 * @cycle_time: seconds into the current emission cycle
 * @cycle_fraction: @cycle_time over the duration, 0..1 (0 when the
 *   duration is 0, i.e. endless)
 * @cycle: completed emission cycles (looping systems)
 * @origin: spawn origin in world space: the system position, or the
 *   point a sub-emitter fires at
 * @rotation: the system's rotation as a quaternion (x, y, z, w); shapes
 *   and directions are rotated by it
 * @direction: the spawn direction (unit): set to the rotated +Y axis
 *   before the shape stage, written by shape modules, read by velocity
 *   modules
 * @inherit_velocity: the system's own movement per second, which the
 *   velocity module's `inherit` factor adds to new particles (zero for
 *   lrg_particle_system_spawn(), which adds its own velocity)
 * @moved: distance the system moved during this step
 * @event_point: world position of the current event (collision point,
 *   death position)
 * @event_normal: surface normal of a collision event (zero otherwise)
 *
 * What modules see while the system steps.  Filled by the system; a
 * module only writes @direction (shape stage) and reads the rest.
 */
struct _LrgParticleModuleContext
{
	LrgParticleSystem *system;
	LrgParticleRng    *rng;
	gfloat             dt;
	gfloat             time;
	gfloat             cycle_time;
	gfloat             cycle_fraction;
	guint              cycle;
	gfloat             origin[3];
	gfloat             rotation[4];
	gfloat             direction[3];
	gfloat             inherit_velocity[3];
	gfloat             moved;
	gfloat             event_point[3];
	gfloat             event_normal[3];

	/*< private >*/
	gpointer _reserved[4];
};

#define LRG_TYPE_PARTICLE_MODULE (lrg_particle_module_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgParticleModule, lrg_particle_module, LRG, PARTICLE_MODULE, GObject)

/**
 * LrgParticleModuleClass:
 * @parent_class: the parent class
 * @stage: when the module runs (set it in class_init)
 * @reset: clears per-run state (accumulators, burst counters); called by
 *   lrg_particle_system_play() after a stop and by
 *   lrg_particle_system_restart()
 * @spawn_count: %LRG_PARTICLE_MODULE_STAGE_SPAWN: how many particles to
 *   emit this step
 * @init_particle: %LRG_PARTICLE_MODULE_STAGE_SHAPE and
 *   %LRG_PARTICLE_MODULE_STAGE_INIT: sets a new particle's values
 * @update_particle: %LRG_PARTICLE_MODULE_STAGE_FORCE,
 *   %LRG_PARTICLE_MODULE_STAGE_COLLISION and
 *   %LRG_PARTICLE_MODULE_STAGE_OVER_LIFE: steps one live particle
 * @particle_event: %LRG_PARTICLE_MODULE_STAGE_EVENT: reacts to an event
 *
 * A module of a particle system's module stack.  Subclass it in C for a
 * custom behaviour: set @stage and implement the vfunc of that stage.
 * Every vfunc is optional.  Built-in modules are created by kind name
 * with lrg_particle_module_new() (see lrg-particle-asset.h); their
 * parameters are GObject properties, which the `.rparticles` format and
 * the editor's property grid read and write.
 */
struct _LrgParticleModuleClass
{
	GObjectClass           parent_class;

	LrgParticleModuleStage stage;

	void     (*reset)           (LrgParticleModule        *self);
	guint    (*spawn_count)     (LrgParticleModule        *self,
	                             LrgParticleModuleContext *ctx);
	void     (*init_particle)   (LrgParticleModule        *self,
	                             LrgParticleModuleContext *ctx,
	                             LrgParticle              *particle);
	void     (*update_particle) (LrgParticleModule        *self,
	                             LrgParticleModuleContext *ctx,
	                             LrgParticle              *particle);
	void     (*particle_event)  (LrgParticleModule        *self,
	                             LrgParticleModuleContext *ctx,
	                             LrgParticle              *particle,
	                             LrgParticleEvent          event);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * lrg_particle_module_get_stage:
 * @self: an #LrgParticleModule
 *
 * Returns: the stage the module runs in (its class's)
 */
LRG_AVAILABLE_IN_ALL
LrgParticleModuleStage lrg_particle_module_get_stage (LrgParticleModule *self);

/**
 * lrg_particle_module_get_enabled:
 * @self: an #LrgParticleModule
 *
 * Returns: whether the module runs (#LrgParticleModule:enabled)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_module_get_enabled (LrgParticleModule *self);

/**
 * lrg_particle_module_set_enabled:
 * @self: an #LrgParticleModule
 * @enabled: whether the module runs
 *
 * A disabled module stays in the stack (and in the file) but does
 * nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_module_set_enabled (LrgParticleModule *self,
                                      gboolean           enabled);

/**
 * lrg_particle_module_reset:
 * @self: an #LrgParticleModule
 *
 * Calls the reset() vfunc.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_module_reset (LrgParticleModule *self);

/**
 * lrg_particle_module_spawn_count:
 * @self: an #LrgParticleModule
 * @ctx: the step's context
 *
 * Calls the spawn_count() vfunc (0 without one, or when disabled).
 *
 * Returns: particles to emit this step
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_module_spawn_count (LrgParticleModule        *self,
                                       LrgParticleModuleContext *ctx);

/**
 * lrg_particle_module_init_particle:
 * @self: an #LrgParticleModule
 * @ctx: the step's context
 * @particle: the new particle
 *
 * Calls the init_particle() vfunc when enabled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_module_init_particle (LrgParticleModule        *self,
                                        LrgParticleModuleContext *ctx,
                                        LrgParticle              *particle);

/**
 * lrg_particle_module_update_particle:
 * @self: an #LrgParticleModule
 * @ctx: the step's context
 * @particle: a live particle
 *
 * Calls the update_particle() vfunc when enabled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_module_update_particle (LrgParticleModule        *self,
                                          LrgParticleModuleContext *ctx,
                                          LrgParticle              *particle);

/**
 * lrg_particle_module_particle_event:
 * @self: an #LrgParticleModule
 * @ctx: the step's context (@event_point and @event_normal filled)
 * @particle: the particle the event is about
 * @event: the event
 *
 * Calls the particle_event() vfunc when enabled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_module_particle_event (LrgParticleModule        *self,
                                         LrgParticleModuleContext *ctx,
                                         LrgParticle              *particle,
                                         LrgParticleEvent          event);

/**
 * lrg_particle_module_copy:
 * @self: an #LrgParticleModule
 *
 * Creates a module of the same type with the same property values
 * (curves and gradients deep-copied) and fresh run state.  Effects copy
 * an asset's modules so that several instances can play one asset.
 *
 * Returns: (transfer full): the copy
 */
LRG_AVAILABLE_IN_ALL
LrgParticleModule * lrg_particle_module_copy (LrgParticleModule *self);

/**
 * LrgParticleRaycastFunc:
 * @from: (array fixed-size=3): segment start
 * @to: (array fixed-size=3): segment end
 * @layer_mask: layers to test
 * @out_point: (out caller-allocates) (array fixed-size=3): the hit point
 * @out_normal: (out caller-allocates) (array fixed-size=3): the unit
 *   surface normal at the hit
 * @user_data: data given to lrg_particle_system_set_raycast_func()
 *
 * Finds the first world surface on a segment, for the `world` mode of the
 * collision module.  The particle component wires it to the physics
 * world's lrg_dynamics_world_raycast().
 *
 * Returns: %TRUE on a hit
 */
typedef gboolean (*LrgParticleRaycastFunc) (const gfloat *from,
                                            const gfloat *to,
                                            guint32       layer_mask,
                                            gfloat       *out_point,
                                            gfloat       *out_normal,
                                            gpointer      user_data);

/**
 * LrgParticleSystemLookupFunc:
 * @name: a system name
 * @user_data: data given to lrg_particle_system_set_lookup_func()
 *
 * Finds a sibling system by name, for sub-emitters.
 *
 * Returns: (transfer none) (nullable): the system, or %NULL
 */
typedef LrgParticleSystem * (*LrgParticleSystemLookupFunc) (const gchar *name,
                                                             gpointer     user_data);

/**
 * lrg_particle_system_new:
 * @max_particles: Maximum number of particles
 *
 * Creates a new particle system.
 *
 * Returns: (transfer full): A new #LrgParticleSystem
 */
LRG_AVAILABLE_IN_ALL
LrgParticleSystem *     lrg_particle_system_new                 (guint                max_particles);

/**
 * lrg_particle_system_update:
 * @self: A #LrgParticleSystem
 * @delta_time: Time since last frame
 *
 * Updates all particles, emitters, and forces.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_update              (LrgParticleSystem   *self,
                                                                 gfloat               delta_time);

/**
 * lrg_particle_system_draw:
 * @self: A #LrgParticleSystem
 *
 * Renders all active particles.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_draw                (LrgParticleSystem   *self);

/**
 * lrg_particle_system_foreach_active:
 * @self: A #LrgParticleSystem
 * @func: (scope call) (closure user_data): called once per active particle
 * @user_data: data for @func
 *
 * Calls @func for every active (alive) particle of the system, newest
 * first.  Renderers use it to read particles without reaching into the
 * pool; @func must not add or remove particles.  Headless.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_foreach_active      (LrgParticleSystem   *self,
                                                                 LrgParticleFunc      func,
                                                                 gpointer             user_data);

/* Emitter management */

/**
 * lrg_particle_system_add_emitter:
 * @self: A #LrgParticleSystem
 * @emitter: (transfer none): Emitter to add
 *
 * Adds an emitter to the system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_add_emitter         (LrgParticleSystem   *self,
                                                                 LrgParticleEmitter  *emitter);

/**
 * lrg_particle_system_remove_emitter:
 * @self: A #LrgParticleSystem
 * @emitter: Emitter to remove
 *
 * Removes an emitter from the system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_remove_emitter      (LrgParticleSystem   *self,
                                                                 LrgParticleEmitter  *emitter);

/**
 * lrg_particle_system_get_emitters:
 * @self: A #LrgParticleSystem
 *
 * Gets all emitters.
 *
 * Returns: (transfer none) (element-type LrgParticleEmitter): The emitter list
 */
LRG_AVAILABLE_IN_ALL
GList *                 lrg_particle_system_get_emitters        (LrgParticleSystem   *self);

/**
 * lrg_particle_system_clear_emitters:
 * @self: A #LrgParticleSystem
 *
 * Removes all emitters.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_clear_emitters      (LrgParticleSystem   *self);

/* Force management */

/**
 * lrg_particle_system_add_force:
 * @self: A #LrgParticleSystem
 * @force: (transfer none): Force to add
 *
 * Adds a force to the system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_add_force           (LrgParticleSystem   *self,
                                                                 LrgParticleForce    *force);

/**
 * lrg_particle_system_remove_force:
 * @self: A #LrgParticleSystem
 * @force: Force to remove
 *
 * Removes a force from the system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_remove_force        (LrgParticleSystem   *self,
                                                                 LrgParticleForce    *force);

/**
 * lrg_particle_system_get_forces:
 * @self: A #LrgParticleSystem
 *
 * Gets all forces.
 *
 * Returns: (transfer none) (element-type LrgParticleForce): The force list
 */
LRG_AVAILABLE_IN_ALL
GList *                 lrg_particle_system_get_forces          (LrgParticleSystem   *self);

/**
 * lrg_particle_system_clear_forces:
 * @self: A #LrgParticleSystem
 *
 * Removes all forces.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_clear_forces        (LrgParticleSystem   *self);

/* Particle control */

/**
 * lrg_particle_system_emit:
 * @self: A #LrgParticleSystem
 * @count: Number of particles to emit
 *
 * Manually emits particles using the first emitter.
 *
 * Returns: Number of particles actually emitted
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_particle_system_emit                (LrgParticleSystem   *self,
                                                                 guint                count);

/**
 * lrg_particle_system_emit_at:
 * @self: A #LrgParticleSystem
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @count: Number of particles
 *
 * Emits particles at a specific position.
 *
 * Returns: Number of particles actually emitted
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_particle_system_emit_at             (LrgParticleSystem   *self,
                                                                 gfloat               x,
                                                                 gfloat               y,
                                                                 gfloat               z,
                                                                 guint                count);

/**
 * lrg_particle_system_clear:
 * @self: A #LrgParticleSystem
 *
 * Kills all active particles immediately.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_clear               (LrgParticleSystem   *self);

/* State control */

/**
 * lrg_particle_system_play:
 * @self: A #LrgParticleSystem
 *
 * Starts or resumes the particle system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_play                (LrgParticleSystem   *self);

/**
 * lrg_particle_system_pause:
 * @self: A #LrgParticleSystem
 *
 * Pauses the particle system.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_pause               (LrgParticleSystem   *self);

/**
 * lrg_particle_system_stop:
 * @self: A #LrgParticleSystem
 *
 * Stops the system and clears all particles.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_stop                (LrgParticleSystem   *self);

/**
 * lrg_particle_system_is_playing:
 * @self: A #LrgParticleSystem
 *
 * Checks if the system is currently playing.
 *
 * Returns: %TRUE if playing
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_particle_system_is_playing          (LrgParticleSystem   *self);

/**
 * lrg_particle_system_is_alive:
 * @self: A #LrgParticleSystem
 *
 * Checks if the system has any active particles or enabled emitters.
 *
 * Returns: %TRUE if alive
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_particle_system_is_alive            (LrgParticleSystem   *self);

/* Properties */

/**
 * lrg_particle_system_get_active_count:
 * @self: A #LrgParticleSystem
 *
 * Gets the number of currently active particles.
 *
 * Returns: Active particle count
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_particle_system_get_active_count    (LrgParticleSystem   *self);

/**
 * lrg_particle_system_get_max_particles:
 * @self: A #LrgParticleSystem
 *
 * Gets the maximum particle capacity.
 *
 * Returns: Maximum particles
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_particle_system_get_max_particles   (LrgParticleSystem   *self);

/**
 * lrg_particle_system_get_render_mode:
 * @self: A #LrgParticleSystem
 *
 * Gets the particle render mode.
 *
 * Returns: The render mode
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRenderMode   lrg_particle_system_get_render_mode     (LrgParticleSystem   *self);

/**
 * lrg_particle_system_set_render_mode:
 * @self: A #LrgParticleSystem
 * @mode: The render mode
 *
 * Sets the particle render mode.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_render_mode     (LrgParticleSystem   *self,
                                                                 LrgParticleRenderMode mode);

/**
 * lrg_particle_system_get_blend_mode:
 * @self: A #LrgParticleSystem
 *
 * Gets the particle blend mode.
 *
 * Returns: The blend mode
 */
LRG_AVAILABLE_IN_ALL
LrgParticleBlendMode    lrg_particle_system_get_blend_mode      (LrgParticleSystem   *self);

/**
 * lrg_particle_system_set_blend_mode:
 * @self: A #LrgParticleSystem
 * @mode: The blend mode
 *
 * Sets the particle blend mode.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_blend_mode      (LrgParticleSystem   *self,
                                                                 LrgParticleBlendMode mode);

/**
 * lrg_particle_system_set_layer:
 * @self: A #LrgParticleSystem
 * @layer: A render layer 0 .. 31 (see lrg_render_layers_get_name())
 *
 * Sets #LrgParticleSystem:layer, the render layer the system draws on.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_layer           (LrgParticleSystem   *self,
                                                                 guint                layer);

/**
 * lrg_particle_system_get_layer:
 * @self: A #LrgParticleSystem
 *
 * Returns: The system's render layer (0, "default", until set)
 */
LRG_AVAILABLE_IN_ALL
guint                   lrg_particle_system_get_layer           (LrgParticleSystem   *self);

/**
 * lrg_particle_system_is_layer_visible:
 * @self: A #LrgParticleSystem
 *
 * Whether the system's layer is in lrg_render_layers_get_active_mask():
 * what lrg_particle_system_draw() and custom particle renderers test
 * before drawing.
 *
 * Returns: %TRUE when the system would draw
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_particle_system_is_layer_visible    (LrgParticleSystem   *self);

/**
 * lrg_particle_system_get_position:
 * @self: A #LrgParticleSystem
 * @x: (out) (nullable): X position
 * @y: (out) (nullable): Y position
 * @z: (out) (nullable): Z position
 *
 * Gets the system world position.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_get_position        (LrgParticleSystem   *self,
                                                                 gfloat              *x,
                                                                 gfloat              *y,
                                                                 gfloat              *z);

/**
 * lrg_particle_system_set_position:
 * @self: A #LrgParticleSystem
 * @x: X position
 * @y: Y position
 * @z: Z position
 *
 * Sets the system world position.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_position        (LrgParticleSystem   *self,
                                                                 gfloat               x,
                                                                 gfloat               y,
                                                                 gfloat               z);

/**
 * lrg_particle_system_get_loop:
 * @self: A #LrgParticleSystem
 *
 * Checks if the system loops.
 *
 * Returns: %TRUE if looping
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_particle_system_get_loop            (LrgParticleSystem   *self);

/**
 * lrg_particle_system_set_loop:
 * @self: A #LrgParticleSystem
 * @loop: Whether to loop
 *
 * Sets whether the system loops after all particles die.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_loop            (LrgParticleSystem   *self,
                                                                 gboolean             loop);

/**
 * lrg_particle_system_get_duration:
 * @self: A #LrgParticleSystem
 *
 * Gets the system duration (for non-looping systems).
 *
 * Returns: Duration in seconds (0 = infinite)
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_system_get_duration        (LrgParticleSystem   *self);

/**
 * lrg_particle_system_set_duration:
 * @self: A #LrgParticleSystem
 * @duration: Duration in seconds (0 = infinite)
 *
 * Sets the system duration.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_duration        (LrgParticleSystem   *self,
                                                                 gfloat               duration);

/**
 * lrg_particle_system_get_time_scale:
 * @self: A #LrgParticleSystem
 *
 * Gets the time scale multiplier.
 *
 * Returns: Time scale
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_particle_system_get_time_scale      (LrgParticleSystem   *self);

/**
 * lrg_particle_system_set_time_scale:
 * @self: A #LrgParticleSystem
 * @scale: Time scale multiplier
 *
 * Sets the time scale (for slow-motion or fast-forward effects).
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_particle_system_set_time_scale      (LrgParticleSystem   *self,
                                                                 gfloat               scale);

/* ==========================================================================
 * Module stack, seeding, naming, sub-emitters (0.9.0)
 * ========================================================================== */

/**
 * lrg_particle_system_add_module:
 * @self: an #LrgParticleSystem
 * @module: (transfer none): a module to append to the stack
 *
 * Appends @module to the module stack.  A system with modules spawns
 * through its spawn-stage modules and steps its particles through the
 * force, collision and over-life stages, after any legacy emitters and
 * forces.  The system keeps a reference; one module object must not be
 * in two systems (copy it with lrg_particle_module_copy()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_add_module (LrgParticleSystem *self,
                                     LrgParticleModule *module);

/**
 * lrg_particle_system_remove_module:
 * @self: an #LrgParticleSystem
 * @module: a module of the stack
 *
 * Removes @module from the stack.
 *
 * Returns: %TRUE if it was in the stack
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_system_remove_module (LrgParticleSystem *self,
                                            LrgParticleModule *module);

/**
 * lrg_particle_system_clear_modules:
 * @self: an #LrgParticleSystem
 *
 * Empties the module stack.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_clear_modules (LrgParticleSystem *self);

/**
 * lrg_particle_system_get_n_modules:
 * @self: an #LrgParticleSystem
 *
 * Returns: the number of modules in the stack
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_system_get_n_modules (LrgParticleSystem *self);

/**
 * lrg_particle_system_get_module:
 * @self: an #LrgParticleSystem
 * @index: a stack index
 *
 * Returns: (transfer none) (nullable): the module, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgParticleModule * lrg_particle_system_get_module (LrgParticleSystem *self,
                                                    guint              index);

/**
 * lrg_particle_system_get_seed:
 * @self: an #LrgParticleSystem
 *
 * Returns: the seed of the system's generator
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_particle_system_get_seed (LrgParticleSystem *self);

/**
 * lrg_particle_system_set_seed:
 * @self: an #LrgParticleSystem
 * @seed: the seed
 *
 * Sets #LrgParticleSystem:seed and reseeds the generator.  The generator
 * is also reseeded from it by lrg_particle_system_restart() and by
 * lrg_particle_system_play() after a stop, so a system replays the same
 * particles for the same step sequence.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_set_seed (LrgParticleSystem *self,
                                   guint64            seed);

/**
 * lrg_particle_system_get_rng:
 * @self: an #LrgParticleSystem
 *
 * Returns: (transfer none): the system's generator
 */
LRG_AVAILABLE_IN_ALL
LrgParticleRng * lrg_particle_system_get_rng (LrgParticleSystem *self);

/**
 * lrg_particle_system_get_name:
 * @self: an #LrgParticleSystem
 *
 * Returns: (nullable): the system's name (an effect's emitter name)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_particle_system_get_name (LrgParticleSystem *self);

/**
 * lrg_particle_system_set_name:
 * @self: an #LrgParticleSystem
 * @name: (nullable): a name
 *
 * Sets #LrgParticleSystem:name, which sub-emitters refer to.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_set_name (LrgParticleSystem *self,
                                   const gchar       *name);

/**
 * lrg_particle_system_set_lookup_func:
 * @self: an #LrgParticleSystem
 * @func: (nullable) (scope notified) (closure user_data): finds sibling
 *   systems by name
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Sets how lrg_particle_system_lookup() finds other systems.  An
 * #LrgParticleEffect sets it on each of its systems.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_set_lookup_func (LrgParticleSystem          *self,
                                          LrgParticleSystemLookupFunc func,
                                          gpointer                    user_data,
                                          GDestroyNotify              destroy);

/**
 * lrg_particle_system_lookup:
 * @self: an #LrgParticleSystem
 * @name: a system name
 *
 * Returns: (transfer none) (nullable): the sibling system called @name,
 *   or %NULL (no lookup function, or no such system)
 */
LRG_AVAILABLE_IN_ALL
LrgParticleSystem * lrg_particle_system_lookup (LrgParticleSystem *self,
                                                const gchar       *name);

/**
 * lrg_particle_system_set_raycast_func:
 * @self: an #LrgParticleSystem
 * @func: (nullable) (scope notified) (closure user_data): the world
 *   raycast for collision modules in `world` mode
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data
 *
 * Without a function, world collision does nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_set_raycast_func (LrgParticleSystem     *self,
                                           LrgParticleRaycastFunc func,
                                           gpointer               user_data,
                                           GDestroyNotify         destroy);

/**
 * lrg_particle_system_raycast:
 * @self: an #LrgParticleSystem
 * @from: (array fixed-size=3): segment start
 * @to: (array fixed-size=3): segment end
 * @layer_mask: layers to test
 * @out_point: (out caller-allocates) (array fixed-size=3): the hit point
 * @out_normal: (out caller-allocates) (array fixed-size=3): the normal
 *
 * Calls the raycast function.
 *
 * Returns: %TRUE on a hit (%FALSE without a function)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_system_raycast (LrgParticleSystem *self,
                                      const gfloat      *from,
                                      const gfloat      *to,
                                      guint32            layer_mask,
                                      gfloat            *out_point,
                                      gfloat            *out_normal);

/**
 * lrg_particle_system_spawn:
 * @self: an #LrgParticleSystem
 * @origin: (array fixed-size=3): spawn origin in world space
 * @inherit_velocity: (array fixed-size=3) (nullable): a velocity added to
 *   every new particle after the init stage (a sub-emitter passes the
 *   parent's velocity times its inherit-velocity)
 * @count: number of particles
 *
 * Spawns @count particles through the shape and init stages of the
 * module stack at @origin (instead of the system position), raising
 * birth events.  Sub-emitters call it on their target system; games can
 * call it for impacts.  Works while paused or stopped.
 *
 * Returns: the number spawned (fewer when the pool is full)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_particle_system_spawn (LrgParticleSystem *self,
                                 const gfloat      *origin,
                                 const gfloat      *inherit_velocity,
                                 guint              count);

/**
 * lrg_particle_system_raise_event:
 * @self: an #LrgParticleSystem
 * @particle: the particle the event is about
 * @event: the event
 * @point: (array fixed-size=3) (nullable): the event position (the
 *   particle's when %NULL)
 * @normal: (array fixed-size=3) (nullable): the surface normal of a
 *   collision
 *
 * Delivers an event to the event-stage modules (collision modules call
 * it on a hit).
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_raise_event (LrgParticleSystem *self,
                                      LrgParticle       *particle,
                                      LrgParticleEvent   event,
                                      const gfloat      *point,
                                      const gfloat      *normal);

/**
 * lrg_particle_system_restart:
 * @self: an #LrgParticleSystem
 *
 * Clears every particle, reseeds the generator, resets the modules and
 * the clock, and plays: the system replays from the start.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_restart (LrgParticleSystem *self);

/**
 * lrg_particle_system_get_time:
 * @self: an #LrgParticleSystem
 *
 * Returns: seconds simulated since the system started playing
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_system_get_time (LrgParticleSystem *self);

/**
 * lrg_particle_system_is_emitting:
 * @self: an #LrgParticleSystem
 *
 * Returns: %FALSE once a non-looping system's duration has passed (its
 *   particles may still be alive)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_system_is_emitting (LrgParticleSystem *self);

/**
 * lrg_particle_system_get_start_delay:
 * @self: an #LrgParticleSystem
 *
 * Returns: seconds after play before the module stack emits
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_particle_system_get_start_delay (LrgParticleSystem *self);

/**
 * lrg_particle_system_set_start_delay:
 * @self: an #LrgParticleSystem
 * @delay: seconds (>= 0)
 *
 * Sets #LrgParticleSystem:start-delay.  The duration and the cycles of
 * the module stack count from the end of the delay.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_set_start_delay (LrgParticleSystem *self,
                                          gfloat             delay);

/**
 * lrg_particle_system_get_rotation:
 * @self: an #LrgParticleSystem
 * @out: (out caller-allocates) (array fixed-size=4): the rotation
 *   quaternion (x, y, z, w)
 *
 * Reads the rotation set by lrg_particle_system_set_rotation().
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_get_rotation (LrgParticleSystem *self,
                                       gfloat            *out);

/**
 * lrg_particle_system_set_rotation:
 * @self: an #LrgParticleSystem
 * @rotation: (array fixed-size=4): a unit quaternion (x, y, z, w)
 *
 * Orients the module stack's shapes and directions (a cone points along
 * the rotated +Y axis).  Identity by default.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_set_rotation (LrgParticleSystem *self,
                                       const gfloat      *rotation);

/**
 * lrg_particle_system_get_local_space:
 * @self: an #LrgParticleSystem
 *
 * Returns: whether live particles follow the system's movement
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_system_get_local_space (LrgParticleSystem *self);

/**
 * lrg_particle_system_set_local_space:
 * @self: an #LrgParticleSystem
 * @local_space: %TRUE to move live particles with the system
 *
 * Sets #LrgParticleSystem:local-space: when set, moving the system with
 * lrg_particle_system_set_position() translates every live particle by
 * the same offset (a torch flame stays on the torch); when unset
 * (world space, the default) particles stay where they were emitted.
 */
LRG_AVAILABLE_IN_ALL
void lrg_particle_system_set_local_space (LrgParticleSystem *self,
                                          gboolean           local_space);

/**
 * lrg_particle_system_get_bounds:
 * @self: an #LrgParticleSystem
 * @out_min: (out caller-allocates) (array fixed-size=3): smallest corner
 * @out_max: (out caller-allocates) (array fixed-size=3): largest corner
 *
 * The axis-aligned box around every live particle, grown by each
 * particle's half size.
 *
 * Returns: %FALSE (and an empty box at the system position) when no
 *   particle is alive
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_particle_system_get_bounds (LrgParticleSystem *self,
                                         gfloat            *out_min,
                                         gfloat            *out_max);

G_END_DECLS
