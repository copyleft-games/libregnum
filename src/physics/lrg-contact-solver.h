/* lrg-contact-solver.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe.
 *
 * The sequential-impulse solver contract of #LrgDynamicsWorld:
 *
 *  - #LrgSolverBody, the per-substep view of one body the solver works
 *    on (centre-of-mass pose, velocities, inverse mass and world inverse
 *    inertia) with the impulse helpers a constraint needs;
 *  - #LrgPhysicsConstraint, the abstract base class every joint derives
 *    from (setup, warm_start, solve_velocity, solve_position, post_solve,
 *    reset_warm_start, debug_draw);
 *  - #LrgContactSolver, the tuning knobs of the built-in contact solver
 *    (iterations, Baumgarte, slop, speculative distance, warm starting).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../debug/lrg-debug-draw.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgSolverBody
 * ========================================================================== */

/**
 * LrgSolverBody:
 * @id: the body id in its #LrgDynamicsWorld, or %G_MAXUINT for the
 *   immovable world frame
 * @position: world position of the centre of mass
 * @orientation: world orientation, xyzw quaternion
 * @linear_velocity: world linear velocity of the centre of mass
 * @angular_velocity: world angular velocity
 * @inv_mass: inverse mass; zero for static, kinematic, frozen and world
 *   bodies, which impulses do not move
 * @inv_inertia: world-space inverse inertia tensor, column-major 3x3;
 *   zero for bodies impulses do not move
 * @com_local: the centre of mass in the body's local frame. A local
 *   anchor measured from the body origin is first offset by -@com_local
 * @inv_inertia_local: body-frame inverse inertia tensor, column-major
 *   3x3 (used to refresh @inv_inertia when @orientation changes)
 *
 * The view of one body a constraint works on during a substep. Every
 * field is in world space except @com_local and @inv_inertia_local.
 * The pointer is only valid inside a #LrgPhysicsConstraintClass vfunc.
 * Write velocities through lrg_solver_body_apply_impulse() and friends
 * and positions through lrg_solver_body_apply_position_correction() so
 * @inv_inertia stays consistent with @orientation.
 */
typedef struct _LrgSolverBody LrgSolverBody;

struct _LrgSolverBody
{
	guint  id;
	gfloat position[3];
	gfloat orientation[4];
	gfloat linear_velocity[3];
	gfloat angular_velocity[3];
	gfloat inv_mass;
	gfloat inv_inertia[9];
	gfloat com_local[3];
	gfloat inv_inertia_local[9];

	/*< private >*/
	guint  flags;
	guint  island;
	gfloat motion_scale;
	gpointer reserved[2];
};

/**
 * lrg_solver_body_get_type:
 *
 * Returns: the #GType of the boxed #LrgSolverBody
 */
LRG_AVAILABLE_IN_ALL
GType lrg_solver_body_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_SOLVER_BODY (lrg_solver_body_get_type ())

/**
 * lrg_solver_body_copy:
 * @body: a solver body
 *
 * Returns: (transfer full): a heap copy of @body
 */
LRG_AVAILABLE_IN_ALL
LrgSolverBody *lrg_solver_body_copy (const LrgSolverBody *body);

/**
 * lrg_solver_body_free:
 * @body: (nullable): a copy made by lrg_solver_body_copy()
 *
 * Frees a copied solver body.
 */
LRG_AVAILABLE_IN_ALL
void lrg_solver_body_free (LrgSolverBody *body);

/**
 * lrg_solver_body_is_dynamic:
 * @body: a solver body
 *
 * Returns: %TRUE when impulses change @body's velocity (its inverse
 *   mass is positive)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_solver_body_is_dynamic (const LrgSolverBody *body);

/**
 * lrg_solver_body_local_to_world:
 * @body: a solver body
 * @local: (array fixed-size=3): a point in the body's local frame,
 *   measured from the body origin (the pose lrg_dynamics_world_get_position()
 *   reports)
 * @out: (out caller-allocates) (array fixed-size=3): the world point
 *
 * Maps a body-local anchor to world space through the current solver
 * pose (which moves during the position solve).
 */
LRG_AVAILABLE_IN_ALL
void lrg_solver_body_local_to_world (const LrgSolverBody *body,
                                     const gfloat        *local,
                                     gfloat              *out);

/**
 * lrg_solver_body_rotate_to_world:
 * @body: a solver body
 * @local: (array fixed-size=3): a body-local direction
 * @out: (out caller-allocates) (array fixed-size=3): the world direction
 */
LRG_AVAILABLE_IN_ALL
void lrg_solver_body_rotate_to_world (const LrgSolverBody *body,
                                      const gfloat        *local,
                                      gfloat              *out);

