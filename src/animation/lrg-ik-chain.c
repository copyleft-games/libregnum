/* lrg-ik-chain.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * IK chains and IK goals.
 *
 * A chain is an ordered list of bones (root first) plus an end effector
 * (an effector bone, or the last bone's tip). All positions are in the
 * skeleton's model space (the space of its world poses). Solvers move the
 * chain with lrg_ik_chain_aim_bone(): rotate one bone about its joint by
 * the shortest arc between two model-space directions, convert the new
 * model-space rotation back to a local rotation under the parent and
 * recompute the world poses. Translations and scales are never touched,
 * so bone lengths and the skeleton's proportions survive any solve.
 *
 * An #LrgIKGoal is one entry of the animation graph's IK post-pass: a
 * chain, a solver kind, a world-space target (converted into model space
 * with the character's model matrix), a pole and a weight that blends the
 * solved rotations with the animated ones.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-ik-chain.h"
#include "lrg-ik-solver.h"

/**
 * SECTION:lrg-ik-chain
 * @Title: LrgIKChain
 * @Short_description: Bone chain for inverse kinematics
 *
 * #LrgIKChain names the bones an IK solver moves and holds the target
 * and pole the solver works toward. #LrgIKGoal wraps a chain and a solver
 * for the animation graph's IK post-pass.
 */

/* ==========================================================================
 * Small vector / quaternion helpers (x, y, z, w)
 * ========================================================================== */

