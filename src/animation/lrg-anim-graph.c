/* lrg-anim-graph.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnimGraph and the whole-skeleton pose utilities.
 *
 * The graph is the .ranim asset and, at the same time, its runtime: the
 * file loads into live LrgAnimationLayer / LrgAnimationStateMachine /
 * LrgAnimationState / LrgBlendTree / LrgAnimationTransition / LrgIKGoal
 * objects, and saving walks those objects. Keys this build does not know
 * are kept (as YAML) on the object they were read with and written back
 * after the known keys, so files from newer tools survive a round trip.
 *
 * Parsing is strict: a value of the wrong type, an unknown enum nick, a
 * duplicate name or a reference to a missing state / parameter / clip is
 * an error whose message starts with the path of the offending key.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include <yaml-glib.h>

#include "lrg-anim-graph.h"
#include "lrg-gltf-animation.h"
#include "lrg-skeleton-private.h"

/* ==========================================================================
 * Small math helpers
 * ========================================================================== */

static void
quat_mul (const gfloat *a, const gfloat *b, gfloat *r)
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
quat_rotate (const gfloat *q, const gfloat *v, gfloat *out)
{
	gfloat tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	gfloat ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	gfloat tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);

	out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
	out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
	out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

/* Rotation about +Y of a quaternion, radians. */
static gfloat
quat_yaw (const LrgBonePose *p)
{
	gfloat x = p->rotation_x, y = p->rotation_y, z = p->rotation_z, w = p->rotation_w;

	return atan2f (2.0f * (w * y + x * z), 1.0f - 2.0f * (x * x + y * y));
}

/* Wraps an angle difference into (-pi, pi]. */
static gfloat
wrap_angle (gfloat a)
{
	while (a > G_PI)
		a -= 2.0f * G_PI;
	while (a <= -G_PI)
		a += 2.0f * G_PI;
	return a;
}

/* ==========================================================================
 * Pose utilities
 * ========================================================================== */

/* The skeleton's layout signature (bone names in index order), cached on
 * the skeleton per bone-list generation. */
typedef struct
{
	guint   generation;
	guint32 signature;
	guint   n;
} SkelSig;

/* A clip's bone-slot -> track table for one skeleton layout. */
typedef struct
{
	guint32  signature;
	guint    n_slots;
	guint    n_tracks;
	gint    *slot_track;
} ClipMap;

static void
clip_map_free (gpointer data)
{
	ClipMap *m = data;

	g_free (m->slot_track);
	g_free (m);
}

static guint32
skeleton_signature (LrgSkeleton *skeleton,
                    guint        n)
{
	static GQuark q = 0;
	SkelSig      *sig;
	guint         gen = lrg_skeleton_get_generation (skeleton);
	guint32       h = 2166136261u;
	guint         i;

	if (q == 0)
		q = g_quark_from_static_string ("lrg-anim-skeleton-signature");
	sig = g_object_get_qdata (G_OBJECT (skeleton), q);
	if (sig != NULL && sig->generation == gen && sig->n == n)
		return sig->signature;

	for (i = 0; i < n; i++)
	{
		LrgBone *b = lrg_skeleton_get_bone (skeleton, (gint) i);

		h = (h ^ (b != NULL ? g_str_hash (lrg_bone_get_name (b)) : 0u)) * 16777619u;
	}
	h = (h ^ n) * 16777619u;
	if (sig == NULL)
	{
		sig = g_new0 (SkelSig, 1);
		g_object_set_qdata_full (G_OBJECT (skeleton), q, sig, g_free);
	}
	sig->generation = gen;
	sig->signature = h;
	sig->n = n;
	return h;
}

static const ClipMap *
clip_map_for (LrgAnimationClip *clip,
              LrgSkeleton      *skeleton,
              guint             n)
{
	static GQuark q = 0;
	ClipMap      *m;
	guint32       sig = skeleton_signature (skeleton, n);
	guint         tracks = lrg_animation_clip_get_track_count (clip);
	guint         i;

	if (q == 0)
		q = g_quark_from_static_string ("lrg-anim-clip-map");
	m = g_object_get_qdata (G_OBJECT (clip), q);
	if (m != NULL && m->signature == sig && m->n_slots == n && m->n_tracks == tracks)
		return m;

	m = g_new0 (ClipMap, 1);
	m->signature = sig;
	m->n_slots = n;
	m->n_tracks = tracks;
	m->slot_track = g_new (gint, MAX (n, 1));
	for (i = 0; i < n; i++)
	{
		LrgBone *b = lrg_skeleton_get_bone (skeleton, (gint) i);

		m->slot_track[i] = b != NULL ? lrg_animation_clip_find_track (clip, lrg_bone_get_name (b)) : -1;
	}
	g_object_set_qdata_full (G_OBJECT (clip), q, m, clip_map_free);
	return m;
}

void
lrg_anim_pose_bind (LrgSkeleton *skeleton,
                    LrgBonePose *poses,
                    guint        n_poses)
{
	guint i;

	g_return_if_fail (LRG_IS_SKELETON (skeleton));
	g_return_if_fail (poses != NULL || n_poses == 0);

	for (i = 0; i < n_poses; i++)
	{
		LrgBone *b = lrg_skeleton_get_bone (skeleton, (gint) i);

		if (b != NULL)
			poses[i] = *lrg_bone_get_bind_pose (b);
		else
			lrg_bone_pose_set_identity (&poses[i]);
	}
}

void
lrg_anim_pose_sample_clip (LrgAnimationClip *clip,
                           LrgSkeleton      *skeleton,
                           gfloat            time,
                           LrgBonePose      *poses,
                           guint             n_poses)
{
	const ClipMap *m;
	guint          i;

	g_return_if_fail (LRG_IS_ANIMATION_CLIP (clip));
	g_return_if_fail (LRG_IS_SKELETON (skeleton));
	g_return_if_fail (poses != NULL || n_poses == 0);

	m = clip_map_for (clip, skeleton, n_poses);
	for (i = 0; i < n_poses; i++)
	{
		if (m->slot_track[i] >= 0)
		{
			lrg_animation_clip_sample_track (clip, (guint) m->slot_track[i], time, &poses[i]);
		}
		else
		{
			LrgBone *b = lrg_skeleton_get_bone (skeleton, (gint) i);

			if (b != NULL)
				poses[i] = *lrg_bone_get_bind_pose (b);
			else
				lrg_bone_pose_set_identity (&poses[i]);
		}
	}
}

/* Shortest-arc slerp between two rotation quaternions into @out. */
static void
slerp4 (const gfloat *a, const gfloat *b_in, gfloat t, gfloat *out)
{
	gfloat b[4] = { b_in[0], b_in[1], b_in[2], b_in[3] };
	gfloat d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	gfloat s0, s1, len;
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
		gfloat th = acosf (MIN (d, 1.0f));
		gfloat sn = sinf (th);

		s0 = sinf ((1.0f - t) * th) / sn;
		s1 = sinf (t * th) / sn;
	}
	for (k = 0; k < 4; k++)
		out[k] = a[k] * s0 + b[k] * s1;
	len = sqrtf (out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
	if (len > 1e-8f)
		for (k = 0; k < 4; k++)
			out[k] /= len;
}

void
lrg_anim_pose_blend (LrgBonePose       *dest,
                     const LrgBonePose *src,
                     gfloat             weight,
                     const gfloat      *bone_weights,
                     guint              n_poses)
{
	guint i;

	g_return_if_fail (dest != NULL || n_poses == 0);
	g_return_if_fail (src != NULL || n_poses == 0);

	for (i = 0; i < n_poses; i++)
	{
		gfloat w = weight * (bone_weights != NULL ? bone_weights[i] : 1.0f);
		gfloat a[4], b[4], r[4];

		if (w <= 0.0f)
			continue;
		if (w >= 1.0f)
		{
			dest[i] = src[i];
			continue;
		}
		dest[i].position_x += (src[i].position_x - dest[i].position_x) * w;
		dest[i].position_y += (src[i].position_y - dest[i].position_y) * w;
		dest[i].position_z += (src[i].position_z - dest[i].position_z) * w;
		dest[i].scale_x += (src[i].scale_x - dest[i].scale_x) * w;
		dest[i].scale_y += (src[i].scale_y - dest[i].scale_y) * w;
		dest[i].scale_z += (src[i].scale_z - dest[i].scale_z) * w;
		a[0] = dest[i].rotation_x; a[1] = dest[i].rotation_y; a[2] = dest[i].rotation_z; a[3] = dest[i].rotation_w;
		b[0] = src[i].rotation_x; b[1] = src[i].rotation_y; b[2] = src[i].rotation_z; b[3] = src[i].rotation_w;
		slerp4 (a, b, w, r);
		dest[i].rotation_x = r[0];
		dest[i].rotation_y = r[1];
		dest[i].rotation_z = r[2];
		dest[i].rotation_w = r[3];
	}
}

void
lrg_anim_pose_add (LrgBonePose       *dest,
                   const LrgBonePose *additive,
                   const LrgBonePose *reference,
                   gfloat             weight,
                   const gfloat      *bone_weights,
                   guint              n_poses)
{
	guint i;

	g_return_if_fail (dest != NULL || n_poses == 0);
	g_return_if_fail (additive != NULL || n_poses == 0);
	g_return_if_fail (reference != NULL || n_poses == 0);

	for (i = 0; i < n_poses; i++)
	{
		gfloat w = weight * (bone_weights != NULL ? bone_weights[i] : 1.0f);
		gfloat ref_inv[4], add[4], diff[4], id[4] = { 0.0f, 0.0f, 0.0f, 1.0f }, part[4], base[4], r[4];

		if (w <= 0.0f)
			continue;
		dest[i].position_x += (additive[i].position_x - reference[i].position_x) * w;
		dest[i].position_y += (additive[i].position_y - reference[i].position_y) * w;
		dest[i].position_z += (additive[i].position_z - reference[i].position_z) * w;
		if (fabsf (reference[i].scale_x) > 1e-6f)
			dest[i].scale_x *= 1.0f + (additive[i].scale_x / reference[i].scale_x - 1.0f) * w;
		if (fabsf (reference[i].scale_y) > 1e-6f)
			dest[i].scale_y *= 1.0f + (additive[i].scale_y / reference[i].scale_y - 1.0f) * w;
		if (fabsf (reference[i].scale_z) > 1e-6f)
			dest[i].scale_z *= 1.0f + (additive[i].scale_z / reference[i].scale_z - 1.0f) * w;

		/* rotation: base * slerp(identity, inverse(ref) * add, w) */
		ref_inv[0] = -reference[i].rotation_x;
		ref_inv[1] = -reference[i].rotation_y;
		ref_inv[2] = -reference[i].rotation_z;
		ref_inv[3] = reference[i].rotation_w;
		add[0] = additive[i].rotation_x;
		add[1] = additive[i].rotation_y;
		add[2] = additive[i].rotation_z;
		add[3] = additive[i].rotation_w;
		quat_mul (ref_inv, add, diff);
		slerp4 (id, diff, MIN (w, 1.0f), part);
		base[0] = dest[i].rotation_x;
		base[1] = dest[i].rotation_y;
		base[2] = dest[i].rotation_z;
		base[3] = dest[i].rotation_w;
		quat_mul (base, part, r);
		dest[i].rotation_x = r[0];
		dest[i].rotation_y = r[1];
		dest[i].rotation_z = r[2];
		dest[i].rotation_w = r[3];
		lrg_bone_pose_normalize_rotation (&dest[i]);
	}
}

void
lrg_anim_pose_apply (LrgSkeleton       *skeleton,
                     const LrgBonePose *poses,
                     guint              n_poses)
{
	guint i;

	g_return_if_fail (LRG_IS_SKELETON (skeleton));
	g_return_if_fail (poses != NULL || n_poses == 0);

	for (i = 0; i < n_poses; i++)
		lrg_skeleton_set_pose (skeleton, (gint) i, &poses[i]);
	lrg_skeleton_calculate_world_poses (skeleton);
}

/* The root track's translation and yaw at @t (loop mode applies). */
static void
root_sample (LrgAnimationClip *clip,
             guint             track,
             gfloat            t,
             gfloat           *pos,
             gfloat           *yaw)
{
	LrgBonePose p;

	lrg_animation_clip_sample_track (clip, track, t, &p);
	pos[0] = p.position_x;
	pos[1] = p.position_y;
	pos[2] = p.position_z;
	*yaw = quat_yaw (&p);
}

void
lrg_anim_pose_clip_root_motion (LrgAnimationClip *clip,
                                const gchar      *bone_name,
                                gfloat            t0,
                                gfloat            t1,
                                gfloat           *out_translation,
                                gfloat           *out_yaw)
{
	gint   track;
	gfloat d;
	gfloat a[3], b[3], ya, yb;
	guint  k;

	g_return_if_fail (LRG_IS_ANIMATION_CLIP (clip));
	g_return_if_fail (bone_name != NULL);
	g_return_if_fail (out_translation != NULL);

	out_translation[0] = out_translation[1] = out_translation[2] = 0.0f;
	if (out_yaw != NULL)
		*out_yaw = 0.0f;
	track = lrg_animation_clip_find_track (clip, bone_name);
	d = lrg_animation_clip_get_duration (clip);
	if (track < 0 || d <= 0.0f || t1 == t0)
		return;

	if (lrg_animation_clip_get_loop_mode (clip) == LRG_ANIMATION_LOOP_REPEAT)
	{
		gfloat c0 = floorf (t0 / d), c1 = floorf (t1 / d);
		gfloat l0 = t0 - c0 * d, l1 = t1 - c1 * d;
		gfloat s[3], e[3], ys, ye;
		gfloat cycles = c1 - c0;

		/* within-cycle delta plus one full cycle per loop crossed */
		root_sample (clip, (guint) track, l0, a, &ya);
		root_sample (clip, (guint) track, l1, b, &yb);
		root_sample (clip, (guint) track, 0.0f, s, &ys);
		root_sample (clip, (guint) track, d * (1.0f - 1e-5f), e, &ye);
		for (k = 0; k < 3; k++)
			out_translation[k] = (b[k] - a[k]) + cycles * (e[k] - s[k]);
		if (out_yaw != NULL)
			*out_yaw = wrap_angle (yb - ya) + cycles * wrap_angle (ye - ys);
		return;
	}

	root_sample (clip, (guint) track, t0, a, &ya);
	root_sample (clip, (guint) track, t1, b, &yb);
	for (k = 0; k < 3; k++)
		out_translation[k] = b[k] - a[k];
	if (out_yaw != NULL)
		*out_yaw = wrap_angle (yb - ya);
}

