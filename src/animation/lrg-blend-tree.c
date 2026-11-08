/* lrg-blend-tree.c
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Blend trees: weighted blends of clips (or nested trees) driven by one
 * or two parameters.
 *
 * Weights:
 *  - 1D: linear between the two thresholds around the parameter, clamped
 *    to the first/last child outside the range.
 *  - 2D cartesian (%LRG_BLEND_TYPE_2D_FREEFORM) and 2D directional
 *    (%LRG_BLEND_TYPE_2D_SIMPLE): gradient band interpolation (Johansen,
 *    "Automated Semi-Procedural Animation for Character Locomotion",
 *    2009). Each child's influence is the minimum, over every other child,
 *    of how far the sample point still is from that other child along the
 *    line between them; the directional variant measures that line in
 *    (magnitude, angle) space so children on a circle blend by angle and a
 *    child at the origin (idle) blends by speed.
 *  - Direct: the children's own weights, normalized.
 *
 * Time: with sync (the default) one normalized phase drives every child
 * (the cycle length is the weight-blended child length), so blended
 * locomotion cycles keep their feet in step.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include <yaml-glib.h>

#include "lrg-blend-tree.h"
#include "lrg-anim-graph.h"
#include "lrg-animation-state-machine.h"

/* ==========================================================================
 * LrgBlendTreeChild
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgBlendTreeChild, lrg_blend_tree_child,
                     lrg_blend_tree_child_copy,
                     lrg_blend_tree_child_free)

LrgBlendTreeChild *
lrg_blend_tree_child_new (LrgAnimationClip *clip)
{
	LrgBlendTreeChild *child;

	child = g_new0 (LrgBlendTreeChild, 1);
	child->clip = clip != NULL ? g_object_ref (clip) : NULL;
	child->speed = 1.0f;
	return child;
}

LrgBlendTreeChild *
lrg_blend_tree_child_copy (const LrgBlendTreeChild *child)
{
	LrgBlendTreeChild *copy;

	if (child == NULL)
		return NULL;

	copy = g_new (LrgBlendTreeChild, 1);
	*copy = *child;
	if (child->clip != NULL)
		g_object_ref (child->clip);
	if (child->subtree != NULL)
		g_object_ref (child->subtree);
	copy->clip_name = g_strdup (child->clip_name);
	copy->weight_parameter = g_strdup (child->weight_parameter);
	if (child->extra != NULL)
		copy->extra = yaml_mapping_ref ((YamlMapping *) child->extra);
	return copy;
}

void
lrg_blend_tree_child_free (LrgBlendTreeChild *child)
{
	if (child == NULL)
		return;

	g_clear_object (&child->clip);
	g_clear_object (&child->subtree);
	g_free (child->clip_name);
	g_free (child->weight_parameter);
	if (child->extra != NULL)
		yaml_mapping_unref ((YamlMapping *) child->extra);
	g_free (child);
}

LrgBlendTree *
lrg_blend_tree_child_get_subtree (const LrgBlendTreeChild *child)
{
	g_return_val_if_fail (child != NULL, NULL);
	return child->subtree;
}

void
lrg_blend_tree_child_set_subtree (LrgBlendTreeChild *child,
                                  LrgBlendTree      *subtree)
{
	g_return_if_fail (child != NULL);
	g_return_if_fail (subtree == NULL || LRG_IS_BLEND_TREE (subtree));
	g_set_object (&child->subtree, subtree);
}

const gchar *
lrg_blend_tree_child_get_clip_name (const LrgBlendTreeChild *child)
{
	g_return_val_if_fail (child != NULL, NULL);
	return child->clip_name;
}

void
lrg_blend_tree_child_set_clip_name (LrgBlendTreeChild *child,
                                    const gchar       *clip_name)
{
	g_return_if_fail (child != NULL);
	if (child->clip_name == clip_name)
		return;
	g_free (child->clip_name);
	child->clip_name = g_strdup (clip_name);
}

const gchar *
lrg_blend_tree_child_get_weight_parameter (const LrgBlendTreeChild *child)
{
	g_return_val_if_fail (child != NULL, NULL);
	return child->weight_parameter;
}

void
lrg_blend_tree_child_set_weight_parameter (LrgBlendTreeChild *child,
                                           const gchar       *name)
{
	g_return_if_fail (child != NULL);
	if (child->weight_parameter == name)
		return;
	g_free (child->weight_parameter);
	child->weight_parameter = g_strdup (name);
}

/* ==========================================================================
 * LrgBlendTree
 * ========================================================================== */

