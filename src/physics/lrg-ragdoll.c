/* lrg-ragdoll.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhysicsAsset (the `.rphysasset` format: parse, validate, write,
 * generate from a skeleton) and LrgRagdoll (bodies and joints built from
 * an asset and a skeleton, motor drive toward an animation pose,
 * physics/animation blend and pose write-back).
 *
 * Portions derived from Jolt Physics (MIT), Copyright 2021 Jorrit Rouwe:
 * lrg_physics_asset_stabilize() follows RagdollSettings::Stabilize (its
 * mass pass) and lrg_ragdoll_drive_to_pose() follows
 * Ragdoll::DriveToPoseUsingMotors.
 *
 * Frames. Every part has three frames, all rigid (scale is ignored):
 *  - the bone frame B (the skeleton's model-space world pose of the bone,
 *    placed in the world by the root transform R);
 *  - the body frame = B * O, O being the part's offset (shape centre and
 *    rotation in the bone frame). The body origin is the shape origin;
 *  - the joint frame = B * J, J being the part's joint position/rotation
 *    in the bone frame. Its X axis is the twist/hinge/cone axis.
 * A joint links the parent part's body (A) to this part's body (B). Its
 * frame in B is O^-1 * J; in A it is taken from the bind pose,
 * (Bp_bind * Op)^-1 * (B_bind * J), so every joint is at rest (zero
 * twist and swing) in the bind pose and limits are measured from it.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "physics/lrg-ragdoll.h"
#include "animation/lrg-bone.h"
#include "core/lrg-data-loader.h"

#define R_PI (3.14159265358979323846f)

/* ==================================================================
 * Rigid transform helpers (position + xyzw rotation)
 * ================================================================== */

typedef struct
{
	gfloat p[3];
	gfloat q[4];
} Xf;

static void
q_mul (gfloat *o, const gfloat *a, const gfloat *b)
{
	gfloat x, y, z, w;

	x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	o[0] = x;
	o[1] = y;
	o[2] = z;
	o[3] = w;
}

static void
q_conj (gfloat *o, const gfloat *q)
{
	o[0] = -q[0];
	o[1] = -q[1];
	o[2] = -q[2];
	o[3] = q[3];
}

static void
q_normalize (gfloat *q)
{
	gfloat len;

	len = sqrtf (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (len < 1e-12f || !isfinite (len))
	{
		q[0] = q[1] = q[2] = 0.0f;
		q[3] = 1.0f;
		return;
	}
	q[0] /= len;
	q[1] /= len;
	q[2] /= len;
	q[3] /= len;
}

static void
q_rotate (gfloat *o, const gfloat *q, const gfloat *v)
{
	gfloat t[3];
	gfloat r[3];

	t[0] = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	t[1] = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	t[2] = 2.0f * (q[0] * v[1] - q[1] * v[0]);
	r[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
	r[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
	r[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
	o[0] = r[0];
	o[1] = r[1];
	o[2] = r[2];
}

/* shortest-arc rotation taking unit @from to unit @to */
static void
q_from_to (gfloat *o, const gfloat *from, const gfloat *to)
{
	gfloat d;
	gfloat c[3];

	d = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
	if (d < -0.9999f)
	{
		/* opposite: half turn about any perpendicular */
		if (fabsf (from[0]) < 0.9f)
		{
			c[0] = 0.0f;
			c[1] = from[2];
			c[2] = -from[1];
		}
		else
		{
			c[0] = -from[2];
			c[1] = 0.0f;
			c[2] = from[0];
		}
		o[0] = c[0];
		o[1] = c[1];
		o[2] = c[2];
		o[3] = 0.0f;
		q_normalize (o);
		return;
	}
	c[0] = from[1] * to[2] - from[2] * to[1];
	c[1] = from[2] * to[0] - from[0] * to[2];
	c[2] = from[0] * to[1] - from[1] * to[0];
	o[0] = c[0];
	o[1] = c[1];
	o[2] = c[2];
	o[3] = 1.0f + d;
	q_normalize (o);
}

static void
xf_identity (Xf *x)
{
	memset (x, 0, sizeof (*x));
	x->q[3] = 1.0f;
}

static void
xf_set (Xf *x, const gfloat *p, const gfloat *q)
{
	memcpy (x->p, p, sizeof (x->p));
	memcpy (x->q, q, sizeof (x->q));
	q_normalize (x->q);
}

/* o = a * b: apply b, then a */
static void
xf_mul (Xf *o, const Xf *a, const Xf *b)
{
	Xf r;

	q_rotate (r.p, a->q, b->p);
	r.p[0] += a->p[0];
	r.p[1] += a->p[1];
	r.p[2] += a->p[2];
	q_mul (r.q, a->q, b->q);
	q_normalize (r.q);
	*o = r;
}

static void
xf_inverse (Xf *o, const Xf *a)
{
	Xf r;
	gfloat n[3];

	q_conj (r.q, a->q);
	n[0] = -a->p[0];
	n[1] = -a->p[1];
	n[2] = -a->p[2];
	q_rotate (r.p, r.q, n);
	*o = r;
}

static void
xf_from_pose (Xf *o, const LrgBonePose *pose)
{
	o->p[0] = pose->position_x;
	o->p[1] = pose->position_y;
	o->p[2] = pose->position_z;
	o->q[0] = pose->rotation_x;
	o->q[1] = pose->rotation_y;
	o->q[2] = pose->rotation_z;
	o->q[3] = pose->rotation_w;
	q_normalize (o->q);
}

/* ==================================================================
 * Enums, errors, the part boxed type
 * ================================================================== */

G_DEFINE_QUARK (lrg-ragdoll-error-quark, lrg_ragdoll_error)

static GType
register_enum (const gchar *name, const GEnumValue *values)
{
	static GMutex lock;
	GType type;

	g_mutex_lock (&lock);
	type = g_type_from_name (name);
	if (type == 0)
		type = g_enum_register_static (name, values);
	g_mutex_unlock (&lock);
	return type;
}

GType
lrg_physics_asset_shape_get_type (void)
{
	static const GEnumValue values[] = {
		{ LRG_PHYSICS_ASSET_SHAPE_CAPSULE, "LRG_PHYSICS_ASSET_SHAPE_CAPSULE", "capsule" },
		{ LRG_PHYSICS_ASSET_SHAPE_BOX, "LRG_PHYSICS_ASSET_SHAPE_BOX", "box" },
		{ LRG_PHYSICS_ASSET_SHAPE_SPHERE, "LRG_PHYSICS_ASSET_SHAPE_SPHERE", "sphere" },
		{ 0, NULL, NULL }
	};

	return register_enum ("LrgPhysicsAssetShape", values);
}

GType
lrg_ragdoll_mode_get_type (void)
{
	static const GEnumValue values[] = {
		{ LRG_RAGDOLL_PASSIVE, "LRG_RAGDOLL_PASSIVE", "passive" },
		{ LRG_RAGDOLL_POWERED, "LRG_RAGDOLL_POWERED", "powered" },
		{ 0, NULL, NULL }
	};

	return register_enum ("LrgRagdollMode", values);
}

G_DEFINE_BOXED_TYPE (LrgPhysicsAssetPart, lrg_physics_asset_part,
                     lrg_physics_asset_part_copy, lrg_physics_asset_part_free)

/* the defaults the format leaves out (keep in sync with the docs) */
static void
part_defaults (LrgPhysicsAssetPart *part)
{
	memset (part, 0, sizeof (*part));
	part->shape = LRG_PHYSICS_ASSET_SHAPE_CAPSULE;
	part->radius = 0.05f;
	part->half_height = 0.1f;
	part->half_extents[0] = part->half_extents[1] = part->half_extents[2] = 0.1f;
	part->rotation[3] = 1.0f;
	part->mass = 1.0f;
	part->joint = LRG_JOINT_SWING_TWIST;
	part->joint_rotation[3] = 1.0f;
	part->twist_min = -0.25f;
	part->twist_max = 0.25f;
	part->swing_y = 0.5f;
	part->swing_z = 0.5f;
	part->motor_max_torque = 300.0f;
	part->motor_frequency = 10.0f;
	part->motor_damping = 8.0f;
}

LrgPhysicsAssetPart *
lrg_physics_asset_part_new (const gchar *bone)
{
	LrgPhysicsAssetPart *part;

	part = g_new (LrgPhysicsAssetPart, 1);
	part_defaults (part);
	part->bone = g_strdup (bone);
	return part;
}

LrgPhysicsAssetPart *
lrg_physics_asset_part_copy (const LrgPhysicsAssetPart *part)
{
	LrgPhysicsAssetPart *copy;

	g_return_val_if_fail (part != NULL, NULL);
	copy = g_new (LrgPhysicsAssetPart, 1);
	*copy = *part;
	copy->bone = g_strdup (part->bone);
	return copy;
}

void
lrg_physics_asset_part_free (LrgPhysicsAssetPart *part)
{
	if (part == NULL)
		return;
	g_free (part->bone);
	g_free (part);
}

/* ==================================================================
 * LrgPhysicsAsset
 * ================================================================== */

struct _LrgPhysicsAsset
{
	GObject parent_instance;

	gchar *name;
	gboolean exclude_adjacent;
	GPtrArray *parts;       /* LrgPhysicsAssetPart */
	GPtrArray *exclusions;  /* gchar*, two per exclusion */
};

G_DEFINE_FINAL_TYPE (LrgPhysicsAsset, lrg_physics_asset, G_TYPE_OBJECT)

enum {
	ASSET_PROP_0,
	ASSET_PROP_NAME,
	ASSET_PROP_EXCLUDE_ADJACENT,
	ASSET_N_PROPS
};

static GParamSpec *asset_properties[ASSET_N_PROPS];

static void
lrg_physics_asset_finalize (GObject *object)
{
	LrgPhysicsAsset *self = LRG_PHYSICS_ASSET (object);

	g_free (self->name);
	g_ptr_array_unref (self->parts);
	g_ptr_array_unref (self->exclusions);
	G_OBJECT_CLASS (lrg_physics_asset_parent_class)->finalize (object);
}

static void
lrg_physics_asset_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgPhysicsAsset *self = LRG_PHYSICS_ASSET (object);

	switch (prop_id)
	{
	case ASSET_PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case ASSET_PROP_EXCLUDE_ADJACENT:
		g_value_set_boolean (value, self->exclude_adjacent);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_physics_asset_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgPhysicsAsset *self = LRG_PHYSICS_ASSET (object);

	switch (prop_id)
	{
	case ASSET_PROP_NAME:
		lrg_physics_asset_set_name (self, g_value_get_string (value));
		break;
	case ASSET_PROP_EXCLUDE_ADJACENT:
		lrg_physics_asset_set_exclude_adjacent (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_physics_asset_class_init (LrgPhysicsAssetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_physics_asset_finalize;
	object_class->get_property = lrg_physics_asset_get_property;
	object_class->set_property = lrg_physics_asset_set_property;

	/**
	 * LrgPhysicsAsset:name:
	 *
	 * Display name of the asset (the `name:` key).
	 */
	asset_properties[ASSET_PROP_NAME] =
		g_param_spec_string ("name", "Name", "Display name", NULL,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
		                     G_PARAM_STATIC_STRINGS);

	/**
	 * LrgPhysicsAsset:exclude-adjacent:
	 *
	 * Whether a part never collides with its parent part (the
	 * `exclude-adjacent:` key).
	 */
	asset_properties[ASSET_PROP_EXCLUDE_ADJACENT] =
		g_param_spec_boolean ("exclude-adjacent", "Exclude adjacent",
		                      "Jointed parts do not collide", TRUE,
		                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
		                      G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, ASSET_N_PROPS, asset_properties);
}

static void
lrg_physics_asset_init (LrgPhysicsAsset *self)
{
	self->exclude_adjacent = TRUE;
	self->parts = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_physics_asset_part_free);
	self->exclusions = g_ptr_array_new_with_free_func (g_free);
}

LrgPhysicsAsset *
lrg_physics_asset_new (void)
{
	return g_object_new (LRG_TYPE_PHYSICS_ASSET, NULL);
}

const gchar *
lrg_physics_asset_get_name (LrgPhysicsAsset *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), NULL);
	return self->name;
}

void
lrg_physics_asset_set_name (LrgPhysicsAsset *self,
                            const gchar     *name)
{
	g_return_if_fail (LRG_IS_PHYSICS_ASSET (self));
	if (g_strcmp0 (self->name, name) == 0)
		return;
	g_free (self->name);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), asset_properties[ASSET_PROP_NAME]);
}

gboolean
lrg_physics_asset_get_exclude_adjacent (LrgPhysicsAsset *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), TRUE);
	return self->exclude_adjacent;
}