GPtrArray *
lrg_anim_pose_clip_events (LrgAnimationClip *clip,
                           gfloat            t0,
                           gfloat            t1)
{
	GPtrArray *out;
	guint      n, i;
	gfloat     d;
	gint       c, c0, c1;

	g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (clip), NULL);

	out = g_ptr_array_new ();
	n = lrg_animation_clip_get_event_count (clip);
	d = lrg_animation_clip_get_duration (clip);
	if (n == 0 || t1 < t0)
		return out;

	if (lrg_animation_clip_get_loop_mode (clip) != LRG_ANIMATION_LOOP_REPEAT || d <= 0.0f)
	{
		/* one pass over the clip, times clamped to it */
		gfloat a = d > 0.0f ? CLAMP (t0, 0.0f, d) : t0;
		gfloat b = d > 0.0f ? CLAMP (t1, 0.0f, d) : t1;

		if (a == b && !(t0 == 0.0f && t1 == 0.0f))
			return out;
		for (i = 0; i < n; i++)
		{
			const LrgAnimationEvent *ev = lrg_animation_clip_get_event (clip, i);

			if ((ev->time > a || (a == 0.0f && t0 <= 0.0f && ev->time == 0.0f)) && ev->time <= b)
				g_ptr_array_add (out, (gpointer) ev);
		}
		return out;
	}

	/* repeating: every crossed loop, at most 1000 */
	c0 = (gint) floorf (t0 / d);
	c1 = (gint) floorf (t1 / d);
	if (c1 - c0 > 1000)
		c0 = c1 - 1000;
	for (c = c0; c <= c1; c++)
	{
		g_autoptr(GPtrArray) cyc = g_ptr_array_new ();
		guint                j;

		for (i = 0; i < n; i++)
		{
			const LrgAnimationEvent *ev = lrg_animation_clip_get_event (clip, i);
			gfloat                   at = (gfloat) c * d + ev->time;

			if ((at > t0 || (t0 == 0.0f && at == 0.0f)) && at <= t1)
				g_ptr_array_add (cyc, (gpointer) ev);
		}
		/* playback order within the loop */
		for (i = 1; i < cyc->len; i++)
			for (j = i; j > 0 && ((LrgAnimationEvent *) cyc->pdata[j - 1])->time >
			                     ((LrgAnimationEvent *) cyc->pdata[j])->time; j--)
			{
				gpointer tmp = cyc->pdata[j];

				cyc->pdata[j] = cyc->pdata[j - 1];
				cyc->pdata[j - 1] = tmp;
			}
		for (i = 0; i < cyc->len; i++)
			g_ptr_array_add (out, cyc->pdata[i]);
	}
	return out;
}

/* ==========================================================================
 * Enumerations and errors
 * ========================================================================== */

GType
lrg_root_motion_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ROOT_MOTION_NONE, "LRG_ROOT_MOTION_NONE", "none" },
			{ LRG_ROOT_MOTION_XZ, "LRG_ROOT_MOTION_XZ", "xz" },
			{ LRG_ROOT_MOTION_XYZ, "LRG_ROOT_MOTION_XYZ", "xyz" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgRootMotionMode", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

GType
lrg_anim_graph_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ANIM_GRAPH_ERROR_PARSE, "LRG_ANIM_GRAPH_ERROR_PARSE", "parse" },
			{ LRG_ANIM_GRAPH_ERROR_VERSION, "LRG_ANIM_GRAPH_ERROR_VERSION", "version" },
			{ LRG_ANIM_GRAPH_ERROR_INVALID, "LRG_ANIM_GRAPH_ERROR_INVALID", "invalid" },
			{ LRG_ANIM_GRAPH_ERROR_REFERENCE, "LRG_ANIM_GRAPH_ERROR_REFERENCE", "reference" },
			{ LRG_ANIM_GRAPH_ERROR_CLIP, "LRG_ANIM_GRAPH_ERROR_CLIP", "clip" },
			{ LRG_ANIM_GRAPH_ERROR_IO, "LRG_ANIM_GRAPH_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgAnimGraphError", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

G_DEFINE_QUARK (lrg-anim-graph-error-quark, lrg_anim_graph_error)

/* File nicks (snake_case as the file spells them). */
typedef struct
{
	gint         value;
	const gchar *nick;
} Nick;

static const Nick blend_type_nicks[] = {
	{ LRG_BLEND_TYPE_1D, "1d" },
	{ LRG_BLEND_TYPE_2D_SIMPLE, "2d_directional" },
	{ LRG_BLEND_TYPE_2D_FREEFORM, "2d_cartesian" },
	{ LRG_BLEND_TYPE_DIRECT, "direct" },
	{ 0, NULL }
};

static const Nick interruption_nicks[] = {
	{ LRG_TRANSITION_INTERRUPTION_NONE, "none" },
	{ LRG_TRANSITION_INTERRUPTION_SOURCE, "source" },
	{ LRG_TRANSITION_INTERRUPTION_DESTINATION, "destination" },
	{ LRG_TRANSITION_INTERRUPTION_SOURCE_THEN_DESTINATION, "source_then_destination" },
	{ LRG_TRANSITION_INTERRUPTION_DESTINATION_THEN_SOURCE, "destination_then_source" },
	{ 0, NULL }
};

static const Nick root_mode_nicks[] = {
	{ LRG_ROOT_MOTION_NONE, "none" },
	{ LRG_ROOT_MOTION_XZ, "xz" },
	{ LRG_ROOT_MOTION_XYZ, "xyz" },
	{ 0, NULL }
};

static const Nick ik_kind_nicks[] = {
	{ LRG_IK_GOAL_TWO_BONE, "two_bone" },
	{ LRG_IK_GOAL_CCD, "ccd" },
	{ LRG_IK_GOAL_FABRIK, "fabrik" },
	{ LRG_IK_GOAL_LOOK_AT, "look_at" },
	{ LRG_IK_GOAL_FOOT, "foot" },
	{ 0, NULL }
};

static const Nick layer_blend_nicks[] = {
	{ LRG_LAYER_BLEND_OVERRIDE, "override" },
	{ LRG_LAYER_BLEND_ADDITIVE, "additive" },
	{ 0, NULL }
};

static const Nick param_type_nicks[] = {
	{ LRG_ANIM_PARAMETER_FLOAT, "float" },
	{ LRG_ANIM_PARAMETER_INT, "int" },
	{ LRG_ANIM_PARAMETER_BOOL, "bool" },
	{ LRG_ANIM_PARAMETER_TRIGGER, "trigger" },
	{ 0, NULL }
};

static const Nick op_nicks[] = {
	{ LRG_CONDITION_EQUALS, "==" },
	{ LRG_CONDITION_NOT_EQUALS, "!=" },
	{ LRG_CONDITION_GREATER, ">" },
	{ LRG_CONDITION_LESS, "<" },
	{ LRG_CONDITION_GREATER_EQUAL, ">=" },
	{ LRG_CONDITION_LESS_EQUAL, "<=" },
	{ 0, NULL }
};

static const gchar *
nick_of (const Nick *table,
         gint        value)
{
	guint i;

	for (i = 0; table[i].nick != NULL; i++)
		if (table[i].value == value)
			return table[i].nick;
	return table[0].nick;
}

static gboolean
nick_value (const Nick  *table,
            const gchar *nick,
            gint        *out)
{
	guint i;

	for (i = 0; nick != NULL && table[i].nick != NULL; i++)
		if (g_strcmp0 (table[i].nick, nick) == 0)
		{
			*out = table[i].value;
			return TRUE;
		}
	return FALSE;
}

/* Lists a table's nicks for error messages. */
static gchar *
nick_list (const Nick *table)
{
	GString *s = g_string_new (NULL);
	guint    i;

	for (i = 0; table[i].nick != NULL; i++)
		g_string_append_printf (s, "%s%s", i > 0 ? ", " : "", table[i].nick);
	return g_string_free (s, FALSE);
}

/* ==========================================================================
 * The graph
 * ========================================================================== */

typedef struct
{
	gchar                *name;
	LrgAnimParameterType  type;
	GVariant             *def;
	YamlMapping          *extra;
} ParamDef;

typedef struct
{
	gchar            *name;
	gchar            *source;
	gchar            *animation;
	LrgAnimationClip *clip;
	YamlMapping      *extra;
} ClipDef;

struct _LrgAnimGraph
{
	GObject parent_instance;

	gchar                    *name;
	gchar                    *base_dir;
	GPtrArray                *params;    /* ParamDef */
	GPtrArray                *clips;     /* ClipDef */
	GPtrArray                *layers;    /* LrgAnimationLayer (ref) */
	GPtrArray                *goals;     /* LrgIKGoal (ref) */

	gchar                    *rm_bone;
	LrgRootMotionMode         rm_mode;
	gboolean                  rm_rotation;
	gboolean                  rm_apply;
	YamlMapping              *rm_extra;
	YamlMapping              *extra;

	LrgAnimationStateMachine *param_owner;  /* holds the shared parameter table */
	LrgAnimClipResolveFunc    resolver;
	gpointer                  resolver_data;
	GDestroyNotify            resolver_destroy;
	gboolean                  started;
	GArray                   *scratch;      /* LrgBonePose */
};

G_DEFINE_FINAL_TYPE (LrgAnimGraph, lrg_anim_graph, G_TYPE_OBJECT)

enum
{
	SIGNAL_EVENT,
	SIGNAL_STATE_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static GQuark
extra_quark (void)
{
	static GQuark q = 0;

	if (q == 0)
		q = g_quark_from_static_string ("lrg-anim-graph-extra");
	return q;
}

static void
param_def_free (gpointer data)
{
	ParamDef *p = data;

	g_free (p->name);
	g_variant_unref (p->def);
	if (p->extra != NULL)
		yaml_mapping_unref (p->extra);
	g_free (p);
}

static void
clip_def_free (gpointer data)
{
	ClipDef *c = data;

	g_free (c->name);
	g_free (c->source);
	g_free (c->animation);
	g_clear_object (&c->clip);
	if (c->extra != NULL)
		yaml_mapping_unref (c->extra);
	g_free (c);
}

static ParamDef *
find_param (LrgAnimGraph *self,
            const gchar  *name)
{
	guint i;

	for (i = 0; name != NULL && i < self->params->len; i++)
	{
		ParamDef *p = g_ptr_array_index (self->params, i);

		if (g_strcmp0 (p->name, name) == 0)
			return p;
	}
	return NULL;
}

static ClipDef *
find_clip (LrgAnimGraph *self,
           const gchar  *name)
{
	guint i;

	for (i = 0; name != NULL && i < self->clips->len; i++)
	{
		ClipDef *c = g_ptr_array_index (self->clips, i);

		if (g_strcmp0 (c->name, name) == 0)
			return c;
	}
	return NULL;
}

/* --- signal forwarding --------------------------------------------------- */

static const gchar *
layer_name_of_machine (LrgAnimGraph             *self,
                       LrgAnimationStateMachine *machine)
{
	guint i;

	for (i = 0; i < self->layers->len; i++)
	{
		LrgAnimationLayer *l = g_ptr_array_index (self->layers, i);

		if (lrg_animation_layer_get_state_machine (l) == machine)
			return lrg_animation_layer_get_name (l);
	}
	return NULL;
}

static void
on_machine_event (LrgAnimationStateMachine *machine,
                  const gchar              *state_name,
                  LrgAnimationEvent        *event,
                  gpointer                  user_data)
{
	LrgAnimGraph *self = LRG_ANIM_GRAPH (user_data);

	g_signal_emit (self, signals[SIGNAL_EVENT],
	               event->name != NULL ? g_quark_from_string (event->name) : 0,
	               layer_name_of_machine (self, machine), state_name, event);
}

static void
on_machine_entered (LrgAnimationStateMachine *machine,
                    const gchar              *state_name,
                    gpointer                  user_data)
{
	LrgAnimGraph *self = LRG_ANIM_GRAPH (user_data);

	g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0,
	               layer_name_of_machine (self, machine), state_name);
}

/* Shares the parameters with a layer's machine and forwards its signals. */
static void
hook_layer (LrgAnimGraph      *self,
            LrgAnimationLayer *layer)
{
	LrgAnimationStateMachine *m = lrg_animation_layer_get_state_machine (layer);

	if (m == NULL)
	{
		g_autoptr(LrgAnimationStateMachine) fresh = lrg_animation_state_machine_new ();

		lrg_animation_layer_set_state_machine (layer, fresh);
		m = fresh;
	}
	lrg_animation_state_machine_share_parameters (m, self->param_owner);
	g_signal_connect_object (m, "event", G_CALLBACK (on_machine_event), self, 0);
	g_signal_connect_object (m, "state-entered", G_CALLBACK (on_machine_entered), self, 0);
}

static void
unhook_layer (LrgAnimGraph      *self,
              LrgAnimationLayer *layer)
{
	LrgAnimationStateMachine *m = lrg_animation_layer_get_state_machine (layer);

	if (m != NULL)
		g_signal_handlers_disconnect_by_data (m, self);
}

/* --- GObject ------------------------------------------------------------- */

static void
clear_contents (LrgAnimGraph *self)
{
	guint i;

	for (i = 0; i < self->layers->len; i++)
		unhook_layer (self, g_ptr_array_index (self->layers, i));
	g_ptr_array_set_size (self->layers, 0);
	g_ptr_array_set_size (self->goals, 0);
	g_ptr_array_set_size (self->params, 0);
	g_ptr_array_set_size (self->clips, 0);
	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->rm_bone, g_free);
	g_clear_pointer (&self->rm_extra, yaml_mapping_unref);
	g_clear_pointer (&self->extra, yaml_mapping_unref);
	self->rm_mode = LRG_ROOT_MOTION_NONE;
	self->rm_rotation = FALSE;
	self->rm_apply = TRUE;
	self->started = FALSE;
}

static void
lrg_anim_graph_dispose (GObject *object)
{
	LrgAnimGraph *self = LRG_ANIM_GRAPH (object);

	if (self->layers != NULL)
		clear_contents (self);
	G_OBJECT_CLASS (lrg_anim_graph_parent_class)->dispose (object);
}

static void
lrg_anim_graph_finalize (GObject *object)
{
	LrgAnimGraph *self = LRG_ANIM_GRAPH (object);

	g_ptr_array_unref (self->layers);
	g_ptr_array_unref (self->goals);
	g_ptr_array_unref (self->params);
	g_ptr_array_unref (self->clips);
	g_free (self->base_dir);
	g_clear_object (&self->param_owner);
	if (self->resolver_destroy != NULL)
		self->resolver_destroy (self->resolver_data);
	g_array_unref (self->scratch);

	G_OBJECT_CLASS (lrg_anim_graph_parent_class)->finalize (object);
}