typedef struct
{
	LrgBlendType  blend_type;
	GList        *children;       /* LrgBlendTreeChild*, owned */
	gfloat        param_x;
	gfloat        param_y;
	gfloat        time;           /* seconds since reset */
	gfloat        normalized;     /* cycles since reset (not wrapped) */
	gboolean      sync;
	gchar        *param_name;
	gchar        *param_y_name;
	GArray       *scratch;        /* LrgBonePose, one child's pose */
} LrgBlendTreePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgBlendTree, lrg_blend_tree, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_BLEND_TYPE,
	PROP_PARAMETER_NAME,
	PROP_PARAMETER_Y_NAME,
	PROP_SYNC,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

#define WEIGHT_EPSILON (1e-4f)

/* ==========================================================================
 * Weights
 * ========================================================================== */

/* Normalizes the computed weights to sum to 1 (no-op when all zero). */
static gfloat
normalize_weights (LrgBlendTreePrivate *priv)
{
	GList  *l;
	gfloat  total = 0.0f;

	for (l = priv->children; l != NULL; l = l->next)
		total += ((LrgBlendTreeChild *) l->data)->computed_weight;
	if (total > 1e-8f)
		for (l = priv->children; l != NULL; l = l->next)
			((LrgBlendTreeChild *) l->data)->computed_weight /= total;
	return total;
}

/* 1D: the two neighbouring thresholds share the weight linearly; outside
 * the range the nearest end child takes all of it. Children with equal
 * thresholds split it evenly. */
static void
compute_weights_1d (LrgBlendTreePrivate *priv)
{
	LrgBlendTreeChild *lower = NULL;
	LrgBlendTreeChild *upper = NULL;
	GList             *l;
	gfloat             p = priv->param_x;

	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		c->computed_weight = 0.0f;
		if (c->threshold <= p && (lower == NULL || c->threshold > lower->threshold))
			lower = c;
		if (c->threshold >= p && (upper == NULL || c->threshold < upper->threshold))
			upper = c;
	}

	if (lower != NULL && upper != NULL && lower != upper)
	{
		gfloat range = upper->threshold - lower->threshold;
		gfloat t = range > 1e-6f ? (p - lower->threshold) / range : 0.5f;

		lower->computed_weight = 1.0f - t;
		upper->computed_weight = t;
	}
	else if (lower != NULL)
		lower->computed_weight = 1.0f;
	else if (upper != NULL)
		upper->computed_weight = 1.0f;

	/* duplicates of the chosen thresholds take an even share */
	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		if (c->computed_weight == 0.0f && lower != NULL && c != lower &&
		    c->threshold == lower->threshold)
			c->computed_weight = lower->computed_weight;
		else if (c->computed_weight == 0.0f && upper != NULL && c != upper &&
		         c->threshold == upper->threshold)
			c->computed_weight = upper->computed_weight;
	}
	normalize_weights (priv);
}

/* Signed angle (radians) from (ax, ay) to (bx, by). */
static gfloat
signed_angle (gfloat ax, gfloat ay, gfloat bx, gfloat by)
{
	return atan2f (ax * by - ay * bx, ax * bx + ay * by);
}

/* The gradient band influence of child i at p against child j.
 * Cartesian: 1 - (p - pi).(pj - pi) / |pj - pi|^2.
 * Directional: the same in (magnitude / mean magnitude, angle * 2) space;
 * a child at the origin has no direction and contributes angle 0, which
 * turns the pair into a pure speed blend. */