static gfloat
v3_len (const gfloat *v)
{
	return sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static gboolean
v3_normalize (gfloat *v)
{
	gfloat l = v3_len (v);

	if (l < 1e-8f)
		return FALSE;
	v[0] /= l;
	v[1] /= l;
	v[2] /= l;
	return TRUE;
}

static gfloat
v3_dot (const gfloat *a, const gfloat *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
v3_cross (const gfloat *a, const gfloat *b, gfloat *out)
{
	gfloat x = a[1] * b[2] - a[2] * b[1];
	gfloat y = a[2] * b[0] - a[0] * b[2];
	gfloat z = a[0] * b[1] - a[1] * b[0];

	out[0] = x;
	out[1] = y;
	out[2] = z;
}

/* r = a * b */
static void
q_mul (const gfloat *a, const gfloat *b, gfloat *r)
{
	gfloat x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	gfloat y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	gfloat z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	gfloat w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];

	r[0] = x;
	r[1] = y;
	r[2] = z;
	r[3] = w;
}

static void
q_normalize (gfloat *q)
{
	gfloat l = sqrtf (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

	if (l < 1e-8f)
	{
		q[0] = q[1] = q[2] = 0.0f;
		q[3] = 1.0f;
		return;
	}
	q[0] /= l;
	q[1] /= l;
	q[2] /= l;
	q[3] /= l;
}

/* Rotates v by the unit quaternion q. */
static void
q_rotate (const gfloat *q, const gfloat *v, gfloat *out)
{
	gfloat u[3] = { q[0], q[1], q[2] };
	gfloat t[3], c[3];

	v3_cross (u, v, t);
	t[0] *= 2.0f;
	t[1] *= 2.0f;
	t[2] *= 2.0f;
	v3_cross (u, t, c);
	out[0] = v[0] + q[3] * t[0] + c[0];
	out[1] = v[1] + q[3] * t[1] + c[1];
	out[2] = v[2] + q[3] * t[2] + c[2];
}

/* The shortest rotation taking direction a to direction b. */
static void
q_from_to (const gfloat *a_in, const gfloat *b_in, gfloat *q)
{
	gfloat a[3] = { a_in[0], a_in[1], a_in[2] };
	gfloat b[3] = { b_in[0], b_in[1], b_in[2] };
	gfloat c[3];
	gfloat d;

	if (!v3_normalize (a) || !v3_normalize (b))
	{
		q[0] = q[1] = q[2] = 0.0f;
		q[3] = 1.0f;
		return;
	}
	d = v3_dot (a, b);
	if (d < -0.999999f)
	{
		/* opposite: half turn about any axis perpendicular to a */
		gfloat axis[3] = { 1.0f, 0.0f, 0.0f };

		v3_cross (axis, a, c);
		if (v3_len (c) < 1e-4f)
		{
			axis[0] = 0.0f;
			axis[1] = 1.0f;
			v3_cross (axis, a, c);
		}
		v3_normalize (c);
		q[0] = c[0];
		q[1] = c[1];
		q[2] = c[2];
		q[3] = 0.0f;
		return;
	}
	v3_cross (a, b, c);
	q[0] = c[0];
	q[1] = c[1];
	q[2] = c[2];
	q[3] = 1.0f + d;
	q_normalize (q);
}

/* Spherical interpolation along the shorter arc. */
static void
q_slerp (const gfloat *a, const gfloat *b_in, gfloat t, gfloat *out)
{
	gfloat b[4] = { b_in[0], b_in[1], b_in[2], b_in[3] };
	gfloat d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	gfloat s0, s1;
	guint  k;

	if (d < 0.0f)
	{
		d = -d;
		for (k = 0; k < 4; k++)
			b[k] = -b[k];
	}
	if (d > 0.9995f)
	{
		s0 = 1.0f - t;
		s1 = t;
	}
	else
	{
		gfloat th = acosf (d);
		gfloat sn = sinf (th);

		s0 = sinf ((1.0f - t) * th) / sn;
		s1 = sinf (t * th) / sn;
	}
	for (k = 0; k < 4; k++)
		out[k] = a[k] * s0 + b[k] * s1;
	q_normalize (out);
}

static void
pose_rot (const LrgBonePose *p, gfloat *q)
{
	q[0] = p->rotation_x;
	q[1] = p->rotation_y;
	q[2] = p->rotation_z;
	q[3] = p->rotation_w;
	q_normalize (q);
}

/* ==========================================================================
 * LrgIKChain
 * ========================================================================== */

struct _LrgIKChain
{
	GObject parent_instance;

	LrgSkeleton *skeleton;
	GPtrArray   *bone_names;
	gchar       *effector;

	gfloat       target[3];
	gfloat       pole[3];
	gboolean     has_pole;
	gfloat       axis[3];
};

G_DEFINE_TYPE (LrgIKChain, lrg_ik_chain, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_SKELETON,
	PROP_EFFECTOR_BONE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_ik_chain_finalize (GObject *object)
{
	LrgIKChain *self = LRG_IK_CHAIN (object);

	g_clear_object (&self->skeleton);
	g_ptr_array_unref (self->bone_names);
	g_free (self->effector);

	G_OBJECT_CLASS (lrg_ik_chain_parent_class)->finalize (object);
}

static void
lrg_ik_chain_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgIKChain *self = LRG_IK_CHAIN (object);

	switch (prop_id)
	{
	case PROP_SKELETON:
		g_value_set_object (value, self->skeleton);
		break;
	case PROP_EFFECTOR_BONE:
		g_value_set_string (value, self->effector);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_ik_chain_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgIKChain *self = LRG_IK_CHAIN (object);

	switch (prop_id)
	{
	case PROP_SKELETON:
		g_set_object (&self->skeleton, g_value_get_object (value));
		break;
	case PROP_EFFECTOR_BONE:
		lrg_ik_chain_set_effector_bone (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_ik_chain_class_init (LrgIKChainClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_ik_chain_finalize;
	object_class->get_property = lrg_ik_chain_get_property;
	object_class->set_property = lrg_ik_chain_set_property;

	/**
	 * LrgIKChain:skeleton:
	 *
	 * The skeleton the chain's bones belong to.
	 */
	properties[PROP_SKELETON] =
		g_param_spec_object ("skeleton", "Skeleton", "Target skeleton", LRG_TYPE_SKELETON,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgIKChain:effector-bone:
	 *
	 * The bone that must reach the target, or %NULL for the last bone's
	 * tip.
	 */
	properties[PROP_EFFECTOR_BONE] =
		g_param_spec_string ("effector-bone", "Effector Bone", "End effector bone", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_ik_chain_init (LrgIKChain *self)
{
	self->bone_names = g_ptr_array_new_with_free_func (g_free);
	self->pole[2] = 1.0f;
	self->axis[1] = 1.0f;
}

LrgIKChain *
lrg_ik_chain_new (LrgSkeleton *skeleton)
{
	return g_object_new (LRG_TYPE_IK_CHAIN, "skeleton", skeleton, NULL);
}

LrgSkeleton *
lrg_ik_chain_get_skeleton (LrgIKChain *self)
{
	g_return_val_if_fail (LRG_IS_IK_CHAIN (self), NULL);
	return self->skeleton;
}

void
lrg_ik_chain_add_bone (LrgIKChain  *self,
                       const gchar *bone_name)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));
	g_return_if_fail (bone_name != NULL);
	g_ptr_array_add (self->bone_names, g_strdup (bone_name));
}

void
lrg_ik_chain_clear_bones (LrgIKChain *self)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));
	g_ptr_array_set_size (self->bone_names, 0);
}

guint
lrg_ik_chain_get_bone_count (LrgIKChain *self)
{
	g_return_val_if_fail (LRG_IS_IK_CHAIN (self), 0);
	return self->bone_names->len;
}

const gchar *
lrg_ik_chain_get_bone_name (LrgIKChain *self,
                            guint       index)
{
	g_return_val_if_fail (LRG_IS_IK_CHAIN (self), NULL);
	if (index >= self->bone_names->len)
		return NULL;
	return g_ptr_array_index (self->bone_names, index);
}

LrgBone *
lrg_ik_chain_get_bone (LrgIKChain *self,
                       guint       index)
{
	const gchar *name;

	g_return_val_if_fail (LRG_IS_IK_CHAIN (self), NULL);
	if (self->skeleton == NULL)
		return NULL;
	name = lrg_ik_chain_get_bone_name (self, index);
	return name != NULL ? lrg_skeleton_get_bone_by_name (self->skeleton, name) : NULL;
}

void
lrg_ik_chain_get_target_position (LrgIKChain *self,
                                  gfloat     *x,
                                  gfloat     *y,
                                  gfloat     *z)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));
	if (x != NULL)
		*x = self->target[0];
	if (y != NULL)
		*y = self->target[1];
	if (z != NULL)
		*z = self->target[2];
}

void
lrg_ik_chain_set_target_position (LrgIKChain *self,
                                  gfloat      x,
                                  gfloat      y,
                                  gfloat      z)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));
	self->target[0] = x;
	self->target[1] = y;
	self->target[2] = z;
}