static void
lrg_anim_graph_class_init (LrgAnimGraphClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_anim_graph_dispose;
	object_class->finalize = lrg_anim_graph_finalize;

	/**
	 * LrgAnimGraph::event:
	 * @self: the graph
	 * @layer_name: (nullable): the layer whose machine played the event
	 * @state_name: the state whose clip carries it
	 * @event: the animation event (owned by the clip)
	 *
	 * A clip event played by any layer (see
	 * #LrgAnimationStateMachine::event). Detailed by the event name.
	 */
	signals[SIGNAL_EVENT] =
		g_signal_new ("event", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING,
		              LRG_TYPE_ANIMATION_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * LrgAnimGraph::state-changed:
	 * @self: the graph
	 * @layer_name: (nullable): the layer
	 * @state_name: the state that became current
	 *
	 * A layer's machine entered a state.
	 */
	signals[SIGNAL_STATE_CHANGED] =
		g_signal_new ("state-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_anim_graph_init (LrgAnimGraph *self)
{
	self->params = g_ptr_array_new_with_free_func (param_def_free);
	self->clips = g_ptr_array_new_with_free_func (clip_def_free);
	self->layers = g_ptr_array_new_with_free_func (g_object_unref);
	self->goals = g_ptr_array_new_with_free_func (g_object_unref);
	self->rm_apply = TRUE;
	self->param_owner = lrg_animation_state_machine_new ();
	self->scratch = g_array_new (FALSE, TRUE, sizeof (LrgBonePose));
}

/* ==========================================================================
 * Parsing
 * ========================================================================== */

typedef struct
{
	LrgAnimGraph  *graph;   /* the graph being built (not self) */
	GError       **error;
} Ctx;

static gboolean G_GNUC_PRINTF (4, 5)
fail (Ctx         *ctx,
      gint         code,
      const gchar *path,
      const gchar *fmt,
      ...)
{
	g_autofree gchar *msg = NULL;
	va_list           ap;

	va_start (ap, fmt);
	msg = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	g_set_error (ctx->error, LRG_ANIM_GRAPH_ERROR, code, "%s: %s", path, msg);
	return FALSE;
}

static gchar *
sub_path (const gchar *path,
          const gchar *key)
{
	return (path == NULL || *path == '\0') ? g_strdup (key) : g_strdup_printf ("%s.%s", path, key);
}

static gchar *
index_path (const gchar *path,
            const gchar *key,
            guint        i)
{
	return (path == NULL || *path == '\0') ? g_strdup_printf ("%s[%u]", key, i)
	                                       : g_strdup_printf ("%s.%s[%u]", path, key, i);
}

static YamlMapping *
as_mapping (Ctx         *ctx,
            YamlNode    *node,
            const gchar *path)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "expected a mapping");
		return NULL;
	}
	return yaml_node_get_mapping (node);
}

static YamlSequence *
as_sequence (Ctx         *ctx,
             YamlNode    *node,
             const gchar *path)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "expected a sequence");
		return NULL;
	}
	return yaml_node_get_sequence (node);
}

static const gchar *
as_scalar (Ctx         *ctx,
           YamlNode    *node,
           const gchar *path)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR ||
	    yaml_node_get_scalar (node) == NULL)
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "expected a scalar");
		return NULL;
	}
	return yaml_node_get_scalar (node);
}

/* A member's scalar text (with its path for errors), NULL when absent. */
static gboolean
get_string (Ctx          *ctx,
            YamlMapping  *map,
            const gchar  *path,
            const gchar  *key,
            const gchar **out)
{
	g_autofree gchar *p = NULL;
	YamlNode         *n = yaml_mapping_get_member (map, key);

	*out = NULL;
	if (n == NULL)
		return TRUE;
	p = sub_path (path, key);
	*out = as_scalar (ctx, n, p);
	return *out != NULL;
}

static gboolean
parse_float_text (Ctx         *ctx,
                  const gchar *text,
                  const gchar *path,
                  gfloat      *out)
{
	gchar  *end = NULL;
	gdouble v;

	v = g_ascii_strtod (text, &end);
	if (end == text || *end != '\0' || !isfinite (v))
		return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "'%s' is not a number", text);
	*out = (gfloat) v;
	return TRUE;
}

static gboolean
get_float (Ctx         *ctx,
           YamlMapping *map,
           const gchar *path,
           const gchar *key,
           gfloat      *out,
           gboolean    *present)
{
	g_autofree gchar *p = sub_path (path, key);
	const gchar      *t;

	if (present != NULL)
		*present = FALSE;
	if (!yaml_mapping_has_member (map, key))
		return TRUE;
	if (!get_string (ctx, map, path, key, &t))
		return FALSE;
	if (!parse_float_text (ctx, t, p, out))
		return FALSE;
	if (present != NULL)
		*present = TRUE;
	return TRUE;
}

static gboolean
parse_bool_text (Ctx         *ctx,
                 const gchar *text,
                 const gchar *path,
                 gboolean    *out)
{
	if (g_strcmp0 (text, "true") == 0)
		*out = TRUE;
	else if (g_strcmp0 (text, "false") == 0)
		*out = FALSE;
	else
		return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "'%s' is not true or false", text);
	return TRUE;
}

static gboolean
get_bool (Ctx         *ctx,
          YamlMapping *map,
          const gchar *path,
          const gchar *key,
          gboolean    *out)
{
	g_autofree gchar *p = sub_path (path, key);
	const gchar      *t;

	if (!yaml_mapping_has_member (map, key))
		return TRUE;
	if (!get_string (ctx, map, path, key, &t))
		return FALSE;
	return parse_bool_text (ctx, t, p, out);
}

static gboolean
parse_int_text (Ctx         *ctx,
                const gchar *text,
                const gchar *path,
                gint64      *out)
{
	gchar *end = NULL;
	gint64 v;

	v = g_ascii_strtoll (text, &end, 10);
	if (end == text || *end != '\0')
		return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "'%s' is not an integer", text);
	*out = v;
	return TRUE;
}

static gboolean
get_int (Ctx         *ctx,
         YamlMapping *map,
         const gchar *path,
         const gchar *key,
         gint64      *out)
{
	g_autofree gchar *p = sub_path (path, key);
	const gchar      *t;

	if (!yaml_mapping_has_member (map, key))
		return TRUE;
	if (!get_string (ctx, map, path, key, &t))
		return FALSE;
	return parse_int_text (ctx, t, p, out);
}

static gboolean
get_nick (Ctx         *ctx,
          YamlMapping *map,
          const gchar *path,
          const gchar *key,
          const Nick  *table,
          gint        *out)
{
	g_autofree gchar *p = sub_path (path, key);
	const gchar      *t;

	if (!yaml_mapping_has_member (map, key))
		return TRUE;
	if (!get_string (ctx, map, path, key, &t))
		return FALSE;
	if (!nick_value (table, t, out))
	{
		g_autofree gchar *list = nick_list (table);

		return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, p, "unknown value '%s' (one of: %s)", t, list);
	}
	return TRUE;
}

/* A fixed-length float vector [a, b, ...]. */
static gboolean
get_vec (Ctx         *ctx,
         YamlMapping *map,
         const gchar *path,
         const gchar *key,
         guint        n,
         gfloat      *out,
         gboolean    *present)
{
	g_autofree gchar *p = sub_path (path, key);
	YamlSequence     *seq;
	guint             i;

	if (present != NULL)
		*present = FALSE;
	if (!yaml_mapping_has_member (map, key))
		return TRUE;
	seq = as_sequence (ctx, yaml_mapping_get_member (map, key), p);
	if (seq == NULL)
		return FALSE;
	if (yaml_sequence_get_length (seq) != n)
		return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, p, "expected %u numbers", n);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *ep = index_path (path, key, i);
		const gchar      *t = as_scalar (ctx, yaml_sequence_get_element (seq, i), ep);

		if (t == NULL || !parse_float_text (ctx, t, ep, &out[i]))
			return FALSE;
	}
	if (present != NULL)
		*present = TRUE;
	return TRUE;
}

/* A list of strings. */
static gboolean
get_strv (Ctx          *ctx,
          YamlMapping  *map,
          const gchar  *path,
          const gchar  *key,
          GPtrArray   **out)
{
	g_autofree gchar *p = sub_path (path, key);
	YamlSequence     *seq;
	guint             i;

	*out = NULL;
	if (!yaml_mapping_has_member (map, key))
		return TRUE;
	seq = as_sequence (ctx, yaml_mapping_get_member (map, key), p);
	if (seq == NULL)
		return FALSE;
	*out = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar *ep = index_path (path, key, i);
		const gchar      *t = as_scalar (ctx, yaml_sequence_get_element (seq, i), ep);

		if (t == NULL)
		{
			g_clear_pointer (out, g_ptr_array_unref);
			return FALSE;
		}
		g_ptr_array_add (*out, g_strdup (t));
	}
	g_ptr_array_add (*out, NULL);
	return TRUE;
}

/* The members of @map not in @known (NULL-terminated), or NULL. */
static YamlMapping *
collect_extra (YamlMapping        *map,
               const gchar *const *known)
{
	g_autoptr(GList) keys = yaml_mapping_get_members (map);
	YamlMapping     *extra = NULL;
	GList           *l;

	for (l = keys; l != NULL; l = l->next)
	{
		if (g_strv_contains (known, l->data))
			continue;
		if (extra == NULL)
			extra = yaml_mapping_new ();
		yaml_mapping_set_member (extra, l->data, yaml_mapping_get_member (map, l->data));
	}
	return extra;
}

static void
stash_extra (gpointer            object,
             YamlMapping        *map,
             const gchar *const *known)
{
	YamlMapping *extra = collect_extra (map, known);

	if (extra != NULL)
		g_object_set_qdata_full (G_OBJECT (object), extra_quark (), extra,
		                         (GDestroyNotify) yaml_mapping_unref);
}

/* --- the pieces ---------------------------------------------------------- */

static LrgAnimationStateMachine *parse_machine (Ctx *ctx, YamlMapping *map, const gchar *path);

/* A declared numeric parameter (float or int). */
static gboolean
check_numeric_param (Ctx         *ctx,
                     const gchar *name,
                     const gchar *path)
{
	ParamDef *p = find_param (ctx->graph, name);

	if (p == NULL)
		return fail (ctx, LRG_ANIM_GRAPH_ERROR_REFERENCE, path, "unknown parameter '%s'", name);
	if (p->type != LRG_ANIM_PARAMETER_FLOAT && p->type != LRG_ANIM_PARAMETER_INT)
		return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "parameter '%s' is not a float or int", name);
	return TRUE;
}

static gboolean
check_clip (Ctx         *ctx,
            const gchar *name,
            const gchar *path)
{
	if (find_clip (ctx->graph, name) == NULL)
		return fail (ctx, LRG_ANIM_GRAPH_ERROR_REFERENCE, path, "unknown clip '%s' (declare it under clips:)", name);
	return TRUE;
}

static LrgBlendTree *
parse_tree (Ctx         *ctx,
            YamlMapping *map,
            const gchar *path)
{
	static const gchar *const known[] = { "type", "parameter", "parameter_y", "sync", "children", NULL };
	static const gchar *const child_known[] = { "clip", "tree", "threshold", "position", "speed",
	                                           "parameter", "weight", NULL };
	g_autoptr(LrgBlendTree)   tree = NULL;
	gint                      type = LRG_BLEND_TYPE_1D;
	const gchar              *param = NULL, *param_y = NULL;
	gboolean                  sync = TRUE;
	YamlSequence             *children = NULL;
	guint                     i;

	if (!get_nick (ctx, map, path, "type", blend_type_nicks, &type) ||
	    !get_string (ctx, map, path, "parameter", &param) ||
	    !get_string (ctx, map, path, "parameter_y", &param_y) ||
	    !get_bool (ctx, map, path, "sync", &sync))
		return NULL;

	/* 1D and 2D trees need their parameters; direct trees use the children's */
	if (type != LRG_BLEND_TYPE_DIRECT)
	{
		g_autofree gchar *pp = sub_path (path, "parameter");

		if (param == NULL)
		{
			fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "a %s blend tree needs a parameter",
			      nick_of (blend_type_nicks, type));
			return NULL;
		}
		if (!check_numeric_param (ctx, param, pp))
			return NULL;
	}
	if (type == LRG_BLEND_TYPE_2D_SIMPLE || type == LRG_BLEND_TYPE_2D_FREEFORM)
	{
		g_autofree gchar *pp = sub_path (path, "parameter_y");

		if (param_y == NULL)
		{
			fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "a 2D blend tree needs parameter_y");
			return NULL;
		}
		if (!check_numeric_param (ctx, param_y, pp))
			return NULL;
	}

	tree = lrg_blend_tree_new ((LrgBlendType) type);
	lrg_blend_tree_set_parameter_name (tree, param);
	lrg_blend_tree_set_parameter_y_name (tree, param_y);
	lrg_blend_tree_set_sync (tree, sync);

	if (yaml_mapping_has_member (map, "children"))
	{
		g_autofree gchar *cp = sub_path (path, "children");

		children = as_sequence (ctx, yaml_mapping_get_member (map, "children"), cp);
		if (children == NULL)
			return NULL;
	}
	for (i = 0; children != NULL && i < yaml_sequence_get_length (children); i++)
	{
		g_autofree gchar   *cp = index_path (path, "children", i);
		YamlMapping        *cm = as_mapping (ctx, yaml_sequence_get_element (children, i), cp);
		LrgBlendTreeChild  *child;
		const gchar        *clip = NULL, *wparam = NULL;
		gfloat              pos[2] = { 0.0f, 0.0f };
		gboolean            ok;

		if (cm == NULL)
			return NULL;
		child = lrg_blend_tree_child_new (NULL);
		ok = get_string (ctx, cm, cp, "clip", &clip) &&
		     get_float (ctx, cm, cp, "threshold", &child->threshold, NULL) &&
		     get_vec (ctx, cm, cp, "position", 2, pos, NULL) &&
		     get_float (ctx, cm, cp, "speed", &child->speed, NULL) &&
		     get_float (ctx, cm, cp, "weight", &child->weight, NULL) &&
		     get_string (ctx, cm, cp, "parameter", &wparam);
		if (ok && (clip != NULL) == yaml_mapping_has_member (cm, "tree"))
			ok = fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, cp, "a child needs exactly one of clip or tree");
		if (ok && clip != NULL)
		{
			g_autofree gchar *kp = sub_path (cp, "clip");

			ok = check_clip (ctx, clip, kp);
			lrg_blend_tree_child_set_clip_name (child, clip);
		}
		if (ok && wparam != NULL)
		{
			g_autofree gchar *kp = sub_path (cp, "parameter");

			ok = check_numeric_param (ctx, wparam, kp);
			lrg_blend_tree_child_set_weight_parameter (child, wparam);
		}
		if (ok && yaml_mapping_has_member (cm, "tree"))
		{
			g_autofree gchar *tp = sub_path (cp, "tree");
			YamlMapping      *tm = as_mapping (ctx, yaml_mapping_get_member (cm, "tree"), tp);
			LrgBlendTree     *sub = tm != NULL ? parse_tree (ctx, tm, tp) : NULL;

			ok = sub != NULL;
			if (ok)
			{
				lrg_blend_tree_child_set_subtree (child, sub);
				g_object_unref (sub);
			}
		}
		if (!ok)
		{
			lrg_blend_tree_child_free (child);
			return NULL;
		}
		child->position_x = pos[0];
		child->position_y = pos[1];
		child->extra = collect_extra (cm, child_known);
		lrg_blend_tree_add_child_full (tree, child);
		lrg_blend_tree_child_free (child);
	}
	stash_extra (tree, map, known);
	return g_steal_pointer (&tree);
}

