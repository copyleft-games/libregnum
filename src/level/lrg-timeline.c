/* lrg-timeline.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Keyframed property animation stored in a level. Tracks wrap an
 * #LrgKeyframeCurve (times in seconds, per-key easing) and address a node
 * channel by name; see lrg-timeline.h for the channel list.
 */

#include "lrg-timeline.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "../scene/lrg-material3d.h"
#include <math.h>
#include <string.h>

#define KEY_EPSILON (0.0005f)

/* ==========================================================================
 * Channels
 * ========================================================================== */

typedef enum
{
	CH_INVALID,
	CH_LOCATION,
	CH_ROTATION,
	CH_SCALE,
	CH_COLOR,
	CH_ROUGHNESS,
	CH_METALLIC,
	CH_PARAM
} ChannelKind;

/* Splits a channel name into its kind and component (axis index or the
 * param name). */
static ChannelKind
parse_channel (const gchar  *channel,
               gint         *component,
               const gchar **param)
{
	static const struct { const gchar *prefix; ChannelKind kind; const gchar *axes; } vec[] = {
		{ "location.", CH_LOCATION, "xyz" },
		{ "rotation.", CH_ROTATION, "xyz" },
		{ "scale.", CH_SCALE, "xyz" },
		{ "color.", CH_COLOR, "rgba" },
	};
	gsize i;

	*component = 0;
	*param = NULL;
	if (channel == NULL)
		return CH_INVALID;

	for (i = 0; i < G_N_ELEMENTS (vec); i++)
	{
		gsize       plen = strlen (vec[i].prefix);
		const gchar *axis;

		if (strncmp (channel, vec[i].prefix, plen) != 0 || channel[plen] == '\0' || channel[plen + 1] != '\0')
			continue;
		axis = strchr (vec[i].axes, channel[plen]);
		if (axis == NULL)
			return CH_INVALID;
		*component = (gint) (axis - vec[i].axes);
		return vec[i].kind;
	}
	if (strcmp (channel, "roughness") == 0)
		return CH_ROUGHNESS;
	if (strcmp (channel, "metallic") == 0)
		return CH_METALLIC;
	if (g_str_has_prefix (channel, "param.") && channel[6] != '\0')
	{
		*param = channel + 6;
		return CH_PARAM;
	}
	return CH_INVALID;
}

gboolean
lrg_timeline_channel_is_valid (const gchar *channel)
{
	gint         c;
	const gchar *p;

	return parse_channel (channel, &c, &p) != CH_INVALID;
}

static gfloat
vec_get (GrlVector3 *v,
         gint        axis)
{
	return axis == 0 ? v->x : (axis == 1 ? v->y : v->z);
}

