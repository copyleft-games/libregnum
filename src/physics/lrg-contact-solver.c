/* lrg-contact-solver.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe.
 * (The solver body impulse and non-linear Gauss-Seidel position update
 * follow Jolt's MotionProperties / AxisConstraintPart.)
 *
 * LrgSolverBody helpers, the abstract LrgPhysicsConstraint base class and
 * the LrgContactSolver settings object. The contact constraint itself
 * lives in lrg-dynamics.c next to the contact cache it reads.
 */

#include "config.h"

#include <float.h>
#include <math.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-contact-solver.h"
#include "physics/lrg-dynamics-private.h"

/* ==========================================================================
 * Small vector helpers
 * ========================================================================== */

/* r = a x b */
static void
cs_cross (const gfloat *a,
          const gfloat *b,
          gfloat       *r)
{
	gfloat x;
	gfloat y;
	gfloat z;

	x = a[1] * b[2] - a[2] * b[1];
	y = a[2] * b[0] - a[0] * b[2];
	z = a[0] * b[1] - a[1] * b[0];
	r[0] = x;
	r[1] = y;
	r[2] = z;
}

/* ==========================================================================
 * LrgSolverBody
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgSolverBody, lrg_solver_body, lrg_solver_body_copy, lrg_solver_body_free)

LrgSolverBody *
lrg_solver_body_copy (const LrgSolverBody *body)
{
	g_return_val_if_fail (body != NULL, NULL);
	return (LrgSolverBody *) g_memdup2 (body, sizeof (LrgSolverBody));
}

void
lrg_solver_body_free (LrgSolverBody *body)
{
	g_free (body);
}

gboolean
lrg_solver_body_is_dynamic (const LrgSolverBody *body)
{
	g_return_val_if_fail (body != NULL, FALSE);
	return body->inv_mass > 0.0f;
}

void
lrg_solver_body_rotate_to_world (const LrgSolverBody *body,
                                 const gfloat        *local,
                                 gfloat              *out)
{
	g_return_if_fail (body != NULL && local != NULL && out != NULL);
	lrg_dyn_quat_rotate (body->orientation, local, out);
}

void
lrg_solver_body_local_to_world (const LrgSolverBody *body,
                                const gfloat        *local,
                                gfloat              *out)
{
	gfloat rel[3];
	gfloat rot[3];

	g_return_if_fail (body != NULL && local != NULL && out != NULL);

	/* The solver pose is the centre of mass; a local anchor is measured
	 * from the body origin, so shift it by the centre-of-mass offset. */
	rel[0] = local[0] - body->com_local[0];
	rel[1] = local[1] - body->com_local[1];
	rel[2] = local[2] - body->com_local[2];
	lrg_dyn_quat_rotate (body->orientation, rel, rot);
	out[0] = body->position[0] + rot[0];
	out[1] = body->position[1] + rot[1];
	out[2] = body->position[2] + rot[2];
}

void
lrg_solver_body_mul_inv_inertia (const LrgSolverBody *body,
                                 const gfloat        *v,
                                 gfloat              *out)
{
	g_return_if_fail (body != NULL && v != NULL && out != NULL);
	lrg_dyn_mat3_mul_vec (body->inv_inertia, v, out);
}

void
lrg_solver_body_get_point_velocity (const LrgSolverBody *body,
                                    const gfloat        *point,
                                    gfloat              *out)
{
	gfloat r[3];
	gfloat t[3];

	g_return_if_fail (body != NULL && point != NULL && out != NULL);
	r[0] = point[0] - body->position[0];
	r[1] = point[1] - body->position[1];
	r[2] = point[2] - body->position[2];
	cs_cross (body->angular_velocity, r, t);
	out[0] = body->linear_velocity[0] + t[0];
	out[1] = body->linear_velocity[1] + t[1];
	out[2] = body->linear_velocity[2] + t[2];
}

void
lrg_solver_body_apply_impulse (LrgSolverBody *body,
                               const gfloat  *impulse,
                               const gfloat  *point)
{
	gfloat r[3];
	gfloat torque[3];

	g_return_if_fail (body != NULL && impulse != NULL);
	if (!(body->inv_mass > 0.0f))
		return;
	body->linear_velocity[0] += impulse[0] * body->inv_mass;
	body->linear_velocity[1] += impulse[1] * body->inv_mass;
	body->linear_velocity[2] += impulse[2] * body->inv_mass;
	if (point == NULL)
		return;
	r[0] = point[0] - body->position[0];
	r[1] = point[1] - body->position[1];
	r[2] = point[2] - body->position[2];
	cs_cross (r, impulse, torque);
	lrg_solver_body_apply_angular_impulse (body, torque);
}

void
lrg_solver_body_apply_angular_impulse (LrgSolverBody *body,
                                       const gfloat  *impulse)
{
	gfloat dw[3];

	g_return_if_fail (body != NULL && impulse != NULL);
	if (!(body->inv_mass > 0.0f))
		return;
	lrg_dyn_mat3_mul_vec (body->inv_inertia, impulse, dw);
	body->angular_velocity[0] += dw[0];
	body->angular_velocity[1] += dw[1];
	body->angular_velocity[2] += dw[2];
}

gfloat
lrg_solver_body_get_effective_mass_inv (const LrgSolverBody *body,
                                        const gfloat        *point,
                                        const gfloat        *direction)
{
	gfloat r[3];
	gfloat rn[3];
	gfloat irn[3];

	g_return_val_if_fail (body != NULL && point != NULL && direction != NULL, 0.0f);
	if (!(body->inv_mass > 0.0f))
		return 0.0f;
	r[0] = point[0] - body->position[0];
	r[1] = point[1] - body->position[1];
	r[2] = point[2] - body->position[2];
	cs_cross (r, direction, rn);
	lrg_dyn_mat3_mul_vec (body->inv_inertia, rn, irn);
	return body->inv_mass + rn[0] * irn[0] + rn[1] * irn[1] + rn[2] * irn[2];
}

void
lrg_solver_body_apply_position_correction (LrgSolverBody *body,
                                           const gfloat  *linear,
                                           const gfloat  *point,
                                           const gfloat  *angular)
{
	gfloat ang[3];
	gfloat r[3];
	gfloat t[3];
	gfloat dtheta[3];

	g_return_if_fail (body != NULL);
	if (!(body->inv_mass > 0.0f))
		return;

	/* Accumulate the angular part: the torque of the linear position
	 * impulse about the centre of mass plus any explicit angular one. */
	ang[0] = 0.0f;
	ang[1] = 0.0f;
	ang[2] = 0.0f;
	if (linear != NULL)
	{
		/* Lever arm from the centre of mass before it moves. */
		if (point != NULL)
		{
			r[0] = point[0] - body->position[0];
			r[1] = point[1] - body->position[1];
			r[2] = point[2] - body->position[2];
			cs_cross (r, linear, t);
			ang[0] += t[0];
			ang[1] += t[1];
			ang[2] += t[2];
		}
		body->position[0] += linear[0] * body->inv_mass;
		body->position[1] += linear[1] * body->inv_mass;
		body->position[2] += linear[2] * body->inv_mass;
	}
	if (angular != NULL)
	{
		ang[0] += angular[0];
		ang[1] += angular[1];
		ang[2] += angular[2];
	}
	if (ang[0] == 0.0f && ang[1] == 0.0f && ang[2] == 0.0f)
		return;

	/* Turn the body by I^-1 * angular (a rotation vector), then refresh
	 * the world inverse inertia for the new orientation. */
	lrg_dyn_mat3_mul_vec (body->inv_inertia, ang, dtheta);
	lrg_dyn_quat_add_rotation (body->orientation, dtheta);
	lrg_dyn_solver_body_update_inertia (body);
}

