/* lrg-dynamics.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDynamicsWorld - bounded 3D rigid motion. Y is up. Contacts are
 * sphere-sphere, sphere-plane, sphyl-sphere, sphyl-plane and
 * sphyl-sphyl. A sphyl is a capsule whose segment axis is the caller's
 * unit vector, not the collision library's local +Y sphyl. A box
 * half-extent or an explicit diagonal sets body-frame inertia and is
 * not a contact shape. A contactless body has mass and a diagonal
 * inertia and generates no contacts. This world does not admit a
 * ragdoll, an Onslaught goal, or an Assault goal, and it does not
 * interpret Karma physics bytes. LrgRigidBody stays 2D.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../animation/lrg-bone-pose.h"

G_BEGIN_DECLS

/**
 * LRG_DYNAMICS_MAX_BODIES:
 *
 * Most bodies one world accepts, including bodies later evicted.
 */
#define LRG_DYNAMICS_MAX_BODIES (16u)

/**
 * LRG_DYNAMICS_MAX_JOINTS:
 *
 * Most joints one world accepts.
 */
#define LRG_DYNAMICS_MAX_JOINTS (16u)

/**
 * LRG_DYNAMICS_MAX_SUBSTEPS:
 *
 * Most fixed substeps one lrg_dynamics_world_step() call will run.
 */
#define LRG_DYNAMICS_MAX_SUBSTEPS (8u)

/**
 * LRG_DYNAMICS_WORLD_BODY:
 *
 * Joint endpoint that means the immovable world frame. The anchor and
 * axis on that side are already in world space.
 */
#define LRG_DYNAMICS_WORLD_BODY (G_MAXUINT)

/**
 * LRG_DYNAMICS_ERROR:
 *
 * Error domain for #LrgDynamicsWorld. Errors carry an #LrgDynamicsError
 * code.
 */
#define LRG_DYNAMICS_ERROR (lrg_dynamics_error_quark ())

/**
 * LrgDynamicsError:
 * @LRG_DYNAMICS_ERROR_INVALID: an argument is non-finite or out of range
 * @LRG_DYNAMICS_ERROR_CAPACITY: the body or joint list is full
 * @LRG_DYNAMICS_ERROR_MISSING: the body id is unknown or was evicted
 * @LRG_DYNAMICS_ERROR_STATE: the body cannot accept that operation
 *
 * Error codes for the %LRG_DYNAMICS_ERROR domain.
 */
typedef enum {
    LRG_DYNAMICS_ERROR_INVALID = 1,
    LRG_DYNAMICS_ERROR_CAPACITY = 2,
    LRG_DYNAMICS_ERROR_MISSING = 3,
    LRG_DYNAMICS_ERROR_STATE = 4
} LrgDynamicsError;

/**
 * LrgDynamicsBodyKind:
 * @LRG_DYNAMICS_BODY_DYNAMIC: force, gravity, and contacts change velocity
 * @LRG_DYNAMICS_BODY_KINEMATIC: velocity is integrated, forces are not
 * @LRG_DYNAMICS_BODY_STATIC: the body does not move
 *
 * How a sphere is integrated. A plane is always static.
 */
typedef enum {
    LRG_DYNAMICS_BODY_DYNAMIC = 1,
    LRG_DYNAMICS_BODY_KINEMATIC = 2,
    LRG_DYNAMICS_BODY_STATIC = 3
} LrgDynamicsBodyKind;

/**
 * LrgDynamicsContactKind:
 * @LRG_DYNAMICS_CONTACT_SPHERE_SPHERE: two sphere surfaces
 * @LRG_DYNAMICS_CONTACT_SPHERE_PLANE: a sphere against an infinite plane
 * @LRG_DYNAMICS_CONTACT_SPHYL_SPHERE: a caller-axis capsule against a sphere
 * @LRG_DYNAMICS_CONTACT_SPHYL_PLANE: a caller-axis capsule against an infinite plane
 * @LRG_DYNAMICS_CONTACT_SPHYL_SPHYL: two caller-axis capsules
 *
 * The contact that was generated. None of these is a box SAT test, and
 * a sphyl contact is not the collision library's local +Y solid.
 */