/**
 * lrg_solver_body_mul_inv_inertia:
 * @body: a solver body
 * @v: (array fixed-size=3): a world vector (an angular impulse)
 * @out: (out caller-allocates) (array fixed-size=3): I⁻¹ @v in world
 *   space (the angular velocity change)
 */
LRG_AVAILABLE_IN_ALL
void lrg_solver_body_mul_inv_inertia (const LrgSolverBody *body,
                                      const gfloat        *v,
                                      gfloat              *out);

/**
 * lrg_solver_body_get_point_velocity:
 * @body: a solver body
 * @point: (array fixed-size=3): a world point
 * @out: (out caller-allocates) (array fixed-size=3): v + ω × (point − com)
 */
LRG_AVAILABLE_IN_ALL
void lrg_solver_body_get_point_velocity (const LrgSolverBody *body,
                                         const gfloat        *point,
                                         gfloat              *out);

/**
 * lrg_solver_body_apply_impulse:
 * @body: a solver body
 * @impulse: (array fixed-size=3): world linear impulse (N·s)
 * @point: (array fixed-size=3) (nullable): world point it acts at, or
 *   %NULL for the centre of mass
 *
 * Changes the linear and angular velocity. Does nothing to a body whose
 * inverse mass is zero.
 */
LRG_AVAILABLE_IN_ALL
void lrg_solver_body_apply_impulse (LrgSolverBody *body,
                                    const gfloat  *impulse,
                                    const gfloat  *point);

/**
 * lrg_solver_body_apply_angular_impulse:
 * @body: a solver body
 * @impulse: (array fixed-size=3): world angular impulse (N·m·s)
 *
 * ω += I⁻¹ @impulse. Does nothing to a body impulses do not move.
 */
LRG_AVAILABLE_IN_ALL
void lrg_solver_body_apply_angular_impulse (LrgSolverBody *body,
                                            const gfloat  *impulse);

/**
 * lrg_solver_body_get_effective_mass_inv:
 * @body: a solver body
 * @point: (array fixed-size=3): world point
 * @direction: (array fixed-size=3): unit world direction
 *
 * The inverse effective mass of @body along @direction at @point,
 * 1/m + (r × n)·I⁻¹(r × n). Sum it over both bodies of a constraint row
 * to get the row's K.
 *
 * Returns: the inverse effective mass (zero for an immovable body)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_solver_body_get_effective_mass_inv (const LrgSolverBody *body,
                                               const gfloat        *point,
                                               const gfloat        *direction);

/**
 * lrg_solver_body_apply_position_correction:
 * @body: a solver body
 * @linear: (array fixed-size=3) (nullable): a position impulse; the
 *   centre of mass moves by @linear × inverse mass
 * @point: (array fixed-size=3) (nullable): world point @linear acts at
 *   (also turns the body), or %NULL for the centre of mass
 * @angular: (array fixed-size=3) (nullable): an extra angular position
 *   impulse; the body turns by I⁻¹ @angular radians
 *
 * The non-linear Gauss-Seidel step of the position solve: moves and
 * turns the body directly (no velocity change) and refreshes the world
 * inverse inertia. Does nothing to a body impulses do not move.
 */
LRG_AVAILABLE_IN_ALL
void lrg_solver_body_apply_position_correction (LrgSolverBody *body,
                                                const gfloat  *linear,
                                                const gfloat  *point,
                                                const gfloat  *angular);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSolverBody, lrg_solver_body_free)

/* ==========================================================================
 * LrgPhysicsConstraint
 * ========================================================================== */

#define LRG_TYPE_PHYSICS_CONSTRAINT (lrg_physics_constraint_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgPhysicsConstraint, lrg_physics_constraint, LRG, PHYSICS_CONSTRAINT, GObject)

/**
 * LrgPhysicsConstraintClass:
 * @parent_class: the parent class
 * @setup: called once per substep before warm starting, with the
 *   substep length. Compute world anchors, axes and effective masses
 *   here. Default: nothing
 * @warm_start: apply the accumulated impulses of the last substep scaled
 *   by @ratio (the substep length ratio, 1 with a fixed step).
 *   Default: nothing
 * @solve_velocity: one velocity iteration. Return %TRUE when an impulse
 *   was applied. Default: %FALSE
 * @solve_position: one position iteration (non-linear Gauss-Seidel):
 *   move the bodies with lrg_solver_body_apply_position_correction()
 *   by @baumgarte times the remaining error. Return %TRUE when a
 *   correction was applied. Default: %FALSE
 * @post_solve: called once after all iterations of a substep; read the
 *   accumulated impulses here (breakable joints, motor feedback).
 *   Default: nothing
 * @reset_warm_start: forget the accumulated impulses (the world calls it
 *   when the constraint is (re)added, re-enabled or a body teleports).
 *   Default: nothing
 * @debug_draw: draw the constraint (anchors, axes, limits). Default:
 *   a line between the two world anchors' centres of mass
 *
 * The solver contract every joint implements. Each vfunc receives the
 * two #LrgSolverBody views (never %NULL: the world frame is a static
 * solver body with id %G_MAXUINT). The world runs, per substep:
 * setup → warm_start → N × solve_velocity → integrate →
 * M × solve_position → post_solve, constraints in insertion order
 * after the contacts of the same island.
 */