/* ==========================================================================
 * LrgPhysicsConstraint
 * ========================================================================== */

typedef struct
{
	guint    body_a;
	guint    body_b;
	gboolean enabled;
	guint    world_id;
	gboolean in_world;
	gboolean dirty;
} LrgPhysicsConstraintPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (LrgPhysicsConstraint, lrg_physics_constraint, G_TYPE_OBJECT)

enum
{
	CPROP_0,
	CPROP_BODY_A,
	CPROP_BODY_B,
	CPROP_ENABLED,
	N_CPROPS
};

static GParamSpec *constraint_props[N_CPROPS];

/* Default debug draw: a line between the two centres of mass. */
static void
constraint_default_debug_draw (LrgPhysicsConstraint *self,
                               const LrgSolverBody  *a,
                               const LrgSolverBody  *b,
                               LrgDebugDraw         *draw)
{
	GrlVector3 from;
	GrlVector3 to;
	GrlColor color;

	(void) self;
	from.x = a->position[0];
	from.y = a->position[1];
	from.z = a->position[2];
	to.x = b->position[0];
	to.y = b->position[1];
	to.z = b->position[2];
	color.r = 255;
	color.g = 160;
	color.b = 40;
	color.a = 255;
	lrg_debug_draw_line (draw, &from, &to, &color, 0.0f);
}