gboolean
lrg_timeline_channel_read (LrgNode     *node,
                           const gchar *channel,
                           gfloat      *out_value)
{
	gint           comp;
	const gchar   *param;
	ChannelKind    kind;
	LrgNodeVisual *visual;
	LrgMaterial3D *mat;
	gfloat         rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (out_value != NULL, FALSE);

	kind = parse_channel (channel, &comp, &param);
	switch (kind)
	{
	case CH_LOCATION:
		*out_value = vec_get (lrg_node_get_location (node), comp);
		return TRUE;
	case CH_ROTATION:
		*out_value = vec_get (lrg_node_get_rotation (node), comp);
		return TRUE;
	case CH_SCALE:
		*out_value = vec_get (lrg_node_get_scale (node), comp);
		return TRUE;
	default:
		break;
	}

	if (kind == CH_INVALID || (visual = lrg_node_get_visual (node)) == NULL)
		return FALSE;

	mat = lrg_node_visual_get_material (visual);
	switch (kind)
	{
	case CH_COLOR:
		if (mat != NULL)
			lrg_material3d_get_color (mat, &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
		*out_value = rgba[comp];
		return TRUE;
	case CH_ROUGHNESS:
		*out_value = (mat != NULL) ? lrg_material3d_get_roughness (mat) : 0.5f;
		return TRUE;
	case CH_METALLIC:
		*out_value = (mat != NULL) ? lrg_material3d_get_metallic (mat) : 0.0f;
		return TRUE;
	case CH_PARAM:
		*out_value = (gfloat) lrg_node_visual_get_param_double (visual, param, 0.0);
		return TRUE;
	default:
		return FALSE;
	}
}

/* Material of @visual, created on first write. */
static LrgMaterial3D *
ensure_material (LrgNodeVisual *visual)
{
	LrgMaterial3D *mat = lrg_node_visual_get_material (visual);

	if (mat == NULL)
	{
		g_autoptr(LrgMaterial3D) fresh = lrg_material3d_new ();

		lrg_node_visual_set_material (visual, fresh);
		mat = lrg_node_visual_get_material (visual);
	}
	return mat;
}

gboolean
lrg_timeline_channel_write (LrgNode     *node,
                            const gchar *channel,
                            gfloat       value)
{
	gint           comp;
	const gchar   *param;
	ChannelKind    kind;
	LrgNodeVisual *visual;
	GrlVector3    *v;
	gfloat         xyz[3];
	gfloat         rgba[4];

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);

	kind = parse_channel (channel, &comp, &param);
	if (kind == CH_LOCATION || kind == CH_ROTATION || kind == CH_SCALE)
	{
		v = (kind == CH_LOCATION) ? lrg_node_get_location (node)
		    : (kind == CH_ROTATION) ? lrg_node_get_rotation (node) : lrg_node_get_scale (node);
		xyz[0] = v->x;
		xyz[1] = v->y;
		xyz[2] = v->z;
		xyz[comp] = value;
		if (kind == CH_LOCATION)
			lrg_node_set_location_xyz (node, xyz[0], xyz[1], xyz[2]);
		else if (kind == CH_ROTATION)
			lrg_node_set_rotation_xyz (node, xyz[0], xyz[1], xyz[2]);
		else
			lrg_node_set_scale_xyz (node, xyz[0], xyz[1], xyz[2]);
		return TRUE;
	}

	if (kind == CH_INVALID || (visual = lrg_node_get_visual (node)) == NULL)
		return FALSE;

	switch (kind)
	{
	case CH_COLOR:
		lrg_material3d_get_color (ensure_material (visual), &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
		rgba[comp] = value;
		lrg_material3d_set_color (ensure_material (visual), rgba[0], rgba[1], rgba[2], rgba[3]);
		return TRUE;
	case CH_ROUGHNESS:
		lrg_material3d_set_roughness (ensure_material (visual), value);
		return TRUE;
	case CH_METALLIC:
		lrg_material3d_set_metallic (ensure_material (visual), value);
		return TRUE;
	case CH_PARAM:
		lrg_node_visual_set_param_double (visual, param, value);
		return TRUE;
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * LrgTimelineTrack
 * ========================================================================== */

/**
 * LrgTimelineTrack:
 *
 * One animated channel of one node.
 */
struct _LrgTimelineTrack
{
	GObject parent_instance;

	gchar            *node_guid;
	gchar            *channel;
	LrgKeyframeCurve *curve;
};

G_DEFINE_FINAL_TYPE (LrgTimelineTrack, lrg_timeline_track, G_TYPE_OBJECT)

static void
lrg_timeline_track_finalize (GObject *object)
{
	LrgTimelineTrack *self = LRG_TIMELINE_TRACK (object);

	g_clear_pointer (&self->node_guid, g_free);
	g_clear_pointer (&self->channel, g_free);
	g_clear_object (&self->curve);

	G_OBJECT_CLASS (lrg_timeline_track_parent_class)->finalize (object);
}

static void
lrg_timeline_track_class_init (LrgTimelineTrackClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_timeline_track_finalize;
}

static void
lrg_timeline_track_init (LrgTimelineTrack *self)
{
	self->curve = lrg_keyframe_curve_new ();
}

LrgTimelineTrack *
lrg_timeline_track_new (const gchar *node_guid,
                        const gchar *channel)
{
	LrgTimelineTrack *self;

	g_return_val_if_fail (node_guid != NULL, NULL);
	g_return_val_if_fail (channel != NULL, NULL);

	self = g_object_new (LRG_TYPE_TIMELINE_TRACK, NULL);
	self->node_guid = g_strdup (node_guid);
	self->channel = g_strdup (channel);
	return self;
}

const gchar *
lrg_timeline_track_get_node_guid (LrgTimelineTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (self), NULL);
	return self->node_guid;
}

const gchar *
lrg_timeline_track_get_channel (LrgTimelineTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (self), NULL);
	return self->channel;
}

LrgKeyframeCurve *
lrg_timeline_track_get_curve (LrgTimelineTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (self), NULL);
	return self->curve;
}