void
lrg_ik_chain_get_pole_position (LrgIKChain *self,
                                gfloat     *x,
                                gfloat     *y,
                                gfloat     *z)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));
	if (x != NULL)
		*x = self->pole[0];
	if (y != NULL)
		*y = self->pole[1];
	if (z != NULL)
		*z = self->pole[2];
}

void
lrg_ik_chain_set_pole_position (LrgIKChain *self,
                                gfloat      x,
                                gfloat      y,
                                gfloat      z)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));
	self->pole[0] = x;
	self->pole[1] = y;
	self->pole[2] = z;
	self->has_pole = TRUE;
}

gboolean
lrg_ik_chain_has_pole (LrgIKChain *self)
{
	g_return_val_if_fail (LRG_IS_IK_CHAIN (self), FALSE);
	return self->has_pole;
}

void
lrg_ik_chain_clear_pole (LrgIKChain *self)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));
	self->has_pole = FALSE;
}

const gchar *
lrg_ik_chain_get_effector_bone (LrgIKChain *self)
{
	g_return_val_if_fail (LRG_IS_IK_CHAIN (self), NULL);
	return self->effector;
}

void
lrg_ik_chain_set_effector_bone (LrgIKChain  *self,
                                const gchar *bone_name)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));
	if (g_strcmp0 (self->effector, bone_name) == 0)
		return;
	g_free (self->effector);
	self->effector = g_strdup (bone_name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EFFECTOR_BONE]);
}

void
lrg_ik_chain_get_bone_axis (LrgIKChain *self,
                            gfloat     *x,
                            gfloat     *y,
                            gfloat     *z)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));
	if (x != NULL)
		*x = self->axis[0];
	if (y != NULL)
		*y = self->axis[1];
	if (z != NULL)
		*z = self->axis[2];
}

void
lrg_ik_chain_set_bone_axis (LrgIKChain *self,
                            gfloat      x,
                            gfloat      y,
                            gfloat      z)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));
	self->axis[0] = x;
	self->axis[1] = y;
	self->axis[2] = z;
	if (!v3_normalize (self->axis))
	{
		self->axis[0] = self->axis[2] = 0.0f;
		self->axis[1] = 1.0f;
	}
}

/* The end effector in model space. */
static gboolean
effector_position (LrgIKChain *self,
                   gfloat     *out)
{
	LrgBone           *tip;
	const LrgBonePose *wp;

	if (self->skeleton == NULL || self->bone_names->len == 0)
		return FALSE;
	if (self->effector != NULL)
	{
		LrgBone *eff = lrg_skeleton_get_bone_by_name (self->skeleton, self->effector);

		if (eff == NULL)
			return FALSE;
		wp = lrg_bone_get_world_pose (eff);
		out[0] = wp->position_x;
		out[1] = wp->position_y;
		out[2] = wp->position_z;
		return TRUE;
	}
	tip = lrg_ik_chain_get_bone (self, self->bone_names->len - 1);
	if (tip == NULL)
		return FALSE;
	wp = lrg_bone_get_world_pose (tip);
	{
		gfloat q[4], a[3], len = lrg_bone_get_length (tip);

		/* a bone without a length still has a direction (look-at) */
		if (len <= 0.0f)
			len = 1.0f;
		pose_rot (wp, q);
		q_rotate (q, self->axis, a);
		out[0] = wp->position_x + a[0] * len * wp->scale_x;
		out[1] = wp->position_y + a[1] * len * wp->scale_y;
		out[2] = wp->position_z + a[2] * len * wp->scale_z;
	}
	return TRUE;
}

guint
lrg_ik_chain_get_joint_positions (LrgIKChain *self,
                                  gfloat     *out_positions,
                                  guint       max_joints)
{
	guint  n, i;
	gfloat eff[3];

	g_return_val_if_fail (LRG_IS_IK_CHAIN (self), 0);
	g_return_val_if_fail (out_positions != NULL || max_joints == 0, 0);

	n = self->bone_names->len;
	if (n == 0 || !effector_position (self, eff))
		return 0;
	for (i = 0; i < n; i++)
	{
		LrgBone           *bone = lrg_ik_chain_get_bone (self, i);
		const LrgBonePose *wp;

		if (bone == NULL)
			return 0;
		if (i >= max_joints)
			continue;
		wp = lrg_bone_get_world_pose (bone);
		out_positions[3 * i] = wp->position_x;
		out_positions[3 * i + 1] = wp->position_y;
		out_positions[3 * i + 2] = wp->position_z;
	}
	if (n < max_joints)
		memcpy (out_positions + 3 * n, eff, sizeof eff);
	return n + 1;
}

gfloat
lrg_ik_chain_get_total_length (LrgIKChain *self)
{
	gfloat  total = 0.0f;
	gfloat *p;
	guint   n, i;

	g_return_val_if_fail (LRG_IS_IK_CHAIN (self), 0.0f);

	n = self->bone_names->len + 1;
	p = g_newa (gfloat, 3 * n);
	if (lrg_ik_chain_get_joint_positions (self, p, n) == n)
	{
		for (i = 0; i + 1 < n; i++)
		{
			gfloat d[3] = { p[3 * i + 3] - p[3 * i], p[3 * i + 4] - p[3 * i + 1], p[3 * i + 5] - p[3 * i + 2] };

			total += v3_len (d);
		}
		if (total > 0.0f)
			return total;
	}
	/* not posed yet: the declared bone lengths */
	for (i = 0; i < self->bone_names->len; i++)
	{
		LrgBone *b = lrg_ik_chain_get_bone (self, i);

		if (b != NULL)
			total += lrg_bone_get_length (b);
	}
	return total;
}