void
lrg_physics_asset_set_exclude_adjacent (LrgPhysicsAsset *self,
                                        gboolean         exclude)
{
	g_return_if_fail (LRG_IS_PHYSICS_ASSET (self));
	exclude = !!exclude;
	if (self->exclude_adjacent == exclude)
		return;
	self->exclude_adjacent = exclude;
	g_object_notify_by_pspec (G_OBJECT (self), asset_properties[ASSET_PROP_EXCLUDE_ADJACENT]);
}

guint
lrg_physics_asset_get_part_count (LrgPhysicsAsset *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), 0);
	return self->parts->len;
}

const LrgPhysicsAssetPart *
lrg_physics_asset_get_part (LrgPhysicsAsset *self,
                            guint            index)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), NULL);
	if (index >= self->parts->len)
		return NULL;
	return g_ptr_array_index (self->parts, index);
}

gint
lrg_physics_asset_find_part (LrgPhysicsAsset *self,
                             const gchar     *bone)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), -1);
	for (i = 0; i < self->parts->len; i++)
	{
		const LrgPhysicsAssetPart *p = g_ptr_array_index (self->parts, i);

		if (g_strcmp0 (p->bone, bone) == 0)
			return (gint) i;
	}
	return -1;
}

static gboolean
finite_n (const gfloat *v, guint n)
{
	guint i;

	for (i = 0; i < n; i++)
		if (!isfinite (v[i]))
			return FALSE;
	return TRUE;
}

/*
 * part_check:
 * Validates a part; @skip is the index allowed to hold the same bone
 * (the one being replaced), or -1.
 */
static gboolean
part_check (LrgPhysicsAsset            *self,
            const LrgPhysicsAssetPart  *part,
            gint                        skip,
            GError                    **error)
{
	gint existing;

	if (part->bone == NULL || part->bone[0] == '\0')
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT, "part has no bone");
		return FALSE;
	}
	existing = lrg_physics_asset_find_part (self, part->bone);
	if (existing >= 0 && existing != skip)
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_SKELETON,
		             "bone '%s' already has a part", part->bone);
		return FALSE;
	}
	if ((guint) part->shape > LRG_PHYSICS_ASSET_SHAPE_SPHERE)
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT,
		             "part '%s': unknown shape %d", part->bone, (gint) part->shape);
		return FALSE;
	}
	if (!isfinite (part->mass) || part->mass <= 0.0f ||
	    !isfinite (part->radius) || !isfinite (part->half_height) ||
	    !finite_n (part->half_extents, 3) || !finite_n (part->offset, 3) ||
	    !finite_n (part->rotation, 4) || !finite_n (part->joint_position, 3) ||
	    !finite_n (part->joint_rotation, 4))
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT,
		             "part '%s': mass must be positive and every value finite", part->bone);
		return FALSE;
	}
	if ((part->shape != LRG_PHYSICS_ASSET_SHAPE_BOX && part->radius <= 0.0f) ||
	    (part->shape == LRG_PHYSICS_ASSET_SHAPE_CAPSULE && part->half_height < 0.0f) ||
	    (part->shape == LRG_PHYSICS_ASSET_SHAPE_BOX &&
	     (part->half_extents[0] <= 0.0f || part->half_extents[1] <= 0.0f ||
	      part->half_extents[2] <= 0.0f)))
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT,
		             "part '%s': shape sizes must be positive", part->bone);
		return FALSE;
	}
	if (part->joint != LRG_JOINT_FIXED && part->joint != LRG_JOINT_POINT &&
	    part->joint != LRG_JOINT_HINGE && part->joint != LRG_JOINT_CONE &&
	    part->joint != LRG_JOINT_SWING_TWIST)
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT,
		             "part '%s': a ragdoll joint is fixed, point, hinge, cone or swing-twist",
		             part->bone);
		return FALSE;
	}
	if (!isfinite (part->twist_min) || !isfinite (part->twist_max) ||
	    part->twist_min < -R_PI || part->twist_max > R_PI || part->twist_min > part->twist_max ||
	    !isfinite (part->swing_y) || !isfinite (part->swing_z) ||
	    part->swing_y < 0.0f || part->swing_y > R_PI ||
	    part->swing_z < 0.0f || part->swing_z > R_PI)
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT,
		             "part '%s': twist must be an ordered range in [-pi, pi] and swing in [0, pi]",
		             part->bone);
		return FALSE;
	}
	if (!isfinite (part->friction) || part->friction < 0.0f ||
	    !isfinite (part->motor_max_torque) || part->motor_max_torque < 0.0f ||
	    !isfinite (part->motor_frequency) || part->motor_frequency <= 0.0f ||
	    !isfinite (part->motor_damping) || part->motor_damping < 0.0f)
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT,
		             "part '%s': friction and motor torque must be zero or positive, "
		             "motor frequency positive, damping zero or positive", part->bone);
		return FALSE;
	}
	return TRUE;
}

static LrgPhysicsAssetPart *
part_normalised_copy (const LrgPhysicsAssetPart *part)
{
	LrgPhysicsAssetPart *copy;

	copy = lrg_physics_asset_part_copy (part);
	q_normalize (copy->rotation);
	q_normalize (copy->joint_rotation);
	return copy;
}

gint
lrg_physics_asset_add_part (LrgPhysicsAsset            *self,
                            const LrgPhysicsAssetPart  *part,
                            GError                    **error)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), -1);
	g_return_val_if_fail (part != NULL, -1);

	if (!part_check (self, part, -1, error))
		return -1;
	g_ptr_array_add (self->parts, part_normalised_copy (part));
	return (gint) self->parts->len - 1;
}

gboolean
lrg_physics_asset_set_part (LrgPhysicsAsset            *self,
                            guint                       index,
                            const LrgPhysicsAssetPart  *part,
                            GError                    **error)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), FALSE);
	g_return_val_if_fail (part != NULL, FALSE);

	if (index >= self->parts->len)
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT,
		             "part index %u out of range", index);
		return FALSE;
	}
	if (!part_check (self, part, (gint) index, error))
		return FALSE;
	lrg_physics_asset_part_free (g_ptr_array_index (self->parts, index));
	g_ptr_array_index (self->parts, index) = part_normalised_copy (part);
	return TRUE;
}

void
lrg_physics_asset_remove_part (LrgPhysicsAsset *self,
                               guint            index)
{
	const LrgPhysicsAssetPart *part;
	guint i;

	g_return_if_fail (LRG_IS_PHYSICS_ASSET (self));
	g_return_if_fail (index < self->parts->len);

	part = g_ptr_array_index (self->parts, index);
	for (i = self->exclusions->len; i >= 2; i -= 2)
	{
		const gchar *a = g_ptr_array_index (self->exclusions, i - 2);
		const gchar *b = g_ptr_array_index (self->exclusions, i - 1);

		if (g_strcmp0 (a, part->bone) == 0 || g_strcmp0 (b, part->bone) == 0)
			g_ptr_array_remove_range (self->exclusions, i - 2, 2);
	}
	g_ptr_array_remove_index (self->parts, index);
}

void
lrg_physics_asset_add_exclusion (LrgPhysicsAsset *self,
                                 const gchar     *bone_a,
                                 const gchar     *bone_b)
{
	guint i;

	g_return_if_fail (LRG_IS_PHYSICS_ASSET (self));
	g_return_if_fail (bone_a != NULL && bone_b != NULL);
	g_return_if_fail (g_strcmp0 (bone_a, bone_b) != 0);

	for (i = 0; i + 1 < self->exclusions->len; i += 2)
	{
		const gchar *a = g_ptr_array_index (self->exclusions, i);
		const gchar *b = g_ptr_array_index (self->exclusions, i + 1);

		if ((g_strcmp0 (a, bone_a) == 0 && g_strcmp0 (b, bone_b) == 0) ||
		    (g_strcmp0 (a, bone_b) == 0 && g_strcmp0 (b, bone_a) == 0))
			return;
	}
	g_ptr_array_add (self->exclusions, g_strdup (bone_a));
	g_ptr_array_add (self->exclusions, g_strdup (bone_b));
}