static gfloat
band_influence (gboolean directional,
                gfloat   px, gfloat py,
                gfloat   ix, gfloat iy,
                gfloat   jx, gfloat jy)
{
	gfloat vx, vy, wx, wy, len2;

	if (!directional)
	{
		vx = jx - ix;
		vy = jy - iy;
		wx = px - ix;
		wy = py - iy;
	}
	else
	{
		const gfloat alpha = 2.0f;
		gfloat mi = sqrtf (ix * ix + iy * iy);
		gfloat mj = sqrtf (jx * jx + jy * jy);
		gfloat mp = sqrtf (px * px + py * py);
		gfloat avg = 0.5f * (mi + mj);
		gfloat a_ij = 0.0f;
		gfloat a_ip = 0.0f;

		if (avg < 1e-6f)
			avg = 1e-6f;
		if (mi > 1e-6f && mj > 1e-6f)
			a_ij = signed_angle (ix, iy, jx, jy);
		if (mi > 1e-6f && mp > 1e-6f)
			a_ip = signed_angle (ix, iy, px, py);
		vx = (mj - mi) / avg;
		vy = a_ij * alpha;
		wx = (mp - mi) / avg;
		wy = a_ip * alpha;
	}
	len2 = vx * vx + vy * vy;
	if (len2 < 1e-10f)
		return 1.0f;
	return 1.0f - (wx * vx + wy * vy) / len2;
}

/* 2D gradient band interpolation (both variants). */
static void
compute_weights_2d (LrgBlendTreePrivate *priv,
                    gboolean             directional)
{
	GList *l, *m;

	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *ci = l->data;
		gfloat             h = 1.0f;

		for (m = priv->children; m != NULL; m = m->next)
		{
			LrgBlendTreeChild *cj = m->data;
			gfloat             v;

			if (cj == ci)
				continue;
			v = band_influence (directional, priv->param_x, priv->param_y,
			                    ci->position_x, ci->position_y,
			                    cj->position_x, cj->position_y);
			if (v < h)
				h = v;
		}
		ci->computed_weight = MAX (h, 0.0f);
	}

	/* outside every band (should not happen): the nearest child wins */
	if (normalize_weights (priv) <= 1e-8f)
	{
		LrgBlendTreeChild *best = NULL;
		gfloat             best_d = G_MAXFLOAT;

		for (l = priv->children; l != NULL; l = l->next)
		{
			LrgBlendTreeChild *c = l->data;
			gfloat dx = priv->param_x - c->position_x;
			gfloat dy = priv->param_y - c->position_y;

			if (dx * dx + dy * dy < best_d)
			{
				best_d = dx * dx + dy * dy;
				best = c;
			}
		}
		if (best != NULL)
			best->computed_weight = 1.0f;
	}
}

/* Direct: the children's own weights, normalized. */
static void
compute_weights_direct (LrgBlendTreePrivate *priv)
{
	GList *l;

	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		c->computed_weight = MAX (c->weight, 0.0f);
	}
	normalize_weights (priv);
}

void
lrg_blend_tree_compute_weights (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;
	GList               *l;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);

	switch (priv->blend_type)
	{
	case LRG_BLEND_TYPE_1D:
		compute_weights_1d (priv);
		break;
	case LRG_BLEND_TYPE_2D_SIMPLE:
		compute_weights_2d (priv, TRUE);
		break;
	case LRG_BLEND_TYPE_2D_FREEFORM:
		compute_weights_2d (priv, FALSE);
		break;
	case LRG_BLEND_TYPE_DIRECT:
	default:
		compute_weights_direct (priv);
		break;
	}
	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		if (c->subtree != NULL)
			lrg_blend_tree_compute_weights (c->subtree);
	}
}

/* ==========================================================================
 * Time
 * ========================================================================== */

/* One cycle of a child in seconds (its speed folded in). */
static gfloat
child_duration (LrgBlendTreeChild *c)
{
	gfloat d = 0.0f;
	gfloat s = fabsf (c->speed);

	if (c->subtree != NULL)
		d = lrg_blend_tree_get_duration (c->subtree);
	else if (c->clip != NULL)
		d = lrg_animation_clip_get_duration (c->clip);
	if (s < 1e-4f)
		return 0.0f;
	return d / s;
}

gfloat
lrg_blend_tree_get_duration (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;
	GList               *l;
	gfloat               d = 0.0f;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), 0.0f);
	priv = lrg_blend_tree_get_instance_private (self);

	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		if (c->computed_weight > WEIGHT_EPSILON)
			d += c->computed_weight * child_duration (c);
	}
	return d;
}

/* Puts every child (recursively) on the phase @normalized, remembering
 * where each was for events and root motion. */