struct _LrgPhysicsConstraintClass
{
	GObjectClass parent_class;

	void     (*setup)            (LrgPhysicsConstraint *self,
	                              LrgSolverBody        *a,
	                              LrgSolverBody        *b,
	                              gfloat                dt);
	void     (*warm_start)       (LrgPhysicsConstraint *self,
	                              LrgSolverBody        *a,
	                              LrgSolverBody        *b,
	                              gfloat                ratio);
	gboolean (*solve_velocity)   (LrgPhysicsConstraint *self,
	                              LrgSolverBody        *a,
	                              LrgSolverBody        *b,
	                              gfloat                dt);
	gboolean (*solve_position)   (LrgPhysicsConstraint *self,
	                              LrgSolverBody        *a,
	                              LrgSolverBody        *b,
	                              gfloat                dt,
	                              gfloat                baumgarte);
	void     (*post_solve)       (LrgPhysicsConstraint *self,
	                              LrgSolverBody        *a,
	                              LrgSolverBody        *b,
	                              gfloat                dt);
	void     (*reset_warm_start) (LrgPhysicsConstraint *self);
	void     (*debug_draw)       (LrgPhysicsConstraint *self,
	                              const LrgSolverBody  *a,
	                              const LrgSolverBody  *b,
	                              LrgDebugDraw         *draw);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * lrg_physics_constraint_get_body_a:
 * @self: a constraint
 *
 * Returns: the first body id (#LrgPhysicsConstraint:body-a)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_physics_constraint_get_body_a (LrgPhysicsConstraint *self);

/**
 * lrg_physics_constraint_get_body_b:
 * @self: a constraint
 *
 * Returns: the second body id, or %G_MAXUINT for the world frame
 *   (#LrgPhysicsConstraint:body-b)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_physics_constraint_get_body_b (LrgPhysicsConstraint *self);

/**
 * lrg_physics_constraint_set_bodies:
 * @self: a constraint that is not in a world
 * @body_a: first body id
 * @body_b: second body id, or %G_MAXUINT for the world frame
 *
 * Sets both ends. Rejected (with a warning) while the constraint is in
 * a world: remove it, change the bodies, and add it again.
 *
 * Returns: %TRUE when the bodies were changed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_constraint_set_bodies (LrgPhysicsConstraint *self,
                                            guint                 body_a,
                                            guint                 body_b);

/**
 * lrg_physics_constraint_get_enabled:
 * @self: a constraint
 *
 * Returns: whether the solver runs it (#LrgPhysicsConstraint:enabled)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_constraint_get_enabled (LrgPhysicsConstraint *self);

/**
 * lrg_physics_constraint_set_enabled:
 * @self: a constraint
 * @enabled: whether the solver runs it
 *
 * A disabled constraint is skipped and does not link islands.
 * Re-enabling resets its warm start and wakes its bodies on the next
 * step.
 */
LRG_AVAILABLE_IN_ALL
void lrg_physics_constraint_set_enabled (LrgPhysicsConstraint *self,
                                         gboolean              enabled);

/**
 * lrg_physics_constraint_get_in_world:
 * @self: a constraint
 *
 * Returns: %TRUE while the constraint belongs to a world
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_physics_constraint_get_in_world (LrgPhysicsConstraint *self);

/**
 * lrg_physics_constraint_get_id:
 * @self: a constraint
 *
 * Returns: the id lrg_dynamics_world_add_constraint() gave it, or
 *   %G_MAXUINT when it is not in a world
 */
LRG_AVAILABLE_IN_ALL
guint lrg_physics_constraint_get_id (LrgPhysicsConstraint *self);

/* ==========================================================================
 * LrgContactSolver
 * ========================================================================== */

#define LRG_TYPE_CONTACT_SOLVER (lrg_contact_solver_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgContactSolver, lrg_contact_solver, LRG, CONTACT_SOLVER, GObject)

/**
 * lrg_contact_solver_new:
 *
 * A settings object with the defaults: 8 velocity iterations, 2 position
 * iterations, Baumgarte 0.2, penetration slop 0.02 m, speculative
 * distance 0.02 m, restitution threshold 1 m/s, warm starting on,
 * maximum position correction 0.2 m per iteration. Each
 * #LrgDynamicsWorld owns one (lrg_dynamics_world_get_contact_solver()).
 *
 * Returns: (transfer full): a new #LrgContactSolver
 */
LRG_AVAILABLE_IN_ALL
LrgContactSolver *lrg_contact_solver_new (void);

/**
 * lrg_contact_solver_get_velocity_iterations:
 * @self: a contact solver
 *
 * Returns: velocity iterations per substep (#LrgContactSolver:velocity-iterations)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_contact_solver_get_velocity_iterations (LrgContactSolver *self);

/**
 * lrg_contact_solver_set_velocity_iterations:
 * @self: a contact solver
 * @iterations: 1 through 64
 */
LRG_AVAILABLE_IN_ALL
void lrg_contact_solver_set_velocity_iterations (LrgContactSolver *self,
                                                 guint             iterations);

/**
 * lrg_contact_solver_get_position_iterations:
 * @self: a contact solver
 *
 * Returns: position iterations per substep (#LrgContactSolver:position-iterations)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_contact_solver_get_position_iterations (LrgContactSolver *self);

/**
 * lrg_contact_solver_set_position_iterations:
 * @self: a contact solver
 * @iterations: 0 through 64 (0 turns position correction off)
 */
LRG_AVAILABLE_IN_ALL
void lrg_contact_solver_set_position_iterations (LrgContactSolver *self,
                                                 guint             iterations);

/**
 * lrg_contact_solver_get_baumgarte:
 * @self: a contact solver
 *
 * Returns: the fraction of the remaining penetration removed per
 *   position iteration (#LrgContactSolver:baumgarte)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_contact_solver_get_baumgarte (LrgContactSolver *self);

/**
 * lrg_contact_solver_set_baumgarte:
 * @self: a contact solver
 * @baumgarte: in [0, 1]
 */
LRG_AVAILABLE_IN_ALL
void lrg_contact_solver_set_baumgarte (LrgContactSolver *self,
                                       gfloat            baumgarte);

/**
 * lrg_contact_solver_get_penetration_slop:
 * @self: a contact solver
 *
 * Returns: penetration (m) the position solve leaves alone, which keeps
 *   resting contacts touching (#LrgContactSolver:penetration-slop)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_contact_solver_get_penetration_slop (LrgContactSolver *self);

/**
 * lrg_contact_solver_set_penetration_slop:
 * @self: a contact solver
 * @slop: zero or positive metres
 */
LRG_AVAILABLE_IN_ALL
void lrg_contact_solver_set_penetration_slop (LrgContactSolver *self,
                                              gfloat            slop);

/**
 * lrg_contact_solver_get_speculative_distance:
 * @self: a contact solver
 *
 * Returns: the gap (m) below which separated shapes already get a
 *   contact constraint that only stops them closing faster than the gap
 *   allows (#LrgContactSolver:speculative-distance)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_contact_solver_get_speculative_distance (LrgContactSolver *self);

/**
 * lrg_contact_solver_set_speculative_distance:
 * @self: a contact solver
 * @distance: zero or positive metres
 */
LRG_AVAILABLE_IN_ALL
void lrg_contact_solver_set_speculative_distance (LrgContactSolver *self,
                                                  gfloat            distance);

/**
 * lrg_contact_solver_get_restitution_threshold:
 * @self: a contact solver
 *
 * Returns: closing speed (m/s) below which a contact does not bounce
 *   (#LrgContactSolver:restitution-threshold)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_contact_solver_get_restitution_threshold (LrgContactSolver *self);

/**
 * lrg_contact_solver_set_restitution_threshold:
 * @self: a contact solver
 * @speed: zero or positive m/s
 */
LRG_AVAILABLE_IN_ALL
void lrg_contact_solver_set_restitution_threshold (LrgContactSolver *self,
                                                   gfloat            speed);

/**
 * lrg_contact_solver_get_warm_starting:
 * @self: a contact solver
 *
 * Returns: whether persistent contacts and constraints start from last
 *   substep's impulses (#LrgContactSolver:warm-starting)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_contact_solver_get_warm_starting (LrgContactSolver *self);

/**
 * lrg_contact_solver_set_warm_starting:
 * @self: a contact solver
 * @enabled: warm start or not
 */
LRG_AVAILABLE_IN_ALL
void lrg_contact_solver_set_warm_starting (LrgContactSolver *self,
                                           gboolean          enabled);

/**
 * lrg_contact_solver_get_max_correction:
 * @self: a contact solver
 *
 * Returns: the largest position correction (m) one contact applies per
 *   position iteration (#LrgContactSolver:max-correction)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_contact_solver_get_max_correction (LrgContactSolver *self);

/**
 * lrg_contact_solver_set_max_correction:
 * @self: a contact solver
 * @distance: positive metres
 */
LRG_AVAILABLE_IN_ALL
void lrg_contact_solver_set_max_correction (LrgContactSolver *self,
                                            gfloat            distance);

G_END_DECLS
