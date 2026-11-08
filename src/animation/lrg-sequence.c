/* lrg-sequence.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSequence: the `.rseq` cinematic sequence asset, its tracks, Bézier
 * curves and sections, the YAML reader/writer and the level timeline
 * conversion. Playback lives in lrg-sequence-player.c.
 */

#include "config.h"

#include "lrg-sequence.h"
#include "../core/lrg-math3d.h"
#include "../level/lrg-timeline.h"
#include <yaml-glib.h>
#include <math.h>
#include <string.h>

/* Two keys closer than this are the same key (half a millisecond, the
 * same snapping the level timeline uses). */
#define KEY_EPSILON (0.0005f)

G_DEFINE_QUARK (lrg-sequence-error-quark, lrg_sequence_error)

/* ==========================================================================
 * Enumerations (hand-written GTypes, nicks are the YAML names)
 * ========================================================================== */

GType
lrg_sequence_track_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SEQUENCE_TRACK_TRANSFORM, "LRG_SEQUENCE_TRACK_TRANSFORM", "transform" },
			{ LRG_SEQUENCE_TRACK_PROPERTY, "LRG_SEQUENCE_TRACK_PROPERTY", "property" },
			{ LRG_SEQUENCE_TRACK_CAMERA_CUT, "LRG_SEQUENCE_TRACK_CAMERA_CUT", "camera-cut" },
			{ LRG_SEQUENCE_TRACK_AUDIO, "LRG_SEQUENCE_TRACK_AUDIO", "audio" },
			{ LRG_SEQUENCE_TRACK_EVENT, "LRG_SEQUENCE_TRACK_EVENT", "event" },
			{ LRG_SEQUENCE_TRACK_ANIMATION, "LRG_SEQUENCE_TRACK_ANIMATION", "animation" },
			{ LRG_SEQUENCE_TRACK_SUBSEQUENCE, "LRG_SEQUENCE_TRACK_SUBSEQUENCE", "subsequence" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static ("LrgSequenceTrackKind", values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_sequence_tangent_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SEQUENCE_TANGENT_AUTO, "LRG_SEQUENCE_TANGENT_AUTO", "auto" },
			{ LRG_SEQUENCE_TANGENT_USER, "LRG_SEQUENCE_TANGENT_USER", "user" },
			{ LRG_SEQUENCE_TANGENT_BROKEN, "LRG_SEQUENCE_TANGENT_BROKEN", "broken" },
			{ LRG_SEQUENCE_TANGENT_LINEAR, "LRG_SEQUENCE_TANGENT_LINEAR", "linear" },
			{ LRG_SEQUENCE_TANGENT_CONSTANT, "LRG_SEQUENCE_TANGENT_CONSTANT", "constant" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static ("LrgSequenceTangentMode", values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_sequence_play_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SEQUENCE_PLAY_ONCE, "LRG_SEQUENCE_PLAY_ONCE", "once" },
			{ LRG_SEQUENCE_PLAY_LOOP, "LRG_SEQUENCE_PLAY_LOOP", "loop" },
			{ LRG_SEQUENCE_PLAY_PING_PONG, "LRG_SEQUENCE_PLAY_PING_PONG", "ping-pong" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static ("LrgSequencePlayMode", values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

/* Channel names in enum order; also the YAML keys of a transform track. */
static const gchar * const channel_names[LRG_SEQUENCE_N_CHANNELS] = {
	"location.x", "location.y", "location.z",
	"rotation.x", "rotation.y", "rotation.z",
	"scale.x", "scale.y", "scale.z"
};

GType
lrg_sequence_transform_channel_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SEQUENCE_CHANNEL_LOCATION_X, "LRG_SEQUENCE_CHANNEL_LOCATION_X", "location-x" },
			{ LRG_SEQUENCE_CHANNEL_LOCATION_Y, "LRG_SEQUENCE_CHANNEL_LOCATION_Y", "location-y" },
			{ LRG_SEQUENCE_CHANNEL_LOCATION_Z, "LRG_SEQUENCE_CHANNEL_LOCATION_Z", "location-z" },
			{ LRG_SEQUENCE_CHANNEL_ROTATION_X, "LRG_SEQUENCE_CHANNEL_ROTATION_X", "rotation-x" },
			{ LRG_SEQUENCE_CHANNEL_ROTATION_Y, "LRG_SEQUENCE_CHANNEL_ROTATION_Y", "rotation-y" },
			{ LRG_SEQUENCE_CHANNEL_ROTATION_Z, "LRG_SEQUENCE_CHANNEL_ROTATION_Z", "rotation-z" },
			{ LRG_SEQUENCE_CHANNEL_SCALE_X, "LRG_SEQUENCE_CHANNEL_SCALE_X", "scale-x" },
			{ LRG_SEQUENCE_CHANNEL_SCALE_Y, "LRG_SEQUENCE_CHANNEL_SCALE_Y", "scale-y" },
			{ LRG_SEQUENCE_CHANNEL_SCALE_Z, "LRG_SEQUENCE_CHANNEL_SCALE_Z", "scale-z" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static ("LrgSequenceTransformChannel", values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

const gchar *
lrg_sequence_transform_channel_to_string (LrgSequenceTransformChannel channel)
{
	if ((guint) channel >= LRG_SEQUENCE_N_CHANNELS)
		return NULL;
	return channel_names[channel];
}

gboolean
lrg_sequence_transform_channel_from_string (const gchar                 *name,
                                            LrgSequenceTransformChannel *out_channel)
{
	guint i;

	g_return_val_if_fail (out_channel != NULL, FALSE);

	if (name == NULL)
		return FALSE;
	for (i = 0; i < LRG_SEQUENCE_N_CHANNELS; i++)
	{
		if (strcmp (name, channel_names[i]) == 0)
		{
			*out_channel = (LrgSequenceTransformChannel) i;
			return TRUE;
		}
	}
	return FALSE;
}

/* Nick <-> value helpers over the enum GTypes above. */
static const gchar *
enum_nick (GType type,
           gint  value)
{
	GEnumClass *klass = g_type_class_ref (type);
	GEnumValue *ev = g_enum_get_value (klass, value);
	const gchar *nick = (ev != NULL) ? ev->value_nick : NULL;

	g_type_class_unref (klass);
	return nick;
}

static gboolean
enum_from_nick (GType        type,
                const gchar *nick,
                gint        *out_value)
{
	GEnumClass *klass;
	GEnumValue *ev;

	if (nick == NULL)
		return FALSE;
	klass = g_type_class_ref (type);
	ev = g_enum_get_value_by_nick (klass, nick);
	if (ev != NULL)
		*out_value = ev->value;
	g_type_class_unref (klass);
	return ev != NULL;
}

/* ==========================================================================
 * LrgSequenceKey / LrgSequenceSection (boxed)
 * ========================================================================== */

LrgSequenceKey *
lrg_sequence_key_copy (const LrgSequenceKey *key)
{
	g_return_val_if_fail (key != NULL, NULL);
	return g_memdup2 (key, sizeof (LrgSequenceKey));
}

void
lrg_sequence_key_free (LrgSequenceKey *key)
{
	g_free (key);
}

G_DEFINE_BOXED_TYPE (LrgSequenceKey, lrg_sequence_key, lrg_sequence_key_copy, lrg_sequence_key_free)

/* Fills @s with the documented defaults (no strings). */
static void
section_init (LrgSequenceSection *s,
              gfloat              start)
{
	memset (s, 0, sizeof (LrgSequenceSection));
	s->start = start;
	s->rate = 1.0f;
	s->weight = 1.0f;
}

static void
section_clear (LrgSequenceSection *s)
{
	g_clear_pointer (&s->name, g_free);
	g_clear_pointer (&s->payload, g_free);
}

/* Deep copy into caller storage. */
static void
section_copy_into (LrgSequenceSection       *dst,
                   const LrgSequenceSection *src)
{
	*dst = *src;
	dst->name = g_strdup (src->name);
	dst->payload = g_strdup (src->payload);
}

static gboolean
section_equal (const LrgSequenceSection *a,
               const LrgSequenceSection *b)
{
	return a->start == b->start && a->duration == b->duration && a->offset == b->offset &&
	       a->rate == b->rate && a->weight == b->weight && (a->loop != FALSE) == (b->loop != FALSE) &&
	       g_strcmp0 (a->name, b->name) == 0 && g_strcmp0 (a->payload, b->payload) == 0;
}

LrgSequenceSection *
lrg_sequence_section_new (gfloat       start,
                          const gchar *name)
{
	LrgSequenceSection *s = g_new (LrgSequenceSection, 1);

	section_init (s, start);
	s->name = g_strdup (name);
	return s;
}

LrgSequenceSection *
lrg_sequence_section_copy (const LrgSequenceSection *section)
{
	LrgSequenceSection *s;

	g_return_val_if_fail (section != NULL, NULL);

	s = g_new (LrgSequenceSection, 1);
	section_copy_into (s, section);
	return s;
}

void
lrg_sequence_section_free (LrgSequenceSection *section)
{
	if (section == NULL)
		return;
	section_clear (section);
	g_free (section);
}

G_DEFINE_BOXED_TYPE (LrgSequenceSection, lrg_sequence_section, lrg_sequence_section_copy,
                     lrg_sequence_section_free)

/* ==========================================================================
 * Bézier math (shared with the level timeline)
 * ========================================================================== */

gfloat
lrg_sequence_curve_hermite (gfloat t0,
                            gfloat v0,
                            gfloat out_slope,
                            gfloat t1,
                            gfloat v1,
                            gfloat in_slope,
                            gfloat time)
{
	gfloat dt = t1 - t0;
	gfloat s, s2, s3;

	/* A degenerate segment has no inside: return its end. */
	if (!(dt > 0.0f))
		return v1;

	/*
	 * Cubic Hermite in normalised time s = (time - t0) / dt. The handles of
	 * the equivalent Bézier sit at dt/3 along each slope, so the tangents
	 * in s-space are slope * dt.
	 */
	s = CLAMP ((time - t0) / dt, 0.0f, 1.0f);
	s2 = s * s;
	s3 = s2 * s;
	return (2.0f * s3 - 3.0f * s2 + 1.0f) * v0 +
	       (s3 - 2.0f * s2 + s) * dt * out_slope +
	       (-2.0f * s3 + 3.0f * s2) * v1 +
	       (s3 - s2) * dt * in_slope;
}

gfloat
lrg_sequence_curve_auto_slope (gboolean has_prev,
                               gfloat   t_prev,
                               gfloat   v_prev,
                               gfloat   t,
                               gfloat   v,
                               gboolean has_next,
                               gfloat   t_next,
                               gfloat   v_next)
{
	gfloat left, right, slope, limit;

	/* Ends are flat (ease in / ease out), as in Unity and Unreal. */
	if (!has_prev || !has_next || !(t - t_prev > 0.0f) || !(t_next - t > 0.0f))
		return 0.0f;

	left = (v - v_prev) / (t - t_prev);
	right = (v_next - v) / (t_next - t);

	/* Local extremum or plateau: flat, so the curve cannot overshoot. */
	if (left * right <= 0.0f)
		return 0.0f;

	/* Non-uniform Catmull-Rom, limited (Fritsch-Carlson) to keep a
	 * monotone run monotone. */
	slope = (v_next - v_prev) / (t_next - t_prev);
	limit = 3.0f * MIN (fabsf (left), fabsf (right));
	if (fabsf (slope) > limit)
		slope = (slope > 0.0f) ? limit : -limit;
	return slope;
}

/* ==========================================================================
 * LrgSequenceCurve
 * ========================================================================== */

/**
 * LrgSequenceCurve:
 *
 * A scalar curve of #LrgSequenceKey s with Bézier tangents.
 */
struct _LrgSequenceCurve
{
	GObject parent_instance;

	GArray *keys;   /* LrgSequenceKey, sorted by time */
};

G_DEFINE_FINAL_TYPE (LrgSequenceCurve, lrg_sequence_curve, G_TYPE_OBJECT)

static void
lrg_sequence_curve_finalize (GObject *object)
{
	LrgSequenceCurve *self = LRG_SEQUENCE_CURVE (object);

	g_clear_pointer (&self->keys, g_array_unref);
	G_OBJECT_CLASS (lrg_sequence_curve_parent_class)->finalize (object);
}

static void
lrg_sequence_curve_class_init (LrgSequenceCurveClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_sequence_curve_finalize;
}

static void
lrg_sequence_curve_init (LrgSequenceCurve *self)
{
	self->keys = g_array_new (FALSE, TRUE, sizeof (LrgSequenceKey));
}

LrgSequenceCurve *
lrg_sequence_curve_new (void)
{
	return g_object_new (LRG_TYPE_SEQUENCE_CURVE, NULL);
}

#define KEY_AT(self, i) (&g_array_index ((self)->keys, LrgSequenceKey, (i)))

guint
lrg_sequence_curve_get_n_keys (LrgSequenceCurve *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), 0);
	return self->keys->len;
}

gboolean
lrg_sequence_curve_get_key (LrgSequenceCurve *self,
                            guint             index,
                            LrgSequenceKey   *out_key)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), FALSE);
	g_return_val_if_fail (out_key != NULL, FALSE);

	if (index >= self->keys->len)
		return FALSE;
	*out_key = *KEY_AT (self, index);
	return TRUE;
}