static void
place_synced (LrgBlendTree *self,
              gfloat        normalized,
              gboolean      keep_previous)
{
	LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);
	GList               *l;

	priv->normalized = normalized;
	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		if (c->subtree != NULL)
		{
			place_synced (c->subtree, normalized, keep_previous);
			continue;
		}
		c->previous_time = keep_previous ? c->time : 0.0f;
		if (c->clip != NULL)
			c->time = normalized * lrg_animation_clip_get_duration (c->clip);
		if (!keep_previous)
			c->previous_time = c->time;
	}
}

static void
lrg_blend_tree_real_update (LrgBlendTree *self,
                            gfloat        delta_time)
{
	LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);
	GList               *l;
	gfloat               duration;

	lrg_blend_tree_compute_weights (self);
	duration = lrg_blend_tree_get_duration (self);
	priv->time += delta_time;

	if (priv->sync)
	{
		gfloat n = priv->normalized;

		if (duration > 1e-6f)
			n += delta_time / duration;
		place_synced (self, n, TRUE);
		return;
	}

	/* free running: every child on its own clock */
	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		if (c->subtree != NULL)
		{
			lrg_blend_tree_update (c->subtree, delta_time * c->speed);
			continue;
		}
		c->previous_time = c->time;
		c->time += delta_time * c->speed;
	}
	if (duration > 1e-6f)
		priv->normalized += delta_time / duration;
}

gfloat
lrg_blend_tree_get_normalized_time (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), 0.0f);
	priv = lrg_blend_tree_get_instance_private (self);
	return priv->normalized;
}

void
lrg_blend_tree_set_normalized_time (LrgBlendTree *self,
                                    gfloat        normalized_time)
{
	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	place_synced (self, normalized_time, FALSE);
}

void
lrg_blend_tree_reset (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;
	GList               *l;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);

	priv->time = 0.0f;
	priv->normalized = 0.0f;
	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		c->time = 0.0f;
		c->previous_time = 0.0f;
		if (c->subtree != NULL)
			lrg_blend_tree_reset (c->subtree);
	}
}

/* ==========================================================================
 * Sampling
 * ========================================================================== */

/* Per-bone sample of one child (legacy name-based path). */
static gboolean
sample_child_bone (LrgBlendTreeChild *c,
                   const gchar       *bone_name,
                   LrgBonePose       *out)
{
	gint track;

	if (c->subtree != NULL)
	{
		lrg_blend_tree_sample (c->subtree, out, bone_name);
		return TRUE;
	}
	if (c->clip == NULL)
		return FALSE;
	track = lrg_animation_clip_find_track (c->clip, bone_name);
	if (track < 0)
		return FALSE;
	lrg_animation_clip_sample_track (c->clip, (guint) track, c->time, out);
	return TRUE;
}

/* Adds @w x @src into the accumulator @acc (rotation sign-aligned with
 * what is already accumulated). @first starts the sum. */
static void
accumulate (LrgBonePose       *acc,
            const LrgBonePose *src,
            gfloat             w,
            gboolean           first)
{
	gfloat qx = src->rotation_x, qy = src->rotation_y, qz = src->rotation_z, qw = src->rotation_w;

	if (first)
	{
		memset (acc, 0, sizeof *acc);
	}
	else if (acc->rotation_x * qx + acc->rotation_y * qy + acc->rotation_z * qz + acc->rotation_w * qw < 0.0f)
	{
		qx = -qx;
		qy = -qy;
		qz = -qz;
		qw = -qw;
	}
	acc->position_x += src->position_x * w;
	acc->position_y += src->position_y * w;
	acc->position_z += src->position_z * w;
	acc->scale_x += src->scale_x * w;
	acc->scale_y += src->scale_y * w;
	acc->scale_z += src->scale_z * w;
	acc->rotation_x += qx * w;
	acc->rotation_y += qy * w;
	acc->rotation_z += qz * w;
	acc->rotation_w += qw * w;
}

static void
lrg_blend_tree_real_sample (LrgBlendTree *self,
                            LrgBonePose  *out_pose,
                            const gchar  *bone_name)
{
	LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);
	GList               *l;
	gboolean             first = TRUE;
	gfloat               total = 0.0f;

	lrg_bone_pose_set_identity (out_pose);
	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;
		LrgBonePose        child_pose;

		if (c->computed_weight < WEIGHT_EPSILON)
			continue;
		if (!sample_child_bone (c, bone_name, &child_pose))
			continue;
		accumulate (out_pose, &child_pose, c->computed_weight, first);
		total += c->computed_weight;
		first = FALSE;
	}
	if (first)
	{
		lrg_bone_pose_set_identity (out_pose);
		return;
	}
	/* bones only some children animate: renormalize what was summed */
	if (total > 1e-6f && fabsf (total - 1.0f) > 1e-6f)
	{
		out_pose->position_x /= total;
		out_pose->position_y /= total;
		out_pose->position_z /= total;
		out_pose->scale_x /= total;
		out_pose->scale_y /= total;
		out_pose->scale_z /= total;
	}
	lrg_bone_pose_normalize_rotation (out_pose);
}