static LrgAnimationState *
parse_state (Ctx         *ctx,
             YamlMapping *map,
             const gchar *path)
{
	static const gchar *const known[] = { "name", "clip", "blend_tree", "machine", "speed",
	                                     "speed_parameter", "mirror", NULL };
	g_autoptr(LrgAnimationState) state = NULL;
	const gchar                 *name = NULL, *clip = NULL, *speed_param = NULL;
	gfloat                       speed = 1.0f;
	gboolean                     mirror = FALSE;
	guint                        sources;

	if (!get_string (ctx, map, path, "name", &name) ||
	    !get_string (ctx, map, path, "clip", &clip) ||
	    !get_string (ctx, map, path, "speed_parameter", &speed_param) ||
	    !get_float (ctx, map, path, "speed", &speed, NULL) ||
	    !get_bool (ctx, map, path, "mirror", &mirror))
		return NULL;
	if (name == NULL || *name == '\0')
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "a state needs a name");
		return NULL;
	}
	if (g_strcmp0 (name, LRG_ANIMATION_TRANSITION_ANY_STATE) == 0)
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "'%s' is reserved for any-state transitions", name);
		return NULL;
	}
	sources = (clip != NULL) + yaml_mapping_has_member (map, "blend_tree") + yaml_mapping_has_member (map, "machine");
	if (sources > 1)
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "a state plays one of clip, blend_tree or machine");
		return NULL;
	}

	state = lrg_animation_state_new (name);
	lrg_animation_state_set_speed (state, speed);
	lrg_animation_state_set_mirror (state, mirror);
	if (clip != NULL)
	{
		g_autofree gchar *kp = sub_path (path, "clip");

		if (!check_clip (ctx, clip, kp))
			return NULL;
		lrg_animation_state_set_clip_name (state, clip);
	}
	if (speed_param != NULL)
	{
		g_autofree gchar *kp = sub_path (path, "speed_parameter");

		if (!check_numeric_param (ctx, speed_param, kp))
			return NULL;
		lrg_animation_state_set_speed_parameter (state, speed_param);
	}
	if (yaml_mapping_has_member (map, "blend_tree"))
	{
		g_autofree gchar        *tp = sub_path (path, "blend_tree");
		YamlMapping             *tm = as_mapping (ctx, yaml_mapping_get_member (map, "blend_tree"), tp);
		g_autoptr(LrgBlendTree)  tree = tm != NULL ? parse_tree (ctx, tm, tp) : NULL;

		if (tree == NULL)
			return NULL;
		lrg_animation_state_set_blend_tree (state, tree);
	}
	if (yaml_mapping_has_member (map, "machine"))
	{
		g_autofree gchar                    *mp = sub_path (path, "machine");
		YamlMapping                         *mm = as_mapping (ctx, yaml_mapping_get_member (map, "machine"), mp);
		g_autoptr(LrgAnimationStateMachine)  sub = mm != NULL ? parse_machine (ctx, mm, mp) : NULL;

		if (sub == NULL)
			return NULL;
		lrg_animation_state_set_sub_machine (state, sub);
	}
	stash_extra (state, map, known);
	return g_steal_pointer (&state);
}

static LrgAnimationTransition *
parse_transition (Ctx                      *ctx,
                  LrgAnimationStateMachine *machine,
                  YamlMapping              *map,
                  const gchar              *path,
                  gboolean                  any_state)
{
	static const gchar *const known[] = { "from", "to", "duration", "exit_time", "offset", "interruption",
	                                     "priority", "can_transition_to_self", "conditions", NULL };
	static const gchar *const cond_known[] = { "parameter", "op", "value", NULL };
	g_autoptr(LrgAnimationTransition) t = NULL;
	const gchar                      *from = NULL, *to = NULL;
	gfloat                            duration = 0.25f, exit_time = 0.0f, offset = 0.0f;
	gboolean                          has_exit = FALSE, to_self = FALSE;
	gint                              interruption = LRG_TRANSITION_INTERRUPTION_NONE;
	gint64                            priority = 0;
	YamlSequence                     *conds = NULL;
	guint                             i;

	if (!get_string (ctx, map, path, "from", &from) ||
	    !get_string (ctx, map, path, "to", &to) ||
	    !get_float (ctx, map, path, "duration", &duration, NULL) ||
	    !get_float (ctx, map, path, "exit_time", &exit_time, &has_exit) ||
	    !get_float (ctx, map, path, "offset", &offset, NULL) ||
	    !get_nick (ctx, map, path, "interruption", interruption_nicks, &interruption) ||
	    !get_int (ctx, map, path, "priority", &priority) ||
	    !get_bool (ctx, map, path, "can_transition_to_self", &to_self))
		return NULL;

	if (any_state && from != NULL)
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "any_state transitions have no 'from'");
		return NULL;
	}
	if (!any_state && from == NULL)
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "a transition needs 'from'");
		return NULL;
	}
	if (to == NULL)
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "a transition needs 'to'");
		return NULL;
	}
	if (!any_state && lrg_animation_state_machine_get_state (machine, from) == NULL)
	{
		g_autofree gchar *kp = sub_path (path, "from");

		fail (ctx, LRG_ANIM_GRAPH_ERROR_REFERENCE, kp, "unknown state '%s'", from);
		return NULL;
	}
	if (lrg_animation_state_machine_get_state (machine, to) == NULL)
	{
		g_autofree gchar *kp = sub_path (path, "to");

		fail (ctx, LRG_ANIM_GRAPH_ERROR_REFERENCE, kp, "unknown state '%s'", to);
		return NULL;
	}
	if (duration < 0.0f || duration > 10.0f || offset < 0.0f || offset > 1.0f ||
	    (has_exit && (exit_time < 0.0f || exit_time > 1000.0f)))
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path,
		      "out of range (duration 0..10, offset 0..1, exit_time 0..1000)");
		return NULL;
	}

	t = lrg_animation_transition_new (any_state ? LRG_ANIMATION_TRANSITION_ANY_STATE : from, to);
	lrg_animation_transition_set_duration (t, duration);
	lrg_animation_transition_set_has_exit_time (t, has_exit);
	lrg_animation_transition_set_exit_time (t, has_exit ? exit_time : -1.0f);
	lrg_animation_transition_set_offset (t, offset);
	lrg_animation_transition_set_interruption (t, (LrgTransitionInterruption) interruption);
	lrg_animation_transition_set_priority (t, (gint) priority);
	lrg_animation_transition_set_can_transition_to_self (t, to_self);

	if (yaml_mapping_has_member (map, "conditions"))
	{
		g_autofree gchar *cp = sub_path (path, "conditions");

		conds = as_sequence (ctx, yaml_mapping_get_member (map, "conditions"), cp);
		if (conds == NULL)
			return NULL;
	}
	for (i = 0; conds != NULL && i < yaml_sequence_get_length (conds); i++)
	{
		g_autofree gchar *cp = index_path (path, "conditions", i);
		YamlMapping      *cm = as_mapping (ctx, yaml_sequence_get_element (conds, i), cp);
		const gchar      *pname = NULL, *vtext = NULL;
		gint              op = LRG_CONDITION_EQUALS;
		ParamDef         *pd;
		GVariant         *value = NULL;
		YamlMapping      *extra;

		if (cm == NULL || !get_string (ctx, cm, cp, "parameter", &pname) ||
		    !get_nick (ctx, cm, cp, "op", op_nicks, &op) ||
		    !get_string (ctx, cm, cp, "value", &vtext))
			return NULL;
		if (pname == NULL)
		{
			fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, cp, "a condition needs a parameter");
			return NULL;
		}
		pd = find_param (ctx->graph, pname);
		if (pd == NULL)
		{
			fail (ctx, LRG_ANIM_GRAPH_ERROR_REFERENCE, cp, "unknown parameter '%s'", pname);
			return NULL;
		}
		switch (pd->type)
		{
		case LRG_ANIM_PARAMETER_FLOAT:
		case LRG_ANIM_PARAMETER_INT:
			if (!yaml_mapping_has_member (cm, "op") || vtext == NULL)
			{
				fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, cp, "a %s condition needs op and value",
				      nick_of (param_type_nicks, pd->type));
				return NULL;
			}
			if (pd->type == LRG_ANIM_PARAMETER_FLOAT)
			{
				gfloat f;

				if (!parse_float_text (ctx, vtext, cp, &f))
					return NULL;
				value = g_variant_new_double (f);
			}
			else
			{
				gint64 v;

				if (!parse_int_text (ctx, vtext, cp, &v))
					return NULL;
				value = g_variant_new_int32 ((gint32) v);
			}
			break;
		case LRG_ANIM_PARAMETER_BOOL:
		case LRG_ANIM_PARAMETER_TRIGGER:
		default:
		{
			gboolean b = TRUE;

			if (op != LRG_CONDITION_EQUALS && op != LRG_CONDITION_NOT_EQUALS)
			{
				fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, cp, "a %s condition compares with == or !=",
				      nick_of (param_type_nicks, pd->type));
				return NULL;
			}
			if (vtext != NULL && !parse_bool_text (ctx, vtext, cp, &b))
				return NULL;
			if (pd->type == LRG_ANIM_PARAMETER_TRIGGER && (op != LRG_CONDITION_EQUALS || !b))
			{
				fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, cp, "a trigger condition is just the parameter");
				return NULL;
			}
			value = g_variant_new_boolean (b);
			break;
		}
		}
		lrg_animation_transition_add_condition (t, pname, op, value);
		/* unknown condition keys are not round-tripped (conditions are
		 * plain structs); reject them so nothing is silently lost */
		extra = collect_extra (cm, cond_known);
		if (extra != NULL)
		{
			g_autoptr(GList) keys = yaml_mapping_get_members (extra);

			fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, cp, "unknown condition key '%s'", (const gchar *) keys->data);
			yaml_mapping_unref (extra);
			return NULL;
		}
	}
	stash_extra (t, map, known);
	return g_steal_pointer (&t);
}

/* A machine block: default, states, transitions, any_state. @map may be a
 * layer mapping (the layer's own keys are ignored here). */
static gboolean
parse_machine_into (Ctx                      *ctx,
                    LrgAnimationStateMachine *machine,
                    YamlMapping              *map,
                    const gchar              *path)
{
	YamlSequence *states = NULL;
	const gchar  *def = NULL;
	guint         i;
	gint          pass;

	if (!get_string (ctx, map, path, "default", &def))
		return FALSE;
	if (yaml_mapping_has_member (map, "states"))
	{
		g_autofree gchar *sp = sub_path (path, "states");

		states = as_sequence (ctx, yaml_mapping_get_member (map, "states"), sp);
		if (states == NULL)
			return FALSE;
	}
	for (i = 0; states != NULL && i < yaml_sequence_get_length (states); i++)
	{
		g_autofree gchar             *sp = index_path (path, "states", i);
		YamlMapping                  *sm = as_mapping (ctx, yaml_sequence_get_element (states, i), sp);
		g_autoptr(LrgAnimationState)  st = sm != NULL ? parse_state (ctx, sm, sp) : NULL;

		if (st == NULL)
			return FALSE;
		if (lrg_animation_state_machine_get_state (machine, lrg_animation_state_get_name (st)) != NULL)
			return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, sp, "duplicate state '%s'",
			             lrg_animation_state_get_name (st));
		lrg_animation_state_machine_add_state (machine, st);
	}

	if (def != NULL)
	{
		g_autofree gchar *dp = sub_path (path, "default");

		if (lrg_animation_state_machine_get_state (machine, def) == NULL)
			return fail (ctx, LRG_ANIM_GRAPH_ERROR_REFERENCE, dp, "unknown state '%s'", def);
		lrg_animation_state_machine_set_default_state (machine, def);
	}
	else if (states != NULL && yaml_sequence_get_length (states) > 0)
	{
		/* no default: the first state */
		g_autoptr(GList) ordered = lrg_animation_state_machine_get_states_ordered (machine);

		lrg_animation_state_machine_set_default_state (machine, lrg_animation_state_get_name (ordered->data));
	}

	/* any_state first (they are checked first at runtime), then the rest */
	for (pass = 0; pass < 2; pass++)
	{
		const gchar  *key = pass == 0 ? "any_state" : "transitions";
		YamlSequence *seq;

		if (!yaml_mapping_has_member (map, key))
			continue;
		{
			g_autofree gchar *tp = sub_path (path, key);

			seq = as_sequence (ctx, yaml_mapping_get_member (map, key), tp);
		}
		if (seq == NULL)
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar                  *tp = index_path (path, key, i);
			YamlMapping                       *tm = as_mapping (ctx, yaml_sequence_get_element (seq, i), tp);
			g_autoptr(LrgAnimationTransition)  t = tm != NULL ? parse_transition (ctx, machine, tm, tp, pass == 0)
			                                                  : NULL;

			if (t == NULL)
				return FALSE;
			lrg_animation_state_machine_add_transition (machine, t);
		}
	}
	return TRUE;
}

static LrgAnimationStateMachine *
parse_machine (Ctx         *ctx,
               YamlMapping *map,
               const gchar *path)
{
	static const gchar *const known[] = { "default", "states", "transitions", "any_state", NULL };
	g_autoptr(LrgAnimationStateMachine) m = lrg_animation_state_machine_new ();

	if (!parse_machine_into (ctx, m, map, path))
		return NULL;
	stash_extra (m, map, known);
	return g_steal_pointer (&m);
}

static LrgAnimationLayer *
parse_layer (Ctx         *ctx,
             YamlMapping *map,
             const gchar *path)
{
	static const gchar *const known[] = { "name", "weight", "blend", "enabled", "mask", "mask_recursive",
	                                     "default", "states", "transitions", "any_state", NULL };
	g_autoptr(LrgAnimationLayer)        layer = NULL;
	g_autoptr(LrgAnimationStateMachine) m = NULL;
	g_autoptr(GPtrArray)                mask = NULL;
	const gchar                        *name = NULL;
	gfloat                              weight = 1.0f;
	gint                                blend = LRG_LAYER_BLEND_OVERRIDE;
	gboolean                            enabled = TRUE, recursive = TRUE;
	guint                               i;

	if (!get_string (ctx, map, path, "name", &name) ||
	    !get_float (ctx, map, path, "weight", &weight, NULL) ||
	    !get_nick (ctx, map, path, "blend", layer_blend_nicks, &blend) ||
	    !get_bool (ctx, map, path, "enabled", &enabled) ||
	    !get_bool (ctx, map, path, "mask_recursive", &recursive) ||
	    !get_strv (ctx, map, path, "mask", &mask))
		return NULL;
	if (name == NULL || *name == '\0')
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "a layer needs a name");
		return NULL;
	}
	if (weight < 0.0f || weight > 1.0f)
	{
		g_autofree gchar *wp = sub_path (path, "weight");

		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, wp, "weight must be 0..1");
		return NULL;
	}

	layer = lrg_animation_layer_new (name);
	lrg_animation_layer_set_weight (layer, weight);
	lrg_animation_layer_set_blend_mode (layer, (LrgLayerBlendMode) blend);
	lrg_animation_layer_set_enabled (layer, enabled);
	lrg_animation_layer_set_mask_recursive (layer, recursive);
	for (i = 0; mask != NULL && g_ptr_array_index (mask, i) != NULL; i++)
		lrg_animation_layer_add_mask (layer, g_ptr_array_index (mask, i));

	m = lrg_animation_state_machine_new ();
	if (!parse_machine_into (ctx, m, map, path))
		return NULL;
	lrg_animation_layer_set_state_machine (layer, m);
	stash_extra (layer, map, known);
	return g_steal_pointer (&layer);
}