gint
lrg_sequence_curve_find_key (LrgSequenceCurve *self,
                             gfloat            time,
                             gfloat            tolerance)
{
	guint  i;
	gint   best = -1;
	gfloat best_d = G_MAXFLOAT;

	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), -1);

	for (i = 0; i < self->keys->len; i++)
	{
		gfloat d = fabsf (KEY_AT (self, i)->time - time);

		if (d <= tolerance && d < best_d)
		{
			best = (gint) i;
			best_d = d;
		}
	}
	return best;
}

/* Index of the first key whose time is greater than @time (upper bound). */
static guint
upper_bound (LrgSequenceCurve *self,
             gfloat            time)
{
	guint lo = 0, hi = self->keys->len;

	while (lo < hi)
	{
		guint mid = (lo + hi) / 2;

		if (KEY_AT (self, mid)->time <= time)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

guint
lrg_sequence_curve_insert_key (LrgSequenceCurve     *self,
                               const LrgSequenceKey *key)
{
	LrgSequenceKey k;
	gint           existing;
	guint          at;

	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), 0);
	g_return_val_if_fail (key != NULL, 0);

	k = *key;
	if (!isfinite (k.time))
		k.time = 0.0f;
	if (!isfinite (k.value))
		k.value = 0.0f;
	if (k.tangent_mode != LRG_SEQUENCE_TANGENT_BROKEN)
		k.out_tangent = k.in_tangent;

	/* Replace a key that is practically at the same time. */
	existing = lrg_sequence_curve_find_key (self, k.time, KEY_EPSILON);
	if (existing >= 0)
		g_array_remove_index (self->keys, (guint) existing);

	at = upper_bound (self, k.time);
	g_array_insert_val (self->keys, at, k);
	return at;
}

guint
lrg_sequence_curve_set_key (LrgSequenceCurve       *self,
                            gfloat                  time,
                            gfloat                  value,
                            LrgSequenceTangentMode  mode)
{
	LrgSequenceKey k = { 0 };
	gint           existing;

	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), 0);

	/* Keep the user slopes of a key being re-set (re-keying a frame must
	 * not flatten its handles). */
	existing = lrg_sequence_curve_find_key (self, time, KEY_EPSILON);
	if (existing >= 0)
		k = *KEY_AT (self, existing);
	k.time = time;
	k.value = value;
	k.tangent_mode = mode;
	return lrg_sequence_curve_insert_key (self, &k);
}

gboolean
lrg_sequence_curve_set_key_tangents (LrgSequenceCurve       *self,
                                     guint                   index,
                                     LrgSequenceTangentMode  mode,
                                     gfloat                  in_tangent,
                                     gfloat                  out_tangent)
{
	LrgSequenceKey *k;

	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), FALSE);

	if (index >= self->keys->len)
		return FALSE;
	k = KEY_AT (self, index);
	k->tangent_mode = mode;
	k->in_tangent = isfinite (in_tangent) ? in_tangent : 0.0f;
	k->out_tangent = (mode == LRG_SEQUENCE_TANGENT_BROKEN && isfinite (out_tangent)) ? out_tangent
	                                                                                 : k->in_tangent;
	return TRUE;
}

gint
lrg_sequence_curve_move_key (LrgSequenceCurve *self,
                             guint             index,
                             gfloat            time)
{
	LrgSequenceKey k;

	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), -1);

	if (index >= self->keys->len || !isfinite (time))
		return -1;
	k = *KEY_AT (self, index);
	g_array_remove_index (self->keys, index);
	k.time = time;
	return (gint) lrg_sequence_curve_insert_key (self, &k);
}

gboolean
lrg_sequence_curve_remove_key (LrgSequenceCurve *self,
                               guint             index)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), FALSE);

	if (index >= self->keys->len)
		return FALSE;
	g_array_remove_index (self->keys, index);
	return TRUE;
}

void
lrg_sequence_curve_clear (LrgSequenceCurve *self)
{
	g_return_if_fail (LRG_IS_SEQUENCE_CURVE (self));
	g_array_set_size (self->keys, 0);
}