void
lrg_blend_tree_sample_pose (LrgBlendTree *self,
                            LrgSkeleton  *skeleton,
                            LrgBonePose  *poses,
                            guint         n_poses)
{
	LrgBlendTreePrivate *priv;
	LrgBlendTreeClass   *klass;
	LrgBonePose         *tmp;
	GList               *l;
	gboolean             first = TRUE;
	guint                i;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	g_return_if_fail (LRG_IS_SKELETON (skeleton));
	g_return_if_fail (poses != NULL || n_poses == 0);

	priv = lrg_blend_tree_get_instance_private (self);
	klass = LRG_BLEND_TREE_GET_CLASS (self);

	/* a subclass with its own per-bone sampler is honoured bone by bone */
	if (klass->sample != lrg_blend_tree_real_sample)
	{
		lrg_anim_pose_bind (skeleton, poses, n_poses);
		for (i = 0; i < n_poses; i++)
		{
			LrgBone *bone = lrg_skeleton_get_bone (skeleton, (gint) i);

			if (bone != NULL)
				lrg_blend_tree_sample (self, &poses[i], lrg_bone_get_name (bone));
		}
		return;
	}

	g_array_set_size (priv->scratch, n_poses);
	tmp = (LrgBonePose *) (gpointer) priv->scratch->data;
	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		if (c->computed_weight < WEIGHT_EPSILON)
			continue;
		if (c->subtree != NULL)
			lrg_blend_tree_sample_pose (c->subtree, skeleton, tmp, n_poses);
		else if (c->clip != NULL)
			lrg_anim_pose_sample_clip (c->clip, skeleton, c->time, tmp, n_poses);
		else
			lrg_anim_pose_bind (skeleton, tmp, n_poses);
		for (i = 0; i < n_poses; i++)
			accumulate (&poses[i], &tmp[i], c->computed_weight, first);
		first = FALSE;
	}
	if (first)
	{
		lrg_anim_pose_bind (skeleton, poses, n_poses);
		return;
	}
	for (i = 0; i < n_poses; i++)
		lrg_bone_pose_normalize_rotation (&poses[i]);
}

void
lrg_blend_tree_get_root_motion (LrgBlendTree *self,
                                const gchar  *bone_name,
                                gfloat       *out_translation,
                                gfloat       *out_yaw)
{
	LrgBlendTreePrivate *priv;
	GList               *l;
	gfloat               yaw = 0.0f;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	g_return_if_fail (bone_name != NULL);
	g_return_if_fail (out_translation != NULL);

	priv = lrg_blend_tree_get_instance_private (self);
	out_translation[0] = out_translation[1] = out_translation[2] = 0.0f;

	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;
		gfloat             t[3] = { 0.0f, 0.0f, 0.0f };
		gfloat             y = 0.0f;

		if (c->computed_weight < WEIGHT_EPSILON)
			continue;
		if (c->subtree != NULL)
			lrg_blend_tree_get_root_motion (c->subtree, bone_name, t, &y);
		else if (c->clip != NULL)
			lrg_anim_pose_clip_root_motion (c->clip, bone_name, c->previous_time, c->time, t, &y);
		out_translation[0] += t[0] * c->computed_weight;
		out_translation[1] += t[1] * c->computed_weight;
		out_translation[2] += t[2] * c->computed_weight;
		yaw += y * c->computed_weight;
	}
	if (out_yaw != NULL)
		*out_yaw = yaw;
}