static void
lrg_physics_constraint_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
	LrgPhysicsConstraint *self;
	LrgPhysicsConstraintPrivate *priv;

	self = LRG_PHYSICS_CONSTRAINT (object);
	priv = lrg_physics_constraint_get_instance_private (self);
	switch (prop_id)
	{
	case CPROP_BODY_A:
		g_value_set_uint (value, priv->body_a);
		break;
	case CPROP_BODY_B:
		g_value_set_uint (value, priv->body_b);
		break;
	case CPROP_ENABLED:
		g_value_set_boolean (value, priv->enabled);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_physics_constraint_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
	LrgPhysicsConstraint *self;
	LrgPhysicsConstraintPrivate *priv;

	self = LRG_PHYSICS_CONSTRAINT (object);
	priv = lrg_physics_constraint_get_instance_private (self);
	switch (prop_id)
	{
	case CPROP_BODY_A:
		lrg_physics_constraint_set_bodies (self, g_value_get_uint (value), priv->body_b);
		break;
	case CPROP_BODY_B:
		lrg_physics_constraint_set_bodies (self, priv->body_a, g_value_get_uint (value));
		break;
	case CPROP_ENABLED:
		lrg_physics_constraint_set_enabled (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_physics_constraint_class_init (LrgPhysicsConstraintClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = lrg_physics_constraint_get_property;
	object_class->set_property = lrg_physics_constraint_set_property;
	klass->debug_draw = constraint_default_debug_draw;

	/**
	 * LrgPhysicsConstraint:body-a:
	 *
	 * The first body id.
	 */
	constraint_props[CPROP_BODY_A] =
		g_param_spec_uint ("body-a", "Body A", "First body id",
		                   0, G_MAXUINT, 0,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPhysicsConstraint:body-b:
	 *
	 * The second body id, %G_MAXUINT (the default) for the world frame.
	 */
	constraint_props[CPROP_BODY_B] =
		g_param_spec_uint ("body-b", "Body B", "Second body id (G_MAXUINT: world)",
		                   0, G_MAXUINT, G_MAXUINT,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgPhysicsConstraint:enabled:
	 *
	 * Whether the solver runs the constraint.
	 */
	constraint_props[CPROP_ENABLED] =
		g_param_spec_boolean ("enabled", "Enabled", "Whether the solver runs it",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_CPROPS, constraint_props);
}

static void
lrg_physics_constraint_init (LrgPhysicsConstraint *self)
{
	LrgPhysicsConstraintPrivate *priv;

	priv = lrg_physics_constraint_get_instance_private (self);
	priv->body_a = 0;
	priv->body_b = G_MAXUINT;
	priv->enabled = TRUE;
	priv->world_id = G_MAXUINT;
	priv->in_world = FALSE;
	priv->dirty = TRUE;
}

guint
lrg_physics_constraint_get_body_a (LrgPhysicsConstraint *self)
{
	LrgPhysicsConstraintPrivate *priv;

	g_return_val_if_fail (LRG_IS_PHYSICS_CONSTRAINT (self), 0);
	priv = lrg_physics_constraint_get_instance_private (self);
	return priv->body_a;
}

guint
lrg_physics_constraint_get_body_b (LrgPhysicsConstraint *self)
{
	LrgPhysicsConstraintPrivate *priv;

	g_return_val_if_fail (LRG_IS_PHYSICS_CONSTRAINT (self), G_MAXUINT);
	priv = lrg_physics_constraint_get_instance_private (self);
	return priv->body_b;
}

gboolean
lrg_physics_constraint_set_bodies (LrgPhysicsConstraint *self,
                                   guint                 body_a,
                                   guint                 body_b)
{
	LrgPhysicsConstraintPrivate *priv;
	gboolean changed_a;
	gboolean changed_b;

	g_return_val_if_fail (LRG_IS_PHYSICS_CONSTRAINT (self), FALSE);
	priv = lrg_physics_constraint_get_instance_private (self);
	if (priv->in_world)
	{
		g_warning ("lrg_physics_constraint_set_bodies: constraint is in a world; remove it first");
		return FALSE;
	}
	changed_a = (priv->body_a != body_a);
	changed_b = (priv->body_b != body_b);
	priv->body_a = body_a;
	priv->body_b = body_b;
	if (changed_a)
		g_object_notify_by_pspec (G_OBJECT (self), constraint_props[CPROP_BODY_A]);
	if (changed_b)
		g_object_notify_by_pspec (G_OBJECT (self), constraint_props[CPROP_BODY_B]);
	return TRUE;
}

gboolean
lrg_physics_constraint_get_enabled (LrgPhysicsConstraint *self)
{
	LrgPhysicsConstraintPrivate *priv;

	g_return_val_if_fail (LRG_IS_PHYSICS_CONSTRAINT (self), FALSE);
	priv = lrg_physics_constraint_get_instance_private (self);
	return priv->enabled;
}

void
lrg_physics_constraint_set_enabled (LrgPhysicsConstraint *self,
                                    gboolean              enabled)
{
	LrgPhysicsConstraintPrivate *priv;

	g_return_if_fail (LRG_IS_PHYSICS_CONSTRAINT (self));
	priv = lrg_physics_constraint_get_instance_private (self);
	enabled = enabled ? TRUE : FALSE;
	if (priv->enabled == enabled)
		return;
	priv->enabled = enabled;
	/* Re-enabling must not reuse stale accumulated impulses, and the
	 * world wakes the bodies the next time it sees the flag. */
	priv->dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), constraint_props[CPROP_ENABLED]);
}

gboolean
lrg_physics_constraint_get_in_world (LrgPhysicsConstraint *self)
{
	LrgPhysicsConstraintPrivate *priv;

	g_return_val_if_fail (LRG_IS_PHYSICS_CONSTRAINT (self), FALSE);
	priv = lrg_physics_constraint_get_instance_private (self);
	return priv->in_world;
}

guint
lrg_physics_constraint_get_id (LrgPhysicsConstraint *self)
{
	LrgPhysicsConstraintPrivate *priv;

	g_return_val_if_fail (LRG_IS_PHYSICS_CONSTRAINT (self), G_MAXUINT);
	priv = lrg_physics_constraint_get_instance_private (self);
	return priv->in_world ? priv->world_id : G_MAXUINT;
}

/* --- private hooks used by the world (lrg-dynamics-private.h) --- */

void
lrg_dyn_constraint_attach (LrgPhysicsConstraint *self,
                           guint                 world_id)
{
	LrgPhysicsConstraintPrivate *priv;

	priv = lrg_physics_constraint_get_instance_private (self);
	priv->in_world = TRUE;
	priv->world_id = world_id;
	priv->dirty = TRUE;
}

void
lrg_dyn_constraint_detach (LrgPhysicsConstraint *self)
{
	LrgPhysicsConstraintPrivate *priv;

	priv = lrg_physics_constraint_get_instance_private (self);
	priv->in_world = FALSE;
	priv->world_id = G_MAXUINT;
}

gboolean
lrg_dyn_constraint_take_dirty (LrgPhysicsConstraint *self)
{
	LrgPhysicsConstraintPrivate *priv;
	gboolean dirty;

	priv = lrg_physics_constraint_get_instance_private (self);
	dirty = priv->dirty;
	priv->dirty = FALSE;
	return dirty;
}

/* ==========================================================================
 * LrgContactSolver (settings)
 * ========================================================================== */

struct _LrgContactSolver
{
	GObject parent_instance;

	LrgContactSolverSettings settings;
};

G_DEFINE_FINAL_TYPE (LrgContactSolver, lrg_contact_solver, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_VELOCITY_ITERATIONS,
	PROP_POSITION_ITERATIONS,
	PROP_BAUMGARTE,
	PROP_PENETRATION_SLOP,
	PROP_SPECULATIVE_DISTANCE,
	PROP_RESTITUTION_THRESHOLD,
	PROP_WARM_STARTING,
	PROP_MAX_CORRECTION,
	N_PROPS
};

static GParamSpec *solver_props[N_PROPS];

static void
lrg_contact_solver_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgContactSolver *self;

	self = LRG_CONTACT_SOLVER (object);
	switch (prop_id)
	{
	case PROP_VELOCITY_ITERATIONS:
		g_value_set_uint (value, self->settings.velocity_iterations);
		break;
	case PROP_POSITION_ITERATIONS:
		g_value_set_uint (value, self->settings.position_iterations);
		break;
	case PROP_BAUMGARTE:
		g_value_set_float (value, self->settings.baumgarte);
		break;
	case PROP_PENETRATION_SLOP:
		g_value_set_float (value, self->settings.penetration_slop);
		break;
	case PROP_SPECULATIVE_DISTANCE:
		g_value_set_float (value, self->settings.speculative_distance);
		break;
	case PROP_RESTITUTION_THRESHOLD:
		g_value_set_float (value, self->settings.restitution_threshold);
		break;
	case PROP_WARM_STARTING:
		g_value_set_boolean (value, self->settings.warm_starting);
		break;
	case PROP_MAX_CORRECTION:
		g_value_set_float (value, self->settings.max_correction);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_contact_solver_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgContactSolver *self;

	self = LRG_CONTACT_SOLVER (object);
	switch (prop_id)
	{
	case PROP_VELOCITY_ITERATIONS:
		lrg_contact_solver_set_velocity_iterations (self, g_value_get_uint (value));
		break;
	case PROP_POSITION_ITERATIONS:
		lrg_contact_solver_set_position_iterations (self, g_value_get_uint (value));
		break;
	case PROP_BAUMGARTE:
		lrg_contact_solver_set_baumgarte (self, g_value_get_float (value));
		break;
	case PROP_PENETRATION_SLOP:
		lrg_contact_solver_set_penetration_slop (self, g_value_get_float (value));
		break;
	case PROP_SPECULATIVE_DISTANCE:
		lrg_contact_solver_set_speculative_distance (self, g_value_get_float (value));
		break;
	case PROP_RESTITUTION_THRESHOLD:
		lrg_contact_solver_set_restitution_threshold (self, g_value_get_float (value));
		break;
	case PROP_WARM_STARTING:
		lrg_contact_solver_set_warm_starting (self, g_value_get_boolean (value));
		break;
	case PROP_MAX_CORRECTION:
		lrg_contact_solver_set_max_correction (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_contact_solver_class_init (LrgContactSolverClass *klass)
{
	GObjectClass *object_class;
	GParamFlags flags;

	object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = lrg_contact_solver_get_property;
	object_class->set_property = lrg_contact_solver_set_property;
	flags = (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgContactSolver:velocity-iterations:
	 *
	 * Velocity iterations per substep (1 .. 64).
	 */
	solver_props[PROP_VELOCITY_ITERATIONS] =
		g_param_spec_uint ("velocity-iterations", "Velocity iterations", "Velocity passes per substep",
		                   1, 64, 10, flags);

	/**
	 * LrgContactSolver:position-iterations:
	 *
	 * Position (NGS) iterations per substep (0 .. 64).
	 */
	solver_props[PROP_POSITION_ITERATIONS] =
		g_param_spec_uint ("position-iterations", "Position iterations", "Position passes per substep",
		                   0, 64, 2, flags);

	/**
	 * LrgContactSolver:baumgarte:
	 *
	 * Fraction of the remaining error removed per position iteration.
	 */
	solver_props[PROP_BAUMGARTE] =
		g_param_spec_float ("baumgarte", "Baumgarte", "Position correction factor",
		                    0.0f, 1.0f, 0.2f, flags);

	/**
	 * LrgContactSolver:penetration-slop:
	 *
	 * Penetration in metres that is not corrected.
	 */
	solver_props[PROP_PENETRATION_SLOP] =
		g_param_spec_float ("penetration-slop", "Penetration slop", "Allowed penetration (m)",
		                    0.0f, G_MAXFLOAT, 0.02f, flags);

	/**
	 * LrgContactSolver:speculative-distance:
	 *
	 * Gap in metres under which a separated pair already gets a contact.
	 */
	solver_props[PROP_SPECULATIVE_DISTANCE] =
		g_param_spec_float ("speculative-distance", "Speculative distance", "Speculative contact gap (m)",
		                    0.0f, G_MAXFLOAT, 0.02f, flags);

	/**
	 * LrgContactSolver:restitution-threshold:
	 *
	 * Closing speed (m/s) under which contacts do not bounce.
	 */
	solver_props[PROP_RESTITUTION_THRESHOLD] =
		g_param_spec_float ("restitution-threshold", "Restitution threshold", "Minimum bounce speed (m/s)",
		                    0.0f, G_MAXFLOAT, 1.0f, flags);

	/**
	 * LrgContactSolver:warm-starting:
	 *
	 * Whether persistent contacts start from last substep's impulses.
	 */
	solver_props[PROP_WARM_STARTING] =
		g_param_spec_boolean ("warm-starting", "Warm starting", "Reuse last impulses",
		                      TRUE, flags);

	/**
	 * LrgContactSolver:max-correction:
	 *
	 * Largest position correction (m) per contact per position iteration.
	 */
	solver_props[PROP_MAX_CORRECTION] =
		g_param_spec_float ("max-correction", "Max correction", "Largest correction per iteration (m)",
		                    1.0e-4f, G_MAXFLOAT, 0.2f, flags);

	g_object_class_install_properties (object_class, N_PROPS, solver_props);
}

static void
lrg_contact_solver_init (LrgContactSolver *self)
{
	self->settings.velocity_iterations = 10;
	self->settings.position_iterations = 2;
	self->settings.baumgarte = 0.2f;
	self->settings.penetration_slop = 0.02f;
	self->settings.speculative_distance = 0.02f;
	self->settings.restitution_threshold = 1.0f;
	self->settings.warm_starting = TRUE;
	self->settings.max_correction = 0.2f;
}

LrgContactSolver *
lrg_contact_solver_new (void)
{
	return g_object_new (LRG_TYPE_CONTACT_SOLVER, NULL);
}

const LrgContactSolverSettings *
lrg_dyn_contact_solver_settings (LrgContactSolver *self)
{
	return &self->settings;
}

guint
lrg_contact_solver_get_velocity_iterations (LrgContactSolver *self)
{
	g_return_val_if_fail (LRG_IS_CONTACT_SOLVER (self), 0);
	return self->settings.velocity_iterations;
}

void
lrg_contact_solver_set_velocity_iterations (LrgContactSolver *self,
                                            guint             iterations)
{
	g_return_if_fail (LRG_IS_CONTACT_SOLVER (self));
	iterations = CLAMP (iterations, 1u, 64u);
	if (self->settings.velocity_iterations == iterations)
		return;
	self->settings.velocity_iterations = iterations;
	g_object_notify_by_pspec (G_OBJECT (self), solver_props[PROP_VELOCITY_ITERATIONS]);
}

guint
lrg_contact_solver_get_position_iterations (LrgContactSolver *self)
{
	g_return_val_if_fail (LRG_IS_CONTACT_SOLVER (self), 0);
	return self->settings.position_iterations;
}

void
lrg_contact_solver_set_position_iterations (LrgContactSolver *self,
                                            guint             iterations)
{
	g_return_if_fail (LRG_IS_CONTACT_SOLVER (self));
	iterations = MIN (iterations, 64u);
	if (self->settings.position_iterations == iterations)
		return;
	self->settings.position_iterations = iterations;
	g_object_notify_by_pspec (G_OBJECT (self), solver_props[PROP_POSITION_ITERATIONS]);
}

gfloat
lrg_contact_solver_get_baumgarte (LrgContactSolver *self)
{
	g_return_val_if_fail (LRG_IS_CONTACT_SOLVER (self), 0.0f);
	return self->settings.baumgarte;
}

void
lrg_contact_solver_set_baumgarte (LrgContactSolver *self,
                                  gfloat            baumgarte)
{
	g_return_if_fail (LRG_IS_CONTACT_SOLVER (self));
	if (!isfinite ((double) baumgarte))
		return;
	baumgarte = CLAMP (baumgarte, 0.0f, 1.0f);
	if (self->settings.baumgarte == baumgarte)
		return;
	self->settings.baumgarte = baumgarte;
	g_object_notify_by_pspec (G_OBJECT (self), solver_props[PROP_BAUMGARTE]);
}

gfloat
lrg_contact_solver_get_penetration_slop (LrgContactSolver *self)
{
	g_return_val_if_fail (LRG_IS_CONTACT_SOLVER (self), 0.0f);
	return self->settings.penetration_slop;
}

void
lrg_contact_solver_set_penetration_slop (LrgContactSolver *self,
                                         gfloat            slop)
{
	g_return_if_fail (LRG_IS_CONTACT_SOLVER (self));
	if (!isfinite ((double) slop) || slop < 0.0f)
		return;
	if (self->settings.penetration_slop == slop)
		return;
	self->settings.penetration_slop = slop;
	g_object_notify_by_pspec (G_OBJECT (self), solver_props[PROP_PENETRATION_SLOP]);
}

gfloat
lrg_contact_solver_get_speculative_distance (LrgContactSolver *self)
{
	g_return_val_if_fail (LRG_IS_CONTACT_SOLVER (self), 0.0f);
	return self->settings.speculative_distance;
}

void
lrg_contact_solver_set_speculative_distance (LrgContactSolver *self,
                                             gfloat            distance)
{
	g_return_if_fail (LRG_IS_CONTACT_SOLVER (self));
	if (!isfinite ((double) distance) || distance < 0.0f)
		return;
	if (self->settings.speculative_distance == distance)
		return;
	self->settings.speculative_distance = distance;
	g_object_notify_by_pspec (G_OBJECT (self), solver_props[PROP_SPECULATIVE_DISTANCE]);
}

gfloat
lrg_contact_solver_get_restitution_threshold (LrgContactSolver *self)
{
	g_return_val_if_fail (LRG_IS_CONTACT_SOLVER (self), 0.0f);
	return self->settings.restitution_threshold;
}

void
lrg_contact_solver_set_restitution_threshold (LrgContactSolver *self,
                                              gfloat            speed)
{
	g_return_if_fail (LRG_IS_CONTACT_SOLVER (self));
	if (!isfinite ((double) speed) || speed < 0.0f)
		return;
	if (self->settings.restitution_threshold == speed)
		return;
	self->settings.restitution_threshold = speed;
	g_object_notify_by_pspec (G_OBJECT (self), solver_props[PROP_RESTITUTION_THRESHOLD]);
}

gboolean
lrg_contact_solver_get_warm_starting (LrgContactSolver *self)
{
	g_return_val_if_fail (LRG_IS_CONTACT_SOLVER (self), FALSE);
	return self->settings.warm_starting;
}

void
lrg_contact_solver_set_warm_starting (LrgContactSolver *self,
                                      gboolean          enabled)
{
	g_return_if_fail (LRG_IS_CONTACT_SOLVER (self));
	enabled = enabled ? TRUE : FALSE;
	if (self->settings.warm_starting == enabled)
		return;
	self->settings.warm_starting = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), solver_props[PROP_WARM_STARTING]);
}

gfloat
lrg_contact_solver_get_max_correction (LrgContactSolver *self)
{
	g_return_val_if_fail (LRG_IS_CONTACT_SOLVER (self), 0.0f);
	return self->settings.max_correction;
}

void
lrg_contact_solver_set_max_correction (LrgContactSolver *self,
                                       gfloat            distance)
{
	g_return_if_fail (LRG_IS_CONTACT_SOLVER (self));
	if (!isfinite ((double) distance) || distance <= 0.0f)
		return;
	if (self->settings.max_correction == distance)
		return;
	self->settings.max_correction = distance;
	g_object_notify_by_pspec (G_OBJECT (self), solver_props[PROP_MAX_CORRECTION]);
}

/* ==========================================================================
 * Shared maths (lrg-dynamics-private.h)
 * ========================================================================== */

void
lrg_dyn_quat_mul (const gfloat *a,
                  const gfloat *b,
                  gfloat       *out)
{
	gfloat r[4];

	r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	out[0] = r[0];
	out[1] = r[1];
	out[2] = r[2];
	out[3] = r[3];
}

void
lrg_dyn_quat_rotate (const gfloat *q,
                     const gfloat *v,
                     gfloat       *out)
{
	gfloat t[3];
	gfloat r[3];

	/* v' = v + w*t + q.xyz x t, with t = 2 * (q.xyz x v) */
	t[0] = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	t[1] = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	t[2] = 2.0f * (q[0] * v[1] - q[1] * v[0]);
	r[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
	r[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
	r[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
	out[0] = r[0];
	out[1] = r[1];
	out[2] = r[2];
}

void
lrg_dyn_quat_inv_rotate (const gfloat *q,
                         const gfloat *v,
                         gfloat       *out)
{
	gfloat c[4];

	c[0] = -q[0];
	c[1] = -q[1];
	c[2] = -q[2];
	c[3] = q[3];
	lrg_dyn_quat_rotate (c, v, out);
}

void
lrg_dyn_quat_normalize (gfloat *q)
{
	gfloat len;

	len = sqrtf (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (!(len > 1.0e-12f))
	{
		q[0] = 0.0f;
		q[1] = 0.0f;
		q[2] = 0.0f;
		q[3] = 1.0f;
		return;
	}
	q[0] /= len;
	q[1] /= len;
	q[2] /= len;
	q[3] /= len;
}

void
lrg_dyn_quat_add_rotation (gfloat       *q,
                           const gfloat *theta)
{
	gfloat w[4];
	gfloat d[4];

	w[0] = theta[0];
	w[1] = theta[1];
	w[2] = theta[2];
	w[3] = 0.0f;
	lrg_dyn_quat_mul (w, q, d);
	q[0] += 0.5f * d[0];
	q[1] += 0.5f * d[1];
	q[2] += 0.5f * d[2];
	q[3] += 0.5f * d[3];
	lrg_dyn_quat_normalize (q);
}

void
lrg_dyn_quat_to_mat3 (const gfloat *q,
                      gfloat       *m)
{
	gfloat x;
	gfloat y;
	gfloat z;
	gfloat w;

	x = q[0];
	y = q[1];
	z = q[2];
	w = q[3];
	/* column 0 */
	m[0] = 1.0f - 2.0f * (y * y + z * z);
	m[1] = 2.0f * (x * y + z * w);
	m[2] = 2.0f * (x * z - y * w);
	/* column 1 */
	m[3] = 2.0f * (x * y - z * w);
	m[4] = 1.0f - 2.0f * (x * x + z * z);
	m[5] = 2.0f * (y * z + x * w);
	/* column 2 */
	m[6] = 2.0f * (x * z + y * w);
	m[7] = 2.0f * (y * z - x * w);
	m[8] = 1.0f - 2.0f * (x * x + y * y);
}

void
lrg_dyn_mat3_mul_vec (const gfloat *m,
                      const gfloat *v,
                      gfloat       *out)
{
	gfloat r[3];

	r[0] = m[0] * v[0] + m[3] * v[1] + m[6] * v[2];
	r[1] = m[1] * v[0] + m[4] * v[1] + m[7] * v[2];
	r[2] = m[2] * v[0] + m[5] * v[1] + m[8] * v[2];
	out[0] = r[0];
	out[1] = r[1];
	out[2] = r[2];
}

void
lrg_dyn_mat3_mul (const gfloat *a,
                  const gfloat *b,
                  gfloat       *out)
{
	gfloat r[9];
	guint c;
	guint row;

	for (c = 0; c < 3u; c++)
		for (row = 0; row < 3u; row++)
			r[c * 3u + row] = a[0 * 3u + row] * b[c * 3u + 0] +
			                  a[1 * 3u + row] * b[c * 3u + 1] +
			                  a[2 * 3u + row] * b[c * 3u + 2];
	memcpy (out, r, sizeof (r));
}

void
lrg_dyn_mat3_transpose (const gfloat *m,
                        gfloat       *out)
{
	gfloat r[9];
	guint c;
	guint row;

	for (c = 0; c < 3u; c++)
		for (row = 0; row < 3u; row++)
			r[row * 3u + c] = m[c * 3u + row];
	memcpy (out, r, sizeof (r));
}

gboolean
lrg_dyn_mat3_invert (const gfloat *m,
                     gfloat       *out)
{
	gfloat r[9];
	gfloat det;
	gfloat inv;

	/* cofactors, column-major: m[c*3+r] */
	r[0] = m[4] * m[8] - m[7] * m[5];
	r[1] = m[7] * m[2] - m[1] * m[8];
	r[2] = m[1] * m[5] - m[4] * m[2];
	r[3] = m[6] * m[5] - m[3] * m[8];
	r[4] = m[0] * m[8] - m[6] * m[2];
	r[5] = m[3] * m[2] - m[0] * m[5];
	r[6] = m[3] * m[7] - m[6] * m[4];
	r[7] = m[6] * m[1] - m[0] * m[7];
	r[8] = m[0] * m[4] - m[3] * m[1];
	det = m[0] * r[0] + m[3] * r[1] + m[6] * r[2];
	if (!(fabsf (det) > 1.0e-30f) || !isfinite ((double) det))
		return FALSE;
	inv = 1.0f / det;
	{
		guint i;

		for (i = 0; i < 9u; i++)
			out[i] = r[i] * inv;
	}
	return TRUE;
}

void
lrg_dyn_perpendicular (const gfloat *n,
                       gfloat       *out)
{
	gfloat len;

	/* Jolt's Vec3::GetNormalizedPerpendicular: drop the smallest axis. */
	if (fabsf (n[0]) > fabsf (n[1]))
	{
		len = sqrtf (n[0] * n[0] + n[2] * n[2]);
		out[0] = n[2] / len;
		out[1] = 0.0f;
		out[2] = -n[0] / len;
	}
	else
	{
		len = sqrtf (n[1] * n[1] + n[2] * n[2]);
		out[0] = 0.0f;
		out[1] = n[2] / len;
		out[2] = -n[1] / len;
	}
}

void
lrg_dyn_solver_body_update_inertia (LrgSolverBody *body)
{
	gfloat rot[9];
	gfloat rt[9];
	gfloat tmp[9];

	if (!(body->inv_mass > 0.0f))
	{
		memset (body->inv_inertia, 0, sizeof (body->inv_inertia));
		return;
	}
	lrg_dyn_quat_to_mat3 (body->orientation, rot);
	lrg_dyn_mat3_transpose (rot, rt);
	lrg_dyn_mat3_mul (rot, body->inv_inertia_local, tmp);
	lrg_dyn_mat3_mul (tmp, rt, body->inv_inertia);
}

/* ==========================================================================
 * Contact constraint (sequential impulse, Jolt ContactConstraintManager /
 * AxisConstraintPart style): normal row with restitution and speculative
 * target, two friction rows bounded by mu * normal impulse, NGS position
 * pass.
 * ========================================================================== */

static gfloat
cs_dot (const gfloat *a,
        const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* K = sum over bodies of 1/m + (r x n) . I^-1 (r x n) */
static gfloat
cs_row_mass (const LrgSolverBody *a,
             const LrgSolverBody *b,
             const gfloat        *r_a,
             const gfloat        *r_b,
             const gfloat        *dir)
{
	gfloat rn[3];
	gfloat irn[3];
	gfloat k;

	k = 0.0f;
	if (a->inv_mass > 0.0f)
	{
		cs_cross (r_a, dir, rn);
		lrg_dyn_mat3_mul_vec (a->inv_inertia, rn, irn);
		k += a->inv_mass + cs_dot (rn, irn);
	}
	if (b->inv_mass > 0.0f)
	{
		cs_cross (r_b, dir, rn);
		lrg_dyn_mat3_mul_vec (b->inv_inertia, rn, irn);
		k += b->inv_mass + cs_dot (rn, irn);
	}
	return (k > 1.0e-12f) ? 1.0f / k : 0.0f;
}

/* Twist row mass about the normal: 1 / (n.I1^-1 n + n.I2^-1 n). */
static gfloat
cs_twist_mass (const LrgSolverBody *a,
               const LrgSolverBody *b,
               const gfloat        *n)
{
	gfloat in[3];
	gfloat k;

	k = 0.0f;
	if (a->inv_mass > 0.0f)
	{
		lrg_dyn_mat3_mul_vec (a->inv_inertia, n, in);
		k += cs_dot (n, in);
	}
	if (b->inv_mass > 0.0f)
	{
		lrg_dyn_mat3_mul_vec (b->inv_inertia, n, in);
		k += cs_dot (n, in);
	}
	return (k > 1.0e-12f) ? 1.0f / k : 0.0f;
}

/* Apply an angular impulse lambda * n: -on A, +on B. */
static void
cs_apply_twist (LrgSolverBody *a,
                LrgSolverBody *b,
                const gfloat  *n,
                gfloat         lambda)
{
	gfloat p[3];
	gfloat dw[3];

	p[0] = n[0] * lambda;
	p[1] = n[1] * lambda;
	p[2] = n[2] * lambda;
	if (a->inv_mass > 0.0f)
	{
		lrg_dyn_mat3_mul_vec (a->inv_inertia, p, dw);
		a->angular_velocity[0] -= dw[0];
		a->angular_velocity[1] -= dw[1];
		a->angular_velocity[2] -= dw[2];
	}
	if (b->inv_mass > 0.0f)
	{
		lrg_dyn_mat3_mul_vec (b->inv_inertia, p, dw);
		b->angular_velocity[0] += dw[0];
		b->angular_velocity[1] += dw[1];
		b->angular_velocity[2] += dw[2];
	}
}

/* Cache one row's Jacobian terms (only for bodies impulses move). */
static void
cs_row_setup (const LrgSolverBody *a,
              const LrgSolverBody *b,
              const gfloat        *r_a,
              const gfloat        *r_b,
              const gfloat        *dir,
              LrgDynRow           *row)
{
	gfloat k;

	k = 0.0f;
	memset (row, 0, sizeof (*row));
	if (a->inv_mass > 0.0f)
	{
		cs_cross (r_a, dir, row->ra_d);
		lrg_dyn_mat3_mul_vec (a->inv_inertia, row->ra_d, row->ia_ra_d);
		k += a->inv_mass + cs_dot (row->ra_d, row->ia_ra_d);
	}
	else
	{
		/* Still needed to read a kinematic body's point velocity. */
		cs_cross (r_a, dir, row->ra_d);
	}
	if (b->inv_mass > 0.0f)
	{
		cs_cross (r_b, dir, row->rb_d);
		lrg_dyn_mat3_mul_vec (b->inv_inertia, row->rb_d, row->ib_rb_d);
		k += b->inv_mass + cs_dot (row->rb_d, row->ib_rb_d);
	}
	else
	{
		cs_cross (r_b, dir, row->rb_d);
	}
	row->mass = (k > 1.0e-12f) ? 1.0f / k : 0.0f;
}

/* Relative velocity of B with respect to A along the row. */
static gfloat
cs_row_velocity (const LrgSolverBody *a,
                 const LrgSolverBody *b,
                 const gfloat        *dir,
                 const LrgDynRow     *row)
{
	return (b->linear_velocity[0] - a->linear_velocity[0]) * dir[0] +
	       (b->linear_velocity[1] - a->linear_velocity[1]) * dir[1] +
	       (b->linear_velocity[2] - a->linear_velocity[2]) * dir[2] +
	       cs_dot (row->rb_d, b->angular_velocity) - cs_dot (row->ra_d, a->angular_velocity);
}

/* +lambda on B, -lambda on A, through the cached terms. */
static void
cs_row_apply (LrgSolverBody   *a,
              LrgSolverBody   *b,
              const gfloat    *dir,
              const LrgDynRow *row,
              gfloat           lambda)
{
	gfloat la;
	gfloat lb;

	if (a->inv_mass > 0.0f)
	{
		la = lambda * a->inv_mass;
		a->linear_velocity[0] -= dir[0] * la;
		a->linear_velocity[1] -= dir[1] * la;
		a->linear_velocity[2] -= dir[2] * la;
		a->angular_velocity[0] -= row->ia_ra_d[0] * lambda;
		a->angular_velocity[1] -= row->ia_ra_d[1] * lambda;
		a->angular_velocity[2] -= row->ia_ra_d[2] * lambda;
	}
	if (b->inv_mass > 0.0f)
	{
		lb = lambda * b->inv_mass;
		b->linear_velocity[0] += dir[0] * lb;
		b->linear_velocity[1] += dir[1] * lb;
		b->linear_velocity[2] += dir[2] * lb;
		b->angular_velocity[0] += row->ib_rb_d[0] * lambda;
		b->angular_velocity[1] += row->ib_rb_d[1] * lambda;
		b->angular_velocity[2] += row->ib_rb_d[2] * lambda;
	}
}

void
lrg_dyn_manifold_setup (LrgDynManifold                 *m,
                        const LrgContactSolverSettings *settings,
                        gfloat                          dt)
{
	LrgSolverBody *a;
	LrgSolverBody *b;
	guint i;
	guint k;

	a = m->body_a;
	b = m->body_b;

	/* Friction directions: a fixed basis perpendicular to the normal,
	 * so warm-started tangent impulses stay meaningful from substep to
	 * substep (Jolt does the same). */
	lrg_dyn_perpendicular (m->normal, m->tangent[0]);
	cs_cross (m->normal, m->tangent[0], m->tangent[1]);

	memset (m->friction_r_a, 0, sizeof (m->friction_r_a));
	memset (m->friction_r_b, 0, sizeof (m->friction_r_b));
	for (i = 0; i < m->n_points; i++)
	{
		LrgDynContactPoint *p;
		gfloat vn;

		p = &m->points[i];

		/* Lever arms from each centre of mass to that body's own surface
		 * point (they differ along the normal for a speculative point). */
		for (k = 0; k < 3u; k++)
		{
			p->r_a[k] = p->world_a[k] - a->position[k];
			p->r_b[k] = p->world_b[k] - b->position[k];
			m->friction_r_a[k] += p->r_a[k];
			m->friction_r_b[k] += p->r_b[k];
		}
		cs_row_setup (a, b, p->r_a, p->r_b, m->normal, &p->normal_row);

		/* Target normal velocity. A separated (speculative) point may
		 * close by at most its gap this substep. A closing speed above
		 * the threshold that reaches contact within the substep bounces
		 * with the restitution. Penetration is left to the position
		 * pass (no velocity bias, so no energy is added). */
		vn = cs_row_velocity (a, b, m->normal, &p->normal_row);
		p->target_velocity = (p->depth < 0.0f) ? p->depth / dt : 0.0f;
		if (m->restitution > 0.0f && vn < -settings->restitution_threshold &&
		    (p->depth >= 0.0f || -vn * dt > -p->depth))
			p->target_velocity = MAX (p->target_velocity, -m->restitution * vn);
	}

	/* Friction centre per body, and each point's distance to it in the
	 * tangent plane (the twist row's lever). */
	if (m->n_points > 0)
	{
		for (k = 0; k < 3u; k++)
		{
			m->friction_r_a[k] /= (gfloat) m->n_points;
			m->friction_r_b[k] /= (gfloat) m->n_points;
		}
	}
	for (i = 0; i < m->n_points; i++)
	{
		LrgDynContactPoint *p;
		gfloat d[3];
		gfloat dn;

		p = &m->points[i];
		d[0] = p->r_a[0] - m->friction_r_a[0];
		d[1] = p->r_a[1] - m->friction_r_a[1];
		d[2] = p->r_a[2] - m->friction_r_a[2];
		dn = cs_dot (d, m->normal);
		d[0] -= dn * m->normal[0];
		d[1] -= dn * m->normal[1];
		d[2] -= dn * m->normal[2];
		p->friction_lever = sqrtf (cs_dot (d, d));
	}
	cs_row_setup (a, b, m->friction_r_a, m->friction_r_b, m->tangent[0], &m->tangent_row[0]);
	cs_row_setup (a, b, m->friction_r_a, m->friction_r_b, m->tangent[1], &m->tangent_row[1]);
	m->twist_mass = (m->n_points > 1) ? cs_twist_mass (a, b, m->normal) : 0.0f;
	if (m->n_points <= 1)
		m->twist_impulse = 0.0f;
	if (!(m->friction > 0.0f))
	{
		m->tangent_impulse[0] = 0.0f;
		m->tangent_impulse[1] = 0.0f;
		m->twist_impulse = 0.0f;
	}
}

void
lrg_dyn_manifold_warm_start (LrgDynManifold *m,
                             gfloat          ratio)
{
	guint i;

	m->tangent_impulse[0] *= ratio;
	m->tangent_impulse[1] *= ratio;
	m->twist_impulse *= ratio;
	if (m->tangent_impulse[0] != 0.0f)
		cs_row_apply (m->body_a, m->body_b, m->tangent[0], &m->tangent_row[0], m->tangent_impulse[0]);
	if (m->tangent_impulse[1] != 0.0f)
		cs_row_apply (m->body_a, m->body_b, m->tangent[1], &m->tangent_row[1], m->tangent_impulse[1]);
	if (m->twist_impulse != 0.0f)
		cs_apply_twist (m->body_a, m->body_b, m->normal, m->twist_impulse);
	for (i = 0; i < m->n_points; i++)
	{
		LrgDynContactPoint *p;

		p = &m->points[i];
		p->normal_impulse *= ratio;
		if (p->normal_impulse != 0.0f)
			cs_row_apply (m->body_a, m->body_b, m->normal, &p->normal_row, p->normal_impulse);
	}
}

gboolean
lrg_dyn_manifold_solve_velocity (LrgDynManifold *m,
                                 gboolean        reverse)
{
	LrgSolverBody *a;
	LrgSolverBody *b;
	gboolean applied;
	gfloat max_linear;
	gfloat max_twist;
	guint i;

	a = m->body_a;
	b = m->body_b;
	applied = FALSE;

	/* Friction first, bounded by the normal impulses of the previous
	 * iteration: non-penetration is more important, so it is solved
	 * last and has the final say (Jolt). */
	max_linear = 0.0f;
	max_twist = 0.0f;
	for (i = 0; i < m->n_points; i++)
	{
		max_linear += m->points[i].normal_impulse;
		max_twist += m->points[i].friction_lever * m->points[i].normal_impulse;
	}
	max_linear *= m->friction;
	max_twist *= m->friction;
	if (m->friction > 0.0f)
	{
		gfloat vt0;
		gfloat vt1;
		gfloat new0;
		gfloat new1;
		gfloat len2;
		gfloat d0;
		gfloat d1;

		/* Both tangent rows at the friction centre, clamped together to
		 * the friction circle. */
		vt0 = cs_row_velocity (a, b, m->tangent[0], &m->tangent_row[0]);
		vt1 = cs_row_velocity (a, b, m->tangent[1], &m->tangent_row[1]);
		new0 = m->tangent_impulse[0] - vt0 * m->tangent_row[0].mass;
		new1 = m->tangent_impulse[1] - vt1 * m->tangent_row[1].mass;
		len2 = new0 * new0 + new1 * new1;
		if (len2 > max_linear * max_linear + FLT_MIN)
		{
			gfloat scale;

			scale = max_linear / sqrtf (len2);
			new0 *= scale;
			new1 *= scale;
		}
		d0 = new0 - m->tangent_impulse[0];
		d1 = new1 - m->tangent_impulse[1];
		m->tangent_impulse[0] = new0;
		m->tangent_impulse[1] = new1;
		if (d0 != 0.0f)
		{
			cs_row_apply (a, b, m->tangent[0], &m->tangent_row[0], d0);
			applied = TRUE;
		}
		if (d1 != 0.0f)
		{
			cs_row_apply (a, b, m->tangent[1], &m->tangent_row[1], d1);
			applied = TRUE;
		}

		/* Twist about the normal. */
		if (m->twist_mass > 0.0f)
		{
			gfloat wrel;
			gfloat nt;
			gfloat dt;

			wrel = (b->angular_velocity[0] - a->angular_velocity[0]) * m->normal[0] +
			       (b->angular_velocity[1] - a->angular_velocity[1]) * m->normal[1] +
			       (b->angular_velocity[2] - a->angular_velocity[2]) * m->normal[2];
			nt = CLAMP (m->twist_impulse - wrel * m->twist_mass, -max_twist, max_twist);
			dt = nt - m->twist_impulse;
			m->twist_impulse = nt;
			if (dt != 0.0f)
			{
				cs_apply_twist (a, b, m->normal, dt);
				applied = TRUE;
			}
		}
	}

	/* Then every normal row: vn >= target, accumulated impulse >= 0.
	 * Alternate iterations walk the points in opposite orders, so the
	 * Gauss-Seidel order bias (the first point taking the most) does not
	 * turn a resting body one way every substep. */
	for (i = 0; i < m->n_points; i++)
	{
		LrgDynContactPoint *p;
		gfloat vn;
		gfloat lambda;
		gfloat old;

		p = &m->points[reverse ? (m->n_points - 1u - i) : i];
		vn = cs_row_velocity (a, b, m->normal, &p->normal_row);
		lambda = (p->target_velocity - vn) * p->normal_row.mass;
		old = p->normal_impulse;
		p->normal_impulse = MAX (old + lambda, 0.0f);
		lambda = p->normal_impulse - old;
		if (lambda != 0.0f)
		{
			cs_row_apply (a, b, m->normal, &p->normal_row, lambda);
			applied = TRUE;
		}
	}
	return applied;
}

gboolean
lrg_dyn_manifold_solve_position (LrgDynManifold                 *m,
                                 const LrgContactSolverSettings *settings)
{
	LrgSolverBody *a;
	LrgSolverBody *b;
	gboolean applied;
	guint i;

	a = m->body_a;
	b = m->body_b;
	applied = FALSE;
	for (i = 0; i < m->n_points; i++)
	{
		LrgDynContactPoint *p;
		gfloat pa[3];
		gfloat pb[3];
		gfloat ra[3];
		gfloat rb[3];
		gfloat separation;
		gfloat k;
		gfloat lambda;
		gfloat impulse[3];
		gfloat neg[3];

		p = &m->points[i];

		/* Current surface points from the stored body-local points. */
		lrg_dyn_quat_rotate (a->orientation, p->local_a, ra);
		lrg_dyn_quat_rotate (b->orientation, p->local_b, rb);
		pa[0] = a->position[0] + ra[0];
		pa[1] = a->position[1] + ra[1];
		pa[2] = a->position[2] + ra[2];
		pb[0] = b->position[0] + rb[0];
		pb[1] = b->position[1] + rb[1];
		pb[2] = b->position[2] + rb[2];

		/* Positive separation is a gap; allow slop of penetration. */
		separation = (pb[0] - pa[0]) * m->normal[0] +
		             (pb[1] - pa[1]) * m->normal[1] +
		             (pb[2] - pa[2]) * m->normal[2] + settings->penetration_slop;
		if (separation >= 0.0f)
			continue;
		k = cs_row_mass (a, b, ra, rb, m->normal);
		if (!(k > 0.0f))
			continue;
		lambda = -settings->baumgarte * separation * k;
		lambda = MIN (lambda, settings->max_correction * k);
		impulse[0] = m->normal[0] * lambda;
		impulse[1] = m->normal[1] * lambda;
		impulse[2] = m->normal[2] * lambda;
		neg[0] = -impulse[0];
		neg[1] = -impulse[1];
		neg[2] = -impulse[2];
		lrg_solver_body_apply_position_correction (b, impulse, pb, NULL);
		lrg_solver_body_apply_position_correction (a, neg, pa, NULL);
		applied = TRUE;
	}
	return applied;
}

guint
lrg_dyn_manifold_match (LrgDynManifold       *m,
                        const LrgDynManifold *old)
{
	guint i;
	guint j;
	guint matched;
	gboolean used[LRG_DYN_MAX_POINTS];
	const gfloat tol2 = 1.0e-4f; /* 1 cm, Jolt's default contact point preserve distance */

	/* The normal must still point the same way, else the impulses are
	 * meaningless (Jolt: cos 5 degrees). */
	if (cs_dot (m->normal, old->normal) < 0.996f)
		return 0;
	memset (used, 0, sizeof (used));
	matched = 0;
	for (i = 0; i < m->n_points; i++)
	{
		LrgDynContactPoint *p;
		guint best;
		gfloat best_d;

		p = &m->points[i];
		best = G_MAXUINT;
		best_d = tol2;
		for (j = 0; j < old->n_points; j++)
		{
			const LrgDynContactPoint *q;
			gfloat da[3];
			gfloat db[3];
			gfloat d;

			if (used[j])
				continue;
			q = &old->points[j];
			da[0] = p->local_a[0] - q->local_a[0];
			da[1] = p->local_a[1] - q->local_a[1];
			da[2] = p->local_a[2] - q->local_a[2];
			db[0] = p->local_b[0] - q->local_b[0];
			db[1] = p->local_b[1] - q->local_b[1];
			db[2] = p->local_b[2] - q->local_b[2];
			d = MAX (cs_dot (da, da), cs_dot (db, db));
			if (p->feature != 0u && p->feature == q->feature)
				d = 0.0f;
			if (d < best_d || (d == 0.0f && best == G_MAXUINT))
			{
				best = j;
				best_d = d;
			}
		}
		if (best == G_MAXUINT)
			continue;
		used[best] = TRUE;
		p->normal_impulse = old->points[best].normal_impulse;
		matched++;
	}
	/* The manifold friction carries over once the manifold matched. */
	if (matched > 0)
	{
		m->tangent_impulse[0] = old->tangent_impulse[0];
		m->tangent_impulse[1] = old->tangent_impulse[1];
		m->twist_impulse = old->twist_impulse;
	}
	return matched;
}
