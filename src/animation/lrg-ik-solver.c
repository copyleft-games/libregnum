/* lrg-ik-solver.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * IK solvers. Each one reads the chain's joint positions (bones, then the
 * end effector) in model space and moves the chain only through
 * lrg_ik_chain_aim_bone(), which rotates a bone about its joint and keeps
 * every bone's length: a solved chain is always a valid pose.
 *
 *  - Two-bone: the law of cosines gives the middle joint on the circle of
 *    points at distance len0 from the root and len1 from the (clamped)
 *    target; the pole (or the current bend) picks the point on that
 *    circle. Exact in reach, fully stretched toward the target beyond.
 *  - FABRIK (Aristidou and Lasenby, 2011): alternate backward (from the
 *    target) and forward (from the root) passes on joint positions, then
 *    aim each bone at its solved child joint.
 *  - CCD: from the last bone to the first, turn each bone so the effector
 *    lies on the line from its joint to the target; repeat.
 *  - Look-at: turn the single bone so its bone axis points at the target.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-ik-solver.h"

/**
 * SECTION:lrg-ik-solver
 * @Title: LrgIKSolver
 * @Short_description: IK solver interface and implementations
 *
 * #LrgIKSolver is the base class for inverse kinematics solvers.
 * Several implementations are provided:
 *
 * - #LrgIKSolverFABRIK: Forward And Backward Reaching IK, works with any chain length
 * - #LrgIKSolverCCD: Cyclic Coordinate Descent, works with any chain length
 * - #LrgIKSolverTwoBone: Analytical solver for exactly 2 bones
 * - #LrgIKSolverLookAt: Simple aim constraint for a single bone
 */

G_DEFINE_TYPE (LrgIKSolver, lrg_ik_solver, G_TYPE_OBJECT)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gfloat
len3 (const gfloat *v)
{
	return sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static gfloat
dist3 (const gfloat *a, const gfloat *b)
{
	gfloat d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };

	return len3 (d);
}

static void
sub3 (const gfloat *a, const gfloat *b, gfloat *out)
{
	out[0] = a[0] - b[0];
	out[1] = a[1] - b[1];
	out[2] = a[2] - b[2];
}

static gboolean
norm3 (gfloat *v)
{
	gfloat l = len3 (v);

	if (l < 1e-8f)
		return FALSE;
	v[0] /= l;
	v[1] /= l;
	v[2] /= l;
	return TRUE;
}