static LrgIKGoal *
parse_goal (Ctx         *ctx,
            YamlMapping *map,
            const gchar *path)
{
	static const gchar *const known[] = { "name", "type", "bones", "effector", "pole", "target", "weight",
	                                     "iterations", "tolerance", "axis", "enabled", "ray_up", "ray_down",
	                                     "align", NULL };
	g_autoptr(LrgIKGoal)  goal = NULL;
	g_autoptr(GPtrArray)  bones = NULL;
	const gchar          *name = NULL, *effector = NULL;
	gint                  kind = LRG_IK_GOAL_TWO_BONE;
	gfloat                pole[3], target[3], axis[3] = { 0.0f, 1.0f, 0.0f };
	gboolean              has_pole = FALSE, has_target = FALSE, enabled = TRUE, align = FALSE;
	gfloat                weight = 1.0f, tolerance = 0.001f, ray_up = 0.5f, ray_down = 0.5f;
	gint64                iterations = 10;
	guint                 need, nb;

	if (!get_string (ctx, map, path, "name", &name) ||
	    !get_nick (ctx, map, path, "type", ik_kind_nicks, &kind) ||
	    !get_strv (ctx, map, path, "bones", &bones) ||
	    !get_string (ctx, map, path, "effector", &effector) ||
	    !get_vec (ctx, map, path, "pole", 3, pole, &has_pole) ||
	    !get_vec (ctx, map, path, "target", 3, target, &has_target) ||
	    !get_vec (ctx, map, path, "axis", 3, axis, NULL) ||
	    !get_float (ctx, map, path, "weight", &weight, NULL) ||
	    !get_int (ctx, map, path, "iterations", &iterations) ||
	    !get_float (ctx, map, path, "tolerance", &tolerance, NULL) ||
	    !get_bool (ctx, map, path, "enabled", &enabled) ||
	    !get_float (ctx, map, path, "ray_up", &ray_up, NULL) ||
	    !get_float (ctx, map, path, "ray_down", &ray_down, NULL) ||
	    !get_bool (ctx, map, path, "align", &align))
		return NULL;
	if (name == NULL || *name == '\0')
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "an IK goal needs a name");
		return NULL;
	}
	nb = bones != NULL ? bones->len - 1 : 0;
	need = (kind == LRG_IK_GOAL_TWO_BONE || kind == LRG_IK_GOAL_FOOT) ? 2 : kind == LRG_IK_GOAL_LOOK_AT ? 1 : 0;
	if ((need > 0 && nb != need) || nb == 0)
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "a %s goal needs %s bone%s",
		      nick_of (ik_kind_nicks, kind), need == 0 ? "at least one" : need == 1 ? "exactly one" : "exactly two",
		      need == 1 ? "" : "s");
		return NULL;
	}
	if (weight < 0.0f || weight > 1.0f || iterations < 1 || iterations > 1000)
	{
		fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, path, "weight must be 0..1 and iterations 1..1000");
		return NULL;
	}

	goal = lrg_ik_goal_new (name, (LrgIKGoalKind) kind);
	lrg_ik_goal_set_bones (goal, (const gchar *const *) bones->pdata);
	lrg_ik_goal_set_effector (goal, effector);
	if (has_pole)
		lrg_ik_goal_set_pole (goal, pole[0], pole[1], pole[2]);
	if (has_target)
		lrg_ik_goal_set_target (goal, target[0], target[1], target[2]);
	lrg_ik_goal_set_bone_axis (goal, axis[0], axis[1], axis[2]);
	lrg_ik_goal_set_weight (goal, weight);
	lrg_ik_goal_set_iterations (goal, (guint) iterations);
	lrg_ik_goal_set_tolerance (goal, tolerance);
	lrg_ik_goal_set_enabled (goal, enabled);
	lrg_ik_goal_set_foot (goal, ray_up, ray_down, align);
	stash_extra (goal, map, known);
	return g_steal_pointer (&goal);
}

static gboolean
parse_graph (Ctx         *ctx,
             YamlNode    *root)
{
	static const gchar *const known[] = { "version", "name", "clips", "parameters", "root_motion", "layers",
	                                     "ik", NULL };
	static const gchar *const param_known[] = { "name", "type", "default", NULL };
	static const gchar *const clip_known[] = { "source", "animation", NULL };
	static const gchar *const rm_known[] = { "bone", "mode", "rotation", "apply", NULL };
	LrgAnimGraph             *g = ctx->graph;
	YamlMapping              *map;
	const gchar              *name = NULL;
	gint64                    version = LRG_ANIM_GRAPH_FORMAT_VERSION;
	guint                     i;

	map = as_mapping (ctx, root, "(root)");
	if (map == NULL)
	{
		g_clear_error (ctx->error);
		return fail (ctx, LRG_ANIM_GRAPH_ERROR_PARSE, "(root)", "an animation graph is a YAML mapping");
	}
	if (!get_int (ctx, map, "", "version", &version))
		return FALSE;
	if (version > LRG_ANIM_GRAPH_FORMAT_VERSION || version < 1)
		return fail (ctx, LRG_ANIM_GRAPH_ERROR_VERSION, "version",
		             "version %" G_GINT64_FORMAT " is not supported (this build reads 1..%d)",
		             version, LRG_ANIM_GRAPH_FORMAT_VERSION);
	if (!get_string (ctx, map, "", "name", &name))
		return FALSE;
	g->name = g_strdup (name);

	/* clips: key -> {source, animation} */
	if (yaml_mapping_has_member (map, "clips"))
	{
		YamlMapping     *cm = as_mapping (ctx, yaml_mapping_get_member (map, "clips"), "clips");
		g_autoptr(GList) keys = NULL;
		GList           *l;

		if (cm == NULL)
			return FALSE;
		keys = yaml_mapping_get_members (cm);
		for (l = keys; l != NULL; l = l->next)
		{
			g_autofree gchar *cp = sub_path ("clips", l->data);
			YamlMapping      *em = as_mapping (ctx, yaml_mapping_get_member (cm, l->data), cp);
			const gchar      *src = NULL, *anim = NULL;
			ClipDef          *c;

			if (em == NULL || !get_string (ctx, em, cp, "source", &src) ||
			    !get_string (ctx, em, cp, "animation", &anim))
				return FALSE;
			c = g_new0 (ClipDef, 1);
			c->name = g_strdup (l->data);
			c->source = g_strdup (src);
			c->animation = g_strdup (anim);
			c->extra = collect_extra (em, clip_known);
			g_ptr_array_add (g->clips, c);
		}
	}

	/* parameters */
	if (yaml_mapping_has_member (map, "parameters"))
	{
		YamlSequence *seq = as_sequence (ctx, yaml_mapping_get_member (map, "parameters"), "parameters");

		if (seq == NULL)
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *pp = index_path ("", "parameters", i);
			YamlMapping      *pm = as_mapping (ctx, yaml_sequence_get_element (seq, i), pp);
			const gchar      *pname = NULL, *dtext = NULL;
			gint              type = LRG_ANIM_PARAMETER_FLOAT;
			GVariant         *def = NULL;
			ParamDef         *p;

			if (pm == NULL || !get_string (ctx, pm, pp, "name", &pname) ||
			    !get_nick (ctx, pm, pp, "type", param_type_nicks, &type) ||
			    !get_string (ctx, pm, pp, "default", &dtext))
				return FALSE;
			if (pname == NULL || *pname == '\0')
				return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, pp, "a parameter needs a name");
			if (!yaml_mapping_has_member (pm, "type"))
				return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, pp, "a parameter needs a type");
			if (find_param (g, pname) != NULL)
				return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, pp, "duplicate parameter '%s'", pname);
			if (dtext != NULL)
			{
				g_autofree gchar *dp = sub_path (pp, "default");

				switch (type)
				{
				case LRG_ANIM_PARAMETER_FLOAT:
				{
					gfloat f;

					if (!parse_float_text (ctx, dtext, dp, &f))
						return FALSE;
					def = g_variant_new_double (f);
					break;
				}
				case LRG_ANIM_PARAMETER_INT:
				{
					gint64 v;

					if (!parse_int_text (ctx, dtext, dp, &v))
						return FALSE;
					def = g_variant_new_int32 ((gint32) v);
					break;
				}
				default:
				{
					gboolean b;

					if (!parse_bool_text (ctx, dtext, dp, &b))
						return FALSE;
					def = g_variant_new_boolean (b);
					break;
				}
				}
			}
			lrg_anim_graph_add_parameter (g, pname, (LrgAnimParameterType) type, def);
			p = find_param (g, pname);
			if (p != NULL)
				p->extra = collect_extra (pm, param_known);
		}
	}

	/* root motion */
	if (yaml_mapping_has_member (map, "root_motion"))
	{
		YamlMapping *rm = as_mapping (ctx, yaml_mapping_get_member (map, "root_motion"), "root_motion");
		const gchar *bone = NULL;
		gint         mode = LRG_ROOT_MOTION_NONE;

		if (rm == NULL || !get_string (ctx, rm, "root_motion", "bone", &bone) ||
		    !get_nick (ctx, rm, "root_motion", "mode", root_mode_nicks, &mode) ||
		    !get_bool (ctx, rm, "root_motion", "rotation", &g->rm_rotation) ||
		    !get_bool (ctx, rm, "root_motion", "apply", &g->rm_apply))
			return FALSE;
		if (bone == NULL && (mode != LRG_ROOT_MOTION_NONE || g->rm_rotation))
			return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, "root_motion", "root motion needs a bone");
		g->rm_bone = g_strdup (bone);
		g->rm_mode = (LrgRootMotionMode) mode;
		g->rm_extra = collect_extra (rm, rm_known);
	}

	/* layers */
	if (yaml_mapping_has_member (map, "layers"))
	{
		YamlSequence *seq = as_sequence (ctx, yaml_mapping_get_member (map, "layers"), "layers");

		if (seq == NULL)
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar             *lp = index_path ("", "layers", i);
			YamlMapping                  *lm = as_mapping (ctx, yaml_sequence_get_element (seq, i), lp);
			g_autoptr(LrgAnimationLayer)  layer = lm != NULL ? parse_layer (ctx, lm, lp) : NULL;

			if (layer == NULL)
				return FALSE;
			if (lrg_anim_graph_get_layer_by_name (g, lrg_animation_layer_get_name (layer)) != NULL)
				return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, lp, "duplicate layer '%s'",
				             lrg_animation_layer_get_name (layer));
			g_ptr_array_add (g->layers, g_object_ref (layer));
		}
	}

	/* IK goals */
	if (yaml_mapping_has_member (map, "ik"))
	{
		YamlSequence *seq = as_sequence (ctx, yaml_mapping_get_member (map, "ik"), "ik");

		if (seq == NULL)
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar     *ip = index_path ("", "ik", i);
			YamlMapping          *im = as_mapping (ctx, yaml_sequence_get_element (seq, i), ip);
			g_autoptr(LrgIKGoal)  goal = im != NULL ? parse_goal (ctx, im, ip) : NULL;

			if (goal == NULL)
				return FALSE;
			if (lrg_anim_graph_get_ik_goal_by_name (g, lrg_ik_goal_get_name (goal)) != NULL)
				return fail (ctx, LRG_ANIM_GRAPH_ERROR_INVALID, ip, "duplicate IK goal '%s'",
				             lrg_ik_goal_get_name (goal));
			g_ptr_array_add (g->goals, g_object_ref (goal));
		}
	}

	g->extra = collect_extra (map, known);
	return TRUE;
}

/* Moves everything parsed into @src over to @self (whose old contents go). */
static void
adopt_contents (LrgAnimGraph *self,
                LrgAnimGraph *src)
{
	guint i;

	clear_contents (self);
	self->name = g_steal_pointer (&src->name);
	g_ptr_array_extend_and_steal (self->params, g_steal_pointer (&src->params));
	src->params = g_ptr_array_new_with_free_func (param_def_free);
	g_ptr_array_extend_and_steal (self->clips, g_steal_pointer (&src->clips));
	src->clips = g_ptr_array_new_with_free_func (clip_def_free);
	g_ptr_array_extend_and_steal (self->goals, g_steal_pointer (&src->goals));
	src->goals = g_ptr_array_new_with_free_func (g_object_unref);
	self->rm_bone = g_steal_pointer (&src->rm_bone);
	self->rm_mode = src->rm_mode;
	self->rm_rotation = src->rm_rotation;
	self->rm_apply = src->rm_apply;
	self->rm_extra = g_steal_pointer (&src->rm_extra);
	self->extra = g_steal_pointer (&src->extra);

	/* parameters into our own table */
	g_clear_object (&self->param_owner);
	self->param_owner = lrg_animation_state_machine_new ();
	for (i = 0; i < self->params->len; i++)
	{
		ParamDef *p = g_ptr_array_index (self->params, i);

		lrg_animation_state_machine_add_parameter (self->param_owner, p->name, p->type, p->def);
	}
	for (i = 0; i < src->layers->len; i++)
		lrg_anim_graph_add_layer (self, g_ptr_array_index (src->layers, i));
	g_ptr_array_set_size (src->layers, 0);
}

static gboolean
load_root (LrgAnimGraph  *self,
           YamlNode      *root,
           GError       **error)
{
	g_autoptr(LrgAnimGraph) tmp = lrg_anim_graph_new ();
	Ctx                     ctx = { tmp, error };

	if (!parse_graph (&ctx, root))
		return FALSE;
	adopt_contents (self, tmp);
	return TRUE;
}

gboolean
lrg_anim_graph_load_from_data (LrgAnimGraph  *self,
                               const gchar   *data,
                               gssize         length,
                               GError       **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError)     local = NULL;
	YamlNode             *root;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_ANIM_GRAPH_ERROR, LRG_ANIM_GRAPH_ERROR_PARSE, "%s", local->message);
		return FALSE;
	}
	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error_literal (error, LRG_ANIM_GRAPH_ERROR, LRG_ANIM_GRAPH_ERROR_PARSE, "the document is empty");
		return FALSE;
	}
	return load_root (self, root, error);
}

gboolean
lrg_anim_graph_load_from_file (LrgAnimGraph  *self,
                               const gchar   *path,
                               GError       **error)
{
	g_autofree gchar *text = NULL;
	gsize             len = 0;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, LRG_ANIM_GRAPH_ERROR, LRG_ANIM_GRAPH_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	if (!lrg_anim_graph_load_from_data (self, text, (gssize) len, error))
		return FALSE;
	g_free (self->base_dir);
	self->base_dir = g_path_get_dirname (path);
	return TRUE;
}

LrgAnimGraph *
lrg_anim_graph_new (void)
{
	return g_object_new (LRG_TYPE_ANIM_GRAPH, NULL);
}

LrgAnimGraph *
lrg_anim_graph_new_from_file (const gchar  *path,
                              GError      **error)
{
	g_autoptr(LrgAnimGraph) g = lrg_anim_graph_new ();

	if (!lrg_anim_graph_load_from_file (g, path, error))
		return NULL;
	return g_steal_pointer (&g);
}

LrgAnimGraph *
lrg_anim_graph_new_from_data (const gchar  *data,
                              gssize        length,
                              GError      **error)
{
	g_autoptr(LrgAnimGraph) g = lrg_anim_graph_new ();

	if (!lrg_anim_graph_load_from_data (g, data, length, error))
		return NULL;
	return g_steal_pointer (&g);
}

/* ==========================================================================
 * Writing
 * ========================================================================== */

static void
put (YamlMapping *map,
     const gchar *key,
     YamlNode    *node)
{
	yaml_mapping_set_member (map, key, node);
	yaml_node_unref (node);
}

static YamlNode *
float_node (gfloat v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	return yaml_node_new_scalar (g_ascii_formatd (buf, sizeof buf, "%.6g", (gdouble) v), YAML_SCALAR_STYLE_PLAIN);
}

static void
put_float (YamlMapping *map,
           const gchar *key,
           gfloat       v)
{
	put (map, key, float_node (v));
}