/* Resolves the slope of key @i on one side (@outgoing selects the side). */
static gfloat
resolve_slope (LrgSequenceCurve *self,
               guint             i,
               gboolean          outgoing)
{
	const LrgSequenceKey *k = KEY_AT (self, i);
	const LrgSequenceKey *prev = (i > 0) ? KEY_AT (self, i - 1) : NULL;
	const LrgSequenceKey *next = (i + 1 < self->keys->len) ? KEY_AT (self, i + 1) : NULL;

	switch (k->tangent_mode)
	{
	case LRG_SEQUENCE_TANGENT_USER:
		return k->in_tangent;
	case LRG_SEQUENCE_TANGENT_BROKEN:
		return outgoing ? k->out_tangent : k->in_tangent;
	case LRG_SEQUENCE_TANGENT_LINEAR:
		/* Point straight at the neighbour on that side. */
		if (outgoing && next != NULL && next->time > k->time)
			return (next->value - k->value) / (next->time - k->time);
		if (!outgoing && prev != NULL && k->time > prev->time)
			return (k->value - prev->value) / (k->time - prev->time);
		return 0.0f;
	case LRG_SEQUENCE_TANGENT_CONSTANT:
		return 0.0f;
	case LRG_SEQUENCE_TANGENT_AUTO:
	default:
		return lrg_sequence_curve_auto_slope (prev != NULL, prev ? prev->time : 0.0f, prev ? prev->value : 0.0f,
		                                      k->time, k->value,
		                                      next != NULL, next ? next->time : 0.0f, next ? next->value : 0.0f);
	}
}

gboolean
lrg_sequence_curve_get_slopes (LrgSequenceCurve *self,
                               guint             index,
                               gfloat           *out_in,
                               gfloat           *out_out)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), FALSE);

	if (index >= self->keys->len)
		return FALSE;
	if (out_in != NULL)
		*out_in = resolve_slope (self, index, FALSE);
	if (out_out != NULL)
		*out_out = resolve_slope (self, index, TRUE);
	return TRUE;
}

gfloat
lrg_sequence_curve_evaluate (LrgSequenceCurve *self,
                             gfloat            time)
{
	guint                 n, hi;
	const LrgSequenceKey *k0, *k1;

	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), 0.0f);

	n = self->keys->len;
	if (n == 0)
		return 0.0f;
	if (!isfinite (time) || time <= KEY_AT (self, 0)->time)
		return KEY_AT (self, 0)->value;
	if (time >= KEY_AT (self, n - 1)->time)
		return KEY_AT (self, n - 1)->value;

	/* Segment [hi - 1, hi] contains @time (binary search, O(log n)). */
	hi = upper_bound (self, time);
	k0 = KEY_AT (self, hi - 1);
	k1 = KEY_AT (self, hi);
	if (k0->tangent_mode == LRG_SEQUENCE_TANGENT_CONSTANT)
		return k0->value;
	return lrg_sequence_curve_hermite (k0->time, k0->value, resolve_slope (self, hi - 1, TRUE),
	                                   k1->time, k1->value, resolve_slope (self, hi, FALSE), time);
}

gboolean
lrg_sequence_curve_get_time_range (LrgSequenceCurve *self,
                                   gfloat           *out_start,
                                   gfloat           *out_end)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), FALSE);

	if (self->keys->len == 0)
		return FALSE;
	if (out_start != NULL)
		*out_start = KEY_AT (self, 0)->time;
	if (out_end != NULL)
		*out_end = KEY_AT (self, self->keys->len - 1)->time;
	return TRUE;
}

LrgSequenceCurve *
lrg_sequence_curve_copy (LrgSequenceCurve *self)
{
	LrgSequenceCurve *copy;

	g_return_val_if_fail (LRG_IS_SEQUENCE_CURVE (self), NULL);

	copy = lrg_sequence_curve_new ();
	if (self->keys->len > 0)
		g_array_append_vals (copy->keys, self->keys->data, self->keys->len);
	return copy;
}

gboolean
lrg_sequence_curve_equal (LrgSequenceCurve *a,
                          LrgSequenceCurve *b)
{
	guint i;

	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL || a->keys->len != b->keys->len)
		return FALSE;
	for (i = 0; i < a->keys->len; i++)
	{
		const LrgSequenceKey *ka = KEY_AT (a, i);
		const LrgSequenceKey *kb = KEY_AT (b, i);

		if (ka->time != kb->time || ka->value != kb->value || ka->tangent_mode != kb->tangent_mode ||
		    ka->in_tangent != kb->in_tangent || ka->out_tangent != kb->out_tangent)
			return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * LrgSequenceTrack
 * ========================================================================== */

/**
 * LrgSequenceTrack:
 *
 * One track of an #LrgSequence: keyed curves (transform, property) or
 * timed sections (camera cut, audio, event, animation, sub-sequence).
 */
struct _LrgSequenceTrack
{
	GObject parent_instance;

	LrgSequenceTrackKind  kind;
	gchar                *name;
	gchar                *binding;
	gboolean              muted;

	/* transform */
	gchar            *bone;
	LrgSequenceCurve *channels[LRG_SEQUENCE_N_CHANNELS];

	/* property */
	gchar            *component_type;
	gchar            *property;
	LrgSequenceCurve *curve;

	/* section kinds: LrgSequenceSection, sorted by start */
	GArray *sections;
};

G_DEFINE_FINAL_TYPE (LrgSequenceTrack, lrg_sequence_track, G_TYPE_OBJECT)

static void
section_array_clear (gpointer data)
{
	section_clear ((LrgSequenceSection *) data);
}

static void
lrg_sequence_track_finalize (GObject *object)
{
	LrgSequenceTrack *self = LRG_SEQUENCE_TRACK (object);
	guint             i;

	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->binding, g_free);
	g_clear_pointer (&self->bone, g_free);
	g_clear_pointer (&self->component_type, g_free);
	g_clear_pointer (&self->property, g_free);
	for (i = 0; i < LRG_SEQUENCE_N_CHANNELS; i++)
		g_clear_object (&self->channels[i]);
	g_clear_object (&self->curve);
	g_clear_pointer (&self->sections, g_array_unref);

	G_OBJECT_CLASS (lrg_sequence_track_parent_class)->finalize (object);
}

static void
lrg_sequence_track_class_init (LrgSequenceTrackClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_sequence_track_finalize;
}

static void
lrg_sequence_track_init (LrgSequenceTrack *self)
{
	self->sections = g_array_new (FALSE, TRUE, sizeof (LrgSequenceSection));
	g_array_set_clear_func (self->sections, section_array_clear);
}

LrgSequenceTrack *
lrg_sequence_track_new (LrgSequenceTrackKind  kind,
                        const gchar          *name)
{
	LrgSequenceTrack *self;

	g_return_val_if_fail ((guint) kind <= LRG_SEQUENCE_TRACK_SUBSEQUENCE, NULL);

	self = g_object_new (LRG_TYPE_SEQUENCE_TRACK, NULL);
	self->kind = kind;
	self->name = g_strdup (name);
	if (kind == LRG_SEQUENCE_TRACK_PROPERTY)
		self->curve = lrg_sequence_curve_new ();
	return self;
}

LrgSequenceTrackKind
lrg_sequence_track_get_kind (LrgSequenceTrack *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), LRG_SEQUENCE_TRACK_TRANSFORM);
	return self->kind;
}

const gchar *
lrg_sequence_track_get_name (LrgSequenceTrack *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), NULL);
	return self->name;
}

void
lrg_sequence_track_set_name (LrgSequenceTrack *self,
                             const gchar      *name)
{
	g_return_if_fail (LRG_IS_SEQUENCE_TRACK (self));
	g_free (self->name);
	self->name = g_strdup (name);
}

const gchar *
lrg_sequence_track_get_binding (LrgSequenceTrack *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), NULL);
	return self->binding;
}

void
lrg_sequence_track_set_binding (LrgSequenceTrack *self,
                                const gchar      *guid)
{
	g_return_if_fail (LRG_IS_SEQUENCE_TRACK (self));
	g_free (self->binding);
	self->binding = g_strdup (guid);
}

gboolean
lrg_sequence_track_get_muted (LrgSequenceTrack *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), FALSE);
	return self->muted;
}

void
lrg_sequence_track_set_muted (LrgSequenceTrack *self,
                              gboolean          muted)
{
	g_return_if_fail (LRG_IS_SEQUENCE_TRACK (self));
	self->muted = muted != FALSE;
}

const gchar *
lrg_sequence_track_get_bone (LrgSequenceTrack *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), NULL);
	return self->bone;
}

void
lrg_sequence_track_set_bone (LrgSequenceTrack *self,
                             const gchar      *bone)
{
	g_return_if_fail (LRG_IS_SEQUENCE_TRACK (self));
	g_free (self->bone);
	self->bone = (bone != NULL && *bone != '\0') ? g_strdup (bone) : NULL;
}

LrgSequenceCurve *
lrg_sequence_track_get_channel_curve (LrgSequenceTrack            *self,
                                      LrgSequenceTransformChannel  channel)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), NULL);
	g_return_val_if_fail ((guint) channel < LRG_SEQUENCE_N_CHANNELS, NULL);
	return self->channels[channel];
}

LrgSequenceCurve *
lrg_sequence_track_ensure_channel_curve (LrgSequenceTrack            *self,
                                         LrgSequenceTransformChannel  channel)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), NULL);
	g_return_val_if_fail ((guint) channel < LRG_SEQUENCE_N_CHANNELS, NULL);
	g_return_val_if_fail (self->kind == LRG_SEQUENCE_TRACK_TRANSFORM, NULL);

	if (self->channels[channel] == NULL)
		self->channels[channel] = lrg_sequence_curve_new ();
	return self->channels[channel];
}