void
lrg_ik_chain_get_end_effector_position (LrgIKChain *self,
                                        gfloat     *x,
                                        gfloat     *y,
                                        gfloat     *z)
{
	gfloat p[3] = { 0.0f, 0.0f, 0.0f };

	g_return_if_fail (LRG_IS_IK_CHAIN (self));

	effector_position (self, p);
	if (x != NULL)
		*x = p[0];
	if (y != NULL)
		*y = p[1];
	if (z != NULL)
		*z = p[2];
}

/* Writes a new model-space rotation for @bone as a local rotation and
 * recomputes the world poses. */
static void
set_bone_model_rotation (LrgSkeleton  *skeleton,
                         LrgBone      *bone,
                         const gfloat *model_q)
{
	LrgBonePose local = *lrg_bone_get_local_pose (bone);
	gfloat      parent_q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	gfloat      inv[4], lq[4];
	gint        parent = lrg_bone_get_parent_index (bone);

	if (parent >= 0)
	{
		LrgBone *pb = lrg_skeleton_get_bone (skeleton, parent);

		if (pb != NULL)
			pose_rot (lrg_bone_get_world_pose (pb), parent_q);
	}
	inv[0] = -parent_q[0];
	inv[1] = -parent_q[1];
	inv[2] = -parent_q[2];
	inv[3] = parent_q[3];
	q_mul (inv, model_q, lq);
	q_normalize (lq);
	local.rotation_x = lq[0];
	local.rotation_y = lq[1];
	local.rotation_z = lq[2];
	local.rotation_w = lq[3];
	lrg_skeleton_set_pose (skeleton, lrg_bone_get_index (bone), &local);
	lrg_skeleton_calculate_world_poses (skeleton);
}

/* Pre-multiplies @bone's model-space rotation by @q. */
static void
rotate_bone_model (LrgSkeleton  *skeleton,
                   LrgBone      *bone,
                   const gfloat *q)
{
	gfloat cur[4], next[4];

	pose_rot (lrg_bone_get_world_pose (bone), cur);
	q_mul (q, cur, next);
	q_normalize (next);
	set_bone_model_rotation (skeleton, bone, next);
}

gboolean
lrg_ik_chain_aim_bone (LrgIKChain   *self,
                       guint         index,
                       const gfloat *from,
                       const gfloat *to)
{
	LrgBone *bone;
	gfloat   q[4];

	g_return_val_if_fail (LRG_IS_IK_CHAIN (self), FALSE);
	g_return_val_if_fail (from != NULL && to != NULL, FALSE);

	bone = lrg_ik_chain_get_bone (self, index);
	if (bone == NULL)
		return FALSE;
	q_from_to (from, to, q);
	rotate_bone_model (self->skeleton, bone, q);
	return TRUE;
}

void
lrg_ik_chain_apply_to_skeleton (LrgIKChain *self)
{
	g_return_if_fail (LRG_IS_IK_CHAIN (self));

	/* the solvers write the skeleton as they go: only refresh */
	if (self->skeleton != NULL)
		lrg_skeleton_calculate_world_poses (self->skeleton);
}

/* ==========================================================================
 * LrgIKGoal
 * ========================================================================== */