guint
lrg_physics_asset_get_exclusion_count (LrgPhysicsAsset *self)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), 0);
	return self->exclusions->len / 2;
}

gboolean
lrg_physics_asset_get_exclusion (LrgPhysicsAsset  *self,
                                 guint             index,
                                 const gchar     **bone_a,
                                 const gchar     **bone_b)
{
	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), FALSE);
	if (index >= self->exclusions->len / 2)
		return FALSE;
	if (bone_a != NULL)
		*bone_a = g_ptr_array_index (self->exclusions, index * 2);
	if (bone_b != NULL)
		*bone_b = g_ptr_array_index (self->exclusions, index * 2 + 1);
	return TRUE;
}

void
lrg_physics_asset_remove_exclusion (LrgPhysicsAsset *self,
                                    guint            index)
{
	g_return_if_fail (LRG_IS_PHYSICS_ASSET (self));
	g_return_if_fail (index < self->exclusions->len / 2);
	g_ptr_array_remove_range (self->exclusions, index * 2, 2);
}

gboolean
lrg_physics_asset_validate (LrgPhysicsAsset  *self,
                            LrgSkeleton      *skeleton,
                            GError          **error)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), FALSE);
	g_return_val_if_fail (LRG_IS_SKELETON (skeleton), FALSE);

	for (i = 0; i < self->parts->len; i++)
	{
		const LrgPhysicsAssetPart *p = g_ptr_array_index (self->parts, i);

		if (lrg_skeleton_get_bone_by_name (skeleton, p->bone) == NULL)
		{
			g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_SKELETON,
			             "/parts/%u/bone: the skeleton has no bone '%s'", i, p->bone);
			return FALSE;
		}
	}
	for (i = 0; i < self->exclusions->len; i++)
	{
		const gchar *bone = g_ptr_array_index (self->exclusions, i);

		if (lrg_physics_asset_find_part (self, bone) < 0)
		{
			g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_SKELETON,
			             "/exclusions/%u: bone '%s' has no part", i / 2, bone);
			return FALSE;
		}
	}
	return TRUE;
}

/* ------------------------------------------------------------------
 * Parsing (hand-written format, see docs/modules/physics/ragdoll.org)
 * ------------------------------------------------------------------ */

typedef struct
{
	const gchar *source;
	GError **error;
} Ctx;

static gboolean G_GNUC_PRINTF (3, 4)
fail (Ctx *ctx, const gchar *path, const gchar *format, ...)
{
	va_list args;
	g_autofree gchar *msg = NULL;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error (ctx->error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_FORMAT, "%s: %s: %s",
	             ctx->source, path, msg);
	return FALSE;
}

static gboolean
read_float (Ctx *ctx, YamlNode *node, const gchar *path, gfloat *out)
{
	const gchar *text;
	gchar *end;
	gdouble v;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return fail (ctx, path, "expected a number");
	text = yaml_node_get_scalar (node);
	if (text == NULL || *text == '\0')
		return fail (ctx, path, "expected a number");
	v = g_ascii_strtod (text, &end);
	if (end == NULL || *end != '\0' || !isfinite (v))
		return fail (ctx, path, "expected a number, got '%s'", text);
	*out = (gfloat) v;
	return TRUE;
}

static gboolean
read_bool (Ctx *ctx, YamlNode *node, const gchar *path, gboolean *out)
{
	const gchar *text;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return fail (ctx, path, "expected true or false");
	text = yaml_node_get_scalar (node);
	if (g_strcmp0 (text, "true") == 0)
		*out = TRUE;
	else if (g_strcmp0 (text, "false") == 0)
		*out = FALSE;
	else
		return fail (ctx, path, "expected true or false, got '%s'", text != NULL ? text : "");
	return TRUE;
}

static gboolean
read_string (Ctx *ctx, YamlNode *node, const gchar *path, const gchar **out)
{
	const gchar *text;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return fail (ctx, path, "expected a string");
	text = yaml_node_get_scalar (node);
	if (text == NULL || *text == '\0')
		return fail (ctx, path, "expected a non-empty string");
	*out = text;
	return TRUE;
}

/* a sequence of exactly @n numbers */
static gboolean
read_vector (Ctx *ctx, YamlNode *node, const gchar *path, gfloat *out, guint n)
{
	YamlSequence *seq;
	guint i;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return fail (ctx, path, "expected a list of %u numbers", n);
	seq = yaml_node_get_sequence (node);
	if (yaml_sequence_get_length (seq) != n)
		return fail (ctx, path, "expected a list of %u numbers", n);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *p = g_strdup_printf ("%s/%u", path, i);

		if (!read_float (ctx, yaml_sequence_get_element (seq, i), p, &out[i]))
			return FALSE;
	}
	return TRUE;
}

static gboolean
read_rotation (Ctx *ctx, YamlNode *node, const gchar *path, gfloat *out)
{
	if (!read_vector (ctx, node, path, out, 4))
		return FALSE;
	if (out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f && out[3] == 0.0f)
		return fail (ctx, path, "a rotation quaternion cannot be zero");
	q_normalize (out);
	return TRUE;
}

static gboolean
key_in (const gchar *key, const gchar * const *allowed)
{
	guint i;

	for (i = 0; allowed[i] != NULL; i++)
		if (g_strcmp0 (key, allowed[i]) == 0)
			return TRUE;
	return FALSE;
}

static gboolean
parse_joint (Ctx *ctx, YamlNode *node, const gchar *path, LrgPhysicsAssetPart *part)
{
	static const gchar * const keys[] = {
		"kind", "position", "rotation", "twist", "swing", "friction",
		"motor-max-torque", "motor-frequency", "motor-damping", NULL
	};
	YamlMapping *map;
	GList *members;
	GList *l;
	gboolean ok;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return fail (ctx, path, "expected a mapping");
	map = yaml_node_get_mapping (node);

	/* the kind first: it decides which other keys apply */
	if (yaml_mapping_has_member (map, "kind"))
	{
		const gchar *kind;
		g_autofree gchar *p = g_strdup_printf ("%s/kind", path);

		if (!read_string (ctx, yaml_mapping_get_member (map, "kind"), p, &kind))
			return FALSE;
		if (g_strcmp0 (kind, "fixed") == 0)
			part->joint = LRG_JOINT_FIXED;
		else if (g_strcmp0 (kind, "point") == 0)
			part->joint = LRG_JOINT_POINT;
		else if (g_strcmp0 (kind, "hinge") == 0)
			part->joint = LRG_JOINT_HINGE;
		else if (g_strcmp0 (kind, "cone") == 0)
			part->joint = LRG_JOINT_CONE;
		else if (g_strcmp0 (kind, "swing-twist") == 0)
			part->joint = LRG_JOINT_SWING_TWIST;
		else
			return fail (ctx, p, "expected fixed, point, hinge, cone or swing-twist, got '%s'", kind);
	}

	ok = TRUE;
	members = yaml_mapping_get_members (map);
	for (l = members; l != NULL && ok; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode *value = yaml_mapping_get_member (map, key);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", path, key);

		if (!key_in (key, keys))
			ok = fail (ctx, p, "unknown key");
		else if (g_strcmp0 (key, "kind") == 0)
			continue;
		else if (g_strcmp0 (key, "position") == 0)
			ok = read_vector (ctx, value, p, part->joint_position, 3);
		else if (g_strcmp0 (key, "rotation") == 0)
			ok = read_rotation (ctx, value, p, part->joint_rotation);
		else if (g_strcmp0 (key, "twist") == 0)
		{
			gfloat t[2];

			if (part->joint != LRG_JOINT_HINGE && part->joint != LRG_JOINT_SWING_TWIST)
				ok = fail (ctx, p, "twist limits apply to hinge and swing-twist joints");
			else if ((ok = read_vector (ctx, value, p, t, 2)))
			{
				if (t[0] < -R_PI || t[1] > R_PI || t[0] > t[1])
					ok = fail (ctx, p, "expected [min, max] with -pi <= min <= max <= pi");
				part->twist_min = t[0];
				part->twist_max = t[1];
			}
		}
		else if (g_strcmp0 (key, "swing") == 0)
		{
			gfloat s[2];

			if (part->joint != LRG_JOINT_CONE && part->joint != LRG_JOINT_SWING_TWIST)
				ok = fail (ctx, p, "swing limits apply to cone and swing-twist joints");
			else if ((ok = read_vector (ctx, value, p, s, 2)))
			{
				if (s[0] < 0.0f || s[0] > R_PI || s[1] < 0.0f || s[1] > R_PI)
					ok = fail (ctx, p, "expected [y, z] half angles in [0, pi]");
				part->swing_y = s[0];
				part->swing_z = s[1];
			}
		}
		else
		{
			gfloat v = 0.0f;

			ok = read_float (ctx, value, p, &v);
			if (!ok)
				break;
			if (g_strcmp0 (key, "friction") == 0)
			{
				if (v < 0.0f)
					ok = fail (ctx, p, "must be zero or positive");
				part->friction = v;
			}
			else if (g_strcmp0 (key, "motor-max-torque") == 0)
			{
				if (v < 0.0f)
					ok = fail (ctx, p, "must be zero or positive");
				part->motor_max_torque = v;
			}
			else if (g_strcmp0 (key, "motor-frequency") == 0)
			{
				if (v <= 0.0f)
					ok = fail (ctx, p, "must be positive");
				part->motor_frequency = v;
			}
			else
			{
				if (v < 0.0f)
					ok = fail (ctx, p, "must be zero or positive");
				part->motor_damping = v;
			}
		}
	}
	g_list_free (members);
	return ok;
}