gboolean
lrg_sequence_track_remove_channel_curve (LrgSequenceTrack            *self,
                                         LrgSequenceTransformChannel  channel)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), FALSE);
	g_return_val_if_fail ((guint) channel < LRG_SEQUENCE_N_CHANNELS, FALSE);

	if (self->channels[channel] == NULL)
		return FALSE;
	g_clear_object (&self->channels[channel]);
	return TRUE;
}

const gchar *
lrg_sequence_track_get_component_type (LrgSequenceTrack *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), NULL);
	return self->component_type;
}

void
lrg_sequence_track_set_component_type (LrgSequenceTrack *self,
                                       const gchar      *type_name)
{
	g_return_if_fail (LRG_IS_SEQUENCE_TRACK (self));
	g_free (self->component_type);
	self->component_type = (type_name != NULL && *type_name != '\0') ? g_strdup (type_name) : NULL;
}

const gchar *
lrg_sequence_track_get_property_name (LrgSequenceTrack *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), NULL);
	return self->property;
}

void
lrg_sequence_track_set_property_name (LrgSequenceTrack *self,
                                      const gchar      *property)
{
	g_return_if_fail (LRG_IS_SEQUENCE_TRACK (self));
	g_free (self->property);
	self->property = g_strdup (property);
}

LrgSequenceCurve *
lrg_sequence_track_get_curve (LrgSequenceTrack *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), NULL);
	return self->curve;
}

#define SECTION_AT(self, i) (&g_array_index ((self)->sections, LrgSequenceSection, (i)))

guint
lrg_sequence_track_add_section (LrgSequenceTrack         *self,
                                const LrgSequenceSection *section)
{
	LrgSequenceSection copy;
	guint              at;

	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), 0);
	g_return_val_if_fail (section != NULL, 0);

	section_copy_into (&copy, section);
	if (!isfinite (copy.start))
		copy.start = 0.0f;
	if (!isfinite (copy.duration) || copy.duration < 0.0f)
		copy.duration = 0.0f;

	/* Stable insertion after sections that start at the same time. */
	at = 0;
	while (at < self->sections->len && SECTION_AT (self, at)->start <= copy.start)
		at++;
	g_array_insert_val (self->sections, at, copy);
	return at;
}

guint
lrg_sequence_track_get_n_sections (LrgSequenceTrack *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), 0);
	return self->sections->len;
}

const LrgSequenceSection *
lrg_sequence_track_get_section (LrgSequenceTrack *self,
                                guint             index)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), NULL);
	return (index < self->sections->len) ? SECTION_AT (self, index) : NULL;
}

gboolean
lrg_sequence_track_remove_section (LrgSequenceTrack *self,
                                   guint             index)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), FALSE);

	if (index >= self->sections->len)
		return FALSE;
	g_array_remove_index (self->sections, index);
	return TRUE;
}

gfloat
lrg_sequence_track_get_section_end (LrgSequenceTrack *self,
                                    guint             index,
                                    gfloat            sequence_length)
{
	const LrgSequenceSection *s;
	guint                     j;

	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), 0.0f);

	if (index >= self->sections->len)
		return 0.0f;
	s = SECTION_AT (self, index);
	if (self->kind == LRG_SEQUENCE_TRACK_EVENT)
		return s->start;
	if (s->duration > 0.0f)
		return s->start + s->duration;

	/* Open-ended: up to the next section that starts later. */
	for (j = index + 1; j < self->sections->len; j++)
		if (SECTION_AT (self, j)->start > s->start)
			return SECTION_AT (self, j)->start;
	return MAX (sequence_length, s->start);
}

gint
lrg_sequence_track_find_section_at (LrgSequenceTrack *self,
                                    gfloat            time,
                                    gfloat            sequence_length)
{
	guint i;

	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), -1);

	/* The last section wins where sections overlap. */
	i = self->sections->len;
	while (i-- > 0)
	{
		const LrgSequenceSection *s = SECTION_AT (self, i);
		gfloat                    end;

		if (s->start > time)
			continue;
		end = lrg_sequence_track_get_section_end (self, i, sequence_length);
		if (time < end || (time >= sequence_length && end >= sequence_length))
			return (gint) i;
	}
	return -1;
}

guint
lrg_sequence_track_add_event (LrgSequenceTrack *self,
                              gfloat            time,
                              const gchar      *name,
                              const gchar      *payload)
{
	LrgSequenceSection s;
	guint              idx;

	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), 0);
	g_return_val_if_fail (name != NULL, 0);

	section_init (&s, time);
	s.name = (gchar *) name;
	s.payload = (gchar *) payload;
	idx = lrg_sequence_track_add_section (self, &s);
	return idx;
}

guint
lrg_sequence_track_add_camera_cut (LrgSequenceTrack *self,
                                   gfloat            time,
                                   const gchar      *camera_guid)
{
	LrgSequenceSection s;

	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), 0);
	g_return_val_if_fail (camera_guid != NULL, 0);

	section_init (&s, time);
	s.name = (gchar *) camera_guid;
	return lrg_sequence_track_add_section (self, &s);
}

guint
lrg_sequence_track_add_audio (LrgSequenceTrack *self,
                              gfloat            time,
                              const gchar      *sound,
                              gfloat            volume,
                              gfloat            duration)
{
	LrgSequenceSection s;

	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), 0);
	g_return_val_if_fail (sound != NULL, 0);

	section_init (&s, time);
	s.name = (gchar *) sound;
	s.weight = volume;
	s.duration = duration;
	return lrg_sequence_track_add_section (self, &s);
}

guint
lrg_sequence_track_add_clip (LrgSequenceTrack *self,
                             gfloat            start,
                             gfloat            duration,
                             const gchar      *clip,
                             gfloat            offset,
                             gfloat            rate,
                             gboolean          loop,
                             gfloat            weight)
{
	LrgSequenceSection s;

	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), 0);
	g_return_val_if_fail (clip != NULL, 0);

	section_init (&s, start);
	s.name = (gchar *) clip;
	s.duration = duration;
	s.offset = offset;
	s.rate = rate;
	s.loop = loop;
	s.weight = weight;
	return lrg_sequence_track_add_section (self, &s);
}

guint
lrg_sequence_track_add_subsequence (LrgSequenceTrack *self,
                                    gfloat            start,
                                    gfloat            duration,
                                    const gchar      *sequence,
                                    gfloat            offset,
                                    gfloat            rate,
                                    gboolean          loop)
{
	LrgSequenceSection s;

	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), 0);
	g_return_val_if_fail (sequence != NULL, 0);

	section_init (&s, start);
	s.name = (gchar *) sequence;
	s.duration = duration;
	s.offset = offset;
	s.rate = rate;
	s.loop = loop;
	return lrg_sequence_track_add_section (self, &s);
}

LrgSequenceTrack *
lrg_sequence_track_copy (LrgSequenceTrack *self)
{
	LrgSequenceTrack *copy;
	guint             i;

	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (self), NULL);

	copy = lrg_sequence_track_new (self->kind, self->name);
	copy->binding = g_strdup (self->binding);
	copy->muted = self->muted;
	copy->bone = g_strdup (self->bone);
	copy->component_type = g_strdup (self->component_type);
	copy->property = g_strdup (self->property);
	for (i = 0; i < LRG_SEQUENCE_N_CHANNELS; i++)
		if (self->channels[i] != NULL)
			copy->channels[i] = lrg_sequence_curve_copy (self->channels[i]);
	if (self->curve != NULL)
	{
		g_clear_object (&copy->curve);
		copy->curve = lrg_sequence_curve_copy (self->curve);
	}
	for (i = 0; i < self->sections->len; i++)
		lrg_sequence_track_add_section (copy, SECTION_AT (self, i));
	return copy;
}

gboolean
lrg_sequence_track_equal (LrgSequenceTrack *a,
                          LrgSequenceTrack *b)
{
	guint i;

	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (a), FALSE);
	g_return_val_if_fail (LRG_IS_SEQUENCE_TRACK (b), FALSE);

	if (a->kind != b->kind || a->muted != b->muted || g_strcmp0 (a->name, b->name) != 0 ||
	    g_strcmp0 (a->binding, b->binding) != 0 || g_strcmp0 (a->bone, b->bone) != 0 ||
	    g_strcmp0 (a->component_type, b->component_type) != 0 || g_strcmp0 (a->property, b->property) != 0 ||
	    !lrg_sequence_curve_equal (a->curve, b->curve) || a->sections->len != b->sections->len)
		return FALSE;
	for (i = 0; i < LRG_SEQUENCE_N_CHANNELS; i++)
		if (!lrg_sequence_curve_equal (a->channels[i], b->channels[i]))
			return FALSE;
	for (i = 0; i < a->sections->len; i++)
		if (!section_equal (SECTION_AT (a, i), SECTION_AT (b, i)))
			return FALSE;
	return TRUE;
}