/* Recursive dominant-clip search: @scale is the parent's weight. */
static LrgAnimationClip *
dominant (LrgBlendTree *self,
          gfloat        scale,
          gfloat       *best_w,
          gfloat       *prev,
          gfloat       *now)
{
	LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);
	LrgAnimationClip    *best = NULL;
	GList               *l;

	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;
		gfloat             w = c->computed_weight * scale;

		if (c->subtree != NULL)
		{
			LrgAnimationClip *sub = dominant (c->subtree, w, best_w, prev, now);

			if (sub != NULL)
				best = sub;
		}
		else if (c->clip != NULL && w > *best_w)
		{
			*best_w = w;
			*prev = c->previous_time;
			*now = c->time;
			best = c->clip;
		}
	}
	return best;
}

LrgAnimationClip *
lrg_blend_tree_get_dominant_clip (LrgBlendTree *self,
                                  gfloat       *out_previous_time,
                                  gfloat       *out_time)
{
	LrgAnimationClip *clip;
	gfloat            best_w = WEIGHT_EPSILON;
	gfloat            prev = 0.0f, now = 0.0f;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), NULL);

	clip = dominant (self, 1.0f, &best_w, &prev, &now);
	if (out_previous_time != NULL)
		*out_previous_time = prev;
	if (out_time != NULL)
		*out_time = now;
	return clip;
}

/* ==========================================================================
 * Parameters
 * ========================================================================== */

/* A numeric parameter of @machine as a float (doubles and ints). */
static gfloat
read_number (LrgAnimationStateMachine *machine,
             const gchar              *name)
{
	GVariant *v = lrg_animation_state_machine_get_parameter (machine, name);

	if (v == NULL)
		return 0.0f;
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
		return (gfloat) g_variant_get_double (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT32))
		return (gfloat) g_variant_get_int32 (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN))
		return g_variant_get_boolean (v) ? 1.0f : 0.0f;
	return 0.0f;
}

void
lrg_blend_tree_pull_parameters (LrgBlendTree             *self,
                                LrgAnimationStateMachine *machine)
{
	LrgBlendTreePrivate *priv;
	GList               *l;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	g_return_if_fail (LRG_IS_ANIMATION_STATE_MACHINE (machine));
	priv = lrg_blend_tree_get_instance_private (self);

	if (priv->param_name != NULL)
		priv->param_x = read_number (machine, priv->param_name);
	if (priv->param_y_name != NULL)
		priv->param_y = read_number (machine, priv->param_y_name);
	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		if (c->weight_parameter != NULL)
			c->weight = read_number (machine, c->weight_parameter);
		if (c->subtree != NULL)
			lrg_blend_tree_pull_parameters (c->subtree, machine);
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_blend_tree_finalize (GObject *object)
{
	LrgBlendTree        *self = LRG_BLEND_TREE (object);
	LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);

	g_list_free_full (priv->children, (GDestroyNotify) lrg_blend_tree_child_free);
	g_free (priv->param_name);
	g_free (priv->param_y_name);
	g_array_unref (priv->scratch);

	G_OBJECT_CLASS (lrg_blend_tree_parent_class)->finalize (object);
}