static gboolean
parse_part (Ctx *ctx, YamlNode *node, const gchar *path, LrgPhysicsAssetPart *part)
{
	static const gchar * const keys[] = {
		"bone", "shape", "radius", "half-height", "half-extents", "offset", "rotation",
		"mass", "joint", NULL
	};
	YamlMapping *map;
	GList *members;
	GList *l;
	gboolean ok;
	const gchar *text;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return fail (ctx, path, "expected a mapping");
	map = yaml_node_get_mapping (node);
	if (!yaml_mapping_has_member (map, "bone"))
	{
		g_autofree gchar *p = g_strdup_printf ("%s/bone", path);

		return fail (ctx, p, "missing (every part names its bone)");
	}
	if (yaml_mapping_has_member (map, "shape"))
	{
		g_autofree gchar *p = g_strdup_printf ("%s/shape", path);

		if (!read_string (ctx, yaml_mapping_get_member (map, "shape"), p, &text))
			return FALSE;
		if (g_strcmp0 (text, "capsule") == 0)
			part->shape = LRG_PHYSICS_ASSET_SHAPE_CAPSULE;
		else if (g_strcmp0 (text, "box") == 0)
			part->shape = LRG_PHYSICS_ASSET_SHAPE_BOX;
		else if (g_strcmp0 (text, "sphere") == 0)
			part->shape = LRG_PHYSICS_ASSET_SHAPE_SPHERE;
		else
			return fail (ctx, p, "expected capsule, box or sphere, got '%s'", text);
	}

	ok = TRUE;
	members = yaml_mapping_get_members (map);
	for (l = members; l != NULL && ok; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode *value = yaml_mapping_get_member (map, key);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", path, key);

		if (!key_in (key, keys))
			ok = fail (ctx, p, "unknown key");
		else if (g_strcmp0 (key, "shape") == 0)
			continue;
		else if (g_strcmp0 (key, "bone") == 0)
		{
			if ((ok = read_string (ctx, value, p, &text)))
			{
				g_free (part->bone);
				part->bone = g_strdup (text);
			}
		}
		else if (g_strcmp0 (key, "radius") == 0)
		{
			if (part->shape == LRG_PHYSICS_ASSET_SHAPE_BOX)
				ok = fail (ctx, p, "does not apply to a box (use half-extents)");
			else if ((ok = read_float (ctx, value, p, &part->radius)) && part->radius <= 0.0f)
				ok = fail (ctx, p, "must be positive");
		}
		else if (g_strcmp0 (key, "half-height") == 0)
		{
			if (part->shape != LRG_PHYSICS_ASSET_SHAPE_CAPSULE)
				ok = fail (ctx, p, "applies to a capsule only");
			else if ((ok = read_float (ctx, value, p, &part->half_height)) && part->half_height < 0.0f)
				ok = fail (ctx, p, "must be zero or positive");
		}
		else if (g_strcmp0 (key, "half-extents") == 0)
		{
			if (part->shape != LRG_PHYSICS_ASSET_SHAPE_BOX)
				ok = fail (ctx, p, "applies to a box only");
			else if ((ok = read_vector (ctx, value, p, part->half_extents, 3)) &&
			         (part->half_extents[0] <= 0.0f || part->half_extents[1] <= 0.0f ||
			          part->half_extents[2] <= 0.0f))
				ok = fail (ctx, p, "every half extent must be positive");
		}
		else if (g_strcmp0 (key, "offset") == 0)
			ok = read_vector (ctx, value, p, part->offset, 3);
		else if (g_strcmp0 (key, "rotation") == 0)
			ok = read_rotation (ctx, value, p, part->rotation);
		else if (g_strcmp0 (key, "mass") == 0)
		{
			if ((ok = read_float (ctx, value, p, &part->mass)) && part->mass <= 0.0f)
				ok = fail (ctx, p, "must be positive");
		}
		else
			ok = parse_joint (ctx, value, p, part);
	}
	g_list_free (members);
	return ok;
}

static LrgPhysicsAsset *
asset_from_node (YamlNode *root, const gchar *source, GError **error)
{
	static const gchar * const keys[] = {
		"version", "name", "exclude-adjacent", "parts", "exclusions", NULL
	};
	g_autoptr(LrgPhysicsAsset) self = NULL;
	YamlMapping *map;
	GList *members;
	GList *l;
	gboolean ok;
	gfloat version;
	Ctx ctx;

	ctx.source = source;
	ctx.error = error;
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_PARSE,
		             "%s: a physics asset is a mapping", source);
		return NULL;
	}
	map = yaml_node_get_mapping (root);
	if (!yaml_mapping_has_member (map, "version"))
	{
		fail (&ctx, "/version", "missing");
		return NULL;
	}
	if (!read_float (&ctx, yaml_mapping_get_member (map, "version"), "/version", &version))
		return NULL;
	if (version != floorf (version) || version < 1.0f)
	{
		fail (&ctx, "/version", "expected a positive whole number");
		return NULL;
	}
	if (version > (gfloat) LRG_PHYSICS_ASSET_FORMAT_VERSION)
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_VERSION,
		             "%s: /version: physics asset version %d is newer than %d", source,
		             (gint) version, LRG_PHYSICS_ASSET_FORMAT_VERSION);
		return NULL;
	}

	self = lrg_physics_asset_new ();
	ok = TRUE;
	members = yaml_mapping_get_members (map);
	for (l = members; l != NULL && ok; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode *value = yaml_mapping_get_member (map, key);
		g_autofree gchar *p = g_strdup_printf ("/%s", key);

		if (!key_in (key, keys))
			ok = fail (&ctx, p, "unknown key");
		else if (g_strcmp0 (key, "name") == 0)
		{
			const gchar *name;

			if ((ok = read_string (&ctx, value, p, &name)))
				lrg_physics_asset_set_name (self, name);
		}
		else if (g_strcmp0 (key, "exclude-adjacent") == 0)
			ok = read_bool (&ctx, value, p, &self->exclude_adjacent);
		else if (g_strcmp0 (key, "parts") == 0)
		{
			YamlSequence *seq;
			guint i;

			if (value == NULL || yaml_node_get_node_type (value) != YAML_NODE_SEQUENCE)
			{
				ok = fail (&ctx, p, "expected a list of parts");
				break;
			}
			seq = yaml_node_get_sequence (value);
			for (i = 0; ok && i < yaml_sequence_get_length (seq); i++)
			{
				g_autoptr(LrgPhysicsAssetPart) part = lrg_physics_asset_part_new (NULL);
				g_autofree gchar *pp = g_strdup_printf ("/parts/%u", i);
				g_autoptr(GError) local = NULL;

				ok = parse_part (&ctx, yaml_sequence_get_element (seq, i), pp, part);
				if (ok && lrg_physics_asset_add_part (self, part, &local) < 0)
				{
					g_autofree gchar *bp = g_strdup_printf ("%s/bone", pp);

					ok = fail (&ctx, bp, "%s", local->message);
				}
			}
		}
		else if (g_strcmp0 (key, "exclusions") == 0)
		{
			YamlSequence *seq;
			guint i;

			if (value == NULL || yaml_node_get_node_type (value) != YAML_NODE_SEQUENCE)
			{
				ok = fail (&ctx, p, "expected a list of [bone, bone] pairs");
				break;
			}
			seq = yaml_node_get_sequence (value);
			for (i = 0; ok && i < yaml_sequence_get_length (seq); i++)
			{
				YamlNode *pair = yaml_sequence_get_element (seq, i);
				g_autofree gchar *pp = g_strdup_printf ("/exclusions/%u", i);
				const gchar *a;
				const gchar *b;
				YamlSequence *ps;

				if (pair == NULL || yaml_node_get_node_type (pair) != YAML_NODE_SEQUENCE ||
				    yaml_sequence_get_length (yaml_node_get_sequence (pair)) != 2)
				{
					ok = fail (&ctx, pp, "expected [bone, bone]");
					break;
				}
				ps = yaml_node_get_sequence (pair);
				if (!read_string (&ctx, yaml_sequence_get_element (ps, 0), pp, &a) ||
				    !read_string (&ctx, yaml_sequence_get_element (ps, 1), pp, &b))
				{
					ok = FALSE;
					break;
				}
				if (g_strcmp0 (a, b) == 0)
				{
					ok = fail (&ctx, pp, "a bone cannot be excluded from itself");
					break;
				}
				lrg_physics_asset_add_exclusion (self, a, b);
			}
		}
	}
	g_list_free (members);
	if (!ok)
		return NULL;

	/* every exclusion must name a part */
	{
		guint i;

		for (i = 0; i < self->exclusions->len; i++)
			if (lrg_physics_asset_find_part (self, g_ptr_array_index (self->exclusions, i)) < 0)
			{
				g_autofree gchar *pp = g_strdup_printf ("/exclusions/%u", i / 2);

				fail (&ctx, pp, "bone '%s' has no part",
				      (const gchar *) g_ptr_array_index (self->exclusions, i));
				return NULL;
			}
	}
	return g_steal_pointer (&self);
}

LrgPhysicsAsset *
lrg_physics_asset_new_from_data (const gchar  *data,
                                 gssize        length,
                                 GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_PARSE, "<data>: %s",
		             local->message);
		return NULL;
	}
	return asset_from_node (yaml_parser_get_root (parser), "<data>", error);
}

LrgPhysicsAsset *
lrg_physics_asset_new_from_file (const gchar  *path,
                                 GError      **error)
{
	g_autofree gchar *text = NULL;
	gsize length;
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &text, &length, error))
		return NULL;
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, text, (gssize) length, &local))
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_PARSE, "%s: %s", path,
		             local->message);
		return NULL;
	}
	return asset_from_node (yaml_parser_get_root (parser), path, error);
}

/* ------------------------------------------------------------------
 * Writing
 * ------------------------------------------------------------------ */

static void
set_float (YamlMapping *map, const gchar *key, gfloat value)
{
	g_autoptr(YamlNode) node = lrg_data_yaml_float (value);

	yaml_mapping_set_member (map, key, node);
}

static void
set_vector (YamlMapping *map, const gchar *key, const gfloat *v, guint n)
{
	YamlSequence *seq;
	g_autoptr(YamlNode) node = NULL;
	guint i;

	seq = yaml_sequence_new ();
	for (i = 0; i < n; i++)
	{
		g_autoptr(YamlNode) e = lrg_data_yaml_float (v[i]);

		yaml_sequence_add_element (seq, e);
	}
	node = yaml_node_new_sequence (seq);
	yaml_sequence_unref (seq);
	yaml_mapping_set_member (map, key, node);
}