/* ==========================================================================
 * LrgSequence
 * ========================================================================== */

typedef struct
{
	gchar *guid;
	gchar *name;
} Binding;

static void
binding_clear (gpointer data)
{
	Binding *b = data;

	g_clear_pointer (&b->guid, g_free);
	g_clear_pointer (&b->name, g_free);
}

/**
 * LrgSequence:
 *
 * A cinematic sequence: length, frame rate, play mode, entity bindings
 * and tracks. Loaded from and saved to `.rseq` YAML.
 */
struct _LrgSequence
{
	GObject parent_instance;

	gchar               *name;
	gfloat               length;
	gfloat               fps;
	LrgSequencePlayMode  play_mode;
	GArray              *bindings;   /* Binding */
	GPtrArray           *tracks;     /* LrgSequenceTrack* (owned) */
	gchar               *base_dir;
};

G_DEFINE_FINAL_TYPE (LrgSequence, lrg_sequence, G_TYPE_OBJECT)

static void
lrg_sequence_finalize (GObject *object)
{
	LrgSequence *self = LRG_SEQUENCE (object);

	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->bindings, g_array_unref);
	g_clear_pointer (&self->tracks, g_ptr_array_unref);
	g_clear_pointer (&self->base_dir, g_free);

	G_OBJECT_CLASS (lrg_sequence_parent_class)->finalize (object);
}

static void
lrg_sequence_class_init (LrgSequenceClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_sequence_finalize;
}

static void
lrg_sequence_init (LrgSequence *self)
{
	self->length = 5.0f;
	self->fps = 30.0f;
	self->play_mode = LRG_SEQUENCE_PLAY_ONCE;
	self->bindings = g_array_new (FALSE, TRUE, sizeof (Binding));
	g_array_set_clear_func (self->bindings, binding_clear);
	self->tracks = g_ptr_array_new_with_free_func (g_object_unref);
}

LrgSequence *
lrg_sequence_new (void)
{
	return g_object_new (LRG_TYPE_SEQUENCE, NULL);
}

const gchar *
lrg_sequence_get_base_dir (LrgSequence *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), NULL);
	return self->base_dir;
}

void
lrg_sequence_set_base_dir (LrgSequence *self,
                           const gchar *base_dir)
{
	g_return_if_fail (LRG_IS_SEQUENCE (self));
	g_free (self->base_dir);
	self->base_dir = g_strdup (base_dir);
}

const gchar *
lrg_sequence_get_name (LrgSequence *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), NULL);
	return self->name;
}

void
lrg_sequence_set_name (LrgSequence *self,
                       const gchar *name)
{
	g_return_if_fail (LRG_IS_SEQUENCE (self));
	g_free (self->name);
	self->name = g_strdup (name);
}

gfloat
lrg_sequence_get_length (LrgSequence *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), 0.0f);
	return self->length;
}

void
lrg_sequence_set_length (LrgSequence *self,
                         gfloat       length)
{
	g_return_if_fail (LRG_IS_SEQUENCE (self));
	g_return_if_fail (isfinite (length) && length > 0.0f);
	self->length = length;
}

gfloat
lrg_sequence_get_fps (LrgSequence *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), 30.0f);
	return self->fps;
}

void
lrg_sequence_set_fps (LrgSequence *self,
                      gfloat       fps)
{
	g_return_if_fail (LRG_IS_SEQUENCE (self));
	g_return_if_fail (isfinite (fps) && fps > 0.0f);
	self->fps = fps;
}

gfloat
lrg_sequence_frame_to_time (LrgSequence *self,
                            gint         frame)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), 0.0f);
	return (gfloat) frame / self->fps;
}

gint
lrg_sequence_time_to_frame (LrgSequence *self,
                            gfloat       time)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), 0);
	if (!isfinite (time))
		return 0;
	return (gint) lroundf (time * self->fps);
}

LrgSequencePlayMode
lrg_sequence_get_play_mode (LrgSequence *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), LRG_SEQUENCE_PLAY_ONCE);
	return self->play_mode;
}

void
lrg_sequence_set_play_mode (LrgSequence         *self,
                            LrgSequencePlayMode  mode)
{
	g_return_if_fail (LRG_IS_SEQUENCE (self));
	g_return_if_fail ((guint) mode <= LRG_SEQUENCE_PLAY_PING_PONG);
	self->play_mode = mode;
}

static gint
find_binding (LrgSequence *self,
              const gchar *guid)
{
	guint i;

	for (i = 0; i < self->bindings->len; i++)
		if (g_strcmp0 (g_array_index (self->bindings, Binding, i).guid, guid) == 0)
			return (gint) i;
	return -1;
}

void
lrg_sequence_add_binding (LrgSequence *self,
                          const gchar *guid,
                          const gchar *name)
{
	gint    idx;
	Binding b;

	g_return_if_fail (LRG_IS_SEQUENCE (self));
	g_return_if_fail (guid != NULL);

	idx = find_binding (self, guid);
	if (idx >= 0)
	{
		Binding *e = &g_array_index (self->bindings, Binding, idx);

		g_free (e->name);
		e->name = g_strdup (name);
		return;
	}
	b.guid = g_strdup (guid);
	b.name = g_strdup (name);
	g_array_append_val (self->bindings, b);
}

gboolean
lrg_sequence_remove_binding (LrgSequence *self,
                             const gchar *guid)
{
	gint idx;

	g_return_val_if_fail (LRG_IS_SEQUENCE (self), FALSE);

	idx = find_binding (self, guid);
	if (idx < 0)
		return FALSE;
	g_array_remove_index (self->bindings, (guint) idx);
	return TRUE;
}

guint
lrg_sequence_get_n_bindings (LrgSequence *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), 0);
	return self->bindings->len;
}

gboolean
lrg_sequence_get_binding (LrgSequence  *self,
                          guint         index,
                          const gchar **out_guid,
                          const gchar **out_name)
{
	Binding *b;

	g_return_val_if_fail (LRG_IS_SEQUENCE (self), FALSE);

	if (index >= self->bindings->len)
		return FALSE;
	b = &g_array_index (self->bindings, Binding, index);
	if (out_guid != NULL)
		*out_guid = b->guid;
	if (out_name != NULL)
		*out_name = b->name;
	return TRUE;
}

const gchar *
lrg_sequence_get_binding_name (LrgSequence *self,
                               const gchar *guid)
{
	gint idx;

	g_return_val_if_fail (LRG_IS_SEQUENCE (self), NULL);

	idx = find_binding (self, guid);
	return (idx >= 0) ? g_array_index (self->bindings, Binding, idx).name : NULL;
}

void
lrg_sequence_add_track (LrgSequence      *self,
                        LrgSequenceTrack *track)
{
	g_return_if_fail (LRG_IS_SEQUENCE (self));
	g_return_if_fail (LRG_IS_SEQUENCE_TRACK (track));
	g_ptr_array_add (self->tracks, g_object_ref (track));
}

guint
lrg_sequence_get_n_tracks (LrgSequence *self)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), 0);
	return self->tracks->len;
}

LrgSequenceTrack *
lrg_sequence_get_track (LrgSequence *self,
                        guint        index)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), NULL);
	return (index < self->tracks->len) ? g_ptr_array_index (self->tracks, index) : NULL;
}

LrgSequenceTrack *
lrg_sequence_find_track (LrgSequence *self,
                         const gchar *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_SEQUENCE (self), NULL);

	for (i = 0; i < self->tracks->len; i++)
	{
		LrgSequenceTrack *t = g_ptr_array_index (self->tracks, i);

		if (g_strcmp0 (t->name, name) == 0)
			return t;
	}
	return NULL;
}

gboolean
lrg_sequence_remove_track (LrgSequence *self,
                           guint        index)
{
	g_return_val_if_fail (LRG_IS_SEQUENCE (self), FALSE);

	if (index >= self->tracks->len)
		return FALSE;
	g_ptr_array_remove_index (self->tracks, index);
	return TRUE;
}

LrgSequence *
lrg_sequence_copy (LrgSequence *self)
{
	LrgSequence *copy;
	guint        i;

	g_return_val_if_fail (LRG_IS_SEQUENCE (self), NULL);

	copy = lrg_sequence_new ();
	copy->name = g_strdup (self->name);
	copy->length = self->length;
	copy->fps = self->fps;
	copy->play_mode = self->play_mode;
	copy->base_dir = g_strdup (self->base_dir);
	for (i = 0; i < self->bindings->len; i++)
	{
		Binding *b = &g_array_index (self->bindings, Binding, i);

		lrg_sequence_add_binding (copy, b->guid, b->name);
	}
	for (i = 0; i < self->tracks->len; i++)
		g_ptr_array_add (copy->tracks, lrg_sequence_track_copy (g_ptr_array_index (self->tracks, i)));
	return copy;
}