guint
lrg_timeline_track_get_n_keys (LrgTimelineTrack *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (self), 0);
	return lrg_keyframe_curve_get_key_count (self->curve);
}

gboolean
lrg_timeline_track_get_key (LrgTimelineTrack *self,
                            guint             index,
                            gfloat           *time,
                            gfloat           *value,
                            LrgEasingType    *easing)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (self), FALSE);
	return lrg_keyframe_curve_get_key (self->curve, index, time, value, easing);
}

gint
lrg_timeline_track_find_key (LrgTimelineTrack *self,
                             gfloat            time,
                             gfloat            tolerance)
{
	guint  i, n;
	gint   best = -1;
	gfloat best_d = G_MAXFLOAT;

	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (self), -1);

	n = lrg_keyframe_curve_get_key_count (self->curve);
	for (i = 0; i < n; i++)
	{
		gfloat t = 0.0f, d;

		lrg_keyframe_curve_get_key (self->curve, i, &t, NULL, NULL);
		d = fabsf (t - time);
		if (d <= tolerance && d < best_d)
		{
			best = (gint) i;
			best_d = d;
		}
	}
	return best;
}

gboolean
lrg_timeline_track_remove_key (LrgTimelineTrack *self,
                               guint             index)
{
	gfloat t = 0.0f;

	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (self), FALSE);

	if (!lrg_keyframe_curve_get_key (self->curve, index, &t, NULL, NULL))
		return FALSE;
	return lrg_keyframe_curve_remove_key (self->curve, t);
}

void
lrg_timeline_track_set_key (LrgTimelineTrack *self,
                            gfloat            time,
                            gfloat            value,
                            LrgEasingType     easing)
{
	gint existing;

	g_return_if_fail (LRG_IS_TIMELINE_TRACK (self));
	g_return_if_fail (isfinite (time) && isfinite (value));

	/* Snap onto a key that is practically at the same time, so keying a
	 * frame twice from float UI times never makes near-duplicates. */
	time = MAX (time, 0.0f);
	existing = lrg_timeline_track_find_key (self, time, KEY_EPSILON);
	if (existing >= 0)
		lrg_timeline_track_remove_key (self, (guint) existing);
	lrg_keyframe_curve_add_key (self->curve, time, value, easing);
}

gboolean
lrg_timeline_track_sample (LrgTimelineTrack *self,
                           gfloat            time,
                           gfloat           *out_value)
{
	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (self), FALSE);
	g_return_val_if_fail (out_value != NULL, FALSE);

	if (lrg_keyframe_curve_get_key_count (self->curve) == 0 || !isfinite (time))
		return FALSE;
	*out_value = lrg_keyframe_curve_sample (self->curve, time);
	return TRUE;
}

LrgTimelineTrack *
lrg_timeline_track_copy (LrgTimelineTrack *self)
{
	LrgTimelineTrack *copy;

	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (self), NULL);

	copy = lrg_timeline_track_new (self->node_guid, self->channel);
	g_object_unref (copy->curve);
	copy->curve = lrg_keyframe_curve_copy (self->curve);
	return copy;
}