static gboolean
vec_equal (const gfloat *a, const gfloat *b, guint n)
{
	return memcmp (a, b, n * sizeof (gfloat)) == 0;
}

static const gchar *
joint_nick (LrgJointKind kind)
{
	switch (kind)
	{
	case LRG_JOINT_FIXED:
		return "fixed";
	case LRG_JOINT_POINT:
		return "point";
	case LRG_JOINT_HINGE:
		return "hinge";
	case LRG_JOINT_CONE:
		return "cone";
	case LRG_JOINT_SWING_TWIST:
	case LRG_JOINT_SLIDER:
	case LRG_JOINT_DISTANCE:
	case LRG_JOINT_SIX_DOF:
	default:
		return "swing-twist";
	}
}

static YamlMapping *
part_to_yaml (const LrgPhysicsAssetPart *part)
{
	LrgPhysicsAssetPart d;
	YamlMapping *map;
	YamlMapping *joint;
	g_autoptr(YamlNode) bone = NULL;

	part_defaults (&d);
	map = yaml_mapping_new ();
	bone = lrg_data_yaml_string (part->bone);
	yaml_mapping_set_member (map, "bone", bone);
	if (part->shape == LRG_PHYSICS_ASSET_SHAPE_BOX)
		yaml_mapping_set_string_member (map, "shape", "box");
	else if (part->shape == LRG_PHYSICS_ASSET_SHAPE_SPHERE)
		yaml_mapping_set_string_member (map, "shape", "sphere");
	if (part->shape != LRG_PHYSICS_ASSET_SHAPE_BOX && part->radius != d.radius)
		set_float (map, "radius", part->radius);
	if (part->shape == LRG_PHYSICS_ASSET_SHAPE_CAPSULE && part->half_height != d.half_height)
		set_float (map, "half-height", part->half_height);
	if (part->shape == LRG_PHYSICS_ASSET_SHAPE_BOX && !vec_equal (part->half_extents, d.half_extents, 3))
		set_vector (map, "half-extents", part->half_extents, 3);
	if (!vec_equal (part->offset, d.offset, 3))
		set_vector (map, "offset", part->offset, 3);
	if (!vec_equal (part->rotation, d.rotation, 4))
		set_vector (map, "rotation", part->rotation, 4);
	if (part->mass != d.mass)
		set_float (map, "mass", part->mass);

	/* the joint block, with only what differs from the defaults */
	joint = yaml_mapping_new ();
	if (part->joint != d.joint)
		yaml_mapping_set_string_member (joint, "kind", joint_nick (part->joint));
	if (!vec_equal (part->joint_position, d.joint_position, 3))
		set_vector (joint, "position", part->joint_position, 3);
	if (!vec_equal (part->joint_rotation, d.joint_rotation, 4))
		set_vector (joint, "rotation", part->joint_rotation, 4);
	if ((part->joint == LRG_JOINT_HINGE || part->joint == LRG_JOINT_SWING_TWIST) &&
	    (part->twist_min != d.twist_min || part->twist_max != d.twist_max))
	{
		gfloat t[2];

		t[0] = part->twist_min;
		t[1] = part->twist_max;
		set_vector (joint, "twist", t, 2);
	}
	if ((part->joint == LRG_JOINT_CONE || part->joint == LRG_JOINT_SWING_TWIST) &&
	    (part->swing_y != d.swing_y || part->swing_z != d.swing_z))
	{
		gfloat s[2];

		s[0] = part->swing_y;
		s[1] = part->swing_z;
		set_vector (joint, "swing", s, 2);
	}
	if (part->friction != d.friction)
		set_float (joint, "friction", part->friction);
	if (part->motor_max_torque != d.motor_max_torque)
		set_float (joint, "motor-max-torque", part->motor_max_torque);
	if (part->motor_frequency != d.motor_frequency)
		set_float (joint, "motor-frequency", part->motor_frequency);
	if (part->motor_damping != d.motor_damping)
		set_float (joint, "motor-damping", part->motor_damping);
	if (yaml_mapping_get_size (joint) > 0)
		yaml_mapping_set_mapping_member (map, "joint", joint);
	yaml_mapping_unref (joint);
	return map;
}

gchar *
lrg_physics_asset_save_to_data (LrgPhysicsAsset  *self,
                                gsize            *length,
                                GError          **error)
{
	g_autoptr(YamlNode) root_node = NULL;
	YamlMapping *root;
	guint i;

	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	root = yaml_mapping_new ();
	yaml_mapping_set_int_member (root, "version", LRG_PHYSICS_ASSET_FORMAT_VERSION);
	if (self->name != NULL)
	{
		g_autoptr(YamlNode) name = lrg_data_yaml_string (self->name);

		yaml_mapping_set_member (root, "name", name);
	}
	if (!self->exclude_adjacent)
		yaml_mapping_set_boolean_member (root, "exclude-adjacent", FALSE);
	if (self->parts->len > 0)
	{
		YamlSequence *parts = yaml_sequence_new ();

		for (i = 0; i < self->parts->len; i++)
		{
			YamlMapping *pm = part_to_yaml (g_ptr_array_index (self->parts, i));

			yaml_sequence_add_mapping_element (parts, pm);
			yaml_mapping_unref (pm);
		}
		yaml_mapping_set_sequence_member (root, "parts", parts);
		yaml_sequence_unref (parts);
	}
	if (self->exclusions->len > 0)
	{
		YamlSequence *ex = yaml_sequence_new ();

		for (i = 0; i + 1 < self->exclusions->len; i += 2)
		{
			YamlSequence *pair = yaml_sequence_new ();
			g_autoptr(YamlNode) a = lrg_data_yaml_string (g_ptr_array_index (self->exclusions, i));
			g_autoptr(YamlNode) b = lrg_data_yaml_string (g_ptr_array_index (self->exclusions, i + 1));

			yaml_sequence_add_element (pair, a);
			yaml_sequence_add_element (pair, b);
			yaml_sequence_add_sequence_element (ex, pair);
			yaml_sequence_unref (pair);
		}
		yaml_mapping_set_sequence_member (root, "exclusions", ex);
		yaml_sequence_unref (ex);
	}
	root_node = yaml_node_new_mapping (root);
	yaml_mapping_unref (root);
	return lrg_data_yaml_to_data (root_node, length, error);
}

gboolean
lrg_physics_asset_save_to_file (LrgPhysicsAsset  *self,
                                const gchar      *path,
                                GError          **error)
{
	g_autofree gchar *text = NULL;
	gsize length;

	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_physics_asset_save_to_data (self, &length, error);
	if (text == NULL)
		return FALSE;
	return g_file_set_contents (path, text, (gssize) length, error);
}

/* ------------------------------------------------------------------
 * Generation from a skeleton
 * ------------------------------------------------------------------ */

static LrgBone *
first_child (LrgSkeleton *skeleton, LrgBone *bone)
{
	GList *children;
	LrgBone *best;
	GList *l;

	/* the child with the lowest index, so the result does not depend on list order */
	children = lrg_skeleton_get_children (skeleton, bone);
	best = NULL;
	for (l = children; l != NULL; l = l->next)
		if (best == NULL || lrg_bone_get_index (l->data) < lrg_bone_get_index (best))
			best = l->data;
	g_list_free (children);
	return best;
}

