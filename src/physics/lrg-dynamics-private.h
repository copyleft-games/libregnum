/* lrg-dynamics-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe.
 *
 * Internal glue between lrg-dynamics.c (bodies, pipeline, queries) and
 * lrg-contact-solver.c (solver bodies, constraints, contact constraint
 * maths). Not installed, not introspected.
 */

#pragma once

#include <glib-object.h>
#include "lrg-contact-solver.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Small maths shared by the solver (column-major 3x3, xyzw quaternions)
 * ========================================================================== */

G_GNUC_INTERNAL void lrg_dyn_quat_rotate (const gfloat *q, const gfloat *v, gfloat *out);
G_GNUC_INTERNAL void lrg_dyn_quat_inv_rotate (const gfloat *q, const gfloat *v, gfloat *out);
G_GNUC_INTERNAL void lrg_dyn_quat_mul (const gfloat *a, const gfloat *b, gfloat *out);
G_GNUC_INTERNAL void lrg_dyn_quat_normalize (gfloat *q);
/* q <- normalize (q + 0.5 * (theta, 0) * q): turn by the rotation vector theta */
G_GNUC_INTERNAL void lrg_dyn_quat_add_rotation (gfloat *q, const gfloat *theta);
G_GNUC_INTERNAL void lrg_dyn_quat_to_mat3 (const gfloat *q, gfloat *m);
G_GNUC_INTERNAL void lrg_dyn_mat3_mul_vec (const gfloat *m, const gfloat *v, gfloat *out);
G_GNUC_INTERNAL void lrg_dyn_mat3_mul (const gfloat *a, const gfloat *b, gfloat *out);
G_GNUC_INTERNAL void lrg_dyn_mat3_transpose (const gfloat *m, gfloat *out);
G_GNUC_INTERNAL gboolean lrg_dyn_mat3_invert (const gfloat *m, gfloat *out);
/* any unit vector perpendicular to the unit vector n (deterministic) */
G_GNUC_INTERNAL void lrg_dyn_perpendicular (const gfloat *n, gfloat *out);

/* Refresh body->inv_inertia = R * inv_inertia_local * R^T. */
G_GNUC_INTERNAL void lrg_dyn_solver_body_update_inertia (LrgSolverBody *body);

/* ==========================================================================
 * Contact solver settings and constraint bookkeeping
 * ========================================================================== */

typedef struct
{
	guint    velocity_iterations;
	guint    position_iterations;
	gfloat   baumgarte;
	gfloat   penetration_slop;
	gfloat   speculative_distance;
	gfloat   restitution_threshold;
	gboolean warm_starting;
	gfloat   max_correction;
} LrgContactSolverSettings;

G_GNUC_INTERNAL const LrgContactSolverSettings *lrg_dyn_contact_solver_settings (LrgContactSolver *self);

G_GNUC_INTERNAL void     lrg_dyn_constraint_attach (LrgPhysicsConstraint *self, guint world_id);
G_GNUC_INTERNAL void     lrg_dyn_constraint_detach (LrgPhysicsConstraint *self);
G_GNUC_INTERNAL gboolean lrg_dyn_constraint_take_dirty (LrgPhysicsConstraint *self);

/* ==========================================================================
 * Contact manifolds (persistent between substeps)
 * ========================================================================== */

#define LRG_DYN_MAX_POINTS (4u)

/* Manifold flags */
#define LRG_DYN_MANIFOLD_SENSOR   (1u << 0)  /* no response, overlap only */
#define LRG_DYN_MANIFOLD_TOUCHING (1u << 1)  /* some point has depth > 0 */
#define LRG_DYN_MANIFOLD_ACTIVE   (1u << 2)  /* solved this substep */

/* One velocity row (Jolt's AxisConstraintPart): the angular Jacobian
 * terms r x d and I^-1 (r x d) of both bodies, cached at setup so an
 * iteration is a few dot products. */
typedef struct
{
	gfloat ra_d[3];
	gfloat ia_ra_d[3];
	gfloat rb_d[3];
	gfloat ib_rb_d[3];
	gfloat mass;
} LrgDynRow;

typedef struct
{
	gfloat  local_a[3];        /* point on A, A's centre-of-mass frame */
	gfloat  local_b[3];        /* point on B, B's centre-of-mass frame */
	gfloat  world_a[3];        /* point on A at generation */
	gfloat  world_b[3];        /* point on B at generation */
	gfloat  depth;             /* > 0 penetration, < 0 speculative gap */
	guint32 feature;

	/* solver state (setup) */
	gfloat  r_a[3];            /* A's point from A's centre of mass */
	gfloat  r_b[3];            /* B's point from B's centre of mass */
	LrgDynRow normal_row;
	gfloat  target_velocity;   /* the normal row's bias */
	gfloat  friction_lever;    /* distance to the friction centre */

	/* accumulated impulse (warm started) */
	gfloat  normal_impulse;
} LrgDynContactPoint;

typedef struct
{
	guint64             key;       /* (min id << 32) | max id */
	guint               a;         /* body slot of A */
	guint               b;         /* body slot of B */
	LrgSolverBody      *body_a;
	LrgSolverBody      *body_b;
	gfloat              normal[3]; /* world, A -> B */
	gfloat              tangent[2][3];
	gfloat              friction;
	gfloat              restitution;
	guint               n_points;
	guint               flags;
	gint                kind;      /* LrgDynamicsContactKind */
	guint               sub_shape_a;
	guint               sub_shape_b;
	gfloat              closing_speed; /* approach speed at generation */

	/* Body pair cache (Jolt): B's pose in A's frame when the manifold
	 * was generated, and the normal in A's frame. While the relative
	 * pose barely changes the manifold is refreshed instead of rebuilt. */
	gboolean            cacheable;
	gfloat              cache_rel_pos[3];
	gfloat              cache_rel_rot[4];
	gfloat              normal_local_a[3];

	/* Manifold friction (Jolt): two tangent rows at each body's friction
	 * point (the average of its contact points) and a twist row about
	 * the normal, bounded by mu * the summed normal impulses. */
	gfloat              friction_r_a[3];
	gfloat              friction_r_b[3];
	LrgDynRow           tangent_row[2];
	gfloat              twist_mass;
	gfloat              tangent_impulse[2];
	gfloat              twist_impulse;
	LrgDynContactPoint  points[LRG_DYN_MAX_POINTS];
} LrgDynManifold;

/* Velocity-level setup of one manifold for a substep of @dt. */
G_GNUC_INTERNAL void     lrg_dyn_manifold_setup (LrgDynManifold                 *m,
                                                 const LrgContactSolverSettings *settings,
                                                 gfloat                          dt);
G_GNUC_INTERNAL void     lrg_dyn_manifold_warm_start (LrgDynManifold *m,
                                                      gfloat          ratio);
G_GNUC_INTERNAL gboolean lrg_dyn_manifold_solve_velocity (LrgDynManifold *m,
                                                          gboolean        reverse);
G_GNUC_INTERNAL gboolean lrg_dyn_manifold_solve_position (LrgDynManifold                 *m,
                                                          const LrgContactSolverSettings *settings);
/* Copy impulses from @old into matching points of @m (feature id or
 * local proximity); returns how many points were matched. */
G_GNUC_INTERNAL guint    lrg_dyn_manifold_match (LrgDynManifold       *m,
                                                 const LrgDynManifold *old);

G_END_DECLS