static void
lrg_blend_tree_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgBlendTree        *self = LRG_BLEND_TREE (object);
	LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_BLEND_TYPE:
		g_value_set_enum (value, priv->blend_type);
		break;
	case PROP_PARAMETER_NAME:
		g_value_set_string (value, priv->param_name);
		break;
	case PROP_PARAMETER_Y_NAME:
		g_value_set_string (value, priv->param_y_name);
		break;
	case PROP_SYNC:
		g_value_set_boolean (value, priv->sync);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_blend_tree_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgBlendTree        *self = LRG_BLEND_TREE (object);
	LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);

	switch (prop_id)
	{
	case PROP_BLEND_TYPE:
		priv->blend_type = g_value_get_enum (value);
		break;
	case PROP_PARAMETER_NAME:
		lrg_blend_tree_set_parameter_name (self, g_value_get_string (value));
		break;
	case PROP_PARAMETER_Y_NAME:
		lrg_blend_tree_set_parameter_y_name (self, g_value_get_string (value));
		break;
	case PROP_SYNC:
		lrg_blend_tree_set_sync (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_blend_tree_class_init (LrgBlendTreeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_blend_tree_finalize;
	object_class->get_property = lrg_blend_tree_get_property;
	object_class->set_property = lrg_blend_tree_set_property;

	klass->update = lrg_blend_tree_real_update;
	klass->sample = lrg_blend_tree_real_sample;

	/**
	 * LrgBlendTree:blend-type:
	 *
	 * How the children's weights are computed.
	 */
	properties[PROP_BLEND_TYPE] =
		g_param_spec_enum ("blend-type", "Blend Type", "Type of blending",
		                   LRG_TYPE_BLEND_TYPE, LRG_BLEND_TYPE_1D,
		                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgBlendTree:parameter-name:
	 *
	 * The float parameter driving the blend position (X for 2D).
	 */
	properties[PROP_PARAMETER_NAME] =
		g_param_spec_string ("parameter-name", "Parameter Name", "Blend parameter",
		                     NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlendTree:parameter-y-name:
	 *
	 * The float parameter driving the Y blend position of a 2D tree.
	 */
	properties[PROP_PARAMETER_Y_NAME] =
		g_param_spec_string ("parameter-y-name", "Parameter Y Name", "Second blend parameter",
		                     NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBlendTree:sync:
	 *
	 * Whether the children share one normalized phase.
	 */
	properties[PROP_SYNC] =
		g_param_spec_boolean ("sync", "Sync", "Synchronise the children",
		                      TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_blend_tree_init (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv = lrg_blend_tree_get_instance_private (self);

	priv->blend_type = LRG_BLEND_TYPE_1D;
	priv->sync = TRUE;
	priv->scratch = g_array_new (FALSE, TRUE, sizeof (LrgBonePose));
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgBlendTree *
lrg_blend_tree_new (LrgBlendType blend_type)
{
	return g_object_new (LRG_TYPE_BLEND_TREE, "blend-type", blend_type, NULL);
}

LrgBlendType
lrg_blend_tree_get_blend_type (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), LRG_BLEND_TYPE_1D);
	priv = lrg_blend_tree_get_instance_private (self);
	return priv->blend_type;
}

void
lrg_blend_tree_add_child (LrgBlendTree     *self,
                          LrgAnimationClip *clip,
                          gfloat            threshold)
{
	LrgBlendTreePrivate *priv;
	LrgBlendTreeChild   *child;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);

	child = lrg_blend_tree_child_new (clip);
	child->threshold = threshold;
	priv->children = g_list_append (priv->children, child);
}

void
lrg_blend_tree_add_child_2d (LrgBlendTree     *self,
                             LrgAnimationClip *clip,
                             gfloat            x,
                             gfloat            y)
{
	LrgBlendTreePrivate *priv;
	LrgBlendTreeChild   *child;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);

	child = lrg_blend_tree_child_new (clip);
	child->position_x = x;
	child->position_y = y;
	priv->children = g_list_append (priv->children, child);
}

void
lrg_blend_tree_add_child_full (LrgBlendTree            *self,
                               const LrgBlendTreeChild *child)
{
	LrgBlendTreePrivate *priv;
	LrgBlendTreeChild   *copy;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	g_return_if_fail (child != NULL);
	priv = lrg_blend_tree_get_instance_private (self);

	copy = lrg_blend_tree_child_copy (child);
	copy->computed_weight = 0.0f;
	copy->time = 0.0f;
	copy->previous_time = 0.0f;
	priv->children = g_list_append (priv->children, copy);
}

guint
lrg_blend_tree_get_child_count (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), 0);
	priv = lrg_blend_tree_get_instance_private (self);
	return g_list_length (priv->children);
}

LrgBlendTreeChild *
lrg_blend_tree_get_child (LrgBlendTree *self,
                          guint         index)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), NULL);
	priv = lrg_blend_tree_get_instance_private (self);
	return g_list_nth_data (priv->children, index);
}

gboolean
lrg_blend_tree_remove_child (LrgBlendTree *self,
                             guint         index)
{
	LrgBlendTreePrivate *priv;
	GList               *link;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), FALSE);
	priv = lrg_blend_tree_get_instance_private (self);

	link = g_list_nth (priv->children, index);
	if (link == NULL)
		return FALSE;
	lrg_blend_tree_child_free (link->data);
	priv->children = g_list_delete_link (priv->children, link);
	return TRUE;
}

gfloat
lrg_blend_tree_get_child_weight (LrgBlendTree *self,
                                 guint         index)
{
	LrgBlendTreeChild *c = lrg_blend_tree_get_child (self, index);

	return c != NULL ? c->computed_weight : 0.0f;
}