GType
lrg_ik_goal_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_IK_GOAL_TWO_BONE, "LRG_IK_GOAL_TWO_BONE", "two-bone" },
			{ LRG_IK_GOAL_CCD, "LRG_IK_GOAL_CCD", "ccd" },
			{ LRG_IK_GOAL_FABRIK, "LRG_IK_GOAL_FABRIK", "fabrik" },
			{ LRG_IK_GOAL_LOOK_AT, "LRG_IK_GOAL_LOOK_AT", "look-at" },
			{ LRG_IK_GOAL_FOOT, "LRG_IK_GOAL_FOOT", "foot" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgIKGoalKind", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

struct _LrgIKGoal
{
	GObject parent_instance;

	gchar         *name;
	LrgIKGoalKind  kind;
	GPtrArray     *bones;      /* gchar* */
	gchar         *effector;
	gfloat         pole[3];
	gboolean       has_pole;
	gfloat         target[3];  /* world */
	gboolean       has_target;
	gfloat         weight;
	guint          iterations;
	gfloat         tolerance;
	gboolean       enabled;
	gfloat         axis[3];
	gfloat         ray_up;
	gfloat         ray_down;
	gboolean       align;

	gfloat         last_target[3];
	gboolean       last_ok;

	LrgIKChain    *chain;      /* rebuilt when the skeleton changes */
	LrgIKSolver   *solver;
	GArray        *saved;      /* LrgBonePose, animated locals */
};

G_DEFINE_FINAL_TYPE (LrgIKGoal, lrg_ik_goal, G_TYPE_OBJECT)

enum
{
	GOAL_PROP_0,
	GOAL_PROP_NAME,
	GOAL_PROP_KIND,
	GOAL_PROP_EFFECTOR,
	GOAL_PROP_WEIGHT,
	GOAL_PROP_ITERATIONS,
	GOAL_PROP_TOLERANCE,
	GOAL_PROP_ENABLED,
	GOAL_N_PROPS
};

static GParamSpec *goal_properties[GOAL_N_PROPS];

static void
lrg_ik_goal_finalize (GObject *object)
{
	LrgIKGoal *self = LRG_IK_GOAL (object);

	g_free (self->name);
	g_free (self->effector);
	g_ptr_array_unref (self->bones);
	g_clear_object (&self->chain);
	g_clear_object (&self->solver);
	g_array_unref (self->saved);

	G_OBJECT_CLASS (lrg_ik_goal_parent_class)->finalize (object);
}

static void
lrg_ik_goal_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	LrgIKGoal *self = LRG_IK_GOAL (object);

	switch (prop_id)
	{
	case GOAL_PROP_NAME:       g_value_set_string (value, self->name); break;
	case GOAL_PROP_KIND:       g_value_set_enum (value, self->kind); break;
	case GOAL_PROP_EFFECTOR:   g_value_set_string (value, self->effector); break;
	case GOAL_PROP_WEIGHT:     g_value_set_float (value, self->weight); break;
	case GOAL_PROP_ITERATIONS: g_value_set_uint (value, self->iterations); break;
	case GOAL_PROP_TOLERANCE:  g_value_set_float (value, self->tolerance); break;
	case GOAL_PROP_ENABLED:    g_value_set_boolean (value, self->enabled); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_ik_goal_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	LrgIKGoal *self = LRG_IK_GOAL (object);

	switch (prop_id)
	{
	case GOAL_PROP_NAME:
		g_free (self->name);
		self->name = g_value_dup_string (value);
		break;
	case GOAL_PROP_KIND:
		self->kind = g_value_get_enum (value);
		break;
	case GOAL_PROP_EFFECTOR:
		lrg_ik_goal_set_effector (self, g_value_get_string (value));
		break;
	case GOAL_PROP_WEIGHT:
		lrg_ik_goal_set_weight (self, g_value_get_float (value));
		break;
	case GOAL_PROP_ITERATIONS:
		lrg_ik_goal_set_iterations (self, g_value_get_uint (value));
		break;
	case GOAL_PROP_TOLERANCE:
		lrg_ik_goal_set_tolerance (self, g_value_get_float (value));
		break;
	case GOAL_PROP_ENABLED:
		lrg_ik_goal_set_enabled (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_ik_goal_class_init (LrgIKGoalClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_ik_goal_finalize;
	object_class->get_property = lrg_ik_goal_get_property;
	object_class->set_property = lrg_ik_goal_set_property;

	/**
	 * LrgIKGoal:name:
	 *
	 * The goal's unique name.
	 */
	goal_properties[GOAL_PROP_NAME] =
		g_param_spec_string ("name", "Name", "Goal name", NULL,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgIKGoal:kind:
	 *
	 * The solver kind.
	 */
	goal_properties[GOAL_PROP_KIND] =
		g_param_spec_enum ("kind", "Kind", "Solver kind", LRG_TYPE_IK_GOAL_KIND, LRG_IK_GOAL_TWO_BONE,
		                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgIKGoal:effector:
	 *
	 * The effector bone.
	 */
	goal_properties[GOAL_PROP_EFFECTOR] =
		g_param_spec_string ("effector", "Effector", "Effector bone", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgIKGoal:weight:
	 *
	 * How much of the solve is used, 0..1.
	 */
	goal_properties[GOAL_PROP_WEIGHT] =
		g_param_spec_float ("weight", "Weight", "Solve weight", 0.0f, 1.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgIKGoal:iterations:
	 *
	 * Iteration budget of CCD/FABRIK.
	 */
	goal_properties[GOAL_PROP_ITERATIONS] =
		g_param_spec_uint ("iterations", "Iterations", "Iteration budget", 1, 1000, 10,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgIKGoal:tolerance:
	 *
	 * Early-out distance of CCD/FABRIK.
	 */
	goal_properties[GOAL_PROP_TOLERANCE] =
		g_param_spec_float ("tolerance", "Tolerance", "Stop distance", 0.0f, G_MAXFLOAT, 0.001f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgIKGoal:enabled:
	 *
	 * Whether the goal runs.
	 */
	goal_properties[GOAL_PROP_ENABLED] =
		g_param_spec_boolean ("enabled", "Enabled", "Whether the goal runs", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, GOAL_N_PROPS, goal_properties);
}

static void
lrg_ik_goal_init (LrgIKGoal *self)
{
	self->bones = g_ptr_array_new_with_free_func (g_free);
	self->weight = 1.0f;
	self->iterations = 10;
	self->tolerance = 0.001f;
	self->enabled = TRUE;
	self->axis[1] = 1.0f;
	self->ray_up = 0.5f;
	self->ray_down = 0.5f;
	self->saved = g_array_new (FALSE, TRUE, sizeof (LrgBonePose));
}

LrgIKGoal *
lrg_ik_goal_new (const gchar   *name,
                 LrgIKGoalKind  kind)
{
	return g_object_new (LRG_TYPE_IK_GOAL, "name", name, "kind", kind, NULL);
}

const gchar *
lrg_ik_goal_get_name (LrgIKGoal *self)
{
	g_return_val_if_fail (LRG_IS_IK_GOAL (self), NULL);
	return self->name;
}

LrgIKGoalKind
lrg_ik_goal_get_kind (LrgIKGoal *self)
{
	g_return_val_if_fail (LRG_IS_IK_GOAL (self), LRG_IK_GOAL_TWO_BONE);
	return self->kind;
}

void
lrg_ik_goal_set_bones (LrgIKGoal          *self,
                       const gchar *const *bones)
{
	guint i;

	g_return_if_fail (LRG_IS_IK_GOAL (self));

	g_ptr_array_set_size (self->bones, 0);
	for (i = 0; bones != NULL && bones[i] != NULL; i++)
		g_ptr_array_add (self->bones, g_strdup (bones[i]));
	g_clear_object (&self->chain);
}

gchar **
lrg_ik_goal_get_bones (LrgIKGoal *self)
{
	gchar **out;
	guint   i;

	g_return_val_if_fail (LRG_IS_IK_GOAL (self), NULL);
	out = g_new0 (gchar *, self->bones->len + 1);
	for (i = 0; i < self->bones->len; i++)
		out[i] = g_strdup (g_ptr_array_index (self->bones, i));
	return out;
}

const gchar *
lrg_ik_goal_get_effector (LrgIKGoal *self)
{
	g_return_val_if_fail (LRG_IS_IK_GOAL (self), NULL);
	return self->effector;
}

void
lrg_ik_goal_set_effector (LrgIKGoal   *self,
                          const gchar *bone_name)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	if (g_strcmp0 (self->effector, bone_name) == 0)
		return;
	g_free (self->effector);
	self->effector = g_strdup (bone_name);
	g_clear_object (&self->chain);
	g_object_notify_by_pspec (G_OBJECT (self), goal_properties[GOAL_PROP_EFFECTOR]);
}

void
lrg_ik_goal_set_pole (LrgIKGoal *self,
                      gfloat     x,
                      gfloat     y,
                      gfloat     z)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	self->pole[0] = x;
	self->pole[1] = y;
	self->pole[2] = z;
	self->has_pole = TRUE;
}

gboolean
lrg_ik_goal_get_pole (LrgIKGoal *self,
                      gfloat    *out_pole)
{
	g_return_val_if_fail (LRG_IS_IK_GOAL (self), FALSE);
	g_return_val_if_fail (out_pole != NULL, FALSE);
	memcpy (out_pole, self->pole, sizeof self->pole);
	return self->has_pole;
}

void
lrg_ik_goal_clear_pole (LrgIKGoal *self)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	self->has_pole = FALSE;
}

void
lrg_ik_goal_set_target (LrgIKGoal *self,
                        gfloat     x,
                        gfloat     y,
                        gfloat     z)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	self->target[0] = x;
	self->target[1] = y;
	self->target[2] = z;
	self->has_target = TRUE;
}

gboolean
lrg_ik_goal_get_target (LrgIKGoal *self,
                        gfloat    *out_target)
{
	g_return_val_if_fail (LRG_IS_IK_GOAL (self), FALSE);
	g_return_val_if_fail (out_target != NULL, FALSE);
	memcpy (out_target, self->target, sizeof self->target);
	return self->has_target;
}

void
lrg_ik_goal_clear_target (LrgIKGoal *self)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	self->has_target = FALSE;
}

gfloat
lrg_ik_goal_get_weight (LrgIKGoal *self)
{
	g_return_val_if_fail (LRG_IS_IK_GOAL (self), 0.0f);
	return self->weight;
}

void
lrg_ik_goal_set_weight (LrgIKGoal *self,
                        gfloat     weight)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	weight = CLAMP (weight, 0.0f, 1.0f);
	if (self->weight == weight)
		return;
	self->weight = weight;
	g_object_notify_by_pspec (G_OBJECT (self), goal_properties[GOAL_PROP_WEIGHT]);
}

guint
lrg_ik_goal_get_iterations (LrgIKGoal *self)
{
	g_return_val_if_fail (LRG_IS_IK_GOAL (self), 0);
	return self->iterations;
}

void
lrg_ik_goal_set_iterations (LrgIKGoal *self,
                            guint      iterations)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	iterations = MAX (iterations, 1);
	if (self->iterations == iterations)
		return;
	self->iterations = iterations;
	g_object_notify_by_pspec (G_OBJECT (self), goal_properties[GOAL_PROP_ITERATIONS]);
}

gfloat
lrg_ik_goal_get_tolerance (LrgIKGoal *self)
{
	g_return_val_if_fail (LRG_IS_IK_GOAL (self), 0.0f);
	return self->tolerance;
}

void
lrg_ik_goal_set_tolerance (LrgIKGoal *self,
                           gfloat     tolerance)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	tolerance = MAX (tolerance, 0.0f);
	if (self->tolerance == tolerance)
		return;
	self->tolerance = tolerance;
	g_object_notify_by_pspec (G_OBJECT (self), goal_properties[GOAL_PROP_TOLERANCE]);
}

gboolean
lrg_ik_goal_get_enabled (LrgIKGoal *self)
{
	g_return_val_if_fail (LRG_IS_IK_GOAL (self), FALSE);
	return self->enabled;
}

void
lrg_ik_goal_set_enabled (LrgIKGoal *self,
                         gboolean   enabled)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	enabled = !!enabled;
	if (self->enabled == enabled)
		return;
	self->enabled = enabled;
	g_object_notify_by_pspec (G_OBJECT (self), goal_properties[GOAL_PROP_ENABLED]);
}

void
lrg_ik_goal_set_bone_axis (LrgIKGoal *self,
                           gfloat     x,
                           gfloat     y,
                           gfloat     z)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	self->axis[0] = x;
	self->axis[1] = y;
	self->axis[2] = z;
	if (!v3_normalize (self->axis))
	{
		self->axis[0] = self->axis[2] = 0.0f;
		self->axis[1] = 1.0f;
	}
	if (self->chain != NULL)
		lrg_ik_chain_set_bone_axis (self->chain, self->axis[0], self->axis[1], self->axis[2]);
}