gboolean
lrg_timeline_track_equal (LrgTimelineTrack *a,
                          LrgTimelineTrack *b)
{
	guint i, n;

	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (a), FALSE);
	g_return_val_if_fail (LRG_IS_TIMELINE_TRACK (b), FALSE);

	n = lrg_keyframe_curve_get_key_count (a->curve);
	if (g_strcmp0 (a->node_guid, b->node_guid) != 0 || g_strcmp0 (a->channel, b->channel) != 0 ||
	    n != lrg_keyframe_curve_get_key_count (b->curve))
		return FALSE;

	for (i = 0; i < n; i++)
	{
		gfloat        ta, va, tb, vb;
		LrgEasingType ea, eb;

		lrg_keyframe_curve_get_key (a->curve, i, &ta, &va, &ea);
		lrg_keyframe_curve_get_key (b->curve, i, &tb, &vb, &eb);
		if (ta != tb || va != vb || ea != eb)
			return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * LrgTimeline
 * ========================================================================== */

/**
 * LrgTimeline:
 *
 * A named, keyframed animation clip stored in a level.
 */
struct _LrgTimeline
{
	GObject parent_instance;

	gchar     *name;
	gfloat     length;
	gboolean   loop;
	gboolean   autoplay;
	GPtrArray *tracks;   /* LrgTimelineTrack* (owned) */
};

G_DEFINE_FINAL_TYPE (LrgTimeline, lrg_timeline, G_TYPE_OBJECT)

static void
lrg_timeline_finalize (GObject *object)
{
	LrgTimeline *self = LRG_TIMELINE (object);

	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->tracks, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_timeline_parent_class)->finalize (object);
}

static void
lrg_timeline_class_init (LrgTimelineClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_timeline_finalize;
}

static void
lrg_timeline_init (LrgTimeline *self)
{
	self->tracks = g_ptr_array_new_with_free_func (g_object_unref);
	self->length = 2.0f;
}

LrgTimeline *
lrg_timeline_new (const gchar *name)
{
	LrgTimeline *self;

	g_return_val_if_fail (name != NULL, NULL);

	self = g_object_new (LRG_TYPE_TIMELINE, NULL);
	self->name = g_strdup (name);
	return self;
}

const gchar *
lrg_timeline_get_name (LrgTimeline *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE (self), NULL);
	return self->name;
}

void
lrg_timeline_set_name (LrgTimeline *self,
                       const gchar *name)
{
	g_return_if_fail (LRG_IS_TIMELINE (self));
	g_return_if_fail (name != NULL);

	g_free (self->name);
	self->name = g_strdup (name);
}

gfloat
lrg_timeline_get_length (LrgTimeline *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE (self), 0.0f);
	return self->length;
}

void
lrg_timeline_set_length (LrgTimeline *self,
                         gfloat       length)
{
	g_return_if_fail (LRG_IS_TIMELINE (self));
	g_return_if_fail (isfinite (length) && length > 0.0f);

	self->length = length;
}

gboolean
lrg_timeline_get_loop (LrgTimeline *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE (self), FALSE);
	return self->loop;
}

void
lrg_timeline_set_loop (LrgTimeline *self,
                       gboolean     loop)
{
	g_return_if_fail (LRG_IS_TIMELINE (self));
	self->loop = loop != FALSE;
}

gboolean
lrg_timeline_get_autoplay (LrgTimeline *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE (self), FALSE);
	return self->autoplay;
}

void
lrg_timeline_set_autoplay (LrgTimeline *self,
                           gboolean     autoplay)
{
	g_return_if_fail (LRG_IS_TIMELINE (self));
	self->autoplay = autoplay != FALSE;
}

guint
lrg_timeline_get_n_tracks (LrgTimeline *self)
{
	g_return_val_if_fail (LRG_IS_TIMELINE (self), 0);
	return self->tracks->len;
}

LrgTimelineTrack *
lrg_timeline_get_track (LrgTimeline *self,
                        guint        index)
{
	g_return_val_if_fail (LRG_IS_TIMELINE (self), NULL);
	return (index < self->tracks->len) ? g_ptr_array_index (self->tracks, index) : NULL;
}

gint
lrg_timeline_find_track (LrgTimeline *self,
                         const gchar *node_guid,
                         const gchar *channel)
{
	guint i;

	g_return_val_if_fail (LRG_IS_TIMELINE (self), -1);

	for (i = 0; i < self->tracks->len; i++)
	{
		LrgTimelineTrack *t = g_ptr_array_index (self->tracks, i);

		if (g_strcmp0 (t->node_guid, node_guid) == 0 && g_strcmp0 (t->channel, channel) == 0)
			return (gint) i;
	}
	return -1;
}