LrgPhysicsAsset *
lrg_physics_asset_new_from_skeleton (LrgSkeleton *skeleton,
                                     gfloat       total_mass,
                                     gfloat       radius_ratio)
{
	LrgPhysicsAsset *self;
	guint n;
	guint i;
	gfloat total_volume;
	g_autofree gfloat *radii = NULL;
	gfloat y_axis[3] = { 0.0f, 1.0f, 0.0f };
	gfloat x_axis[3] = { 1.0f, 0.0f, 0.0f };

	g_return_val_if_fail (LRG_IS_SKELETON (skeleton), NULL);
	g_return_val_if_fail (total_mass > 0.0f, NULL);

	radius_ratio = CLAMP (radius_ratio, 0.05f, 1.0f);
	self = lrg_physics_asset_new ();
	n = lrg_skeleton_get_bone_count (skeleton);
	radii = g_new0 (gfloat, MAX (n, 1u));
	total_volume = 0.0f;

	for (i = 0; i < n; i++)
	{
		LrgBone *bone = lrg_skeleton_get_bone (skeleton, (gint) i);
		LrgBone *child;
		g_autoptr(LrgPhysicsAssetPart) part = NULL;
		gfloat dir[3];
		gfloat len;

		if (bone == NULL)
			continue;
		part = lrg_physics_asset_part_new (lrg_bone_get_name (bone));
		child = first_child (skeleton, bone);
		if (child != NULL)
		{
			const LrgBonePose *cb = lrg_bone_get_bind_pose (child);

			dir[0] = cb->position_x;
			dir[1] = cb->position_y;
			dir[2] = cb->position_z;
		}
		else
		{
			const LrgBonePose *b = lrg_bone_get_bind_pose (bone);
			gfloat inv[4];
			gfloat pos[3];
			gfloat rot[4];

			/* a leaf: keep the incoming direction, in the leaf's own frame */
			pos[0] = b->position_x;
			pos[1] = b->position_y;
			pos[2] = b->position_z;
			rot[0] = b->rotation_x;
			rot[1] = b->rotation_y;
			rot[2] = b->rotation_z;
			rot[3] = b->rotation_w;
			q_normalize (rot);
			q_conj (inv, rot);
			q_rotate (dir, inv, pos);
		}
		len = sqrtf (dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
		if (len > 1e-4f)
		{
			dir[0] /= len;
			dir[1] /= len;
			dir[2] /= len;
		}
		else
		{
			dir[0] = 0.0f;
			dir[1] = 1.0f;
			dir[2] = 0.0f;
			len = 0.1f;
		}

		/* the twist axis runs along the bone */
		q_from_to (part->joint_rotation, x_axis, dir);
		if (child != NULL)
		{
			part->shape = LRG_PHYSICS_ASSET_SHAPE_CAPSULE;
			part->radius = MAX (0.005f, radius_ratio * len);
			part->radius = MIN (part->radius, 0.5f * len);
			part->half_height = MAX (0.0f, 0.5f * len - part->radius);
			part->offset[0] = 0.5f * len * dir[0];
			part->offset[1] = 0.5f * len * dir[1];
			part->offset[2] = 0.5f * len * dir[2];
			q_from_to (part->rotation, y_axis, dir);
			part->mass = R_PI * part->radius * part->radius *
			             (2.0f * part->half_height + (4.0f / 3.0f) * part->radius);
		}
		else
		{
			gint parent = lrg_bone_get_parent_index (bone);

			part->shape = LRG_PHYSICS_ASSET_SHAPE_SPHERE;
			part->radius = parent >= 0 && (guint) parent < n && radii[parent] > 0.0f
			               ? radii[parent] : MAX (0.005f, radius_ratio * len);
			part->offset[0] = part->radius * dir[0];
			part->offset[1] = part->radius * dir[1];
			part->offset[2] = part->radius * dir[2];
			part->mass = (4.0f / 3.0f) * R_PI * part->radius * part->radius * part->radius;
		}
		radii[i] = part->radius;
		total_volume += part->mass;
		lrg_physics_asset_add_part (self, part, NULL);
	}

	/* masses proportional to volume, summing to total_mass */
	for (i = 0; i < self->parts->len; i++)
	{
		LrgPhysicsAssetPart *p = g_ptr_array_index (self->parts, i);

		p->mass = total_volume > 0.0f ? total_mass * p->mass / total_volume : total_mass;
	}
	lrg_physics_asset_stabilize (self, skeleton);
	return self;
}

/* the part of the nearest ancestor bone that has one, or -1 */
static gint
parent_part (LrgPhysicsAsset *self, LrgSkeleton *skeleton, const gchar *bone_name)
{
	LrgBone *bone;
	gint parent;
	guint guard;
	guint n;

	n = lrg_skeleton_get_bone_count (skeleton);
	bone = lrg_skeleton_get_bone_by_name (skeleton, bone_name);
	if (bone == NULL)
		return -1;
	parent = lrg_bone_get_parent_index (bone);
	for (guard = 0; parent >= 0 && guard <= n; guard++)
	{
		LrgBone *pb = lrg_skeleton_get_bone (skeleton, parent);
		gint part;

		if (pb == NULL)
			return -1;
		part = lrg_physics_asset_find_part (self, lrg_bone_get_name (pb));
		if (part >= 0)
			return part;
		parent = lrg_bone_get_parent_index (pb);
	}
	return -1;
}

/*
 * lrg_physics_asset_stabilize:
 * Jolt RagdollSettings::Stabilize, mass pass: walk each tree of parts
 * breadth first from its root; a child's mass ratio is its parent's
 * ratio times its own mass / its parent's mass clamped to [0.8, 1.2];
 * the tree's total mass is then shared out by those ratios.
 */
void
lrg_physics_asset_stabilize (LrgPhysicsAsset *self,
                             LrgSkeleton     *skeleton)
{
	g_autofree gint *parents = NULL;
	g_autofree gboolean *visited = NULL;
	g_autofree gfloat *ratio = NULL;
	g_autofree guint *queue = NULL;
	guint n;
	guint root;

	g_return_if_fail (LRG_IS_PHYSICS_ASSET (self));
	g_return_if_fail (LRG_IS_SKELETON (skeleton));

	n = self->parts->len;
	if (n < 2)
		return;
	parents = g_new (gint, n);
	visited = g_new0 (gboolean, n);
	ratio = g_new (gfloat, n);
	queue = g_new (guint, n);
	for (root = 0; root < n; root++)
	{
		const LrgPhysicsAssetPart *p = g_ptr_array_index (self->parts, root);

		parents[root] = parent_part (self, skeleton, p->bone);
		/* a part whose bone is missing is not touched */
		if (lrg_skeleton_get_bone_by_name (skeleton, p->bone) == NULL)
			visited[root] = TRUE;
	}

	for (root = 0; root < n; root++)
	{
		guint head;
		guint tail;
		guint k;
		gfloat total_mass;
		gfloat total_ratio;

		if (visited[root] || parents[root] >= 0)
			continue;

		/* breadth-first list of the tree under root */
		head = 0;
		tail = 0;
		queue[tail++] = root;
		visited[root] = TRUE;
		while (head < tail)
		{
			guint parent = queue[head++];

			for (k = 0; k < n; k++)
				if (!visited[k] && parents[k] == (gint) parent)
				{
					visited[k] = TRUE;
					queue[tail++] = k;
				}
		}
		if (tail < 2)
			continue;

		ratio[root] = 1.0f;
		total_ratio = 1.0f;
		total_mass = 0.0f;
		for (k = 0; k < tail; k++)
		{
			LrgPhysicsAssetPart *c = g_ptr_array_index (self->parts, queue[k]);

			total_mass += c->mass;
			if (k > 0)
			{
				LrgPhysicsAssetPart *pp = g_ptr_array_index (self->parts, (guint) parents[queue[k]]);

				ratio[queue[k]] = ratio[parents[queue[k]]] * CLAMP (c->mass / pp->mass, 0.8f, 1.2f);
				total_ratio += ratio[queue[k]];
			}
		}
		for (k = 0; k < tail; k++)
		{
			LrgPhysicsAssetPart *c = g_ptr_array_index (self->parts, queue[k]);

			c->mass = ratio[queue[k]] * total_mass / total_ratio;
		}
	}
}

/* ==================================================================
 * LrgRagdoll
 * ================================================================== */

typedef struct
{
	const LrgPhysicsAssetPart *desc;   /* owned by the asset */
	gint bone;                         /* skeleton bone index */
	gint parent;                       /* parent part index, -1 for a root */
	Xf offset;                         /* body frame in the bone frame */
	Xf joint_b;                        /* joint frame in this body */
	Xf joint_a;                        /* joint frame in the parent's body */
	guint body;                        /* body id while in a world */
	LrgJoint *joint;                   /* to the parent; NULL for a root */
} Part;

struct _LrgRagdoll
{
	GObject parent_instance;

	LrgPhysicsAsset *asset;
	LrgSkeleton *skeleton;
	LrgDynamicsWorld *world;
	LrgRagdollMode mode;
	Xf root;

	GArray *parts;          /* Part */
	gint *bone_part;        /* bone index -> part index or -1 */
	gfloat *blend;          /* per bone physics weight */
	guint *order;           /* bones, parents before children */
	guint n_bones;
};

G_DEFINE_FINAL_TYPE (LrgRagdoll, lrg_ragdoll, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_MODE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_ragdoll_dispose (GObject *object)
{
	LrgRagdoll *self = LRG_RAGDOLL (object);

	lrg_ragdoll_remove_from_world (self);
	if (self->parts != NULL)
	{
		guint i;

		for (i = 0; i < self->parts->len; i++)
			g_clear_object (&g_array_index (self->parts, Part, i).joint);
	}
	g_clear_object (&self->asset);
	g_clear_object (&self->skeleton);
	G_OBJECT_CLASS (lrg_ragdoll_parent_class)->dispose (object);
}

static void
lrg_ragdoll_finalize (GObject *object)
{
	LrgRagdoll *self = LRG_RAGDOLL (object);

	g_clear_pointer (&self->parts, g_array_unref);
	g_free (self->bone_part);
	g_free (self->blend);
	g_free (self->order);
	G_OBJECT_CLASS (lrg_ragdoll_parent_class)->finalize (object);
}

static void
lrg_ragdoll_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
	LrgRagdoll *self = LRG_RAGDOLL (object);

	switch (prop_id)
	{
	case PROP_MODE:
		g_value_set_enum (value, self->mode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_ragdoll_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
	LrgRagdoll *self = LRG_RAGDOLL (object);

	switch (prop_id)
	{
	case PROP_MODE:
		lrg_ragdoll_set_mode (self, (LrgRagdollMode) g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_ragdoll_class_init (LrgRagdollClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_ragdoll_dispose;
	object_class->finalize = lrg_ragdoll_finalize;
	object_class->get_property = lrg_ragdoll_get_property;
	object_class->set_property = lrg_ragdoll_set_property;

	/**
	 * LrgRagdoll:mode:
	 *
	 * Passive (limp) or powered (motors track the drive pose).
	 */
	properties[PROP_MODE] =
		g_param_spec_enum ("mode", "Mode", "Passive or powered",
		                   LRG_TYPE_RAGDOLL_MODE, LRG_RAGDOLL_PASSIVE,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
		                   G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_ragdoll_init (LrgRagdoll *self)
{
	self->mode = LRG_RAGDOLL_PASSIVE;
	xf_identity (&self->root);
	self->parts = g_array_new (FALSE, TRUE, sizeof (Part));
}

/* configure one joint's limits, friction and motors from its part */
static void
configure_joint (LrgRagdoll *self, Part *part)
{
	const LrgPhysicsAssetPart *d = part->desc;
	LrgJoint *j = part->joint;
	guint a;

	switch (d->joint)
	{
	case LRG_JOINT_HINGE:
		if (d->twist_min > -R_PI || d->twist_max < R_PI)
			lrg_joint_set_limits (j, LRG_JOINT_AXIS_ROTATION_X, d->twist_min, d->twist_max);
		break;
	case LRG_JOINT_CONE:
		lrg_joint_set_limits (j, LRG_JOINT_AXIS_ROTATION_Y, -d->swing_y, d->swing_y);
		lrg_joint_set_limits (j, LRG_JOINT_AXIS_ROTATION_Z, -d->swing_z, d->swing_z);
		break;
	case LRG_JOINT_SWING_TWIST:
		lrg_joint_set_limits (j, LRG_JOINT_AXIS_ROTATION_X, d->twist_min, d->twist_max);
		lrg_joint_set_limits (j, LRG_JOINT_AXIS_ROTATION_Y, -d->swing_y, d->swing_y);
		lrg_joint_set_limits (j, LRG_JOINT_AXIS_ROTATION_Z, -d->swing_z, d->swing_z);
		break;
	case LRG_JOINT_FIXED:
	case LRG_JOINT_POINT:
	case LRG_JOINT_SLIDER:
	case LRG_JOINT_DISTANCE:
	case LRG_JOINT_SIX_DOF:
	default:
		break;
	}
	for (a = LRG_JOINT_AXIS_ROTATION_X; a <= LRG_JOINT_AXIS_ROTATION_Z; a++)
	{
		lrg_joint_set_friction (j, (LrgJointAxis) a, d->friction);
		lrg_joint_set_motor_spring (j, (LrgJointAxis) a, d->motor_frequency, d->motor_damping);
		lrg_joint_set_motor (j, (LrgJointAxis) a,
		                     self->mode == LRG_RAGDOLL_POWERED ? LRG_JOINT_MOTOR_POSITION
		                                                       : LRG_JOINT_MOTOR_OFF,
		                     d->motor_max_torque);
	}
}

/* parent bone index, -1 for a root or an out-of-range index */
static gint
bone_parent (LrgSkeleton *skeleton, guint bone, guint n)
{
	LrgBone *b = lrg_skeleton_get_bone (skeleton, (gint) bone);
	gint parent;

	if (b == NULL)
		return -1;
	parent = lrg_bone_get_parent_index (b);
	return parent >= 0 && (guint) parent < n ? parent : -1;
}

LrgRagdoll *
lrg_ragdoll_new (LrgPhysicsAsset  *asset,
                 LrgSkeleton      *skeleton,
                 GError          **error)
{
	g_autoptr(LrgRagdoll) self = NULL;
	g_autofree LrgBonePose *bind = NULL;
	g_autofree guint *depth = NULL;
	guint n;
	guint i;
	guint d;
	guint k;

	g_return_val_if_fail (LRG_IS_PHYSICS_ASSET (asset), NULL);
	g_return_val_if_fail (LRG_IS_SKELETON (skeleton), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!lrg_physics_asset_validate (asset, skeleton, error))
		return NULL;
	n = lrg_skeleton_get_bone_count (skeleton);
	bind = g_new0 (LrgBonePose, MAX (n, 1u));
	if (n > 0 && !lrg_skeleton_compute_bind_world_poses (skeleton, bind, n))
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_SKELETON,
		             "skeleton bone indices are not 0 .. %u", n - 1);
		return NULL;
	}

	self = g_object_new (LRG_TYPE_RAGDOLL, NULL);
	self->asset = g_object_ref (asset);
	self->skeleton = g_object_ref (skeleton);
	self->n_bones = n;
	self->bone_part = g_new (gint, MAX (n, 1u));
	self->blend = g_new (gfloat, MAX (n, 1u));
	self->order = g_new (guint, MAX (n, 1u));
	for (i = 0; i < n; i++)
	{
		self->bone_part[i] = -1;
		self->blend[i] = 1.0f;
	}

	/* bones sorted by depth: every parent comes before its children */
	depth = g_new0 (guint, MAX (n, 1u));
	for (i = 0; i < n; i++)
	{
		gint p = bone_parent (skeleton, i, n);
		guint guard = 0;

		while (p >= 0 && guard++ < n)
		{
			depth[i]++;
			p = bone_parent (skeleton, (guint) p, n);
		}
	}
	k = 0;
	for (d = 0; d <= n && k < n; d++)
		for (i = 0; i < n; i++)
			if (depth[i] == d)
				self->order[k++] = i;

	/* one Part per asset part */
	for (i = 0; i < lrg_physics_asset_get_part_count (asset); i++)
	{
		const LrgPhysicsAssetPart *desc = lrg_physics_asset_get_part (asset, i);
		LrgBone *bone;
		Part part;

		/* validated above: every part names a bone of the skeleton */
		if (desc == NULL)
			continue;
		bone = lrg_skeleton_get_bone_by_name (skeleton, desc->bone);
		if (bone == NULL)
			continue;
		memset (&part, 0, sizeof (part));
		part.desc = desc;
		part.bone = lrg_bone_get_index (bone);
		part.parent = -1;
		part.body = G_MAXUINT;
		xf_set (&part.offset, desc->offset, desc->rotation);
		self->bone_part[part.bone] = (gint) i;
		g_array_append_val (self->parts, part);
	}

	/* parents, and the joint frames from the bind pose */
	for (i = 0; i < self->parts->len; i++)
	{
		Part *part = &g_array_index (self->parts, Part, i);
		gint p = bone_parent (skeleton, (guint) part->bone, n);
		Xf j;
		Xf inv;
		Xf bw;
		Xf pw;
		guint guard = 0;

		while (p >= 0 && self->bone_part[p] < 0 && guard++ < n)
			p = bone_parent (skeleton, (guint) p, n);
		if (p < 0)
			continue;
		part->parent = self->bone_part[p];

		/* in this body: O^-1 * J */
		xf_set (&j, part->desc->joint_position, part->desc->joint_rotation);
		xf_inverse (&inv, &part->offset);
		xf_mul (&part->joint_b, &inv, &j);

		/* in the parent's body: (Bp * Op)^-1 * (B * J), at the bind pose */
		{
			Part *pp = &g_array_index (self->parts, Part, part->parent);

			xf_from_pose (&bw, &bind[part->bone]);
			xf_mul (&bw, &bw, &j);
			xf_from_pose (&pw, &bind[pp->bone]);
			xf_mul (&pw, &pw, &pp->offset);
			xf_inverse (&pw, &pw);
			xf_mul (&part->joint_a, &pw, &bw);
		}
	}
	return g_steal_pointer (&self);
}

LrgPhysicsAsset *
lrg_ragdoll_get_asset (LrgRagdoll *self)
{
	g_return_val_if_fail (LRG_IS_RAGDOLL (self), NULL);
	return self->asset;
}

LrgSkeleton *
lrg_ragdoll_get_skeleton (LrgRagdoll *self)
{
	g_return_val_if_fail (LRG_IS_RAGDOLL (self), NULL);
	return self->skeleton;
}

void
lrg_ragdoll_set_root_transform (LrgRagdoll        *self,
                                const LrgBonePose *transform)
{
	g_return_if_fail (LRG_IS_RAGDOLL (self));
	if (transform == NULL)
		xf_identity (&self->root);
	else
		xf_from_pose (&self->root, transform);
}

void
lrg_ragdoll_get_root_transform (LrgRagdoll  *self,
                                LrgBonePose *transform)
{
	g_return_if_fail (LRG_IS_RAGDOLL (self));
	g_return_if_fail (transform != NULL);
	lrg_bone_pose_set_identity (transform);
	transform->position_x = self->root.p[0];
	transform->position_y = self->root.p[1];
	transform->position_z = self->root.p[2];
	transform->rotation_x = self->root.q[0];
	transform->rotation_y = self->root.q[1];
	transform->rotation_z = self->root.q[2];
	transform->rotation_w = self->root.q[3];
}

static LrgPhysicsShape *
part_shape (const LrgPhysicsAssetPart *d)
{
	switch (d->shape)
	{
	case LRG_PHYSICS_ASSET_SHAPE_BOX:
		return lrg_physics_shape_new_box (d->half_extents[0], d->half_extents[1], d->half_extents[2]);
	case LRG_PHYSICS_ASSET_SHAPE_SPHERE:
		return lrg_physics_shape_new_sphere (d->radius);
	case LRG_PHYSICS_ASSET_SHAPE_CAPSULE:
	default:
		return lrg_physics_shape_new_capsule (d->radius, d->half_height);
	}
}

gboolean
lrg_ragdoll_add_to_world (LrgRagdoll        *self,
                          LrgDynamicsWorld  *world,
                          GError           **error)
{
	g_autoptr(GError) local = NULL;
	guint i;

	g_return_val_if_fail (LRG_IS_RAGDOLL (self), FALSE);
	g_return_val_if_fail (LRG_IS_DYNAMICS_WORLD (world), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (self->world != NULL)
	{
		g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_STATE,
		             "the ragdoll is already in a world");
		return FALSE;
	}
	self->world = g_object_ref (world);
	lrg_skeleton_calculate_world_poses (self->skeleton);

	/* bodies: root * bone world * offset */
	for (i = 0; i < self->parts->len; i++)
	{
		Part *part = &g_array_index (self->parts, Part, i);
		g_autoptr(LrgPhysicsShape) shape = part_shape (part->desc);
		LrgBone *bone = lrg_skeleton_get_bone (self->skeleton, part->bone);
		Xf w;

		xf_from_pose (&w, lrg_bone_get_world_pose (bone));
		xf_mul (&w, &self->root, &w);
		xf_mul (&w, &w, &part->offset);
		if (!lrg_dynamics_world_add_body (world, shape, LRG_DYNAMICS_BODY_DYNAMIC, part->desc->mass,
		                                  w.p, w.q, &part->body, &local) ||
		    !lrg_dynamics_world_set_body_layer (world, part->body, LRG_PHYSICS_LAYER_RAGDOLL,
		                                        LRG_PHYSICS_LAYERS_ALL, &local))
			goto failed;
	}

	/* joints from parent to child, then the collision exclusions */
	for (i = 0; i < self->parts->len; i++)
	{
		Part *part = &g_array_index (self->parts, Part, i);
		Part *parent;

		if (part->parent < 0)
			continue;
		parent = &g_array_index (self->parts, Part, part->parent);
		if (part->joint == NULL)
			part->joint = lrg_joint_new (part->desc->joint, parent->body, part->body,
			                             part->joint_a.p, part->joint_a.q,
			                             part->joint_b.p, part->joint_b.q);
		else
			lrg_physics_constraint_set_bodies (LRG_PHYSICS_CONSTRAINT (part->joint),
			                                   parent->body, part->body);
		lrg_joint_repair (part->joint);
		configure_joint (self, part);
		if (!lrg_dynamics_world_add_constraint (world, LRG_PHYSICS_CONSTRAINT (part->joint), NULL,
		                                        &local))
			goto failed;
		if (lrg_physics_asset_get_exclude_adjacent (self->asset) &&
		    !lrg_dynamics_world_set_collision_between (world, parent->body, part->body, FALSE, &local))
			goto failed;
	}
	for (i = 0; i < lrg_physics_asset_get_exclusion_count (self->asset); i++)
	{
		const gchar *a;
		const gchar *b;
		gint pa;
		gint pb;

		lrg_physics_asset_get_exclusion (self->asset, i, &a, &b);
		pa = lrg_physics_asset_find_part (self->asset, a);
		pb = lrg_physics_asset_find_part (self->asset, b);
		if (pa < 0 || pb < 0)
			continue;
		if (!lrg_dynamics_world_set_collision_between (world,
		                                               g_array_index (self->parts, Part, pa).body,
		                                               g_array_index (self->parts, Part, pb).body,
		                                               FALSE, &local))
			goto failed;
	}
	return TRUE;

failed:
	g_set_error (error, LRG_RAGDOLL_ERROR, LRG_RAGDOLL_ERROR_WORLD, "ragdoll: %s",
	             local != NULL ? local->message : "the world refused a body or joint");
	lrg_ragdoll_remove_from_world (self);
	return FALSE;
}

void
lrg_ragdoll_remove_from_world (LrgRagdoll *self)
{
	guint i;

	g_return_if_fail (LRG_IS_RAGDOLL (self));
	if (self->world == NULL)
		return;
	for (i = 0; i < self->parts->len; i++)
	{
		Part *part = &g_array_index (self->parts, Part, i);

		if (part->joint != NULL &&
		    lrg_physics_constraint_get_in_world (LRG_PHYSICS_CONSTRAINT (part->joint)))
			lrg_dynamics_world_remove_constraint (self->world, LRG_PHYSICS_CONSTRAINT (part->joint));
	}
	for (i = 0; i < self->parts->len; i++)
	{
		Part *part = &g_array_index (self->parts, Part, i);

		if (part->body != G_MAXUINT)
			lrg_dynamics_world_remove_body (self->world, part->body, NULL);
		part->body = G_MAXUINT;
	}
	g_clear_object (&self->world);
}

LrgDynamicsWorld *
lrg_ragdoll_get_world (LrgRagdoll *self)
{
	g_return_val_if_fail (LRG_IS_RAGDOLL (self), NULL);
	return self->world;
}

guint
lrg_ragdoll_get_part_count (LrgRagdoll *self)
{
	g_return_val_if_fail (LRG_IS_RAGDOLL (self), 0);
	return self->parts->len;
}

gint
lrg_ragdoll_get_part_bone (LrgRagdoll *self,
                           guint       part)
{
	g_return_val_if_fail (LRG_IS_RAGDOLL (self), -1);
	if (part >= self->parts->len)
		return -1;
	return g_array_index (self->parts, Part, part).bone;
}

gint
lrg_ragdoll_get_part_parent (LrgRagdoll *self,
                             guint       part)
{
	g_return_val_if_fail (LRG_IS_RAGDOLL (self), -1);
	if (part >= self->parts->len)
		return -1;
	return g_array_index (self->parts, Part, part).parent;
}

guint
lrg_ragdoll_get_body (LrgRagdoll *self,
                      guint       part)
{
	g_return_val_if_fail (LRG_IS_RAGDOLL (self), G_MAXUINT);
	if (part >= self->parts->len)
		return G_MAXUINT;
	return g_array_index (self->parts, Part, part).body;
}

LrgJoint *
lrg_ragdoll_get_joint (LrgRagdoll *self,
                       guint       part)
{
	g_return_val_if_fail (LRG_IS_RAGDOLL (self), NULL);
	if (part >= self->parts->len)
		return NULL;
	return g_array_index (self->parts, Part, part).joint;
}

void
lrg_ragdoll_set_mode (LrgRagdoll     *self,
                      LrgRagdollMode  mode)
{
	guint i;

	g_return_if_fail (LRG_IS_RAGDOLL (self));
	g_return_if_fail (mode <= LRG_RAGDOLL_POWERED);

	if (self->mode == mode)
		return;
	self->mode = mode;
	for (i = 0; i < self->parts->len; i++)
	{
		Part *part = &g_array_index (self->parts, Part, i);

		if (part->joint != NULL)
			configure_joint (self, part);
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

LrgRagdollMode
lrg_ragdoll_get_mode (LrgRagdoll *self)
{
	g_return_val_if_fail (LRG_IS_RAGDOLL (self), LRG_RAGDOLL_PASSIVE);
	return self->mode;
}

/*
 * lrg_ragdoll_drive_to_pose:
 * The target of each joint is the rotation of the child's joint frame in
 * the parent's joint frame when both bones are posed like @pose:
 * (Rp * Op * Ja)^-1 * (Rc * Oc * Jb), rotations only (the root
 * transform cancels).
 */
void
lrg_ragdoll_drive_to_pose (LrgRagdoll  *self,
                           LrgSkeleton *pose)
{
	guint i;

	g_return_if_fail (LRG_IS_RAGDOLL (self));
	g_return_if_fail (LRG_IS_SKELETON (pose));

	lrg_skeleton_calculate_world_poses (pose);
	for (i = 0; i < self->parts->len; i++)
	{
		Part *part = &g_array_index (self->parts, Part, i);
		Part *parent;
		LrgBone *cb;
		LrgBone *pb;
		Xf c;
		Xf p;
		gfloat inv[4];
		gfloat q[4];

		if (part->joint == NULL || part->parent < 0)
			continue;
		parent = &g_array_index (self->parts, Part, part->parent);
		cb = lrg_skeleton_get_bone (pose, part->bone);
		pb = lrg_skeleton_get_bone (pose, parent->bone);
		if (cb == NULL || pb == NULL)
			continue;
		xf_from_pose (&c, lrg_bone_get_world_pose (cb));
		xf_mul (&c, &c, &part->offset);
		xf_mul (&c, &c, &part->joint_b);
		xf_from_pose (&p, lrg_bone_get_world_pose (pb));
		xf_mul (&p, &p, &parent->offset);
		xf_mul (&p, &p, &part->joint_a);
		q_conj (inv, p.q);
		q_mul (q, inv, c.q);
		q_normalize (q);
		lrg_joint_set_target_orientation (part->joint, q);
	}
}

void
lrg_ragdoll_set_blend (LrgRagdoll *self,
                       gfloat      weight)
{
	guint i;

	g_return_if_fail (LRG_IS_RAGDOLL (self));
	weight = CLAMP (weight, 0.0f, 1.0f);
	for (i = 0; i < self->n_bones; i++)
		self->blend[i] = weight;
}

void
lrg_ragdoll_set_bone_blend (LrgRagdoll *self,
                            guint       bone,
                            gfloat      weight)
{
	g_return_if_fail (LRG_IS_RAGDOLL (self));
	g_return_if_fail (bone < self->n_bones);
	self->blend[bone] = CLAMP (weight, 0.0f, 1.0f);
}

gfloat
lrg_ragdoll_get_bone_blend (LrgRagdoll *self,
                            guint       bone)
{
	g_return_val_if_fail (LRG_IS_RAGDOLL (self), 0.0f);
	g_return_val_if_fail (bone < self->n_bones, 0.0f);
	return self->blend[bone];
}

/*
 * lrg_ragdoll_write_pose:
 * Walks the bones parents-first keeping each bone's model-space pose
 * (scale ignored). A bone with a part and a non-zero weight gets the
 * local pose that puts it on its body, (parent)^-1 * R^-1 * body * O^-1,
 * blended with the animation's local pose (lerp position, nlerp
 * rotation along the shorter arc).
 */
void
lrg_ragdoll_write_pose (LrgRagdoll *self)
{
	g_autofree Xf *model = NULL;
	Xf root_inv;
	guint k;

	g_return_if_fail (LRG_IS_RAGDOLL (self));
	if (self->world == NULL || self->n_bones == 0)
		return;

	model = g_new (Xf, self->n_bones);
	xf_inverse (&root_inv, &self->root);
	for (k = 0; k < self->n_bones; k++)
	{
		guint b = self->order[k];
		LrgBone *bone = lrg_skeleton_get_bone (self->skeleton, (gint) b);
		const LrgBonePose *anim;
		LrgBonePose out;
		gint parent;
		Xf parent_model;
		Xf local;
		gint pi;

		if (bone == NULL)
		{
			xf_identity (&model[b]);
			continue;
		}
		anim = lrg_bone_get_local_pose (bone);
		out = *anim;
		parent = bone_parent (self->skeleton, b, self->n_bones);
		if (parent >= 0)
			parent_model = model[parent];
		else
			xf_identity (&parent_model);

		pi = self->bone_part[b];
		if (pi >= 0 && self->blend[b] > 0.0f)
		{
			Part *part = &g_array_index (self->parts, Part, (guint) pi);
			gfloat w = self->blend[b];
			Xf body;
			Xf inv;
			Xf a;
			gfloat d;
			guint i;

			if (lrg_dynamics_world_get_position (self->world, part->body, body.p, NULL) &&
			    lrg_dynamics_world_get_orientation (self->world, part->body, body.q, NULL))
			{
				/* bone in model space, then in its parent's frame */
				xf_inverse (&inv, &part->offset);
				xf_mul (&body, &body, &inv);
				xf_mul (&body, &root_inv, &body);
				xf_inverse (&inv, &parent_model);
				xf_mul (&local, &inv, &body);

				xf_from_pose (&a, anim);
				d = a.q[0] * local.q[0] + a.q[1] * local.q[1] + a.q[2] * local.q[2] + a.q[3] * local.q[3];
				for (i = 0; i < 3; i++)
					local.p[i] = a.p[i] + (local.p[i] - a.p[i]) * w;
				for (i = 0; i < 4; i++)
					local.q[i] = a.q[i] * (1.0f - w) + (d < 0.0f ? -local.q[i] : local.q[i]) * w;
				q_normalize (local.q);

				out.position_x = local.p[0];
				out.position_y = local.p[1];
				out.position_z = local.p[2];
				out.rotation_x = local.q[0];
				out.rotation_y = local.q[1];
				out.rotation_z = local.q[2];
				out.rotation_w = local.q[3];
				lrg_skeleton_set_pose (self->skeleton, (gint) b, &out);
			}
		}
		xf_from_pose (&local, &out);
		xf_mul (&model[b], &parent_model, &local);
	}
	lrg_skeleton_calculate_world_poses (self->skeleton);
}

void
lrg_ragdoll_set_linear_velocity (LrgRagdoll   *self,
                                 const gfloat *velocity)
{
	guint i;

	g_return_if_fail (LRG_IS_RAGDOLL (self));
	g_return_if_fail (velocity != NULL);
	if (self->world == NULL)
		return;
	for (i = 0; i < self->parts->len; i++)
		lrg_dynamics_world_set_linear_velocity (self->world,
		                                        g_array_index (self->parts, Part, i).body,
		                                        velocity, NULL);
}