void
lrg_ik_goal_get_bone_axis (LrgIKGoal *self,
                           gfloat    *out_axis)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	g_return_if_fail (out_axis != NULL);
	memcpy (out_axis, self->axis, sizeof self->axis);
}

void
lrg_ik_goal_set_foot (LrgIKGoal *self,
                      gfloat     ray_up,
                      gfloat     ray_down,
                      gboolean   align)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	self->ray_up = MAX (ray_up, 0.0f);
	self->ray_down = MAX (ray_down, 0.0f);
	self->align = !!align;
}

void
lrg_ik_goal_get_foot (LrgIKGoal *self,
                      gfloat    *out_ray_up,
                      gfloat    *out_ray_down,
                      gboolean  *out_align)
{
	g_return_if_fail (LRG_IS_IK_GOAL (self));
	if (out_ray_up != NULL)
		*out_ray_up = self->ray_up;
	if (out_ray_down != NULL)
		*out_ray_down = self->ray_down;
	if (out_align != NULL)
		*out_align = self->align;
}

gboolean
lrg_ik_goal_get_last_target (LrgIKGoal *self,
                             gfloat    *out_target)
{
	g_return_val_if_fail (LRG_IS_IK_GOAL (self), FALSE);
	g_return_val_if_fail (out_target != NULL, FALSE);
	memcpy (out_target, self->last_target, sizeof self->last_target);
	return self->last_ok;
}