typedef enum {
    LRG_DYNAMICS_CONTACT_SPHERE_SPHERE = 1,
    LRG_DYNAMICS_CONTACT_SPHERE_PLANE = 2,
    LRG_DYNAMICS_CONTACT_SPHYL_SPHERE = 3,
    LRG_DYNAMICS_CONTACT_SPHYL_PLANE = 4,
    LRG_DYNAMICS_CONTACT_SPHYL_SPHYL = 5
} LrgDynamicsContactKind;

LRG_AVAILABLE_IN_ALL
GQuark lrg_dynamics_error_quark (void);

LRG_AVAILABLE_IN_ALL
GType lrg_dynamics_body_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DYNAMICS_BODY_KIND (lrg_dynamics_body_kind_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_dynamics_contact_kind_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DYNAMICS_CONTACT_KIND (lrg_dynamics_contact_kind_get_type ())

#define LRG_TYPE_DYNAMICS_WORLD (lrg_dynamics_world_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDynamicsWorld, lrg_dynamics_world, LRG, DYNAMICS_WORLD, GObject)

/**
 * lrg_dynamics_world_new:
 *
 * Creates an empty world. Gravity is zero, the fixed step is 1/60 s,
 * at most four substeps run per call, material restitution and friction
 * are zero, and no eviction bounds are set. One thread owns the world.
 *
 * Returns: (transfer full): a new world
 */
LRG_AVAILABLE_IN_ALL
LrgDynamicsWorld *lrg_dynamics_world_new (void);

/**
 * lrg_dynamics_world_set_gravity:
 * @self: a world
 * @gravity: (array fixed-size=3): acceleration added to dynamic bodies
 * @error: (nullable): return location for a #GError
 *
 * Sets the world acceleration. The default is zero, so a caller chooses
 * the up axis. Y-up callers pass a negative Y component.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_set_gravity (LrgDynamicsWorld *self,
                                         const gfloat     *gravity,
                                         GError          **error);

/**
 * lrg_dynamics_world_set_step_limits:
 * @self: a world
 * @fixed_dt: positive substep length, at most 0.1 s
 * @max_substeps: how many substeps one call may run, 1 through
 *   %LRG_DYNAMICS_MAX_SUBSTEPS
 * @error: (nullable): return location for a #GError
 *
 * A later step that still has at least one full substep after this cap
 * drops that remainder. The dropped time is reported by
 * lrg_dynamics_world_get_dropped_time() and is not carried forward.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_set_step_limits (LrgDynamicsWorld *self,
                                             gfloat            fixed_dt,
                                             guint             max_substeps,
                                             GError          **error);

/**
 * lrg_dynamics_world_set_material:
 * @self: a world
 * @restitution: bounce in [0, 1]
 * @friction: Coulomb friction, zero or positive
 * @error: (nullable): return location for a #GError
 *
 * Both values apply to every contact. Friction uses the body-frame
 * inertia diagonal.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_set_material (LrgDynamicsWorld *self,
                                          gfloat            restitution,
                                          gfloat            friction,
                                          GError          **error);

/**
 * lrg_dynamics_world_set_iterations:
 * @self: a world
 * @iterations: solver passes per substep, 1 through 32
 *
 * Each pass solves contact and joint velocity limits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dynamics_world_set_iterations (LrgDynamicsWorld *self,
                                        guint             iterations);

/**
 * lrg_dynamics_world_set_sleep:
 * @self: a world
 * @speed_squared: a dynamic body quieter than this |v|²+|ω|² can sleep
 * @quiet_substeps: consecutive quiet substeps required, at least 1
 * @error: (nullable): return location for a #GError
 *
 * Sleep skips integration until a force, a torque, a velocity write, or
 * a contact impulse wakes the body. Freeze is separate and does not wake.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_set_sleep (LrgDynamicsWorld *self,
                                       gfloat            speed_squared,
                                       guint             quiet_substeps,
                                       GError          **error);

/**
 * lrg_dynamics_world_set_bounds:
 * @self: a world
 * @minimum: (array fixed-size=3): inclusive lower corner
 * @maximum: (array fixed-size=3): inclusive upper corner
 * @error: (nullable): return location for a #GError
 *
 * After each substep a dynamic sphere, sphyl, or contactless body whose
 * center is outside this box is evicted. Static, kinematic, and plane
 * bodies stay. Passing the same corner on every axis is rejected; the
 * box must have positive extent on each axis.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_set_bounds (LrgDynamicsWorld *self,
                                        const gfloat     *minimum,
                                        const gfloat     *maximum,
                                        GError          **error);

/**
 * lrg_dynamics_world_add_sphere:
 * @self: a world
 * @kind: dynamic, kinematic, or static
 * @mass: positive finite mass for a dynamic body; zero is allowed for
 *   the other kinds
 * @radius: positive finite contact radius
 * @position: (array fixed-size=3): world center
 * @orientation: (array fixed-size=4) (nullable): xyzw quaternion, or
 *   %NULL for identity. A non-zero quaternion is normalised
 * @body_id: (out): id of the new body
 * @error: (nullable): return location for a #GError
 *
 * The contact shape is the sphere. The initial inertia is the solid
 * sphere tensor (2/5)mr². lrg_dynamics_world_set_box_inertia() replaces
 * that tensor and does not change the sphere.
 *
 * Returns: %TRUE on success. A failure leaves the world unchanged.
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_add_sphere (LrgDynamicsWorld     *self,
                                        LrgDynamicsBodyKind   kind,
                                        gfloat                mass,
                                        gfloat                radius,
                                        const gfloat         *position,
                                        const gfloat         *orientation,
                                        guint                *body_id,
                                        GError              **error);

/**
 * lrg_dynamics_world_add_plane:
 * @self: a world
 * @normal: (array fixed-size=3): unit normal. The sphere stays on the
 *   positive side, where dot(normal, point) is at least @offset
 * @offset: plane offset
 * @body_id: (out): id of the new static plane
 * @error: (nullable): return location for a #GError
 *
 * The normal must already have length 1 within 1e-4. A zero or
 * non-unit normal is an error, not a guessed repair.
 *
 * Returns: %TRUE on success. A failure leaves the world unchanged.
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_add_plane (LrgDynamicsWorld *self,
                                       const gfloat     *normal,
                                       gfloat            offset,
                                       guint            *body_id,
                                       GError          **error);

/**
 * lrg_dynamics_world_add_sphyl:
 * @self: a world
 * @kind: dynamic, kinematic, or static
 * @mass: positive finite mass for a dynamic body; zero is allowed for
 *   the other kinds
 * @radius: positive finite capsule radius
 * @half_length: finite half-length of the segment, zero or positive.
 *   The segment runs from -axis*half_length to +axis*half_length
 * @axis: (array fixed-size=3): body-local unit segment direction. The
 *   length must already be 1 within 1e-4. This is not the collision
 *   library's local +Y axis and it is not read from a Karma asset
 * @position: (array fixed-size=3): world center of the segment
 * @orientation: (array fixed-size=4) (nullable): xyzw quaternion, or
 *   %NULL for identity. A non-zero quaternion is normalised. The world
 *   segment direction is this orientation applied to @axis
 * @body_id: (out): id of the new body
 * @error: (nullable): return location for a #GError
 *
 * The contact shape is the capsule. The initial dynamic inertia is the
 * isotropic solid-sphere tensor (2/5)mr².
 * lrg_dynamics_world_set_inertia_diagonal() replaces that tensor and
 * does not change the capsule. A failure leaves the world unchanged.
 * This call does not admit a ragdoll.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_add_sphyl (LrgDynamicsWorld    *self,
                                       LrgDynamicsBodyKind  kind,
                                       gfloat               mass,
                                       gfloat               radius,
                                       gfloat               half_length,
                                       const gfloat        *axis,
                                       const gfloat        *position,
                                       const gfloat        *orientation,
                                       guint               *body_id,
                                       GError             **error);

/**
 * lrg_dynamics_world_add_inertial:
 * @self: a world
 * @mass: positive finite mass
 * @inertia_diagonal: (array fixed-size=3): positive finite body-frame
 *   diagonal. A non-positive component is rejected. No off-diagonal
 *   coupling is stored
 * @position: (array fixed-size=3): world center
 * @orientation: (array fixed-size=4) (nullable): xyzw quaternion, or
 *   %NULL for identity
 * @body_id: (out): id of the new body
 * @error: (nullable): return location for a #GError
 *
 * Adds a dynamic body with no contact shape. No radius is invented.
 * Overlap with a sphere, a sphyl, or a plane does not generate a
 * contact. This is not a ragdoll.
 *
 * Returns: %TRUE on success. A failure leaves the world unchanged.
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_add_inertial (LrgDynamicsWorld *self,
                                          gfloat            mass,
                                          const gfloat     *inertia_diagonal,
                                          const gfloat     *position,
                                          const gfloat     *orientation,
                                          guint            *body_id,
                                          GError          **error);

/**
 * lrg_dynamics_world_set_box_inertia:
 * @self: a world
 * @body_id: a dynamic sphere
 * @half_x: positive finite half extent
 * @half_y: positive finite half extent
 * @half_z: positive finite half extent
 * @error: (nullable): return location for a #GError
 *
 * Stores the diagonal body-frame inertia of a solid box,
 * m/12 * (other two half extents squared and summed). The contact
 * shape stays the sphere that was added.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_set_box_inertia (LrgDynamicsWorld *self,
                                             guint             body_id,
                                             gfloat            half_x,
                                             gfloat            half_y,
                                             gfloat            half_z,
                                             GError          **error);

/**
 * lrg_dynamics_world_set_inertia_diagonal:
 * @self: a world
 * @body_id: a dynamic sphere, sphyl, or contactless body
 * @ixx: positive finite body-frame Ixx
 * @iyy: positive finite body-frame Iyy
 * @izz: positive finite body-frame Izz
 * @error: (nullable): return location for a #GError
 *
 * Replaces the body-frame diagonal. Off-diagonal products are not
 * stored; a non-positive component is an error and leaves the previous
 * diagonal unchanged. The contact shape is unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_set_inertia_diagonal (LrgDynamicsWorld *self,
                                                  guint             body_id,
                                                  gfloat            ixx,
                                                  gfloat            iyy,
                                                  gfloat            izz,
                                                  GError          **error);

/**
 * lrg_dynamics_world_get_inertia:
 * @self: a world
 * @body_id: a sphere
 * @inertia: (out caller-allocates) (array fixed-size=3): body-frame
 *   diagonal
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_get_inertia (LrgDynamicsWorld *self,
                                         guint             body_id,
                                         gfloat           *inertia,
                                         GError          **error);

/**
 * lrg_dynamics_world_set_pose:
 * @self: a world
 * @body_id: a sphere
 * @position: (array fixed-size=3): world center
 * @orientation: (array fixed-size=4): xyzw quaternion
 * @error: (nullable): return location for a #GError
 *
 * Teleports the sphere and wakes it when it is not frozen.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_set_pose (LrgDynamicsWorld *self,
                                      guint             body_id,
                                      const gfloat     *position,
                                      const gfloat     *orientation,
                                      GError          **error);

/**
 * lrg_dynamics_world_get_position:
 * @self: a world
 * @body_id: a sphere, including an evicted one
 * @position: (out caller-allocates) (array fixed-size=3): world center
 * @error: (nullable): return location for a #GError
 *
 * An evicted sphere keeps the pose it had when it left the bounds.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_get_position (LrgDynamicsWorld *self,
                                          guint             body_id,
                                          gfloat           *position,
                                          GError          **error);

/**
 * lrg_dynamics_world_get_orientation:
 * @self: a world
 * @body_id: a sphere
 * @orientation: (out caller-allocates) (array fixed-size=4): xyzw
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_get_orientation (LrgDynamicsWorld *self,
                                             guint             body_id,
                                             gfloat           *orientation,
                                             GError          **error);

/**
 * lrg_dynamics_world_get_linear_velocity:
 * @self: a world
 * @body_id: a sphere
 * @velocity: (out caller-allocates) (array fixed-size=3): world velocity
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_get_linear_velocity (LrgDynamicsWorld *self,
                                                 guint             body_id,
                                                 gfloat           *velocity,
                                                 GError          **error);

/**
 * lrg_dynamics_world_get_angular_velocity:
 * @self: a world
 * @body_id: a sphere
 * @velocity: (out caller-allocates) (array fixed-size=3): world angular
 *   velocity
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_get_angular_velocity (LrgDynamicsWorld *self,
                                                  guint             body_id,
                                                  gfloat           *velocity,
                                                  GError          **error);

/**
 * lrg_dynamics_world_set_linear_velocity:
 * @self: a world
 * @body_id: a dynamic or kinematic sphere that is not frozen
 * @velocity: (array fixed-size=3): world velocity
 * @error: (nullable): return location for a #GError
 *
 * Wakes a sleeping dynamic body.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_set_linear_velocity (LrgDynamicsWorld *self,
                                                 guint             body_id,
                                                 const gfloat     *velocity,
                                                 GError          **error);

/**
 * lrg_dynamics_world_apply_force:
 * @self: a world
 * @body_id: a dynamic sphere that is not frozen
 * @force: (array fixed-size=3): world-space force added this substep
 * @error: (nullable): return location for a #GError
 *
 * Wakes a sleeping body. The force is consumed by the next substep.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_apply_force (LrgDynamicsWorld *self,
                                         guint             body_id,
                                         const gfloat     *force,
                                         GError          **error);

/**
 * lrg_dynamics_world_apply_torque:
 * @self: a world
 * @body_id: a dynamic sphere that is not frozen
 * @torque: (array fixed-size=3): world-space torque added this substep
 * @error: (nullable): return location for a #GError
 *
 * Wakes a sleeping body. The torque is consumed by the next substep.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_apply_torque (LrgDynamicsWorld *self,
                                          guint             body_id,
                                          const gfloat     *torque,
                                          GError          **error);

/**
 * lrg_dynamics_world_set_frozen:
 * @self: a world
 * @body_id: a sphere
 * @frozen: %TRUE to hold the body, %FALSE to thaw it
 * @error: (nullable): return location for a #GError
 *
 * A frozen body keeps its pose and velocity and ignores gravity, force,
 * torque, and contact impulses. Thaw does not itself wake a sleeper;
 * the next force or velocity write does.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_set_frozen (LrgDynamicsWorld *self,
                                        guint             body_id,
                                        gboolean          frozen,
                                        GError          **error);

/**
 * lrg_dynamics_world_get_sleeping:
 * @self: a world
 * @body_id: a sphere
 * @sleeping: (out): whether integration is skipped for sleep
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_get_sleeping (LrgDynamicsWorld *self,
                                          guint             body_id,
                                          gboolean         *sleeping,
                                          GError          **error);

/**
 * lrg_dynamics_world_body_is_active:
 * @self: a world
 * @body_id: a body id returned by an add call
 * @active: (out): %FALSE after eviction
 * @error: (nullable): return location for a #GError
 *
 * An id that was never issued is %LRG_DYNAMICS_ERROR_MISSING.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_body_is_active (LrgDynamicsWorld *self,
                                            guint             body_id,
                                            gboolean         *active,
                                            GError          **error);

/**
 * lrg_dynamics_world_add_joint:
 * @self: a world
 * @body_a: a sphere
 * @body_b: a sphere, or %LRG_DYNAMICS_WORLD_BODY
 * @anchor_a: (array fixed-size=3): anchor in body A's local frame
 * @axis_a: (array fixed-size=3): joint axis in body A's local frame
 * @anchor_b: (array fixed-size=3): anchor in body B's local frame, or
 *   world space when @body_b is the world
 * @axis_b: (array fixed-size=3): joint axis in that same frame
 * @linear_min: minimum distance between the world anchors
 * @linear_max: maximum distance between the world anchors
 * @angular_min: minimum angle, in radians, between the world axes
 * @angular_max: maximum angle, in radians, between the world axes
 * @joint_id: (out): id of the new joint
 * @error: (nullable): return location for a #GError
 *
 * Axes are normalised when their length is non-zero. Distances must be
 * zero or positive with min at most max. Angles must lie in [0, pi]
 * with min at most max. The same body cannot be both ends.
 *
 * Returns: %TRUE on success. A failure leaves the world unchanged.
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_add_joint (LrgDynamicsWorld *self,
                                       guint             body_a,
                                       guint             body_b,
                                       const gfloat     *anchor_a,
                                       const gfloat     *axis_a,
                                       const gfloat     *anchor_b,
                                       const gfloat     *axis_b,
                                       gfloat            linear_min,
                                       gfloat            linear_max,
                                       gfloat            angular_min,
                                       gfloat            angular_max,
                                       guint            *joint_id,
                                       GError          **error);

/**
 * lrg_dynamics_world_step:
 * @self: a world
 * @dt: non-negative frame time
 * @substeps: (out) (nullable): fixed substeps actually integrated
 * @error: (nullable): return location for a #GError
 *
 * Integrates at most the configured number of fixed substeps. Contacts
 * from the last of those substeps stay readable until the next step.
 * Evictions from the whole call stay readable the same way.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_step (LrgDynamicsWorld *self,
                                  gfloat            dt,
                                  guint            *substeps,
                                  GError          **error);

/**
 * lrg_dynamics_world_get_dropped_time:
 * @self: a world
 *
 * Seconds discarded by the last step because another substep would have
 * passed the cap. Zero when the cap was not hit.
 *
 * Returns: dropped time in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_dynamics_world_get_dropped_time (LrgDynamicsWorld *self);

/**
 * lrg_dynamics_world_get_contact_count:
 * @self: a world
 *
 * Contacts generated by the last substep of the last step.
 *
 * Returns: the count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_dynamics_world_get_contact_count (LrgDynamicsWorld *self);

/**
 * lrg_dynamics_world_get_contact:
 * @self: a world
 * @index: index below the contact count
 * @kind: (out): which pair produced the contact
 * @body_a: (out): sphere id
 * @body_b: (out): the other sphere, or the plane
 * @point: (out caller-allocates) (array fixed-size=3): a point on the
 *   contact
 * @normal: (out caller-allocates) (array fixed-size=3): unit direction
 *   that pushes body A out
 * @depth: (out): penetration along @normal before the solve
 * @error: (nullable): return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_get_contact (LrgDynamicsWorld        *self,
                                         guint                    index,
                                         LrgDynamicsContactKind  *kind,
                                         guint                   *body_a,
                                         guint                   *body_b,
                                         gfloat                  *point,
                                         gfloat                  *normal,
                                         gfloat                  *depth,
                                         GError                 **error);

/**
 * lrg_dynamics_world_get_evicted:
 * @self: a world
 * @ids: (out caller-allocates) (array length=max_ids) (nullable):
 *   storage for body ids
 * @max_ids: capacity of @ids. Zero is allowed when @ids is %NULL
 *
 * Bodies evicted by the last step, in the order they were found.
 *
 * Returns: how many bodies were evicted. When that is larger than
 *   @max_ids, only the first @max_ids ids are written.
 */
LRG_AVAILABLE_IN_ALL
guint lrg_dynamics_world_get_evicted (LrgDynamicsWorld *self,
                                      guint            *ids,
                                      guint             max_ids);

/**
 * lrg_dynamics_world_write_bone_pose:
 * @self: a world
 * @body_id: an active body that is not a plane
 * @parent_world: (nullable): the parent's composed world pose, or %NULL
 *   for a root
 * @local: (inout): local pose. Scale is kept. Position and rotation are
 *   replaced with the body expressed in the parent frame
 * @world_pose: (out caller-allocates): parent composed with @local
 * @error: (nullable): return location for a #GError
 *
 * The composition is lrg_bone_pose_compose(). A root is composed with
 * an identity parent, so its world pose matches the body. A failure
 * leaves @local and @world_pose unchanged.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dynamics_world_write_bone_pose (LrgDynamicsWorld *self,
                                             guint             body_id,
                                             const LrgBonePose *parent_world,
                                             LrgBonePose       *local,
                                             LrgBonePose       *world_pose,
                                             GError           **error);

G_END_DECLS