LrgTimelineTrack *
lrg_timeline_ensure_track (LrgTimeline *self,
                           const gchar *node_guid,
                           const gchar *channel)
{
	gint idx;

	g_return_val_if_fail (LRG_IS_TIMELINE (self), NULL);
	g_return_val_if_fail (node_guid != NULL, NULL);
	g_return_val_if_fail (lrg_timeline_channel_is_valid (channel), NULL);

	idx = lrg_timeline_find_track (self, node_guid, channel);
	if (idx >= 0)
		return g_ptr_array_index (self->tracks, idx);
	g_ptr_array_add (self->tracks, lrg_timeline_track_new (node_guid, channel));
	return g_ptr_array_index (self->tracks, self->tracks->len - 1);
}

gboolean
lrg_timeline_remove_track (LrgTimeline *self,
                           guint        index)
{
	g_return_val_if_fail (LRG_IS_TIMELINE (self), FALSE);

	if (index >= self->tracks->len)
		return FALSE;
	g_ptr_array_remove_index (self->tracks, index);
	return TRUE;
}

void
lrg_timeline_add_track (LrgTimeline      *self,
                        LrgTimelineTrack *track)
{
	g_return_if_fail (LRG_IS_TIMELINE (self));
	g_return_if_fail (LRG_IS_TIMELINE_TRACK (track));

	g_ptr_array_add (self->tracks, g_object_ref (track));
}

gfloat
lrg_timeline_wrap_time (LrgTimeline *self,
                        gfloat       time)
{
	g_return_val_if_fail (LRG_IS_TIMELINE (self), 0.0f);

	if (!isfinite (time))
		return 0.0f;
	if (self->loop && self->length > 0.0f)
	{
		gfloat t = fmodf (time, self->length);

		return (t < 0.0f) ? t + self->length : t;
	}
	return CLAMP (time, 0.0f, self->length);
}

guint
lrg_timeline_apply (LrgTimeline *self,
                    LrgNode     *root,
                    gfloat       time)
{
	guint i, written = 0;

	g_return_val_if_fail (LRG_IS_TIMELINE (self), 0);
	g_return_val_if_fail (LRG_IS_NODE (root), 0);

	for (i = 0; i < self->tracks->len; i++)
	{
		LrgTimelineTrack *t = g_ptr_array_index (self->tracks, i);
		LrgNode          *node = lrg_node_find_by_guid (root, t->node_guid);
		gfloat            value;

		if (node == NULL || !lrg_timeline_track_sample (t, time, &value))
			continue;
		if (lrg_timeline_channel_write (node, t->channel, value))
			written++;
	}
	return written;
}

LrgTimeline *
lrg_timeline_copy (LrgTimeline *self)
{
	LrgTimeline *copy;
	guint        i;

	g_return_val_if_fail (LRG_IS_TIMELINE (self), NULL);

	copy = lrg_timeline_new (self->name);
	copy->length = self->length;
	copy->loop = self->loop;
	copy->autoplay = self->autoplay;
	for (i = 0; i < self->tracks->len; i++)
		g_ptr_array_add (copy->tracks, lrg_timeline_track_copy (g_ptr_array_index (self->tracks, i)));
	return copy;
}

gboolean
lrg_timeline_equal (LrgTimeline *a,
                    LrgTimeline *b)
{
	guint i;

	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL)
		return FALSE;
	if (g_strcmp0 (a->name, b->name) != 0 || a->length != b->length || a->loop != b->loop ||
	    a->autoplay != b->autoplay || a->tracks->len != b->tracks->len)
		return FALSE;
	for (i = 0; i < a->tracks->len; i++)
		if (!lrg_timeline_track_equal (g_ptr_array_index (a->tracks, i), g_ptr_array_index (b->tracks, i)))
			return FALSE;
	return TRUE;
}