/* --- applying --------------------------------------------------------------- */

/* p' = M p for a column-major affine matrix. */
static void
mat_point (const gfloat *m, const gfloat *p, gfloat *out)
{
	gfloat x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
	gfloat y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
	gfloat z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];

	out[0] = x;
	out[1] = y;
	out[2] = z;
}

/* The inverse of a column-major affine matrix (3x3 inverse + translation). */
static gboolean
mat_affine_inverse (const gfloat *m, gfloat *inv)
{
	gfloat a = m[0], b = m[4], c = m[8];
	gfloat d = m[1], e = m[5], f = m[9];
	gfloat g = m[2], h = m[6], i = m[10];
	gfloat det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	gfloat t[3];

	if (fabsf (det) < 1e-12f)
		return FALSE;
	memset (inv, 0, 16 * sizeof (gfloat));
	inv[0] = (e * i - f * h) / det;
	inv[4] = (c * h - b * i) / det;
	inv[8] = (b * f - c * e) / det;
	inv[1] = (f * g - d * i) / det;
	inv[5] = (a * i - c * g) / det;
	inv[9] = (c * d - a * f) / det;
	inv[2] = (d * h - e * g) / det;
	inv[6] = (b * g - a * h) / det;
	inv[10] = (a * e - b * d) / det;
	inv[15] = 1.0f;
	t[0] = m[12];
	t[1] = m[13];
	t[2] = m[14];
	inv[12] = -(inv[0] * t[0] + inv[4] * t[1] + inv[8] * t[2]);
	inv[13] = -(inv[1] * t[0] + inv[5] * t[1] + inv[9] * t[2]);
	inv[14] = -(inv[2] * t[0] + inv[6] * t[1] + inv[10] * t[2]);
	return TRUE;
}

/* (Re)builds the chain and the solver for @skeleton. */
static gboolean
ensure_chain (LrgIKGoal   *self,
              LrgSkeleton *skeleton)
{
	guint i;

	if (self->chain != NULL && lrg_ik_chain_get_skeleton (self->chain) == skeleton)
		return TRUE;
	g_clear_object (&self->chain);
	if (self->bones->len == 0)
		return FALSE;

	self->chain = lrg_ik_chain_new (skeleton);
	for (i = 0; i < self->bones->len; i++)
		lrg_ik_chain_add_bone (self->chain, g_ptr_array_index (self->bones, i));
	lrg_ik_chain_set_effector_bone (self->chain, self->effector);
	lrg_ik_chain_set_bone_axis (self->chain, self->axis[0], self->axis[1], self->axis[2]);

	if (self->solver == NULL)
	{
		switch (self->kind)
		{
		case LRG_IK_GOAL_CCD:
			self->solver = LRG_IK_SOLVER (lrg_ik_solver_ccd_new ());
			break;
		case LRG_IK_GOAL_FABRIK:
			self->solver = LRG_IK_SOLVER (lrg_ik_solver_fabrik_new ());
			break;
		case LRG_IK_GOAL_LOOK_AT:
			self->solver = LRG_IK_SOLVER (lrg_ik_solver_look_at_new ());
			break;
		case LRG_IK_GOAL_TWO_BONE:
		case LRG_IK_GOAL_FOOT:
		default:
			self->solver = LRG_IK_SOLVER (lrg_ik_solver_two_bone_new ());
			break;
		}
	}
	return TRUE;
}