gboolean
lrg_sequence_equal (LrgSequence *a,
                    LrgSequence *b)
{
	guint i;

	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL)
		return FALSE;
	if (g_strcmp0 (a->name, b->name) != 0 || a->length != b->length || a->fps != b->fps ||
	    a->play_mode != b->play_mode || a->bindings->len != b->bindings->len ||
	    a->tracks->len != b->tracks->len)
		return FALSE;
	for (i = 0; i < a->bindings->len; i++)
	{
		Binding *ba = &g_array_index (a->bindings, Binding, i);
		Binding *bb = &g_array_index (b->bindings, Binding, i);

		if (g_strcmp0 (ba->guid, bb->guid) != 0 || g_strcmp0 (ba->name, bb->name) != 0)
			return FALSE;
	}
	for (i = 0; i < a->tracks->len; i++)
		if (!lrg_sequence_track_equal (g_ptr_array_index (a->tracks, i), g_ptr_array_index (b->tracks, i)))
			return FALSE;
	return TRUE;
}

void
lrg_sequence_compose_bone_offset (const LrgBonePose *bind,
                                  const gfloat      *offset,
                                  LrgBonePose       *out_pose)
{
	gfloat bq[4], oq[4], rq[4];

	g_return_if_fail (bind != NULL);
	g_return_if_fail (offset != NULL);
	g_return_if_fail (out_pose != NULL);

	/* Location adds, scale multiplies, rotation applies after the bind
	 * rotation (bind * offset, i.e. in the bone's own frame). */
	*out_pose = *bind;
	out_pose->position_x = bind->position_x + offset[0];
	out_pose->position_y = bind->position_y + offset[1];
	out_pose->position_z = bind->position_z + offset[2];

	bq[0] = bind->rotation_x;
	bq[1] = bind->rotation_y;
	bq[2] = bind->rotation_z;
	bq[3] = bind->rotation_w;
	lrg_math3d_euler_to_quat (offset + 3, oq);
	lrg_math3d_quat_multiply (bq, oq, rq);
	lrg_math3d_quat_normalize (rq, rq);
	out_pose->rotation_x = rq[0];
	out_pose->rotation_y = rq[1];
	out_pose->rotation_z = rq[2];
	out_pose->rotation_w = rq[3];

	out_pose->scale_x = bind->scale_x * offset[6];
	out_pose->scale_y = bind->scale_y * offset[7];
	out_pose->scale_z = bind->scale_z * offset[8];
}

/* ==========================================================================
 * YAML writer
 * ========================================================================== */

/*
 * Shortest decimal text that reads back as exactly @v (6 to 9 significant
 * digits, locale independent). Gives "0.1" rather than "0.100000001" and
 * makes save(load(save(x))) byte identical.
 */
static void
format_float (gchar  *buf,
              gsize   len,
              gfloat  v)
{
	static const gchar *formats[] = { "%.6g", "%.7g", "%.8g", "%.9g" };
	guint                i;

	if (!isfinite (v))
		v = 0.0f;
	for (i = 0; i < G_N_ELEMENTS (formats); i++)
	{
		g_ascii_formatd (buf, len, formats[i], (gdouble) v);
		if ((gfloat) g_ascii_strtod (buf, NULL) == v)
			return;
	}
}

static void
set_float (YamlMapping *map,
           const gchar *key,
           gfloat       v)
{
	gchar               buf[G_ASCII_DTOSTR_BUF_SIZE];
	g_autoptr(YamlNode) node = NULL;

	format_float (buf, sizeof buf, v);
	node = yaml_node_new_scalar (buf, YAML_SCALAR_STYLE_PLAIN);
	yaml_mapping_set_member (map, key, node);
}

static YamlNode *
keys_to_node (LrgSequenceCurve *curve)
{
	YamlNode     *node = yaml_node_new_sequence (NULL);
	YamlSequence *seq = yaml_node_get_sequence (node);
	guint         i;

	for (i = 0; i < curve->keys->len; i++)
	{
		const LrgSequenceKey *k = KEY_AT (curve, i);
		g_autoptr(YamlNode)   knode = yaml_node_new_mapping (NULL);
		YamlMapping          *kmap = yaml_node_get_mapping (knode);

		set_float (kmap, "time", k->time);
		set_float (kmap, "value", k->value);
		/* "auto" is the default and is omitted; handles only where used */
		if (k->tangent_mode != LRG_SEQUENCE_TANGENT_AUTO)
			yaml_mapping_set_string_member (kmap, "tangent",
			                                enum_nick (LRG_TYPE_SEQUENCE_TANGENT_MODE, (gint) k->tangent_mode));
		if (k->tangent_mode == LRG_SEQUENCE_TANGENT_USER)
			set_float (kmap, "slope", k->in_tangent);
		else if (k->tangent_mode == LRG_SEQUENCE_TANGENT_BROKEN)
		{
			set_float (kmap, "in", k->in_tangent);
			set_float (kmap, "out", k->out_tangent);
		}
		yaml_sequence_add_element (seq, knode);
	}
	return node;
}

/* Writes the section fields kind @kind uses, with the kind's key names. */
static YamlNode *
section_to_node (LrgSequenceTrackKind      kind,
                 const LrgSequenceSection *s)
{
	YamlNode    *node = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (node);

	switch (kind)
	{
	case LRG_SEQUENCE_TRACK_EVENT:
		set_float (map, "time", s->start);
		yaml_mapping_set_string_member (map, "name", s->name != NULL ? s->name : "");
		if (s->payload != NULL)
			yaml_mapping_set_string_member (map, "payload", s->payload);
		break;
	case LRG_SEQUENCE_TRACK_CAMERA_CUT:
		set_float (map, "start", s->start);
		if (s->duration > 0.0f)
			set_float (map, "duration", s->duration);
		yaml_mapping_set_string_member (map, "camera", s->name != NULL ? s->name : "");
		break;
	case LRG_SEQUENCE_TRACK_AUDIO:
		set_float (map, "start", s->start);
		if (s->duration > 0.0f)
			set_float (map, "duration", s->duration);
		yaml_mapping_set_string_member (map, "sound", s->name != NULL ? s->name : "");
		if (s->weight != 1.0f)
			set_float (map, "volume", s->weight);
		break;
	case LRG_SEQUENCE_TRACK_ANIMATION:
	case LRG_SEQUENCE_TRACK_SUBSEQUENCE:
		set_float (map, "start", s->start);
		if (s->duration > 0.0f)
			set_float (map, "duration", s->duration);
		yaml_mapping_set_string_member (map, kind == LRG_SEQUENCE_TRACK_ANIMATION ? "clip" : "sequence",
		                                s->name != NULL ? s->name : "");
		if (s->offset != 0.0f)
			set_float (map, "offset", s->offset);
		if (s->rate != 1.0f)
			set_float (map, "rate", s->rate);
		if (s->loop)
			yaml_mapping_set_boolean_member (map, "loop", TRUE);
		if (kind == LRG_SEQUENCE_TRACK_ANIMATION && s->weight != 1.0f)
			set_float (map, "weight", s->weight);
		break;
	case LRG_SEQUENCE_TRACK_TRANSFORM:
	case LRG_SEQUENCE_TRACK_PROPERTY:
	default:
		break;
	}
	return node;
}

static YamlNode *
track_to_node (LrgSequenceTrack *t)
{
	YamlNode    *node = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (node);
	guint        i;

	yaml_mapping_set_string_member (map, "kind", enum_nick (LRG_TYPE_SEQUENCE_TRACK_KIND, (gint) t->kind));
	if (t->name != NULL)
		yaml_mapping_set_string_member (map, "name", t->name);
	if (t->binding != NULL)
		yaml_mapping_set_string_member (map, "binding", t->binding);
	if (t->muted)
		yaml_mapping_set_boolean_member (map, "muted", TRUE);

	if (t->kind == LRG_SEQUENCE_TRACK_TRANSFORM)
	{
		g_autoptr(YamlNode) chnode = yaml_node_new_mapping (NULL);
		YamlMapping        *chmap = yaml_node_get_mapping (chnode);

		if (t->bone != NULL)
			yaml_mapping_set_string_member (map, "bone", t->bone);
		for (i = 0; i < LRG_SEQUENCE_N_CHANNELS; i++)
		{
			g_autoptr(YamlNode) keys = NULL;

			if (t->channels[i] == NULL)
				continue;
			keys = keys_to_node (t->channels[i]);
			yaml_mapping_set_member (chmap, channel_names[i], keys);
		}
		yaml_mapping_set_member (map, "channels", chnode);
	}
	else if (t->kind == LRG_SEQUENCE_TRACK_PROPERTY)
	{
		g_autoptr(YamlNode) keys = keys_to_node (t->curve);

		if (t->component_type != NULL)
			yaml_mapping_set_string_member (map, "component", t->component_type);
		yaml_mapping_set_string_member (map, "property", t->property != NULL ? t->property : "");
		yaml_mapping_set_member (map, "keys", keys);
	}
	else
	{
		g_autoptr(YamlNode) snode = yaml_node_new_sequence (NULL);
		YamlSequence       *seq = yaml_node_get_sequence (snode);

		for (i = 0; i < t->sections->len; i++)
		{
			g_autoptr(YamlNode) one = section_to_node (t->kind, SECTION_AT (t, i));

			yaml_sequence_add_element (seq, one);
		}
		yaml_mapping_set_member (map, t->kind == LRG_SEQUENCE_TRACK_EVENT ? "events" : "sections", snode);
	}
	return node;
}