static void
put_vec (YamlMapping  *map,
         const gchar  *key,
         const gfloat *v,
         guint         n)
{
	YamlNode     *node = yaml_node_new_sequence (NULL);
	YamlSequence *seq = yaml_node_get_sequence (node);
	guint         i;

	for (i = 0; i < n; i++)
	{
		YamlNode *e = float_node (v[i]);

		yaml_sequence_add_element (seq, e);
		yaml_node_unref (e);
	}
	put (map, key, node);
}

static void
put_strv (YamlMapping *map,
          const gchar *key,
          gchar      **v)
{
	YamlNode     *node = yaml_node_new_sequence (NULL);
	YamlSequence *seq = yaml_node_get_sequence (node);
	guint         i;

	for (i = 0; v != NULL && v[i] != NULL; i++)
		yaml_sequence_add_string_element (seq, v[i]);
	put (map, key, node);
}

static void
copy_extra (YamlMapping *map,
            YamlMapping *extra)
{
	g_autoptr(GList) keys = NULL;
	GList           *l;

	if (extra == NULL)
		return;
	keys = yaml_mapping_get_members (extra);
	for (l = keys; l != NULL; l = l->next)
		if (!yaml_mapping_has_member (map, l->data))
			yaml_mapping_set_member (map, l->data, yaml_mapping_get_member (extra, l->data));
}

static void
copy_object_extra (YamlMapping *map,
                   gpointer     object)
{
	copy_extra (map, g_object_get_qdata (G_OBJECT (object), extra_quark ()));
}

static YamlNode *write_machine (LrgAnimGraph *self, LrgAnimationStateMachine *m, YamlMapping *into);

static YamlNode *
write_tree (LrgBlendTree *tree)
{
	YamlNode     *node = yaml_node_new_mapping (NULL);
	YamlMapping  *map = yaml_node_get_mapping (node);
	LrgBlendType  type = lrg_blend_tree_get_blend_type (tree);
	guint         i, n = lrg_blend_tree_get_child_count (tree);

	yaml_mapping_set_string_member (map, "type", nick_of (blend_type_nicks, type));
	if (lrg_blend_tree_get_parameter_name (tree) != NULL)
		yaml_mapping_set_string_member (map, "parameter", lrg_blend_tree_get_parameter_name (tree));
	if (lrg_blend_tree_get_parameter_y_name (tree) != NULL)
		yaml_mapping_set_string_member (map, "parameter_y", lrg_blend_tree_get_parameter_y_name (tree));
	if (!lrg_blend_tree_get_sync (tree))
		yaml_mapping_set_boolean_member (map, "sync", FALSE);
	if (n > 0)
	{
		YamlNode     *cnode = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (cnode);

		for (i = 0; i < n; i++)
		{
			LrgBlendTreeChild *c = lrg_blend_tree_get_child (tree, i);
			YamlNode          *enode = yaml_node_new_mapping (NULL);
			YamlMapping       *emap = yaml_node_get_mapping (enode);
			const gchar       *clip = lrg_blend_tree_child_get_clip_name (c);

			if (clip == NULL && c->clip != NULL)
				clip = lrg_animation_clip_get_name (c->clip);
			if (c->subtree != NULL)
				put (emap, "tree", write_tree (c->subtree));
			else if (clip != NULL)
				yaml_mapping_set_string_member (emap, "clip", clip);
			if (type == LRG_BLEND_TYPE_1D)
				put_float (emap, "threshold", c->threshold);
			else if (type == LRG_BLEND_TYPE_2D_SIMPLE || type == LRG_BLEND_TYPE_2D_FREEFORM)
			{
				gfloat pos[2] = { c->position_x, c->position_y };

				put_vec (emap, "position", pos, 2);
			}
			else
			{
				if (c->weight_parameter != NULL)
					yaml_mapping_set_string_member (emap, "parameter", c->weight_parameter);
				else
					put_float (emap, "weight", c->weight);
			}
			if (c->speed != 1.0f)
				put_float (emap, "speed", c->speed);
			copy_extra (emap, c->extra);
			yaml_sequence_add_element (seq, enode);
			yaml_node_unref (enode);
		}
		put (map, "children", cnode);
	}
	copy_object_extra (map, tree);
	return node;
}

static YamlNode *
write_state (LrgAnimGraph      *self,
             LrgAnimationState *st)
{
	YamlNode                 *node = yaml_node_new_mapping (NULL);
	YamlMapping              *map = yaml_node_get_mapping (node);
	const gchar              *clip = lrg_animation_state_get_clip_name (st);
	LrgBlendTree             *tree = lrg_animation_state_get_blend_tree (st);
	LrgAnimationStateMachine *sub = lrg_animation_state_get_sub_machine (st);

	yaml_mapping_set_string_member (map, "name", lrg_animation_state_get_name (st));
	if (clip == NULL && lrg_animation_state_get_clip (st) != NULL)
		clip = lrg_animation_clip_get_name (lrg_animation_state_get_clip (st));
	if (sub != NULL)
		put (map, "machine", write_machine (self, sub, NULL));
	else if (tree != NULL)
		put (map, "blend_tree", write_tree (tree));
	else if (clip != NULL)
		yaml_mapping_set_string_member (map, "clip", clip);
	if (lrg_animation_state_get_speed (st) != 1.0f)
		put_float (map, "speed", lrg_animation_state_get_speed (st));
	if (lrg_animation_state_get_speed_parameter (st) != NULL)
		yaml_mapping_set_string_member (map, "speed_parameter", lrg_animation_state_get_speed_parameter (st));
	if (lrg_animation_state_get_mirror (st))
		yaml_mapping_set_boolean_member (map, "mirror", TRUE);
	copy_object_extra (map, st);
	return node;
}

static YamlNode *
write_transition (LrgAnimGraph           *self,
                  LrgAnimationTransition *t)
{
	YamlNode    *node = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (node);
	GList       *l;

	if (!lrg_animation_transition_is_any_state (t))
		yaml_mapping_set_string_member (map, "from", lrg_animation_transition_get_source (t));
	yaml_mapping_set_string_member (map, "to", lrg_animation_transition_get_target (t));
	put_float (map, "duration", lrg_animation_transition_get_duration (t));
	if (lrg_animation_transition_get_has_exit_time (t) && lrg_animation_transition_get_exit_time (t) >= 0.0f)
		put_float (map, "exit_time", lrg_animation_transition_get_exit_time (t));
	if (lrg_animation_transition_get_offset (t) != 0.0f)
		put_float (map, "offset", lrg_animation_transition_get_offset (t));
	if (lrg_animation_transition_get_interruption (t) != LRG_TRANSITION_INTERRUPTION_NONE)
		yaml_mapping_set_string_member (map, "interruption",
		                                nick_of (interruption_nicks, lrg_animation_transition_get_interruption (t)));
	if (lrg_animation_transition_get_priority (t) != 0)
		yaml_mapping_set_int_member (map, "priority", lrg_animation_transition_get_priority (t));
	if (lrg_animation_transition_get_can_transition_to_self (t))
		yaml_mapping_set_boolean_member (map, "can_transition_to_self", TRUE);

	l = lrg_animation_transition_get_conditions (t);
	if (l != NULL)
	{
		YamlNode     *cnode = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (cnode);

		for (; l != NULL; l = l->next)
		{
			LrgTransitionCondition *c = l->data;
			YamlNode               *enode = yaml_node_new_mapping (NULL);
			YamlMapping            *emap = yaml_node_get_mapping (enode);
			gboolean                is_bool = c->value != NULL && g_variant_is_of_type (c->value, G_VARIANT_TYPE_BOOLEAN);

			yaml_mapping_set_string_member (emap, "parameter", c->parameter);
			/* bool/trigger "== true" is just the parameter */
			if (!(is_bool && c->comparison == LRG_CONDITION_EQUALS && g_variant_get_boolean (c->value)))
			{
				yaml_mapping_set_string_member (emap, "op", nick_of (op_nicks, c->comparison));
				if (c->value == NULL)
					;
				else if (is_bool)
					yaml_mapping_set_boolean_member (emap, "value", g_variant_get_boolean (c->value));
				else if (g_variant_is_of_type (c->value, G_VARIANT_TYPE_INT32))
					yaml_mapping_set_int_member (emap, "value", g_variant_get_int32 (c->value));
				else if (g_variant_is_of_type (c->value, G_VARIANT_TYPE_DOUBLE))
					put_float (emap, "value", (gfloat) g_variant_get_double (c->value));
			}
			yaml_sequence_add_element (seq, enode);
			yaml_node_unref (enode);
		}
		put (map, "conditions", cnode);
	}
	copy_object_extra (map, t);
	return node;
}

/* Writes a machine's keys into @into (a layer mapping) or a new mapping. */
static YamlNode *
write_machine (LrgAnimGraph             *self,
               LrgAnimationStateMachine *m,
               YamlMapping              *into)
{
	YamlNode         *node = into == NULL ? yaml_node_new_mapping (NULL) : NULL;
	YamlMapping      *map = into != NULL ? into : yaml_node_get_mapping (node);
	g_autoptr(GList)  states = lrg_animation_state_machine_get_states_ordered (m);
	g_autoptr(GList)  trans = lrg_animation_state_machine_get_transitions (m);
	GList            *l;
	gint              pass;

	if (lrg_animation_state_machine_get_default_state (m) != NULL)
		yaml_mapping_set_string_member (map, "default", lrg_animation_state_machine_get_default_state (m));
	if (states != NULL)
	{
		YamlNode     *snode = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (snode);

		for (l = states; l != NULL; l = l->next)
		{
			YamlNode *e = write_state (self, l->data);

			yaml_sequence_add_element (seq, e);
			yaml_node_unref (e);
		}
		put (map, "states", snode);
	}
	for (pass = 0; pass < 2; pass++)
	{
		YamlNode     *tnode = NULL;
		YamlSequence *seq = NULL;

		for (l = trans; l != NULL; l = l->next)
		{
			YamlNode *e;

			if (lrg_animation_transition_is_any_state (l->data) != (pass == 0))
				continue;
			if (tnode == NULL)
			{
				tnode = yaml_node_new_sequence (NULL);
				seq = yaml_node_get_sequence (tnode);
			}
			e = write_transition (self, l->data);
			yaml_sequence_add_element (seq, e);
			yaml_node_unref (e);
		}
		if (tnode != NULL)
			put (map, pass == 0 ? "any_state" : "transitions", tnode);
	}
	if (into == NULL)
		copy_object_extra (map, m);
	return node;
}

static YamlNode *
write_goal (LrgIKGoal *goal)
{
	YamlNode        *node = yaml_node_new_mapping (NULL);
	YamlMapping     *map = yaml_node_get_mapping (node);
	g_auto(GStrv)    bones = lrg_ik_goal_get_bones (goal);
	gfloat           v[3], up, down;
	gboolean         align;

	yaml_mapping_set_string_member (map, "name", lrg_ik_goal_get_name (goal));
	yaml_mapping_set_string_member (map, "type", nick_of (ik_kind_nicks, lrg_ik_goal_get_kind (goal)));
	put_strv (map, "bones", bones);
	if (lrg_ik_goal_get_effector (goal) != NULL)
		yaml_mapping_set_string_member (map, "effector", lrg_ik_goal_get_effector (goal));
	if (lrg_ik_goal_get_pole (goal, v))
		put_vec (map, "pole", v, 3);
	if (lrg_ik_goal_get_target (goal, v))
		put_vec (map, "target", v, 3);
	lrg_ik_goal_get_bone_axis (goal, v);
	if (v[0] != 0.0f || v[1] != 1.0f || v[2] != 0.0f)
		put_vec (map, "axis", v, 3);
	if (lrg_ik_goal_get_weight (goal) != 1.0f)
		put_float (map, "weight", lrg_ik_goal_get_weight (goal));
	if (lrg_ik_goal_get_iterations (goal) != 10)
		yaml_mapping_set_int_member (map, "iterations", lrg_ik_goal_get_iterations (goal));
	if (lrg_ik_goal_get_tolerance (goal) != 0.001f)
		put_float (map, "tolerance", lrg_ik_goal_get_tolerance (goal));
	if (!lrg_ik_goal_get_enabled (goal))
		yaml_mapping_set_boolean_member (map, "enabled", FALSE);
	lrg_ik_goal_get_foot (goal, &up, &down, &align);
	if (lrg_ik_goal_get_kind (goal) == LRG_IK_GOAL_FOOT)
	{
		put_float (map, "ray_up", up);
		put_float (map, "ray_down", down);
		if (align)
			yaml_mapping_set_boolean_member (map, "align", TRUE);
	}
	copy_object_extra (map, goal);
	return node;
}

gchar *
lrg_anim_graph_to_data (LrgAnimGraph  *self,
                        GError       **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	YamlMapping             *map;
	guint                    i;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);

	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_int_member (map, "version", LRG_ANIM_GRAPH_FORMAT_VERSION);
	if (self->name != NULL)
		yaml_mapping_set_string_member (map, "name", self->name);

	if (self->clips->len > 0)
	{
		YamlNode    *cnode = yaml_node_new_mapping (NULL);
		YamlMapping *cmap = yaml_node_get_mapping (cnode);

		for (i = 0; i < self->clips->len; i++)
		{
			ClipDef     *c = g_ptr_array_index (self->clips, i);
			YamlNode    *enode = yaml_node_new_mapping (NULL);
			YamlMapping *emap = yaml_node_get_mapping (enode);

			if (c->source != NULL)
				yaml_mapping_set_string_member (emap, "source", c->source);
			if (c->animation != NULL)
				yaml_mapping_set_string_member (emap, "animation", c->animation);
			copy_extra (emap, c->extra);
			put (cmap, c->name, enode);
		}
		put (map, "clips", cnode);
	}

	if (self->params->len > 0)
	{
		YamlNode     *pnode = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (pnode);

		for (i = 0; i < self->params->len; i++)
		{
			ParamDef    *p = g_ptr_array_index (self->params, i);
			YamlNode    *enode = yaml_node_new_mapping (NULL);
			YamlMapping *emap = yaml_node_get_mapping (enode);

			yaml_mapping_set_string_member (emap, "name", p->name);
			yaml_mapping_set_string_member (emap, "type", nick_of (param_type_nicks, p->type));
			if (g_variant_is_of_type (p->def, G_VARIANT_TYPE_DOUBLE) && g_variant_get_double (p->def) != 0.0)
				put_float (emap, "default", (gfloat) g_variant_get_double (p->def));
			else if (g_variant_is_of_type (p->def, G_VARIANT_TYPE_INT32) && g_variant_get_int32 (p->def) != 0)
				yaml_mapping_set_int_member (emap, "default", g_variant_get_int32 (p->def));
			else if (g_variant_is_of_type (p->def, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean (p->def))
				yaml_mapping_set_boolean_member (emap, "default", TRUE);
			copy_extra (emap, p->extra);
			yaml_sequence_add_element (seq, enode);
			yaml_node_unref (enode);
		}
		put (map, "parameters", pnode);
	}

	if (self->rm_bone != NULL || self->rm_extra != NULL)
	{
		YamlNode    *rnode = yaml_node_new_mapping (NULL);
		YamlMapping *rmap = yaml_node_get_mapping (rnode);

		if (self->rm_bone != NULL)
			yaml_mapping_set_string_member (rmap, "bone", self->rm_bone);
		yaml_mapping_set_string_member (rmap, "mode", nick_of (root_mode_nicks, self->rm_mode));
		yaml_mapping_set_boolean_member (rmap, "rotation", self->rm_rotation);
		yaml_mapping_set_boolean_member (rmap, "apply", self->rm_apply);
		copy_extra (rmap, self->rm_extra);
		put (map, "root_motion", rnode);
	}

	if (self->layers->len > 0)
	{
		YamlNode     *lnode = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (lnode);

		for (i = 0; i < self->layers->len; i++)
		{
			LrgAnimationLayer        *layer = g_ptr_array_index (self->layers, i);
			LrgAnimationStateMachine *m = lrg_animation_layer_get_state_machine (layer);
			YamlNode                 *enode = yaml_node_new_mapping (NULL);
			YamlMapping              *emap = yaml_node_get_mapping (enode);
			g_auto(GStrv)             mask = lrg_animation_layer_get_mask (layer);

			yaml_mapping_set_string_member (emap, "name", lrg_animation_layer_get_name (layer));
			if (lrg_animation_layer_get_weight (layer) != 1.0f)
				put_float (emap, "weight", lrg_animation_layer_get_weight (layer));
			if (lrg_animation_layer_get_blend_mode (layer) != LRG_LAYER_BLEND_OVERRIDE)
				yaml_mapping_set_string_member (emap, "blend",
				                                nick_of (layer_blend_nicks, lrg_animation_layer_get_blend_mode (layer)));
			if (!lrg_animation_layer_get_enabled (layer))
				yaml_mapping_set_boolean_member (emap, "enabled", FALSE);
			if (mask != NULL)
				put_strv (emap, "mask", mask);
			if (!lrg_animation_layer_get_mask_recursive (layer))
				yaml_mapping_set_boolean_member (emap, "mask_recursive", FALSE);
			if (m != NULL)
				write_machine (self, m, emap);
			copy_object_extra (emap, layer);
			yaml_sequence_add_element (seq, enode);
			yaml_node_unref (enode);
		}
		put (map, "layers", lnode);
	}

	if (self->goals->len > 0)
	{
		YamlNode     *inode = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (inode);

		for (i = 0; i < self->goals->len; i++)
		{
			YamlNode *e = write_goal (g_ptr_array_index (self->goals, i));

			yaml_sequence_add_element (seq, e);
			yaml_node_unref (e);
		}
		put (map, "ik", inode);
	}
	copy_extra (map, self->extra);

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, NULL, error);
}