static gfloat
dot3 (const gfloat *a, const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Joint positions of @chain into a new array (bones + effector). */
static gfloat *
read_joints (LrgIKChain *chain,
             guint      *out_n)
{
	guint   n = lrg_ik_chain_get_bone_count (chain) + 1;
	gfloat *p = g_new (gfloat, 3 * n);

	if (lrg_ik_chain_get_joint_positions (chain, p, n) != n)
	{
		g_free (p);
		return NULL;
	}
	*out_n = n;
	return p;
}

/* Aims chain bone @i so the segment toward @current points at @wanted
 * (both model-space points). */
static void
aim_at (LrgIKChain   *chain,
        guint         i,
        const gfloat *joint,
        const gfloat *current,
        const gfloat *wanted)
{
	gfloat from[3], to[3];

	sub3 (current, joint, from);
	sub3 (wanted, joint, to);
	if (len3 (from) < 1e-6f || len3 (to) < 1e-6f)
		return;
	lrg_ik_chain_aim_bone (chain, i, from, to);
}

/* ==========================================================================
 * Base class
 * ========================================================================== */

static gboolean
lrg_ik_solver_real_solve (LrgIKSolver *solver,
                          LrgIKChain  *chain,
                          guint        max_iterations,
                          gfloat       tolerance)
{
	return FALSE;
}

static gboolean
lrg_ik_solver_real_supports_chain_length (LrgIKSolver *solver,
                                          guint        bone_count)
{
	return bone_count > 0;
}

static void
lrg_ik_solver_class_init (LrgIKSolverClass *klass)
{
	klass->solve = lrg_ik_solver_real_solve;
	klass->supports_chain_length = lrg_ik_solver_real_supports_chain_length;
}

static void
lrg_ik_solver_init (LrgIKSolver *self)
{
}

gboolean
lrg_ik_solver_solve (LrgIKSolver *solver,
                     LrgIKChain  *chain,
                     guint        max_iterations,
                     gfloat       tolerance)
{
	LrgIKSolverClass *klass;

	g_return_val_if_fail (LRG_IS_IK_SOLVER (solver), FALSE);
	g_return_val_if_fail (LRG_IS_IK_CHAIN (chain), FALSE);

	klass = LRG_IK_SOLVER_GET_CLASS (solver);
	if (!klass->supports_chain_length (solver, lrg_ik_chain_get_bone_count (chain)))
		return FALSE;
	return klass->solve (solver, chain, max_iterations, tolerance);
}

gboolean
lrg_ik_solver_supports_chain_length (LrgIKSolver *solver,
                                     guint        bone_count)
{
	LrgIKSolverClass *klass;

	g_return_val_if_fail (LRG_IS_IK_SOLVER (solver), FALSE);

	klass = LRG_IK_SOLVER_GET_CLASS (solver);
	return klass->supports_chain_length (solver, bone_count);
}

/* ==========================================================================
 * FABRIK
 * ========================================================================== */

struct _LrgIKSolverFABRIK
{
	LrgIKSolver parent_instance;
};

G_DEFINE_TYPE (LrgIKSolverFABRIK, lrg_ik_solver_fabrik, LRG_TYPE_IK_SOLVER)

static gboolean
lrg_ik_solver_fabrik_solve (LrgIKSolver *solver,
                            LrgIKChain  *chain,
                            guint        max_iterations,
                            gfloat       tolerance)
{
	g_autofree gfloat *p = NULL;
	g_autofree gfloat *q = NULL;
	g_autofree gfloat *len = NULL;
	gfloat             target[3], root[3], total = 0.0f;
	guint              n, i, it;

	p = read_joints (chain, &n);
	if (p == NULL)
		return FALSE;
	lrg_ik_chain_get_target_position (chain, &target[0], &target[1], &target[2]);

	/* segment lengths of the current pose */
	len = g_new (gfloat, n - 1);
	for (i = 0; i + 1 < n; i++)
	{
		len[i] = dist3 (&p[3 * i], &p[3 * i + 3]);
		total += len[i];
	}
	q = g_memdup2 (p, 3 * n * sizeof (gfloat));
	memcpy (root, p, sizeof root);

	if (dist3 (root, target) >= total)
	{
		/* out of reach: a straight line toward the target */
		gfloat dir[3];

		sub3 (target, root, dir);
		norm3 (dir);
		for (i = 0; i + 1 < n; i++)
		{
			q[3 * i + 3] = q[3 * i] + dir[0] * len[i];
			q[3 * i + 4] = q[3 * i + 1] + dir[1] * len[i];
			q[3 * i + 5] = q[3 * i + 2] + dir[2] * len[i];
		}
	}
	else
	{
		for (it = 0; it < MAX (max_iterations, 1u); it++)
		{
			gint j;

			if (dist3 (&q[3 * (n - 1)], target) <= tolerance)
				break;
			/* backward: pin the effector to the target */
			memcpy (&q[3 * (n - 1)], target, sizeof target);
			for (j = (gint) n - 2; j >= 0; j--)
			{
				gfloat d[3];
				gfloat l;

				sub3 (&q[3 * j], &q[3 * j + 3], d);
				l = len3 (d);
				if (l < 1e-8f)
					continue;
				q[3 * j] = q[3 * j + 3] + d[0] * len[j] / l;
				q[3 * j + 1] = q[3 * j + 4] + d[1] * len[j] / l;
				q[3 * j + 2] = q[3 * j + 5] + d[2] * len[j] / l;
			}
			/* forward: pin the root back */
			memcpy (q, root, sizeof root);
			for (i = 0; i + 1 < n; i++)
			{
				gfloat d[3];
				gfloat l;

				sub3 (&q[3 * i + 3], &q[3 * i], d);
				l = len3 (d);
				if (l < 1e-8f)
					continue;
				q[3 * i + 3] = q[3 * i] + d[0] * len[i] / l;
				q[3 * i + 4] = q[3 * i + 1] + d[1] * len[i] / l;
				q[3 * i + 5] = q[3 * i + 2] + d[2] * len[i] / l;
			}
		}
	}

	/* aim each bone at its solved child joint (the joints before it are
	 * already in place, so the bone's own joint is q[i]) */
	for (i = 0; i + 1 < n; i++)
	{
		g_autofree gfloat *cur = NULL;
		guint              m;

		cur = read_joints (chain, &m);
		if (cur == NULL)
			return FALSE;
		aim_at (chain, i, &cur[3 * i], &cur[3 * i + 3], &q[3 * i + 3]);
	}
	return TRUE;
}

static void
lrg_ik_solver_fabrik_class_init (LrgIKSolverFABRIKClass *klass)
{
	LRG_IK_SOLVER_CLASS (klass)->solve = lrg_ik_solver_fabrik_solve;
}

static void
lrg_ik_solver_fabrik_init (LrgIKSolverFABRIK *self)
{
}

LrgIKSolverFABRIK *
lrg_ik_solver_fabrik_new (void)
{
	return g_object_new (LRG_TYPE_IK_SOLVER_FABRIK, NULL);
}

/* ==========================================================================
 * CCD
 * ========================================================================== */

struct _LrgIKSolverCCD
{
	LrgIKSolver parent_instance;
};

G_DEFINE_TYPE (LrgIKSolverCCD, lrg_ik_solver_ccd, LRG_TYPE_IK_SOLVER)

static gboolean
lrg_ik_solver_ccd_solve (LrgIKSolver *solver,
                         LrgIKChain  *chain,
                         guint        max_iterations,
                         gfloat       tolerance)
{
	gfloat target[3];
	guint  nb = lrg_ik_chain_get_bone_count (chain);
	guint  it;

	lrg_ik_chain_get_target_position (chain, &target[0], &target[1], &target[2]);
	for (it = 0; it < MAX (max_iterations, 1u); it++)
	{
		gint i;

		for (i = (gint) nb - 1; i >= 0; i--)
		{
			g_autofree gfloat *p = NULL;
			guint              n;

			p = read_joints (chain, &n);
			if (p == NULL)
				return FALSE;
			if (dist3 (&p[3 * (n - 1)], target) <= tolerance)
				return TRUE;
			aim_at (chain, (guint) i, &p[3 * i], &p[3 * (n - 1)], target);
		}
	}
	return TRUE;
}

static void
lrg_ik_solver_ccd_class_init (LrgIKSolverCCDClass *klass)
{
	LRG_IK_SOLVER_CLASS (klass)->solve = lrg_ik_solver_ccd_solve;
}

static void
lrg_ik_solver_ccd_init (LrgIKSolverCCD *self)
{
}

LrgIKSolverCCD *
lrg_ik_solver_ccd_new (void)
{
	return g_object_new (LRG_TYPE_IK_SOLVER_CCD, NULL);
}

/* ==========================================================================
 * Two-bone (analytic)
 * ========================================================================== */

struct _LrgIKSolverTwoBone
{
	LrgIKSolver parent_instance;
};

G_DEFINE_TYPE (LrgIKSolverTwoBone, lrg_ik_solver_two_bone, LRG_TYPE_IK_SOLVER)

static gboolean
lrg_ik_solver_two_bone_supports (LrgIKSolver *solver,
                                 guint        bone_count)
{
	return bone_count == 2;
}

static gboolean
lrg_ik_solver_two_bone_solve (LrgIKSolver *solver,
                              LrgIKChain  *chain,
                              guint        max_iterations,
                              gfloat       tolerance)
{
	g_autofree gfloat *p = NULL;
	gfloat             a[3], b[3], c[3], t[3];
	gfloat             l0, l1, d, dir[3], bend[3], mid[3], end[3];
	gfloat             cos_a, sin_a, along;
	guint              n, k;

	p = read_joints (chain, &n);
	if (p == NULL || n != 3)
		return FALSE;
	memcpy (a, &p[0], sizeof a);
	memcpy (b, &p[3], sizeof b);
	memcpy (c, &p[6], sizeof c);
	lrg_ik_chain_get_target_position (chain, &t[0], &t[1], &t[2]);

	l0 = dist3 (a, b);
	l1 = dist3 (b, c);
	if (l0 < 1e-6f || l1 < 1e-6f)
		return FALSE;

	/* the reachable distance, kept just short of the singular extremes */
	sub3 (t, a, dir);
	d = len3 (dir);
	if (!norm3 (dir))
		return FALSE;
	d = CLAMP (d, fabsf (l0 - l1) + 1e-4f, l0 + l1 - 1e-4f);

	/* bend direction: the pole (or the current knee) perpendicular to dir */
	if (lrg_ik_chain_has_pole (chain))
	{
		gfloat pole[3];

		lrg_ik_chain_get_pole_position (chain, &pole[0], &pole[1], &pole[2]);
		sub3 (pole, a, bend);
	}
	else
	{
		sub3 (b, a, bend);
	}
	along = dot3 (bend, dir);
	for (k = 0; k < 3; k++)
		bend[k] -= along * dir[k];
	if (!norm3 (bend))
	{
		/* straight chain toward the target: any perpendicular */
		gfloat up[3] = { 0.0f, 1.0f, 0.0f };

		if (fabsf (dir[1]) > 0.9f)
		{
			up[1] = 0.0f;
			up[2] = 1.0f;
		}
		along = dot3 (up, dir);
		for (k = 0; k < 3; k++)
			bend[k] = up[k] - along * dir[k];
		norm3 (bend);
	}

	/* law of cosines at the root */
	cos_a = (l0 * l0 + d * d - l1 * l1) / (2.0f * l0 * d);
	cos_a = CLAMP (cos_a, -1.0f, 1.0f);
	sin_a = sqrtf (MAX (0.0f, 1.0f - cos_a * cos_a));
	for (k = 0; k < 3; k++)
	{
		mid[k] = a[k] + dir[k] * l0 * cos_a + bend[k] * l0 * sin_a;
		end[k] = a[k] + dir[k] * d;
	}

	/* upper bone to the new knee, then the lower bone to the target */
	aim_at (chain, 0, a, b, mid);
	g_free (p);
	p = read_joints (chain, &n);
	if (p == NULL)
		return FALSE;
	aim_at (chain, 1, &p[3], &p[6], end);
	return TRUE;
}

static void
lrg_ik_solver_two_bone_class_init (LrgIKSolverTwoBoneClass *klass)
{
	LrgIKSolverClass *solver_class = LRG_IK_SOLVER_CLASS (klass);

	solver_class->solve = lrg_ik_solver_two_bone_solve;
	solver_class->supports_chain_length = lrg_ik_solver_two_bone_supports;
}

static void
lrg_ik_solver_two_bone_init (LrgIKSolverTwoBone *self)
{
}

LrgIKSolverTwoBone *
lrg_ik_solver_two_bone_new (void)
{
	return g_object_new (LRG_TYPE_IK_SOLVER_TWO_BONE, NULL);
}

/* ==========================================================================
 * Look-at
 * ========================================================================== */

struct _LrgIKSolverLookAt
{
	LrgIKSolver parent_instance;

	gfloat up_x;
	gfloat up_y;
	gfloat up_z;
};

G_DEFINE_TYPE (LrgIKSolverLookAt, lrg_ik_solver_look_at, LRG_TYPE_IK_SOLVER)

enum
{
	LOOK_AT_PROP_0,
	LOOK_AT_PROP_UP_X,
	LOOK_AT_PROP_UP_Y,
	LOOK_AT_PROP_UP_Z,
	LOOK_AT_N_PROPS
};

static GParamSpec *look_at_properties[LOOK_AT_N_PROPS];

static gboolean
lrg_ik_solver_look_at_supports (LrgIKSolver *solver,
                                guint        bone_count)
{
	return bone_count == 1;
}

/* Turns the bone's axis (the chain's bone axis rotated into model space)
 * toward the target by the shortest arc. The up vector is not used to
 * fix the roll (the shortest arc keeps the animated roll); it is kept as
 * a property for compatibility. */
static gboolean
lrg_ik_solver_look_at_solve (LrgIKSolver *solver,
                             LrgIKChain  *chain,
                             guint        max_iterations,
                             gfloat       tolerance)
{
	g_autofree gfloat *p = NULL;
	gfloat             t[3];
	guint              n;

	p = read_joints (chain, &n);
	if (p == NULL || n != 2)
		return FALSE;
	lrg_ik_chain_get_target_position (chain, &t[0], &t[1], &t[2]);

	/* joint 1 is the tip along the bone axis (or the effector) */
	aim_at (chain, 0, &p[0], &p[3], t);
	return TRUE;
}

static void
lrg_ik_solver_look_at_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgIKSolverLookAt *self = LRG_IK_SOLVER_LOOK_AT (object);

	switch (prop_id)
	{
	case LOOK_AT_PROP_UP_X: g_value_set_float (value, self->up_x); break;
	case LOOK_AT_PROP_UP_Y: g_value_set_float (value, self->up_y); break;
	case LOOK_AT_PROP_UP_Z: g_value_set_float (value, self->up_z); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_ik_solver_look_at_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgIKSolverLookAt *self = LRG_IK_SOLVER_LOOK_AT (object);

	switch (prop_id)
	{
	case LOOK_AT_PROP_UP_X: self->up_x = g_value_get_float (value); break;
	case LOOK_AT_PROP_UP_Y: self->up_y = g_value_get_float (value); break;
	case LOOK_AT_PROP_UP_Z: self->up_z = g_value_get_float (value); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_ik_solver_look_at_class_init (LrgIKSolverLookAtClass *klass)
{
	GObjectClass     *object_class = G_OBJECT_CLASS (klass);
	LrgIKSolverClass *solver_class = LRG_IK_SOLVER_CLASS (klass);

	object_class->get_property = lrg_ik_solver_look_at_get_property;
	object_class->set_property = lrg_ik_solver_look_at_set_property;

	solver_class->solve = lrg_ik_solver_look_at_solve;
	solver_class->supports_chain_length = lrg_ik_solver_look_at_supports;

	/**
	 * LrgIKSolverLookAt:up-x:
	 *
	 * X of the up vector.
	 */
	look_at_properties[LOOK_AT_PROP_UP_X] =
		g_param_spec_float ("up-x", "Up X", "Up vector X", -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgIKSolverLookAt:up-y:
	 *
	 * Y of the up vector.
	 */
	look_at_properties[LOOK_AT_PROP_UP_Y] =
		g_param_spec_float ("up-y", "Up Y", "Up vector Y", -G_MAXFLOAT, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgIKSolverLookAt:up-z:
	 *
	 * Z of the up vector.
	 */
	look_at_properties[LOOK_AT_PROP_UP_Z] =
		g_param_spec_float ("up-z", "Up Z", "Up vector Z", -G_MAXFLOAT, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, LOOK_AT_N_PROPS, look_at_properties);
}

static void
lrg_ik_solver_look_at_init (LrgIKSolverLookAt *self)
{
	self->up_y = 1.0f;
}

LrgIKSolverLookAt *
lrg_ik_solver_look_at_new (void)
{
	return g_object_new (LRG_TYPE_IK_SOLVER_LOOK_AT, NULL);
}

void
lrg_ik_solver_look_at_get_up_vector (LrgIKSolverLookAt *self,
                                     gfloat            *x,
                                     gfloat            *y,
                                     gfloat            *z)
{
	g_return_if_fail (LRG_IS_IK_SOLVER_LOOK_AT (self));

	if (x != NULL)
		*x = self->up_x;
	if (y != NULL)
		*y = self->up_y;
	if (z != NULL)
		*z = self->up_z;
}

void
lrg_ik_solver_look_at_set_up_vector (LrgIKSolverLookAt *self,
                                     gfloat             x,
                                     gfloat             y,
                                     gfloat             z)
{
	g_return_if_fail (LRG_IS_IK_SOLVER_LOOK_AT (self));

	self->up_x = x;
	self->up_y = y;
	self->up_z = z;
}