gboolean
lrg_ik_goal_apply (LrgIKGoal        *self,
                   LrgSkeleton      *skeleton,
                   const gfloat     *model_matrix,
                   LrgIKRaycastFunc  raycast,
                   gpointer          user_data)
{
	static const gfloat identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	const gfloat       *m = model_matrix != NULL ? model_matrix : identity;
	gfloat              inv[16];
	gfloat              target[3];
	gfloat              normal_model[3] = { 0.0f, 1.0f, 0.0f };
	gboolean            align = FALSE;
	LrgBone            *eff_bone = NULL;
	LrgBonePose        *saved;
	guint               n, i;

	g_return_val_if_fail (LRG_IS_IK_GOAL (self), FALSE);
	g_return_val_if_fail (LRG_IS_SKELETON (skeleton), FALSE);

	self->last_ok = FALSE;
	if (!self->enabled || self->weight <= 0.0f || !ensure_chain (self, skeleton))
		return FALSE;
	if (!mat_affine_inverse (m, inv))
		return FALSE;

	/* 1. the model-space target */
	if (self->kind == LRG_IK_GOAL_FOOT)
	{
		gfloat foot[3], foot_w[3], ground_w[3], origin[3];
		gfloat dir[3] = { 0.0f, -1.0f, 0.0f };
		gfloat hit[3], normal[3];
		gfloat ground_m[3];

		if (raycast == NULL)
			return FALSE;
		lrg_ik_chain_get_end_effector_position (self->chain, &foot[0], &foot[1], &foot[2]);
		mat_point (m, foot, foot_w);
		ground_m[0] = foot[0];
		ground_m[1] = 0.0f;
		ground_m[2] = foot[2];
		mat_point (m, ground_m, ground_w);
		origin[0] = foot_w[0];
		origin[1] = foot_w[1] + self->ray_up;
		origin[2] = foot_w[2];
		if (!raycast (origin, dir, self->ray_up + self->ray_down, hit, normal, user_data))
			return FALSE;

		/* keep the animated lift: move by the ground's height difference */
		foot_w[1] += hit[1] - ground_w[1];
		mat_point (inv, foot_w, target);
		if (self->align)
		{
			/* the normal as a model-space direction */
			gfloat p0[3] = { 0.0f, 0.0f, 0.0f }, p1[3], o[3], e[3];

			mat_point (inv, p0, o);
			p1[0] = normal[0];
			p1[1] = normal[1];
			p1[2] = normal[2];
			mat_point (inv, p1, e);
			normal_model[0] = e[0] - o[0];
			normal_model[1] = e[1] - o[1];
			normal_model[2] = e[2] - o[2];
			align = v3_normalize (normal_model);
		}
	}
	else
	{
		if (!self->has_target)
			return FALSE;
		mat_point (inv, self->target, target);
	}

	/* 2. remember the animated rotations (chain + effector) */
	n = self->bones->len;
	if (self->effector != NULL)
		eff_bone = lrg_skeleton_get_bone_by_name (skeleton, self->effector);
	g_array_set_size (self->saved, n + 1);
	saved = (LrgBonePose *) (gpointer) self->saved->data;
	for (i = 0; i < n; i++)
	{
		LrgBone *b = lrg_ik_chain_get_bone (self->chain, i);

		if (b == NULL)
			return FALSE;
		saved[i] = *lrg_bone_get_local_pose (b);
	}
	if (eff_bone != NULL)
		saved[n] = *lrg_bone_get_local_pose (eff_bone);

	/* 3. solve */
	lrg_ik_chain_set_target_position (self->chain, target[0], target[1], target[2]);
	if (self->has_pole)
		lrg_ik_chain_set_pole_position (self->chain, self->pole[0], self->pole[1], self->pole[2]);
	else
		lrg_ik_chain_clear_pole (self->chain);
	if (!lrg_ik_solver_solve (self->solver, self->chain, self->iterations, self->tolerance))
		return FALSE;

	/* 4. foot alignment: turn the effector's model-space up to the normal */
	if (align && eff_bone != NULL)
	{
		gfloat cur[4], up[3], up_dir[3] = { 0.0f, 1.0f, 0.0f }, q[4];

		pose_rot (lrg_bone_get_world_pose (eff_bone), cur);
		q_rotate (cur, up_dir, up);
		(void) up;
		q_from_to (up_dir, normal_model, q);
		rotate_bone_model (skeleton, eff_bone, q);
	}

	/* 5. weight: slerp the solved locals back toward the animated ones */
	if (self->weight < 1.0f)
	{
		for (i = 0; i <= n; i++)
		{
			LrgBone     *b = i < n ? lrg_ik_chain_get_bone (self->chain, i) : eff_bone;
			LrgBonePose  p;
			gfloat       a[4], s[4], r[4];

			if (b == NULL)
				continue;
			p = *lrg_bone_get_local_pose (b);
			pose_rot (&saved[i], a);
			pose_rot (&p, s);
			q_slerp (a, s, self->weight, r);
			p.rotation_x = r[0];
			p.rotation_y = r[1];
			p.rotation_z = r[2];
			p.rotation_w = r[3];
			lrg_skeleton_set_pose (skeleton, lrg_bone_get_index (b), &p);
		}
		lrg_skeleton_calculate_world_poses (skeleton);
	}

	memcpy (self->last_target, target, sizeof target);
	self->last_ok = TRUE;
	return TRUE;
}