gboolean
lrg_anim_graph_save_to_file (LrgAnimGraph  *self,
                             const gchar   *path,
                             GError       **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_anim_graph_to_data (self, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, -1, &local))
	{
		g_set_error (error, LRG_ANIM_GRAPH_ERROR, LRG_ANIM_GRAPH_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return TRUE;
}

LrgAnimGraph *
lrg_anim_graph_duplicate (LrgAnimGraph  *self,
                          GError       **error)
{
	g_autofree gchar        *text = NULL;
	g_autoptr(LrgAnimGraph)  copy = NULL;
	guint                    i;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);

	text = lrg_anim_graph_to_data (self, error);
	if (text == NULL)
		return NULL;
	copy = lrg_anim_graph_new_from_data (text, -1, error);
	if (copy == NULL)
		return NULL;
	copy->base_dir = g_strdup (self->base_dir);
	copy->resolver = self->resolver;
	copy->resolver_data = self->resolver_data;   /* not owned by the copy */
	for (i = 0; i < self->clips->len; i++)
	{
		ClipDef *c = g_ptr_array_index (self->clips, i);

		if (c->clip != NULL)
			lrg_anim_graph_set_clip (copy, c->name, c->clip);
	}
	return g_steal_pointer (&copy);
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

const gchar *
lrg_anim_graph_get_name (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	return self->name;
}

void
lrg_anim_graph_set_name (LrgAnimGraph *self,
                         const gchar  *name)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	g_free (self->name);
	self->name = g_strdup (name);
}

const gchar *
lrg_anim_graph_get_base_dir (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	return self->base_dir;
}

void
lrg_anim_graph_set_base_dir (LrgAnimGraph *self,
                             const gchar  *dir)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	g_free (self->base_dir);
	self->base_dir = g_strdup (dir);
}

/* --- parameters --------------------------------------------------------- */

void
lrg_anim_graph_add_parameter (LrgAnimGraph         *self,
                              const gchar          *name,
                              LrgAnimParameterType  type,
                              GVariant             *default_value)
{
	ParamDef *p;

	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	g_return_if_fail (name != NULL);

	p = find_param (self, name);
	if (p == NULL)
	{
		p = g_new0 (ParamDef, 1);
		p->name = g_strdup (name);
		g_ptr_array_add (self->params, p);
	}
	else
	{
		g_variant_unref (p->def);
	}
	p->type = type;
	if (default_value == NULL)
	{
		switch (type)
		{
		case LRG_ANIM_PARAMETER_INT:
			default_value = g_variant_new_int32 (0);
			break;
		case LRG_ANIM_PARAMETER_BOOL:
		case LRG_ANIM_PARAMETER_TRIGGER:
			default_value = g_variant_new_boolean (FALSE);
			break;
		case LRG_ANIM_PARAMETER_FLOAT:
		default:
			default_value = g_variant_new_double (0.0);
			break;
		}
	}
	p->def = g_variant_ref_sink (default_value);
	lrg_animation_state_machine_add_parameter (self->param_owner, name, type, p->def);
}

gboolean
lrg_anim_graph_remove_parameter (LrgAnimGraph *self,
                                 const gchar  *name)
{
	ParamDef *p;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	p = find_param (self, name);
	if (p == NULL)
		return FALSE;
	lrg_animation_state_machine_remove_parameter (self->param_owner, name);
	g_ptr_array_remove (self->params, p);
	return TRUE;
}

guint
lrg_anim_graph_get_parameter_count (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), 0);
	return self->params->len;
}

const gchar *
lrg_anim_graph_get_parameter_name (LrgAnimGraph *self,
                                   guint         index)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	if (index >= self->params->len)
		return NULL;
	return ((ParamDef *) g_ptr_array_index (self->params, index))->name;
}

LrgAnimParameterType
lrg_anim_graph_get_parameter_type (LrgAnimGraph *self,
                                   const gchar  *name)
{
	ParamDef *p;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), LRG_ANIM_PARAMETER_FLOAT);
	p = find_param (self, name);
	return p != NULL ? p->type : LRG_ANIM_PARAMETER_FLOAT;
}

GVariant *
lrg_anim_graph_get_parameter_default (LrgAnimGraph *self,
                                      const gchar  *name)
{
	ParamDef *p;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	p = find_param (self, name);
	return p != NULL ? p->def : NULL;
}

void
lrg_anim_graph_set_float (LrgAnimGraph *self,
                          const gchar  *name,
                          gfloat        value)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	lrg_animation_state_machine_set_float (self->param_owner, name, value);
}

gfloat
lrg_anim_graph_get_float (LrgAnimGraph *self,
                          const gchar  *name)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), 0.0f);
	return lrg_animation_state_machine_get_float (self->param_owner, name);
}

void
lrg_anim_graph_set_int (LrgAnimGraph *self,
                        const gchar  *name,
                        gint          value)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	lrg_animation_state_machine_set_int (self->param_owner, name, value);
}

gint
lrg_anim_graph_get_int (LrgAnimGraph *self,
                        const gchar  *name)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), 0);
	return lrg_animation_state_machine_get_int (self->param_owner, name);
}

void
lrg_anim_graph_set_bool (LrgAnimGraph *self,
                         const gchar  *name,
                         gboolean      value)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	lrg_animation_state_machine_set_bool (self->param_owner, name, value);
}

gboolean
lrg_anim_graph_get_bool (LrgAnimGraph *self,
                         const gchar  *name)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	return lrg_animation_state_machine_get_bool (self->param_owner, name);
}

void
lrg_anim_graph_set_trigger (LrgAnimGraph *self,
                            const gchar  *name)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	lrg_animation_state_machine_set_trigger (self->param_owner, name);
}

void
lrg_anim_graph_reset_trigger (LrgAnimGraph *self,
                              const gchar  *name)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	lrg_animation_state_machine_reset_trigger (self->param_owner, name);
}

/* --- clips -------------------------------------------------------------- */

void
lrg_anim_graph_declare_clip (LrgAnimGraph *self,
                             const gchar  *name,
                             const gchar  *source,
                             const gchar  *animation)
{
	ClipDef *c;

	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	g_return_if_fail (name != NULL);

	c = find_clip (self, name);
	if (c == NULL)
	{
		c = g_new0 (ClipDef, 1);
		c->name = g_strdup (name);
		g_ptr_array_add (self->clips, c);
	}
	g_free (c->source);
	g_free (c->animation);
	c->source = g_strdup (source);
	c->animation = g_strdup (animation);
}

guint
lrg_anim_graph_get_clip_count (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), 0);
	return self->clips->len;
}

const gchar *
lrg_anim_graph_get_clip_name (LrgAnimGraph *self,
                              guint         index)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	if (index >= self->clips->len)
		return NULL;
	return ((ClipDef *) g_ptr_array_index (self->clips, index))->name;
}

const gchar *
lrg_anim_graph_get_clip_source (LrgAnimGraph  *self,
                                const gchar   *name,
                                const gchar  **out_animation)
{
	ClipDef *c;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	c = find_clip (self, name);
	if (out_animation != NULL)
		*out_animation = c != NULL ? c->animation : NULL;
	return c != NULL ? c->source : NULL;
}

/* Hands every state / child referring to @name the clip object. */
static void
bind_machine (LrgAnimationStateMachine *m,
              const gchar              *name,
              LrgAnimationClip         *clip);

static void
bind_tree (LrgBlendTree     *tree,
           const gchar      *name,
           LrgAnimationClip *clip)
{
	guint i, n = lrg_blend_tree_get_child_count (tree);

	for (i = 0; i < n; i++)
	{
		LrgBlendTreeChild *c = lrg_blend_tree_get_child (tree, i);

		if (c->subtree != NULL)
			bind_tree (c->subtree, name, clip);
		else if (g_strcmp0 (c->clip_name, name) == 0)
			g_set_object (&c->clip, clip);
	}
}

static void
bind_machine (LrgAnimationStateMachine *m,
              const gchar              *name,
              LrgAnimationClip         *clip)
{
	g_autoptr(GList) states = lrg_animation_state_machine_get_states_ordered (m);
	GList           *l;

	for (l = states; l != NULL; l = l->next)
	{
		LrgAnimationState *st = l->data;

		if (lrg_animation_state_get_sub_machine (st) != NULL)
			bind_machine (lrg_animation_state_get_sub_machine (st), name, clip);
		if (lrg_animation_state_get_blend_tree (st) != NULL)
			bind_tree (lrg_animation_state_get_blend_tree (st), name, clip);
		if (g_strcmp0 (lrg_animation_state_get_clip_name (st), name) == 0)
			lrg_animation_state_set_clip (st, clip);
	}
}

void
lrg_anim_graph_set_clip (LrgAnimGraph     *self,
                         const gchar      *name,
                         LrgAnimationClip *clip)
{
	ClipDef *c;
	guint    i;

	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	g_return_if_fail (name != NULL);
	g_return_if_fail (clip == NULL || LRG_IS_ANIMATION_CLIP (clip));

	c = find_clip (self, name);
	if (c == NULL)
	{
		lrg_anim_graph_declare_clip (self, name, NULL, NULL);
		c = find_clip (self, name);
	}
	if (c == NULL)
		return;
	g_set_object (&c->clip, clip);
	for (i = 0; i < self->layers->len; i++)
	{
		LrgAnimationStateMachine *m = lrg_animation_layer_get_state_machine (g_ptr_array_index (self->layers, i));

		if (m != NULL)
			bind_machine (m, name, clip);
	}
}

LrgAnimationClip *
lrg_anim_graph_get_clip (LrgAnimGraph *self,
                         const gchar  *name)
{
	ClipDef *c;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	c = find_clip (self, name);
	return c != NULL ? c->clip : NULL;
}

void
lrg_anim_graph_set_clip_resolver (LrgAnimGraph           *self,
                                  LrgAnimClipResolveFunc  func,
                                  gpointer                user_data,
                                  GDestroyNotify          destroy)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));

	if (self->resolver_destroy != NULL)
		self->resolver_destroy (self->resolver_data);
	self->resolver = func;
	self->resolver_data = user_data;
	self->resolver_destroy = destroy;
}

gboolean
lrg_anim_graph_resolve_clips (LrgAnimGraph  *self,
                              GError       **error)
{
	g_autoptr(GHashTable) files = NULL;   /* source path -> GPtrArray of clips */
	guint                 i;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);

	files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
	for (i = 0; i < self->clips->len; i++)
	{
		ClipDef                    *c = g_ptr_array_index (self->clips, i);
		const gchar                *anim;
		g_autoptr(LrgAnimationClip) clip = NULL;
		g_autoptr(GError)           local = NULL;

		if (c->clip != NULL || c->source == NULL)
			continue;
		anim = c->animation != NULL ? c->animation : c->name;

		if (self->resolver != NULL)
		{
			clip = self->resolver (self, c->name, c->source, anim, self->resolver_data, &local);
		}
		else
		{
			/* the default: every clip of a glTF file, loaded once */
			g_autofree gchar *path = (self->base_dir != NULL && !g_path_is_absolute (c->source))
			                         ? g_build_filename (self->base_dir, c->source, NULL)
			                         : g_strdup (c->source);
			GPtrArray        *clips = g_hash_table_lookup (files, path);
			guint             k;

			if (clips == NULL)
			{
				clips = lrg_gltf_load_clips (path, &local);
				if (clips != NULL)
					g_hash_table_insert (files, g_strdup (path), clips);
			}
			for (k = 0; clips != NULL && k < clips->len; k++)
				if (g_strcmp0 (lrg_animation_clip_get_name (g_ptr_array_index (clips, k)), anim) == 0)
					clip = g_object_ref (g_ptr_array_index (clips, k));
			if (clips != NULL && clip == NULL)
				g_set_error (&local, LRG_ANIM_GRAPH_ERROR, LRG_ANIM_GRAPH_ERROR_CLIP,
				             "'%s' has no animation '%s'", path, anim);
		}
		if (clip == NULL)
		{
			g_set_error (error, LRG_ANIM_GRAPH_ERROR, LRG_ANIM_GRAPH_ERROR_CLIP, "clips.%s: %s", c->name,
			             local != NULL ? local->message : "the resolver returned nothing");
			return FALSE;
		}
		lrg_anim_graph_set_clip (self, c->name, clip);
	}
	return TRUE;
}

static void
collect_missing_tree (LrgAnimGraph *self,
                      LrgBlendTree *tree,
                      GHashTable   *set)
{
	guint i, n = lrg_blend_tree_get_child_count (tree);

	for (i = 0; i < n; i++)
	{
		LrgBlendTreeChild *c = lrg_blend_tree_get_child (tree, i);

		if (c->subtree != NULL)
			collect_missing_tree (self, c->subtree, set);
		else if (c->clip_name != NULL && c->clip == NULL)
			g_hash_table_add (set, c->clip_name);
	}
}