void
lrg_blend_tree_clear_children (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);

	g_list_free_full (priv->children, (GDestroyNotify) lrg_blend_tree_child_free);
	priv->children = NULL;
}

GList *
lrg_blend_tree_get_children (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), NULL);
	priv = lrg_blend_tree_get_instance_private (self);
	return priv->children;
}

void
lrg_blend_tree_set_parameter (LrgBlendTree *self,
                              gfloat        value)
{
	LrgBlendTreePrivate *priv;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);
	priv->param_x = value;
}

gfloat
lrg_blend_tree_get_parameter (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), 0.0f);
	priv = lrg_blend_tree_get_instance_private (self);
	return priv->param_x;
}

void
lrg_blend_tree_set_parameter_2d (LrgBlendTree *self,
                                 gfloat        x,
                                 gfloat        y)
{
	LrgBlendTreePrivate *priv;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);
	priv->param_x = x;
	priv->param_y = y;
}

gfloat
lrg_blend_tree_get_parameter_x (LrgBlendTree *self)
{
	return lrg_blend_tree_get_parameter (self);
}

gfloat
lrg_blend_tree_get_parameter_y (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), 0.0f);
	priv = lrg_blend_tree_get_instance_private (self);
	return priv->param_y;
}

const gchar *
lrg_blend_tree_get_parameter_name (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), NULL);
	priv = lrg_blend_tree_get_instance_private (self);
	return priv->param_name;
}

void
lrg_blend_tree_set_parameter_name (LrgBlendTree *self,
                                   const gchar  *name)
{
	LrgBlendTreePrivate *priv;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);
	if (g_strcmp0 (priv->param_name, name) == 0)
		return;
	g_free (priv->param_name);
	priv->param_name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PARAMETER_NAME]);
}

const gchar *
lrg_blend_tree_get_parameter_y_name (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), NULL);
	priv = lrg_blend_tree_get_instance_private (self);
	return priv->param_y_name;
}

void
lrg_blend_tree_set_parameter_y_name (LrgBlendTree *self,
                                     const gchar  *name)
{
	LrgBlendTreePrivate *priv;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);
	if (g_strcmp0 (priv->param_y_name, name) == 0)
		return;
	g_free (priv->param_y_name);
	priv->param_y_name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PARAMETER_Y_NAME]);
}

gboolean
lrg_blend_tree_get_sync (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), TRUE);
	priv = lrg_blend_tree_get_instance_private (self);
	return priv->sync;
}

void
lrg_blend_tree_set_sync (LrgBlendTree *self,
                         gboolean      sync)
{
	LrgBlendTreePrivate *priv;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);
	sync = !!sync;
	if (priv->sync == sync)
		return;
	priv->sync = sync;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SYNC]);
}

void
lrg_blend_tree_update (LrgBlendTree *self,
                       gfloat        delta_time)
{
	LrgBlendTreeClass *klass;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	klass = LRG_BLEND_TREE_GET_CLASS (self);
	if (klass->update != NULL)
		klass->update (self, delta_time);
}

void
lrg_blend_tree_sample (LrgBlendTree *self,
                       LrgBonePose  *out_pose,
                       const gchar  *bone_name)
{
	LrgBlendTreeClass *klass;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	g_return_if_fail (out_pose != NULL);
	klass = LRG_BLEND_TREE_GET_CLASS (self);
	if (klass->sample != NULL)
		klass->sample (self, out_pose, bone_name);
}

gfloat
lrg_blend_tree_get_time (LrgBlendTree *self)
{
	LrgBlendTreePrivate *priv;

	g_return_val_if_fail (LRG_IS_BLEND_TREE (self), 0.0f);
	priv = lrg_blend_tree_get_instance_private (self);
	return priv->time;
}

void
lrg_blend_tree_set_time (LrgBlendTree *self,
                         gfloat        time)
{
	LrgBlendTreePrivate *priv;
	GList               *l;

	g_return_if_fail (LRG_IS_BLEND_TREE (self));
	priv = lrg_blend_tree_get_instance_private (self);

	/* every child jumps to the same absolute time */
	priv->time = time;
	for (l = priv->children; l != NULL; l = l->next)
	{
		LrgBlendTreeChild *c = l->data;

		c->time = time;
		c->previous_time = time;
		if (c->subtree != NULL)
			lrg_blend_tree_set_time (c->subtree, time);
	}
}