gchar *
lrg_sequence_to_data (LrgSequence  *self,
                      GError      **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlNode)      bnode = NULL;
	g_autoptr(YamlNode)      tnode = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	g_autoptr(GError)        local = NULL;
	YamlMapping             *map;
	gchar                   *text;
	guint                    i;

	g_return_val_if_fail (LRG_IS_SEQUENCE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* Header */
	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_string_member (map, "type", "sequence");
	yaml_mapping_set_int_member (map, "version", LRG_SEQUENCE_FORMAT_VERSION);
	if (self->name != NULL)
		yaml_mapping_set_string_member (map, "name", self->name);
	set_float (map, "length", self->length);
	set_float (map, "fps", self->fps);
	yaml_mapping_set_string_member (map, "play_mode", enum_nick (LRG_TYPE_SEQUENCE_PLAY_MODE, (gint) self->play_mode));

	/* Bindings */
	bnode = yaml_node_new_sequence (NULL);
	for (i = 0; i < self->bindings->len; i++)
	{
		Binding             *b = &g_array_index (self->bindings, Binding, i);
		g_autoptr(YamlNode)  one = yaml_node_new_mapping (NULL);
		YamlMapping         *bmap = yaml_node_get_mapping (one);

		yaml_mapping_set_string_member (bmap, "guid", b->guid);
		if (b->name != NULL)
			yaml_mapping_set_string_member (bmap, "name", b->name);
		yaml_sequence_add_element (yaml_node_get_sequence (bnode), one);
	}
	yaml_mapping_set_member (map, "bindings", bnode);

	/* Tracks */
	tnode = yaml_node_new_sequence (NULL);
	for (i = 0; i < self->tracks->len; i++)
	{
		g_autoptr(YamlNode) one = track_to_node (g_ptr_array_index (self->tracks, i));

		yaml_sequence_add_element (yaml_node_get_sequence (tnode), one);
	}
	yaml_mapping_set_member (map, "tracks", tnode);

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	text = yaml_generator_to_data (gen, NULL, &local);
	if (text == NULL)
	{
		g_set_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_IO, "%s",
		             local != NULL ? local->message : "YAML generation failed");
		return NULL;
	}
	return text;
}

gboolean
lrg_sequence_save_to_file (LrgSequence  *self,
                           const gchar  *path,
                           GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_SEQUENCE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	text = lrg_sequence_to_data (self, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, -1, &local))
	{
		g_set_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * YAML reader
 * ========================================================================== */

static const gchar *
scalar_member (YamlMapping *map,
               const gchar *key)
{
	YamlNode *n = yaml_mapping_get_member (map, key);

	if (n == NULL || yaml_node_get_node_type (n) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (n);
}

static gfloat
float_member (YamlMapping *map,
              const gchar *key,
              gfloat       def)
{
	const gchar *s = scalar_member (map, key);
	gchar       *end = NULL;
	gdouble      d;

	if (s == NULL)
		return def;
	d = g_ascii_strtod (s, &end);
	if (end == s || !isfinite (d))
		return def;
	return (gfloat) d;
}

static gboolean
bool_member (YamlMapping *map,
             const gchar *key,
             gboolean     def)
{
	const gchar *s = scalar_member (map, key);

	if (s == NULL)
		return def;
	return g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "yes") == 0 || strcmp (s, "1") == 0;
}

static YamlMapping *
mapping_of (YamlNode *n)
{
	return (n != NULL && yaml_node_get_node_type (n) == YAML_NODE_MAPPING) ? yaml_node_get_mapping (n) : NULL;
}

static YamlSequence *
sequence_of (YamlNode *n)
{
	return (n != NULL && yaml_node_get_node_type (n) == YAML_NODE_SEQUENCE) ? yaml_node_get_sequence (n) : NULL;
}

static void
read_keys (LrgSequenceCurve *curve,
           YamlSequence     *seq)
{
	guint i;

	if (seq == NULL)
		return;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		YamlMapping   *kmap = mapping_of (yaml_sequence_get_element (seq, i));
		LrgSequenceKey k = { 0 };
		gint           mode = LRG_SEQUENCE_TANGENT_AUTO;

		if (kmap == NULL)
			continue;
		k.time = float_member (kmap, "time", 0.0f);
		k.value = float_member (kmap, "value", 0.0f);
		if (!enum_from_nick (LRG_TYPE_SEQUENCE_TANGENT_MODE, scalar_member (kmap, "tangent"), &mode))
			mode = LRG_SEQUENCE_TANGENT_AUTO;
		k.tangent_mode = (LrgSequenceTangentMode) mode;
		if (k.tangent_mode == LRG_SEQUENCE_TANGENT_USER)
			k.in_tangent = k.out_tangent = float_member (kmap, "slope", 0.0f);
		else if (k.tangent_mode == LRG_SEQUENCE_TANGENT_BROKEN)
		{
			k.in_tangent = float_member (kmap, "in", 0.0f);
			k.out_tangent = float_member (kmap, "out", 0.0f);
		}
		lrg_sequence_curve_insert_key (curve, &k);
	}
}

static void
read_sections (LrgSequenceTrack *t,
               YamlSequence     *seq)
{
	guint i;

	if (seq == NULL)
		return;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		YamlMapping       *smap = mapping_of (yaml_sequence_get_element (seq, i));
		LrgSequenceSection s;
		const gchar       *name_key;

		if (smap == NULL)
			continue;
		section_init (&s, 0.0f);
		switch (t->kind)
		{
		case LRG_SEQUENCE_TRACK_EVENT:
			s.start = float_member (smap, "time", 0.0f);
			name_key = "name";
			break;
		case LRG_SEQUENCE_TRACK_CAMERA_CUT:
			name_key = "camera";
			break;
		case LRG_SEQUENCE_TRACK_AUDIO:
			name_key = "sound";
			s.weight = float_member (smap, "volume", 1.0f);
			break;
		case LRG_SEQUENCE_TRACK_ANIMATION:
			name_key = "clip";
			s.weight = float_member (smap, "weight", 1.0f);
			break;
		case LRG_SEQUENCE_TRACK_SUBSEQUENCE:
		default:
			name_key = "sequence";
			break;
		}
		if (t->kind != LRG_SEQUENCE_TRACK_EVENT)
		{
			s.start = float_member (smap, "start", 0.0f);
			s.duration = float_member (smap, "duration", 0.0f);
		}
		if (t->kind == LRG_SEQUENCE_TRACK_ANIMATION || t->kind == LRG_SEQUENCE_TRACK_SUBSEQUENCE)
		{
			s.offset = float_member (smap, "offset", 0.0f);
			s.rate = float_member (smap, "rate", 1.0f);
			s.loop = bool_member (smap, "loop", FALSE);
		}
		s.name = (gchar *) scalar_member (smap, name_key);
		s.payload = (t->kind == LRG_SEQUENCE_TRACK_EVENT) ? (gchar *) scalar_member (smap, "payload") : NULL;
		if (s.name == NULL)
			continue;
		lrg_sequence_track_add_section (t, &s);
	}
}

static LrgSequenceTrack *
read_track (YamlMapping  *map,
            guint         index,
            GError      **error)
{
	g_autoptr(LrgSequenceTrack) t = NULL;
	const gchar                *kind_nick = scalar_member (map, "kind");
	gint                        kind;
	guint                       i;

	if (!enum_from_nick (LRG_TYPE_SEQUENCE_TRACK_KIND, kind_nick, &kind))
	{
		g_set_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_INVALID,
		             "track %u: unknown kind '%s'", index, kind_nick != NULL ? kind_nick : "(none)");
		return NULL;
	}

	t = lrg_sequence_track_new ((LrgSequenceTrackKind) kind, scalar_member (map, "name"));
	lrg_sequence_track_set_binding (t, scalar_member (map, "binding"));
	t->muted = bool_member (map, "muted", FALSE);

	switch (t->kind)
	{
	case LRG_SEQUENCE_TRACK_TRANSFORM:
	{
		YamlMapping *chmap = mapping_of (yaml_mapping_get_member (map, "channels"));

		lrg_sequence_track_set_bone (t, scalar_member (map, "bone"));
		for (i = 0; chmap != NULL && i < LRG_SEQUENCE_N_CHANNELS; i++)
		{
			YamlSequence *keys = sequence_of (yaml_mapping_get_member (chmap, channel_names[i]));

			if (keys != NULL)
				read_keys (lrg_sequence_track_ensure_channel_curve (t, (LrgSequenceTransformChannel) i), keys);
		}
		break;
	}
	case LRG_SEQUENCE_TRACK_PROPERTY:
		lrg_sequence_track_set_component_type (t, scalar_member (map, "component"));
		lrg_sequence_track_set_property_name (t, scalar_member (map, "property"));
		read_keys (t->curve, sequence_of (yaml_mapping_get_member (map, "keys")));
		break;
	case LRG_SEQUENCE_TRACK_EVENT:
		read_sections (t, sequence_of (yaml_mapping_get_member (map, "events")));
		break;
	case LRG_SEQUENCE_TRACK_CAMERA_CUT:
	case LRG_SEQUENCE_TRACK_AUDIO:
	case LRG_SEQUENCE_TRACK_ANIMATION:
	case LRG_SEQUENCE_TRACK_SUBSEQUENCE:
	default:
		read_sections (t, sequence_of (yaml_mapping_get_member (map, "sections")));
		break;
	}
	return g_steal_pointer (&t);
}