static void
collect_missing (LrgAnimGraph             *self,
                 LrgAnimationStateMachine *m,
                 GHashTable               *set)
{
	g_autoptr(GList) states = lrg_animation_state_machine_get_states_ordered (m);
	GList           *l;

	for (l = states; l != NULL; l = l->next)
	{
		LrgAnimationState *st = l->data;

		if (lrg_animation_state_get_sub_machine (st) != NULL)
			collect_missing (self, lrg_animation_state_get_sub_machine (st), set);
		if (lrg_animation_state_get_blend_tree (st) != NULL)
			collect_missing_tree (self, lrg_animation_state_get_blend_tree (st), set);
		if (lrg_animation_state_get_clip_name (st) != NULL && lrg_animation_state_get_clip (st) == NULL)
			g_hash_table_add (set, (gpointer) lrg_animation_state_get_clip_name (st));
	}
}

static gint
cmp_str (gconstpointer a,
         gconstpointer b)
{
	return g_strcmp0 (*(const gchar *const *) a, *(const gchar *const *) b);
}

gchar **
lrg_anim_graph_get_missing_clips (LrgAnimGraph *self)
{
	g_autoptr(GHashTable) set = NULL;
	g_autoptr(GPtrArray)  out = NULL;
	GHashTableIter        it;
	gpointer              key;
	guint                 i;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);

	set = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < self->layers->len; i++)
	{
		LrgAnimationStateMachine *m = lrg_animation_layer_get_state_machine (g_ptr_array_index (self->layers, i));

		if (m != NULL)
			collect_missing (self, m, set);
	}
	out = g_ptr_array_new ();
	g_hash_table_iter_init (&it, set);
	while (g_hash_table_iter_next (&it, &key, NULL))
		g_ptr_array_add (out, g_strdup (key));
	g_ptr_array_sort (out, cmp_str);
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (g_steal_pointer (&out), FALSE);
}

/* --- layers ------------------------------------------------------------- */

void
lrg_anim_graph_add_layer (LrgAnimGraph      *self,
                          LrgAnimationLayer *layer)
{
	guint i;

	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	g_return_if_fail (LRG_IS_ANIMATION_LAYER (layer));

	g_ptr_array_add (self->layers, g_object_ref (layer));
	hook_layer (self, layer);
	/* bind already known clips to the new layer's states */
	for (i = 0; i < self->clips->len; i++)
	{
		ClipDef *c = g_ptr_array_index (self->clips, i);

		if (c->clip != NULL)
			bind_machine (lrg_animation_layer_get_state_machine (layer), c->name, c->clip);
	}
}

gboolean
lrg_anim_graph_remove_layer (LrgAnimGraph *self,
                             const gchar  *name)
{
	LrgAnimationLayer *layer;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	layer = lrg_anim_graph_get_layer_by_name (self, name);
	if (layer == NULL)
		return FALSE;
	unhook_layer (self, layer);
	g_ptr_array_remove (self->layers, layer);
	return TRUE;
}

guint
lrg_anim_graph_get_layer_count (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), 0);
	return self->layers->len;
}

LrgAnimationLayer *
lrg_anim_graph_get_layer (LrgAnimGraph *self,
                          guint         index)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	return index < self->layers->len ? g_ptr_array_index (self->layers, index) : NULL;
}

LrgAnimationLayer *
lrg_anim_graph_get_layer_by_name (LrgAnimGraph *self,
                                  const gchar  *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	for (i = 0; name != NULL && i < self->layers->len; i++)
	{
		LrgAnimationLayer *l = g_ptr_array_index (self->layers, i);

		if (g_strcmp0 (lrg_animation_layer_get_name (l), name) == 0)
			return l;
	}
	return NULL;
}

LrgAnimationStateMachine *
lrg_anim_graph_get_machine (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	if (self->layers->len == 0)
		return NULL;
	return lrg_animation_layer_get_state_machine (g_ptr_array_index (self->layers, 0));
}

/* --- root motion -------------------------------------------------------- */

const gchar *
lrg_anim_graph_get_root_motion_bone (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	return self->rm_bone;
}

void
lrg_anim_graph_set_root_motion_bone (LrgAnimGraph *self,
                                     const gchar  *bone_name)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	g_free (self->rm_bone);
	self->rm_bone = g_strdup (bone_name);
}

LrgRootMotionMode
lrg_anim_graph_get_root_motion_mode (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), LRG_ROOT_MOTION_NONE);
	return self->rm_mode;
}

void
lrg_anim_graph_set_root_motion_mode (LrgAnimGraph      *self,
                                     LrgRootMotionMode  mode)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	self->rm_mode = mode;
}

gboolean
lrg_anim_graph_get_root_motion_rotation (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	return self->rm_rotation;
}

void
lrg_anim_graph_set_root_motion_rotation (LrgAnimGraph *self,
                                         gboolean      rotation)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	self->rm_rotation = !!rotation;
}

gboolean
lrg_anim_graph_get_apply_root_motion (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	return self->rm_apply;
}

void
lrg_anim_graph_set_apply_root_motion (LrgAnimGraph *self,
                                      gboolean      apply)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	self->rm_apply = !!apply;
}

/* --- IK ------------------------------------------------------------------ */

void
lrg_anim_graph_add_ik_goal (LrgAnimGraph *self,
                            LrgIKGoal    *goal)
{
	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	g_return_if_fail (LRG_IS_IK_GOAL (goal));
	g_ptr_array_add (self->goals, g_object_ref (goal));
}

gboolean
lrg_anim_graph_remove_ik_goal (LrgAnimGraph *self,
                               const gchar  *name)
{
	LrgIKGoal *g;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	g = lrg_anim_graph_get_ik_goal_by_name (self, name);
	return g != NULL && g_ptr_array_remove (self->goals, g);
}

guint
lrg_anim_graph_get_ik_goal_count (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), 0);
	return self->goals->len;
}

LrgIKGoal *
lrg_anim_graph_get_ik_goal (LrgAnimGraph *self,
                            guint         index)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	return index < self->goals->len ? g_ptr_array_index (self->goals, index) : NULL;
}

LrgIKGoal *
lrg_anim_graph_get_ik_goal_by_name (LrgAnimGraph *self,
                                    const gchar  *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), NULL);
	for (i = 0; name != NULL && i < self->goals->len; i++)
	{
		LrgIKGoal *g = g_ptr_array_index (self->goals, i);

		if (g_strcmp0 (lrg_ik_goal_get_name (g), name) == 0)
			return g;
	}
	return NULL;
}

/* ==========================================================================
 * Validation
 * ========================================================================== */

gboolean
lrg_anim_graph_validate (LrgAnimGraph  *self,
                         GError       **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(LrgAnimGraph) tmp = NULL;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);

	/* the loader is the validator: write and re-read */
	text = lrg_anim_graph_to_data (self, error);
	if (text == NULL)
		return FALSE;
	tmp = lrg_anim_graph_new ();
	{
		g_autoptr(YamlParser) parser = yaml_parser_new ();
		Ctx                   ctx = { tmp, error };

		if (!yaml_parser_load_from_data (parser, text, -1, error))
			return FALSE;
		return parse_graph (&ctx, yaml_parser_get_root (parser));
	}
}

static gboolean
check_bone (LrgSkeleton  *skeleton,
            const gchar  *bone,
            const gchar  *where,
            GError      **error)
{
	if (bone == NULL || lrg_skeleton_get_bone_by_name (skeleton, bone) != NULL)
		return TRUE;
	g_set_error (error, LRG_ANIM_GRAPH_ERROR, LRG_ANIM_GRAPH_ERROR_REFERENCE,
	             "%s: the skeleton has no bone '%s'", where, bone);
	return FALSE;
}

gboolean
lrg_anim_graph_validate_skeleton (LrgAnimGraph  *self,
                                  LrgSkeleton   *skeleton,
                                  GError       **error)
{
	guint i, k;

	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	g_return_val_if_fail (LRG_IS_SKELETON (skeleton), FALSE);

	if (!check_bone (skeleton, self->rm_bone, "root_motion.bone", error))
		return FALSE;
	for (i = 0; i < self->layers->len; i++)
	{
		g_auto(GStrv)     mask = lrg_animation_layer_get_mask (g_ptr_array_index (self->layers, i));
		g_autofree gchar *where = g_strdup_printf ("layers[%u].mask", i);

		for (k = 0; mask != NULL && mask[k] != NULL; k++)
			if (!check_bone (skeleton, mask[k], where, error))
				return FALSE;
	}
	for (i = 0; i < self->goals->len; i++)
	{
		LrgIKGoal        *g = g_ptr_array_index (self->goals, i);
		g_auto(GStrv)     bones = lrg_ik_goal_get_bones (g);
		g_autofree gchar *where = g_strdup_printf ("ik[%u]", i);

		for (k = 0; bones[k] != NULL; k++)
			if (!check_bone (skeleton, bones[k], where, error))
				return FALSE;
		if (!check_bone (skeleton, lrg_ik_goal_get_effector (g), where, error))
			return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Runtime
 * ========================================================================== */

void
lrg_anim_graph_start (LrgAnimGraph *self)
{
	guint i;

	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));

	lrg_animation_state_machine_reset_parameters (self->param_owner);
	for (i = 0; i < self->layers->len; i++)
	{
		LrgAnimationStateMachine *m = lrg_animation_layer_get_state_machine (g_ptr_array_index (self->layers, i));

		if (m == NULL)
			continue;
		if (lrg_animation_state_machine_is_running (m))
			lrg_animation_state_machine_stop (m);
		lrg_animation_state_machine_start (m);
	}
	self->started = TRUE;
}

gboolean
lrg_anim_graph_is_started (LrgAnimGraph *self)
{
	g_return_val_if_fail (LRG_IS_ANIM_GRAPH (self), FALSE);
	return self->started;
}

void
lrg_anim_graph_update (LrgAnimGraph *self,
                       gfloat        delta_time)
{
	guint i;

	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));

	if (!self->started)
		lrg_anim_graph_start (self);
	for (i = 0; i < self->layers->len; i++)
		lrg_animation_layer_update (g_ptr_array_index (self->layers, i), delta_time);
}

/* Model-space rotation of @bone's parent (identity for a root). */
static void
parent_rotation (LrgSkeleton *skeleton,
                 LrgBone     *bone,
                 gfloat      *q)
{
	gint parent = lrg_bone_get_parent_index (bone);

	q[0] = q[1] = q[2] = 0.0f;
	q[3] = 1.0f;
	if (parent >= 0)
	{
		LrgBone *pb = lrg_skeleton_get_bone (skeleton, parent);

		if (pb != NULL)
		{
			const LrgBonePose *w = lrg_bone_get_world_pose (pb);
			gfloat             len;

			q[0] = w->rotation_x;
			q[1] = w->rotation_y;
			q[2] = w->rotation_z;
			q[3] = w->rotation_w;
			len = sqrtf (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
			if (len > 1e-8f)
			{
				q[0] /= len;
				q[1] /= len;
				q[2] /= len;
				q[3] /= len;
			}
		}
	}
}

void
lrg_anim_graph_evaluate (LrgAnimGraph *self,
                         LrgSkeleton  *skeleton,
                         LrgBonePose  *poses,
                         guint         n_poses,
                         gfloat       *out_root_translation,
                         gfloat       *out_root_yaw)
{
	gfloat delta[3] = { 0.0f, 0.0f, 0.0f };
	gfloat yaw = 0.0f;
	guint  i;

	g_return_if_fail (LRG_IS_ANIM_GRAPH (self));
	g_return_if_fail (LRG_IS_SKELETON (skeleton));
	g_return_if_fail (poses != NULL || n_poses == 0);

	/* 1. layers over the bind pose */
	lrg_anim_pose_bind (skeleton, poses, n_poses);
	for (i = 0; i < self->layers->len; i++)
	{
		LrgAnimationLayer *layer = g_ptr_array_index (self->layers, i);
		g_auto(GStrv)      mask = NULL;

		if (i == 0 && lrg_animation_layer_get_enabled (layer) &&
		    lrg_animation_layer_get_blend_mode (layer) == LRG_LAYER_BLEND_OVERRIDE &&
		    lrg_animation_layer_get_weight (layer) >= 1.0f &&
		    (mask = lrg_animation_layer_get_mask (layer)) == NULL)
		{
			/* a full base layer replaces the bind pose: sample in place */
			lrg_animation_layer_sample_pose (layer, skeleton, poses, n_poses);
			continue;
		}
		lrg_animation_layer_blend_pose (layer, skeleton, poses, n_poses);
	}

	/* 2. root motion out of the root bone (base layer only) */
	if (self->rm_bone != NULL && self->layers->len > 0 &&
	    (self->rm_mode != LRG_ROOT_MOTION_NONE || self->rm_rotation))
	{
		LrgBone                  *bone = lrg_skeleton_get_bone_by_name (skeleton, self->rm_bone);
		LrgAnimationStateMachine *m = lrg_anim_graph_get_machine (self);
		gint                      idx = bone != NULL ? lrg_bone_get_index (bone) : -1;

		if (bone != NULL && m != NULL && idx >= 0 && (guint) idx < n_poses)
		{
			const LrgBonePose *bind = lrg_bone_get_bind_pose (bone);
			LrgBonePose       *p = &poses[idx];
			gfloat             pq[4], pinv[4], local[3], model[3];
			gfloat             raw[3];

			parent_rotation (skeleton, bone, pq);
			pinv[0] = -pq[0];
			pinv[1] = -pq[1];
			pinv[2] = -pq[2];
			pinv[3] = pq[3];

			/* the delta, in model space */
			lrg_animation_state_machine_get_root_motion (m, self->rm_bone, raw, &yaw);
			quat_rotate (pq, raw, delta);
			if (self->rm_mode == LRG_ROOT_MOTION_XZ)
				delta[1] = 0.0f;
			else if (self->rm_mode == LRG_ROOT_MOTION_NONE)
				delta[0] = delta[1] = delta[2] = 0.0f;
			if (!self->rm_rotation)
				yaw = 0.0f;

			/* strip the extracted axes from the pose (back to the bind pose) */
			if (self->rm_mode != LRG_ROOT_MOTION_NONE)
			{
				local[0] = p->position_x - bind->position_x;
				local[1] = p->position_y - bind->position_y;
				local[2] = p->position_z - bind->position_z;
				quat_rotate (pq, local, model);
				model[0] = 0.0f;
				model[2] = 0.0f;
				if (self->rm_mode == LRG_ROOT_MOTION_XYZ)
					model[1] = 0.0f;
				quat_rotate (pinv, model, local);
				p->position_x = bind->position_x + local[0];
				p->position_y = bind->position_y + local[1];
				p->position_z = bind->position_z + local[2];
			}
			if (self->rm_rotation)
			{
				gfloat d = wrap_angle (quat_yaw (p) - quat_yaw (bind));
				gfloat yq[4] = { 0.0f, sinf (-0.5f * d), 0.0f, cosf (-0.5f * d) };
				gfloat cur[4] = { p->rotation_x, p->rotation_y, p->rotation_z, p->rotation_w };
				gfloat r[4];

				quat_mul (yq, cur, r);
				p->rotation_x = r[0];
				p->rotation_y = r[1];
				p->rotation_z = r[2];
				p->rotation_w = r[3];
				lrg_bone_pose_normalize_rotation (p);
			}
		}
	}

	if (out_root_translation != NULL)
		memcpy (out_root_translation, delta, sizeof delta);
	if (out_root_yaw != NULL)
		*out_root_yaw = yaw;
}