LrgSequence *
lrg_sequence_new_from_data (const gchar  *data,
                            gssize        length,
                            const gchar  *base_dir,
                            GError      **error)
{
	g_autoptr(LrgSequence) self = NULL;
	g_autoptr(YamlParser)  parser = NULL;
	g_autoptr(GError)      local = NULL;
	YamlMapping           *map;
	YamlSequence          *seq;
	const gchar           *type;
	const gchar           *mode_nick;
	gint                   version, mode;
	gfloat                 f;
	guint                  i;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* Parse the YAML and check the header */
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_PARSE, "%s", local->message);
		return NULL;
	}
	map = mapping_of (yaml_parser_get_root (parser));
	if (map == NULL)
	{
		g_set_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_INVALID, "the document is not a mapping");
		return NULL;
	}
	type = scalar_member (map, "type");
	if (type != NULL && strcmp (type, "sequence") != 0)
	{
		g_set_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_INVALID, "type is '%s', not 'sequence'", type);
		return NULL;
	}
	version = (gint) float_member (map, "version", (gfloat) LRG_SEQUENCE_FORMAT_VERSION);
	if (version > LRG_SEQUENCE_FORMAT_VERSION)
	{
		g_set_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_VERSION,
		             "format version %d is newer than %d", version, LRG_SEQUENCE_FORMAT_VERSION);
		return NULL;
	}

	/* Settings (bad values keep the defaults) */
	self = lrg_sequence_new ();
	self->base_dir = g_strdup (base_dir);
	self->name = g_strdup (scalar_member (map, "name"));
	f = float_member (map, "length", self->length);
	if (f > 0.0f)
		self->length = f;
	f = float_member (map, "fps", self->fps);
	if (f > 0.0f)
		self->fps = f;
	mode_nick = scalar_member (map, "play_mode");
	if (enum_from_nick (LRG_TYPE_SEQUENCE_PLAY_MODE, mode_nick, &mode))
		self->play_mode = (LrgSequencePlayMode) mode;

	/* Bindings */
	seq = sequence_of (yaml_mapping_get_member (map, "bindings"));
	for (i = 0; seq != NULL && i < yaml_sequence_get_length (seq); i++)
	{
		YamlMapping *bmap = mapping_of (yaml_sequence_get_element (seq, i));
		const gchar *guid = (bmap != NULL) ? scalar_member (bmap, "guid") : NULL;

		if (guid != NULL)
			lrg_sequence_add_binding (self, guid, scalar_member (bmap, "name"));
	}

	/* Tracks */
	seq = sequence_of (yaml_mapping_get_member (map, "tracks"));
	for (i = 0; seq != NULL && i < yaml_sequence_get_length (seq); i++)
	{
		YamlMapping      *tmap = mapping_of (yaml_sequence_get_element (seq, i));
		LrgSequenceTrack *t;

		if (tmap == NULL)
			continue;
		t = read_track (tmap, i, error);
		if (t == NULL)
			return NULL;
		g_ptr_array_add (self->tracks, t);
	}
	return g_steal_pointer (&self);
}

LrgSequence *
lrg_sequence_new_from_file (const gchar  *path,
                            GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *dir = NULL;
	g_autoptr(GError) local = NULL;
	LrgSequence      *self;
	gsize             len = 0;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, LRG_SEQUENCE_ERROR, LRG_SEQUENCE_ERROR_IO, "%s", local->message);
		return NULL;
	}
	dir = g_path_get_dirname (path);
	self = lrg_sequence_new_from_data (text, (gssize) len, dir, &local);
	if (self == NULL)
	{
		g_set_error (error, LRG_SEQUENCE_ERROR, local->code, "%s: %s", path, local->message);
		return NULL;
	}
	return self;
}

/* ==========================================================================
 * Level timeline conversion
 * ========================================================================== */

/* The transform track for (@guid, @bone) in @self, appended when missing. */
static LrgSequenceTrack *
ensure_transform_track (LrgSequence *self,
                        const gchar *guid,
                        const gchar *bone)
{
	LrgSequenceTrack *t;
	guint             i;

	for (i = 0; i < self->tracks->len; i++)
	{
		t = g_ptr_array_index (self->tracks, i);
		if (t->kind == LRG_SEQUENCE_TRACK_TRANSFORM && g_strcmp0 (t->binding, guid) == 0 &&
		    g_strcmp0 (t->bone, bone) == 0)
			return t;
	}
	t = lrg_sequence_track_new (LRG_SEQUENCE_TRACK_TRANSFORM, bone != NULL ? bone : guid);
	lrg_sequence_track_set_binding (t, guid);
	lrg_sequence_track_set_bone (t, bone);
	g_ptr_array_add (self->tracks, t);
	lrg_sequence_add_binding (self, guid, NULL);
	return t;
}

/* Copies one timeline track's keys into @curve: Bézier keys keep their
 * handles, linear keys become linear keys, other easings are baked. */
static void
convert_timeline_keys (LrgTimelineTrack *src,
                       LrgSequenceCurve *curve,
                       gfloat            fps)
{
	guint i, n = lrg_timeline_track_get_n_keys (src);

	for (i = 0; i < n; i++)
	{
		gfloat                 t = 0.0f, v = 0.0f, tin = 0.0f, tout = 0.0f;
		LrgEasingType          easing = LRG_EASING_LINEAR;
		LrgSequenceTangentMode mode = LRG_SEQUENCE_TANGENT_LINEAR;
		LrgSequenceKey         k = { 0 };

		lrg_timeline_track_get_key (src, i, &t, &v, &easing);
		if (lrg_timeline_track_get_key_tangents (src, i, &mode, &tin, &tout))
		{
			k.tangent_mode = mode;
			k.in_tangent = tin;
			k.out_tangent = tout;
		}
		else
		{
			k.tangent_mode = LRG_SEQUENCE_TANGENT_LINEAR;
			if (easing != LRG_EASING_LINEAR && i + 1 < n)
			{
				gfloat t1 = 0.0f, step = 1.0f / fps, s;

				/* Bake the eased segment into linear keys at @fps. */
				lrg_timeline_track_get_key (src, i + 1, &t1, NULL, NULL);
				for (s = t + step; s < t1 - KEY_EPSILON; s += step)
				{
					LrgSequenceKey b = { 0 };
					gfloat         bv = 0.0f;

					lrg_timeline_track_sample (src, s, &bv);
					b.time = s;
					b.value = bv;
					b.tangent_mode = LRG_SEQUENCE_TANGENT_LINEAR;
					lrg_sequence_curve_insert_key (curve, &b);
				}
			}
		}
		k.time = t;
		k.value = v;
		lrg_sequence_curve_insert_key (curve, &k);
	}
}

LrgSequence *
lrg_sequence_new_from_timeline (LrgTimeline *timeline,
                                gfloat       fps)
{
	LrgSequence *self;
	guint        i;

	g_return_val_if_fail (LRG_IS_TIMELINE (timeline), NULL);
	g_return_val_if_fail (isfinite (fps) && fps > 0.0f, NULL);

	self = lrg_sequence_new ();
	self->name = g_strdup (lrg_timeline_get_name (timeline));
	self->length = lrg_timeline_get_length (timeline);
	self->fps = fps;
	self->play_mode = lrg_timeline_get_loop (timeline) ? LRG_SEQUENCE_PLAY_LOOP : LRG_SEQUENCE_PLAY_ONCE;

	for (i = 0; i < lrg_timeline_get_n_tracks (timeline); i++)
	{
		LrgTimelineTrack           *src = lrg_timeline_get_track (timeline, i);
		const gchar                *channel = lrg_timeline_track_get_channel (src);
		const gchar                *guid = lrg_timeline_track_get_node_guid (src);
		g_autofree gchar           *bone = NULL;
		LrgSequenceTransformChannel ch;
		LrgSequenceTrack           *t;

		/* Bone channels and the nine node transform channels convert;
		 * material and param channels have no runtime counterpart here. */
		if (!lrg_timeline_channel_get_bone (channel, &bone, &ch) &&
		    !lrg_sequence_transform_channel_from_string (channel, &ch))
			continue;
		t = ensure_transform_track (self, guid, bone);
		convert_timeline_keys (src, lrg_sequence_track_ensure_channel_curve (t, ch), fps);
	}
	return self;
}
