/* lrg-skeleton2d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkeleton2D and LrgSkeleton2DClip: 2D bones, slots, skins of
 * LrgSkinnedSprite attachments, clips with bone TRS / slot colour /
 * attachment / draw-order / event keys, and the `.rskel2d` YAML format.
 *
 * The format and the runtime are our own design.  Ideas common to 2D
 * skeletal tools (bones, slots, skins, setup pose) are used; no code or
 * file format of any other tool is.
 *
 * Conventions: y points down (screen space), rotations are degrees and
 * turn clockwise on screen, a bone's length lies along its local +X.
 * 2D affine matrices are six floats (a, b, c, d, tx, ty) with
 * x' = a*x + c*y + tx and y' = b*x + d*y + ty.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>

#include "animation/lrg-skeleton2d.h"
#include "core/lrg-data-loader.h"

#define DEG2RAD_F (0.017453292519943295f)
#define RAD2DEG_F (57.29577951308232f)
#define KEY_TIME_EPSILON (1e-6f)

/* ================================================================== */
/* Enumerations                                                        */
/* ================================================================== */

G_DEFINE_QUARK (lrg-skeleton2d-error-quark, lrg_skeleton2d_error)

GType
lrg_skeleton2d_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SKELETON2D_ERROR_PARSE, "LRG_SKELETON2D_ERROR_PARSE", "parse" },
			{ LRG_SKELETON2D_ERROR_INVALID, "LRG_SKELETON2D_ERROR_INVALID", "invalid" },
			{ LRG_SKELETON2D_ERROR_VERSION, "LRG_SKELETON2D_ERROR_VERSION", "version" },
			{ LRG_SKELETON2D_ERROR_IO, "LRG_SKELETON2D_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgSkeleton2DError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

GType
lrg_skeleton2d_curve_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SKELETON2D_CURVE_LINEAR, "LRG_SKELETON2D_CURVE_LINEAR", "linear" },
			{ LRG_SKELETON2D_CURVE_STEPPED, "LRG_SKELETON2D_CURVE_STEPPED", "stepped" },
			{ LRG_SKELETON2D_CURVE_BEZIER, "LRG_SKELETON2D_CURVE_BEZIER", "bezier" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgSkeleton2DCurve"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

GType
lrg_skeleton2d_channel_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_SKELETON2D_CHANNEL_TRANSLATE, "LRG_SKELETON2D_CHANNEL_TRANSLATE", "translate" },
			{ LRG_SKELETON2D_CHANNEL_ROTATE, "LRG_SKELETON2D_CHANNEL_ROTATE", "rotate" },
			{ LRG_SKELETON2D_CHANNEL_SCALE, "LRG_SKELETON2D_CHANNEL_SCALE", "scale" },
			{ LRG_SKELETON2D_CHANNEL_COLOR, "LRG_SKELETON2D_CHANNEL_COLOR", "color" },
			{ LRG_SKELETON2D_CHANNEL_ATTACHMENT, "LRG_SKELETON2D_CHANNEL_ATTACHMENT", "attachment" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgSkeleton2DChannel"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

/* ================================================================== */
/* 2D affine helpers                                                   */
/* ================================================================== */

static void
affine_identity (gfloat *m)
{
	m[0] = 1.0f;
	m[1] = 0.0f;
	m[2] = 0.0f;
	m[3] = 1.0f;
	m[4] = 0.0f;
	m[5] = 0.0f;
}

/* m = T(x, y) * R(rotation) * S(sx, sy) */
static void
affine_from_trs (gfloat *m,
                 gfloat  x,
                 gfloat  y,
                 gfloat  rotation,
                 gfloat  sx,
                 gfloat  sy)
{
	gfloat r = rotation * DEG2RAD_F;
	gfloat c = cosf (r);
	gfloat s = sinf (r);

	m[0] = c * sx;
	m[1] = s * sx;
	m[2] = -s * sy;
	m[3] = c * sy;
	m[4] = x;
	m[5] = y;
}

/* out = a * b (apply b first); out may not alias a or b */
static void
affine_multiply (gfloat       *out,
                 const gfloat *a,
                 const gfloat *b)
{
	out[0] = a[0] * b[0] + a[2] * b[1];
	out[1] = a[1] * b[0] + a[3] * b[1];
	out[2] = a[0] * b[2] + a[2] * b[3];
	out[3] = a[1] * b[2] + a[3] * b[3];
	out[4] = a[0] * b[4] + a[2] * b[5] + a[4];
	out[5] = a[1] * b[4] + a[3] * b[5] + a[5];
}

/* out = inverse (m); a singular matrix (a zero scale) inverts to the
 * identity so skinning degrades to "not moved" instead of NaNs */
static void
affine_invert (gfloat       *out,
               const gfloat *m)
{
	gfloat det = m[0] * m[3] - m[1] * m[2];

	if (fabsf (det) < 1e-12f)
	{
		affine_identity (out);
		return;
	}
	out[0] = m[3] / det;
	out[1] = -m[1] / det;
	out[2] = -m[2] / det;
	out[3] = m[0] / det;
	out[4] = (m[2] * m[5] - m[3] * m[4]) / det;
	out[5] = (m[1] * m[4] - m[0] * m[5]) / det;
}

/* wrap an angle in degrees to -180 .. 180 */
static gfloat
wrap_degrees (gfloat d)
{
	d = fmodf (d + 180.0f, 360.0f);
	if (d < 0.0f)
		d += 360.0f;
	return d - 180.0f;
}

/* ================================================================== */
/* LrgSkeleton2DClip                                                   */
/* ================================================================== */

/* A numeric key: values used depend on the channel (rotate 1, translate
 * and scale 2, colour 4).  The curve shapes the span to the next key. */
typedef struct
{
	gfloat             time;
	gfloat             v[4];
	LrgSkeleton2DCurve curve;
	gfloat             bez[4];
} ValueKey;

/* An attachment key (name NULL = hidden) or an event. */
typedef struct
{
	gfloat  time;
	gchar  *name;
} NameKey;

/* A draw-order key (slots NULL = setup order). */
typedef struct
{
	gfloat   time;
	gchar  **slots;
} OrderKey;

/* Timelines of one bone: translate, rotate, scale (NULL until keyed). */
typedef struct
{
	gchar  *name;
	GArray *ch[3];
} BoneTrack;

/* Timelines of one slot. */
typedef struct
{
	gchar  *name;
	GArray *color;    /* ValueKey */
	GArray *attach;   /* NameKey */
} SlotTrack;

struct _LrgSkeleton2DClip
{
	GObject    parent_instance;

	gchar     *name;
	gfloat     duration;
	gboolean   loop;

	GPtrArray *bone_tracks;  /* BoneTrack*, first-keyed order */
	GPtrArray *slot_tracks;  /* SlotTrack* */
	GArray    *order;        /* OrderKey */
	GArray    *events;       /* NameKey */
};

G_DEFINE_FINAL_TYPE (LrgSkeleton2DClip, lrg_skeleton2d_clip, G_TYPE_OBJECT)

enum
{
	CLIP_PROP_0,
	CLIP_PROP_NAME,
	CLIP_PROP_DURATION,
	CLIP_PROP_LOOP,
	CLIP_N_PROPS
};

static GParamSpec *clip_properties[CLIP_N_PROPS];

static void
name_key_clear (gpointer data)
{
	NameKey *k = data;

	g_clear_pointer (&k->name, g_free);
}

static void
order_key_clear (gpointer data)
{
	OrderKey *k = data;

	g_clear_pointer (&k->slots, g_strfreev);
}

static void
bone_track_free (gpointer data)
{
	BoneTrack *t = data;
	guint      i;

	for (i = 0; i < 3; i++)
		g_clear_pointer (&t->ch[i], g_array_unref);
	g_free (t->name);
	g_free (t);
}

static void
slot_track_free (gpointer data)
{
	SlotTrack *t = data;

	g_clear_pointer (&t->color, g_array_unref);
	g_clear_pointer (&t->attach, g_array_unref);
	g_free (t->name);
	g_free (t);
}

static void
lrg_skeleton2d_clip_finalize (GObject *object)
{
	LrgSkeleton2DClip *self = LRG_SKELETON2D_CLIP (object);

	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->bone_tracks, g_ptr_array_unref);
	g_clear_pointer (&self->slot_tracks, g_ptr_array_unref);
	g_clear_pointer (&self->order, g_array_unref);
	g_clear_pointer (&self->events, g_array_unref);

	G_OBJECT_CLASS (lrg_skeleton2d_clip_parent_class)->finalize (object);
}

static void
lrg_skeleton2d_clip_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgSkeleton2DClip *self = LRG_SKELETON2D_CLIP (object);

	switch (prop_id)
	{
	case CLIP_PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case CLIP_PROP_DURATION:
		g_value_set_float (value, self->duration);
		break;
	case CLIP_PROP_LOOP:
		g_value_set_boolean (value, self->loop);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_skeleton2d_clip_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgSkeleton2DClip *self = LRG_SKELETON2D_CLIP (object);

	switch (prop_id)
	{
	case CLIP_PROP_NAME:
		lrg_skeleton2d_clip_set_name (self, g_value_get_string (value));
		break;
	case CLIP_PROP_DURATION:
		lrg_skeleton2d_clip_set_duration (self, g_value_get_float (value));
		break;
	case CLIP_PROP_LOOP:
		lrg_skeleton2d_clip_set_loop (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_skeleton2d_clip_class_init (LrgSkeleton2DClipClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_skeleton2d_clip_finalize;
	object_class->get_property = lrg_skeleton2d_clip_get_property;
	object_class->set_property = lrg_skeleton2d_clip_set_property;

	/**
	 * LrgSkeleton2DClip:name:
	 *
	 * The clip name, unique within a skeleton.
	 */
	clip_properties[CLIP_PROP_NAME] =
		g_param_spec_string ("name", "Name", "Clip name", "clip",
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2DClip:duration:
	 *
	 * The clip length in seconds.  Keys later than it extend it.
	 */
	clip_properties[CLIP_PROP_DURATION] =
		g_param_spec_float ("duration", "Duration", "Clip length in seconds",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2DClip:loop:
	 *
	 * Whether players loop the clip by default.
	 */
	clip_properties[CLIP_PROP_LOOP] =
		g_param_spec_boolean ("loop", "Loop", "Whether the clip loops by default", FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, CLIP_N_PROPS, clip_properties);
}

static void
lrg_skeleton2d_clip_init (LrgSkeleton2DClip *self)
{
	self->name = g_strdup ("clip");
	self->bone_tracks = g_ptr_array_new_with_free_func (bone_track_free);
	self->slot_tracks = g_ptr_array_new_with_free_func (slot_track_free);
	self->order = g_array_new (FALSE, TRUE, sizeof (OrderKey));
	g_array_set_clear_func (self->order, order_key_clear);
	self->events = g_array_new (FALSE, TRUE, sizeof (NameKey));
	g_array_set_clear_func (self->events, name_key_clear);
}

LrgSkeleton2DClip *
lrg_skeleton2d_clip_new (const gchar *name)
{
	g_return_val_if_fail (name != NULL && name[0] != '\0', NULL);

	return g_object_new (LRG_TYPE_SKELETON2D_CLIP, "name", name, NULL);
}

const gchar *
lrg_skeleton2d_clip_get_name (LrgSkeleton2DClip *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), NULL);

	return self->name;
}

void
lrg_skeleton2d_clip_set_name (LrgSkeleton2DClip *self,
                              const gchar       *name)
{
	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));
	g_return_if_fail (name != NULL && name[0] != '\0');

	if (g_strcmp0 (self->name, name) == 0)
		return;
	g_free (self->name);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), clip_properties[CLIP_PROP_NAME]);
}

gfloat
lrg_skeleton2d_clip_get_duration (LrgSkeleton2DClip *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), 0.0f);

	return self->duration;
}

void
lrg_skeleton2d_clip_set_duration (LrgSkeleton2DClip *self,
                                  gfloat             duration)
{
	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));

	duration = MAX (duration, 0.0f);
	if (self->duration == duration)
		return;
	self->duration = duration;
	g_object_notify_by_pspec (G_OBJECT (self), clip_properties[CLIP_PROP_DURATION]);
}

gboolean
lrg_skeleton2d_clip_get_loop (LrgSkeleton2DClip *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), FALSE);

	return self->loop;
}

void
lrg_skeleton2d_clip_set_loop (LrgSkeleton2DClip *self,
                              gboolean           loop)
{
	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));

	loop = !!loop;
	if (self->loop == loop)
		return;
	self->loop = loop;
	g_object_notify_by_pspec (G_OBJECT (self), clip_properties[CLIP_PROP_LOOP]);
}

/* ---- track lookup ---- */

static BoneTrack *
find_bone_track (LrgSkeleton2DClip *self,
                 const gchar       *name,
                 gboolean           create)
{
	BoneTrack *t;
	guint      i;

	for (i = 0; i < self->bone_tracks->len; i++)
	{
		t = g_ptr_array_index (self->bone_tracks, i);
		if (g_strcmp0 (t->name, name) == 0)
			return t;
	}
	if (!create)
		return NULL;
	t = g_new0 (BoneTrack, 1);
	t->name = g_strdup (name);
	g_ptr_array_add (self->bone_tracks, t);
	return t;
}

static SlotTrack *
find_slot_track (LrgSkeleton2DClip *self,
                 const gchar       *name,
                 gboolean           create)
{
	SlotTrack *t;
	guint      i;

	for (i = 0; i < self->slot_tracks->len; i++)
	{
		t = g_ptr_array_index (self->slot_tracks, i);
		if (g_strcmp0 (t->name, name) == 0)
			return t;
	}
	if (!create)
		return NULL;
	t = g_new0 (SlotTrack, 1);
	t->name = g_strdup (name);
	g_ptr_array_add (self->slot_tracks, t);
	return t;
}

/* The key array of a numeric (target, channel) timeline, created on
 * demand; NULL for the attachment channel or a missing timeline. */
static GArray *
value_keys (LrgSkeleton2DClip    *self,
            const gchar          *target,
            LrgSkeleton2DChannel  channel,
            gboolean              create)
{
	if (channel <= LRG_SKELETON2D_CHANNEL_SCALE)
	{
		BoneTrack *t = find_bone_track (self, target, create);

		if (t == NULL)
			return NULL;
		if (t->ch[channel] == NULL && create)
			t->ch[channel] = g_array_new (FALSE, TRUE, sizeof (ValueKey));
		return t->ch[channel];
	}
	if (channel == LRG_SKELETON2D_CHANNEL_COLOR)
	{
		SlotTrack *t = find_slot_track (self, target, create);

		if (t == NULL)
			return NULL;
		if (t->color == NULL && create)
			t->color = g_array_new (FALSE, TRUE, sizeof (ValueKey));
		return t->color;
	}
	return NULL;
}

/* Index at which a key at @time goes in a time-sorted array of structs
 * whose first member is the time; *replace says an existing key sits at
 * that time. */
static guint
key_insert_index (GArray   *keys,
                  gsize     stride,
                  gfloat    time,
                  gboolean *replace)
{
	guint i;

	*replace = FALSE;
	for (i = 0; i < keys->len; i++)
	{
		gfloat t = *(const gfloat *)(gconstpointer)(keys->data + i * stride);

		if (fabsf (t - time) <= KEY_TIME_EPSILON)
		{
			*replace = TRUE;
			return i;
		}
		if (t > time)
			return i;
	}
	return keys->len;
}

static void
extend_duration (LrgSkeleton2DClip *self,
                 gfloat             time)
{
	if (time > self->duration)
		lrg_skeleton2d_clip_set_duration (self, time);
}

static void
add_value_key (LrgSkeleton2DClip    *self,
               const gchar          *target,
               LrgSkeleton2DChannel  channel,
               gfloat                time,
               const gfloat         *v,
               guint                 n,
               LrgSkeleton2DCurve    curve)
{
	GArray   *keys;
	ValueKey  k;
	gboolean  replace;
	guint     at;

	memset (&k, 0, sizeof k);
	k.time = MAX (time, 0.0f);
	memcpy (k.v, v, sizeof (gfloat) * n);
	k.curve = curve;
	/* a Bézier key without control points eases linearly until
	 * lrg_skeleton2d_clip_set_key_bezier() gives it some */
	k.bez[0] = 1.0f / 3.0f;
	k.bez[1] = 1.0f / 3.0f;
	k.bez[2] = 2.0f / 3.0f;
	k.bez[3] = 2.0f / 3.0f;

	keys = value_keys (self, target, channel, TRUE);
	at = key_insert_index (keys, sizeof (ValueKey), k.time, &replace);
	if (replace)
		g_array_index (keys, ValueKey, at) = k;
	else
		g_array_insert_val (keys, at, k);
	extend_duration (self, k.time);
}

void
lrg_skeleton2d_clip_add_translate_key (LrgSkeleton2DClip  *self,
                                       const gchar        *bone,
                                       gfloat              time,
                                       gfloat              x,
                                       gfloat              y,
                                       LrgSkeleton2DCurve  curve)
{
	gfloat v[2];

	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));
	g_return_if_fail (bone != NULL);

	v[0] = x;
	v[1] = y;
	add_value_key (self, bone, LRG_SKELETON2D_CHANNEL_TRANSLATE, time, v, 2, curve);
}

void
lrg_skeleton2d_clip_add_rotate_key (LrgSkeleton2DClip  *self,
                                    const gchar        *bone,
                                    gfloat              time,
                                    gfloat              degrees,
                                    LrgSkeleton2DCurve  curve)
{
	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));
	g_return_if_fail (bone != NULL);

	add_value_key (self, bone, LRG_SKELETON2D_CHANNEL_ROTATE, time, &degrees, 1, curve);
}

void
lrg_skeleton2d_clip_add_scale_key (LrgSkeleton2DClip  *self,
                                   const gchar        *bone,
                                   gfloat              time,
                                   gfloat              x,
                                   gfloat              y,
                                   LrgSkeleton2DCurve  curve)
{
	gfloat v[2];

	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));
	g_return_if_fail (bone != NULL);

	v[0] = x;
	v[1] = y;
	add_value_key (self, bone, LRG_SKELETON2D_CHANNEL_SCALE, time, v, 2, curve);
}

void
lrg_skeleton2d_clip_add_color_key (LrgSkeleton2DClip  *self,
                                   const gchar        *slot,
                                   gfloat              time,
                                   gfloat              r,
                                   gfloat              g,
                                   gfloat              b,
                                   gfloat              a,
                                   LrgSkeleton2DCurve  curve)
{
	gfloat v[4];

	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));
	g_return_if_fail (slot != NULL);

	v[0] = r;
	v[1] = g;
	v[2] = b;
	v[3] = a;
	add_value_key (self, slot, LRG_SKELETON2D_CHANNEL_COLOR, time, v, 4, curve);
}

void
lrg_skeleton2d_clip_add_attachment_key (LrgSkeleton2DClip *self,
                                        const gchar       *slot,
                                        gfloat             time,
                                        const gchar       *attachment)
{
	SlotTrack *t;
	NameKey    k;
	gboolean   replace;
	guint      at;

	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));
	g_return_if_fail (slot != NULL);

	t = find_slot_track (self, slot, TRUE);
	if (t->attach == NULL)
	{
		t->attach = g_array_new (FALSE, TRUE, sizeof (NameKey));
		g_array_set_clear_func (t->attach, name_key_clear);
	}
	k.time = MAX (time, 0.0f);
	k.name = g_strdup (attachment);
	at = key_insert_index (t->attach, sizeof (NameKey), k.time, &replace);
	if (replace)
	{
		NameKey *old = &g_array_index (t->attach, NameKey, at);

		g_free (old->name);
		*old = k;
	}
	else
	{
		g_array_insert_val (t->attach, at, k);
	}
	extend_duration (self, k.time);
}

void
lrg_skeleton2d_clip_add_draw_order_key (LrgSkeleton2DClip   *self,
                                        gfloat               time,
                                        const gchar * const *slots)
{
	OrderKey k;
	gboolean replace;
	guint    at;

	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));

	k.time = MAX (time, 0.0f);
	k.slots = slots != NULL ? g_strdupv ((gchar **)slots) : NULL;
	at = key_insert_index (self->order, sizeof (OrderKey), k.time, &replace);
	if (replace)
	{
		OrderKey *old = &g_array_index (self->order, OrderKey, at);

		g_strfreev (old->slots);
		*old = k;
	}
	else
	{
		g_array_insert_val (self->order, at, k);
	}
	extend_duration (self, k.time);
}

void
lrg_skeleton2d_clip_add_event_key (LrgSkeleton2DClip *self,
                                   gfloat             time,
                                   const gchar       *name)
{
	NameKey k;
	guint   at;

	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));
	g_return_if_fail (name != NULL);

	/* events may share a time: insert after every event at or before it */
	k.time = MAX (time, 0.0f);
	k.name = g_strdup (name);
	for (at = 0; at < self->events->len; at++)
	{
		if (g_array_index (self->events, NameKey, at).time > k.time)
			break;
	}
	g_array_insert_val (self->events, at, k);
	extend_duration (self, k.time);
}

gboolean
lrg_skeleton2d_clip_set_key_bezier (LrgSkeleton2DClip    *self,
                                    const gchar          *target,
                                    LrgSkeleton2DChannel  channel,
                                    guint                 index,
                                    gfloat                cx1,
                                    gfloat                cy1,
                                    gfloat                cx2,
                                    gfloat                cy2)
{
	GArray   *keys;
	ValueKey *k;

	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), FALSE);
	g_return_val_if_fail (target != NULL, FALSE);

	keys = value_keys (self, target, channel, FALSE);
	if (keys == NULL || index >= keys->len)
		return FALSE;
	k = &g_array_index (keys, ValueKey, index);
	k->curve = LRG_SKELETON2D_CURVE_BEZIER;
	/* X stays in 0 .. 1 so the timing curve is a function of time */
	k->bez[0] = CLAMP (cx1, 0.0f, 1.0f);
	k->bez[1] = cy1;
	k->bez[2] = CLAMP (cx2, 0.0f, 1.0f);
	k->bez[3] = cy2;
	return TRUE;
}

guint
lrg_skeleton2d_clip_get_key_count (LrgSkeleton2DClip    *self,
                                   const gchar          *target,
                                   LrgSkeleton2DChannel  channel)
{
	GArray *keys;

	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), 0);
	g_return_val_if_fail (target != NULL, 0);

	if (channel == LRG_SKELETON2D_CHANNEL_ATTACHMENT)
	{
		SlotTrack *t = find_slot_track (self, target, FALSE);

		return t != NULL && t->attach != NULL ? t->attach->len : 0;
	}
	keys = value_keys (self, target, channel, FALSE);
	return keys != NULL ? keys->len : 0;
}

gboolean
lrg_skeleton2d_clip_get_key (LrgSkeleton2DClip    *self,
                             const gchar          *target,
                             LrgSkeleton2DChannel  channel,
                             guint                 index,
                             gfloat               *time,
                             gfloat               *values,
                             LrgSkeleton2DCurve   *curve)
{
	GArray         *keys;
	const ValueKey *k;

	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), FALSE);
	g_return_val_if_fail (target != NULL, FALSE);

	keys = value_keys (self, target, channel, FALSE);
	if (keys == NULL || index >= keys->len)
		return FALSE;
	k = &g_array_index (keys, ValueKey, index);
	if (time != NULL)
		*time = k->time;
	if (values != NULL)
		memcpy (values, k->v, sizeof k->v);
	if (curve != NULL)
		*curve = k->curve;
	return TRUE;
}

gboolean
lrg_skeleton2d_clip_get_attachment_key (LrgSkeleton2DClip  *self,
                                        const gchar        *slot,
                                        guint               index,
                                        gfloat             *time,
                                        const gchar       **attachment)
{
	SlotTrack     *t;
	const NameKey *k;

	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), FALSE);
	g_return_val_if_fail (slot != NULL, FALSE);

	t = find_slot_track (self, slot, FALSE);
	if (t == NULL || t->attach == NULL || index >= t->attach->len)
		return FALSE;
	k = &g_array_index (t->attach, NameKey, index);
	if (time != NULL)
		*time = k->time;
	if (attachment != NULL)
		*attachment = k->name;
	return TRUE;
}

gboolean
lrg_skeleton2d_clip_remove_key (LrgSkeleton2DClip    *self,
                                const gchar          *target,
                                LrgSkeleton2DChannel  channel,
                                guint                 index)
{
	GArray *keys;

	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), FALSE);
	g_return_val_if_fail (target != NULL, FALSE);

	if (channel == LRG_SKELETON2D_CHANNEL_ATTACHMENT)
	{
		SlotTrack *t = find_slot_track (self, target, FALSE);

		keys = t != NULL ? t->attach : NULL;
	}
	else
	{
		keys = value_keys (self, target, channel, FALSE);
	}
	if (keys == NULL || index >= keys->len)
		return FALSE;
	g_array_remove_index (keys, index);
	return TRUE;
}

/* The eased fraction of a cubic Bézier timing curve with control points
 * (x1, y1), (x2, y2) between (0, 0) and (1, 1) at time fraction @f:
 * solve x(s) = f for s (Newton, then bisection), return y(s). */
static gfloat
bezier_ease (const gfloat *bez,
             gfloat        f)
{
	gfloat x1 = bez[0];
	gfloat y1 = bez[1];
	gfloat x2 = bez[2];
	gfloat y2 = bez[3];
	gfloat s = f;
	gfloat lo = 0.0f;
	gfloat hi = 1.0f;
	gfloat u;
	guint  i;

	for (i = 0; i < 8; i++)
	{
		gfloat inv = 1.0f - s;
		gfloat x = 3.0f * inv * inv * s * x1 + 3.0f * inv * s * s * x2 + s * s * s;
		gfloat dx = 3.0f * inv * inv * x1 + 6.0f * inv * s * (x2 - x1) + 3.0f * s * s * (1.0f - x2);

		if (fabsf (x - f) < 1e-6f)
			break;
		if (fabsf (dx) < 1e-6f)
		{
			/* flat derivative: fall back to bisection */
			for (i = 0; i < 24; i++)
			{
				gfloat m = 0.5f * (lo + hi);
				gfloat im = 1.0f - m;
				gfloat xm = 3.0f * im * im * m * x1 + 3.0f * im * m * m * x2 + m * m * m;

				if (xm < f)
					lo = m;
				else
					hi = m;
			}
			s = 0.5f * (lo + hi);
			break;
		}
		s -= (x - f) / dx;
		s = CLAMP (s, 0.0f, 1.0f);
	}
	u = 1.0f - s;
	return 3.0f * u * u * s * y1 + 3.0f * u * s * s * y2 + s * s * s;
}

/* Samples a time-sorted ValueKey array at @t into @out (@n values). */
static void
sample_value_keys (GArray *keys,
                   gfloat  t,
                   guint   n,
                   gfloat *out)
{
	const ValueKey *k = (const ValueKey *)(gconstpointer)keys->data;
	guint           lo;
	guint           hi;
	guint           i;
	gfloat          f;

	if (t <= k[0].time || keys->len == 1)
	{
		memcpy (out, k[0].v, sizeof (gfloat) * n);
		return;
	}
	if (t >= k[keys->len - 1].time)
	{
		memcpy (out, k[keys->len - 1].v, sizeof (gfloat) * n);
		return;
	}

	/* binary search: k[lo].time <= t < k[hi].time */
	lo = 0;
	hi = keys->len - 1;
	while (hi - lo > 1)
	{
		guint mid = (lo + hi) / 2;

		if (k[mid].time <= t)
			lo = mid;
		else
			hi = mid;
	}

	f = (t - k[lo].time) / (k[hi].time - k[lo].time);
	switch (k[lo].curve)
	{
	case LRG_SKELETON2D_CURVE_STEPPED:
		f = 0.0f;
		break;
	case LRG_SKELETON2D_CURVE_BEZIER:
		f = bezier_ease (k[lo].bez, f);
		break;
	case LRG_SKELETON2D_CURVE_LINEAR:
	default:
		break;
	}
	for (i = 0; i < n; i++)
		out[i] = k[lo].v[i] + (k[hi].v[i] - k[lo].v[i]) * f;
}

/* index of the last NameKey / OrderKey at or before @t, or -1 */
static gint
last_key_at (GArray *keys,
             gsize   stride,
             gfloat  t)
{
	gint i;

	for (i = (gint)keys->len - 1; i >= 0; i--)
	{
		gfloat kt = *(const gfloat *)(gconstpointer)(keys->data + (gsize)i * stride);

		if (kt <= t + KEY_TIME_EPSILON)
			return i;
	}
	return -1;
}

static guint
channel_width (LrgSkeleton2DChannel channel)
{
	switch (channel)
	{
	case LRG_SKELETON2D_CHANNEL_ROTATE:
		return 1;
	case LRG_SKELETON2D_CHANNEL_TRANSLATE:
	case LRG_SKELETON2D_CHANNEL_SCALE:
		return 2;
	case LRG_SKELETON2D_CHANNEL_COLOR:
		return 4;
	case LRG_SKELETON2D_CHANNEL_ATTACHMENT:
	default:
		return 0;
	}
}

gboolean
lrg_skeleton2d_clip_sample (LrgSkeleton2DClip    *self,
                            const gchar          *target,
                            LrgSkeleton2DChannel  channel,
                            gfloat                time,
                            gfloat               *values)
{
	GArray *keys;

	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), FALSE);
	g_return_val_if_fail (target != NULL, FALSE);
	g_return_val_if_fail (values != NULL, FALSE);

	memset (values, 0, sizeof (gfloat) * 4);
	keys = value_keys (self, target, channel, FALSE);
	if (keys == NULL || keys->len == 0)
		return FALSE;
	sample_value_keys (keys, CLAMP (time, 0.0f, self->duration), channel_width (channel), values);
	return TRUE;
}

gchar **
lrg_skeleton2d_clip_get_targets (LrgSkeleton2DClip *self,
                                 gboolean           slots)
{
	GPtrArray *tracks;
	GStrvBuilder *b;
	guint      i;

	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), NULL);

	b = g_strv_builder_new ();
	tracks = slots ? self->slot_tracks : self->bone_tracks;
	for (i = 0; i < tracks->len; i++)
	{
		/* both track structs start with the name */
		const gchar *name = slots ? ((SlotTrack *)g_ptr_array_index (tracks, i))->name
		                          : ((BoneTrack *)g_ptr_array_index (tracks, i))->name;

		g_strv_builder_add (b, name);
	}
	{
		gchar **out = g_strv_builder_end (b);

		g_strv_builder_unref (b);
		return out;
	}
}

guint
lrg_skeleton2d_clip_get_draw_order_key_count (LrgSkeleton2DClip *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), 0);

	return self->order->len;
}

const gchar * const *
lrg_skeleton2d_clip_get_draw_order_key (LrgSkeleton2DClip *self,
                                        guint              index,
                                        gfloat            *time)
{
	const OrderKey *k;

	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), NULL);

	if (index >= self->order->len)
		return NULL;
	k = &g_array_index (self->order, OrderKey, index);
	if (time != NULL)
		*time = k->time;
	return (const gchar * const *)k->slots;
}

guint
lrg_skeleton2d_clip_get_event_count (LrgSkeleton2DClip *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), 0);

	return self->events->len;
}

const gchar *
lrg_skeleton2d_clip_get_event (LrgSkeleton2DClip *self,
                               guint              index,
                               gfloat            *time)
{
	const NameKey *k;

	g_return_val_if_fail (LRG_IS_SKELETON2D_CLIP (self), NULL);

	if (index >= self->events->len)
		return NULL;
	k = &g_array_index (self->events, NameKey, index);
	if (time != NULL)
		*time = k->time;
	return k->name;
}

/* ================================================================== */
/* LrgSkeleton2D                                                       */
/* ================================================================== */

/* setup / local pose layout */
enum { P_X, P_Y, P_ROT, P_SX, P_SY, P_N };

typedef struct
{
	gchar  *name;
	gint    parent;
	gfloat  setup[P_N];
	gfloat  local[P_N];
	gfloat  length;
	gfloat  world[6];
	gfloat  inv_bind[6];
} Bone;

typedef struct
{
	gchar  *name;
	gint    bone;
	gchar  *setup_attachment;
	gchar  *attachment;
	gfloat  setup_color[4];
	gfloat  color[4];
} Slot;

/* One attachment of a skin. */
typedef struct
{
	gint              slot;
	gchar            *name;
	LrgSkinnedSprite *sprite;
} SkinEntry;

typedef struct
{
	gchar     *name;
	GPtrArray *entries;   /* SkinEntry*, insertion order (owns) */
	GPtrArray *per_slot;  /* GHashTable* (name -> SkinEntry*) per slot, NULL until used */
} Skin;

struct _LrgSkeleton2D
{
	GObject     parent_instance;

	gchar      *name;
	gchar      *base_dir;

	GArray     *bones;        /* Bone */
	GHashTable *bone_index;   /* name (borrowed) -> index + 1 */
	GArray     *slots;        /* Slot */
	GHashTable *slot_index;   /* name (borrowed) -> index + 1 */
	GArray     *draw_order;   /* gint */

	GPtrArray  *skins;        /* Skin*, [0] = default */
	guint       active_skin;

	GPtrArray  *clips;        /* LrgSkeleton2DClip* */

	gfloat      root[6];
	GArray     *palette;      /* gfloat, 6 per bone */
	gboolean    bind_dirty;
};

G_DEFINE_FINAL_TYPE (LrgSkeleton2D, lrg_skeleton2d, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_NAME,
	PROP_BASE_DIR,
	PROP_SKIN,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
bone_clear (gpointer data)
{
	Bone *b = data;

	g_clear_pointer (&b->name, g_free);
}

static void
slot_clear (gpointer data)
{
	Slot *s = data;

	g_clear_pointer (&s->name, g_free);
	g_clear_pointer (&s->setup_attachment, g_free);
	g_clear_pointer (&s->attachment, g_free);
}

static void
skin_entry_free (gpointer data)
{
	SkinEntry *e = data;

	g_clear_object (&e->sprite);
	g_free (e->name);
	g_free (e);
}

static void
skin_slot_table_free (gpointer data)
{
	if (data != NULL)
		g_hash_table_unref (data);
}

static Skin *
skin_new (const gchar *name)
{
	Skin *s = g_new0 (Skin, 1);

	s->name = g_strdup (name);
	s->entries = g_ptr_array_new_with_free_func (skin_entry_free);
	s->per_slot = g_ptr_array_new_with_free_func (skin_slot_table_free);
	return s;
}

static void
skin_free (gpointer data)
{
	Skin *s = data;

	/* lookup tables first: they borrow the entries */
	g_ptr_array_unref (s->per_slot);
	g_ptr_array_unref (s->entries);
	g_free (s->name);
	g_free (s);
}

static void
emit_changed (LrgSkeleton2D *self)
{
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
lrg_skeleton2d_finalize (GObject *object)
{
	LrgSkeleton2D *self = LRG_SKELETON2D (object);

	g_clear_pointer (&self->name, g_free);
	g_clear_pointer (&self->base_dir, g_free);
	g_clear_pointer (&self->bone_index, g_hash_table_unref);
	g_clear_pointer (&self->slot_index, g_hash_table_unref);
	g_clear_pointer (&self->bones, g_array_unref);
	g_clear_pointer (&self->slots, g_array_unref);
	g_clear_pointer (&self->draw_order, g_array_unref);
	g_clear_pointer (&self->skins, g_ptr_array_unref);
	g_clear_pointer (&self->clips, g_ptr_array_unref);
	g_clear_pointer (&self->palette, g_array_unref);

	G_OBJECT_CLASS (lrg_skeleton2d_parent_class)->finalize (object);
}

static void
lrg_skeleton2d_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgSkeleton2D *self = LRG_SKELETON2D (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case PROP_BASE_DIR:
		g_value_set_string (value, self->base_dir);
		break;
	case PROP_SKIN:
		g_value_set_string (value, lrg_skeleton2d_get_skin (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_skeleton2d_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgSkeleton2D *self = LRG_SKELETON2D (object);

	switch (prop_id)
	{
	case PROP_NAME:
		lrg_skeleton2d_set_name (self, g_value_get_string (value));
		break;
	case PROP_BASE_DIR:
		lrg_skeleton2d_set_base_dir (self, g_value_get_string (value));
		break;
	case PROP_SKIN:
		lrg_skeleton2d_set_skin (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_skeleton2d_class_init (LrgSkeleton2DClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_skeleton2d_finalize;
	object_class->get_property = lrg_skeleton2d_get_property;
	object_class->set_property = lrg_skeleton2d_set_property;

	/**
	 * LrgSkeleton2D:name:
	 *
	 * The skeleton's name (informational; the `name:` key of the file).
	 */
	properties[PROP_NAME] =
		g_param_spec_string ("name", "Name", "Skeleton name", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2D:base-dir:
	 *
	 * Directory relative image paths are resolved against; set to the
	 * file's directory by lrg_skeleton2d_new_from_file().
	 */
	properties[PROP_BASE_DIR] =
		g_param_spec_string ("base-dir", "Base directory", "Directory image paths resolve against", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSkeleton2D:skin:
	 *
	 * The active skin's name.
	 */
	properties[PROP_SKIN] =
		g_param_spec_string ("skin", "Skin", "Active skin", LRG_SKELETON2D_DEFAULT_SKIN,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgSkeleton2D::changed:
	 * @self: the skeleton
	 *
	 * Emitted after a structural edit: a bone, slot, skin, attachment or
	 * clip was added, removed or replaced, or a setup pose, setup
	 * attachment, setup colour or bone length changed.  Posing (current
	 * pose, current attachment, draw order) does not emit it.  The
	 * rigging tool and inspector listen to it.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_skeleton2d_init (LrgSkeleton2D *self)
{
	self->bones = g_array_new (FALSE, TRUE, sizeof (Bone));
	g_array_set_clear_func (self->bones, bone_clear);
	self->bone_index = g_hash_table_new (g_str_hash, g_str_equal);
	self->slots = g_array_new (FALSE, TRUE, sizeof (Slot));
	g_array_set_clear_func (self->slots, slot_clear);
	self->slot_index = g_hash_table_new (g_str_hash, g_str_equal);
	self->draw_order = g_array_new (FALSE, FALSE, sizeof (gint));
	self->skins = g_ptr_array_new_with_free_func (skin_free);
	g_ptr_array_add (self->skins, skin_new (LRG_SKELETON2D_DEFAULT_SKIN));
	self->clips = g_ptr_array_new_with_free_func (g_object_unref);
	self->palette = g_array_new (FALSE, TRUE, sizeof (gfloat));
	affine_identity (self->root);
	self->bind_dirty = TRUE;
}

LrgSkeleton2D *
lrg_skeleton2d_new (void)
{
	return g_object_new (LRG_TYPE_SKELETON2D, NULL);
}

const gchar *
lrg_skeleton2d_get_name (LrgSkeleton2D *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	return self->name;
}

void
lrg_skeleton2d_set_name (LrgSkeleton2D *self,
                         const gchar   *name)
{
	g_return_if_fail (LRG_IS_SKELETON2D (self));

	if (name != NULL && name[0] == '\0')
		name = NULL;
	if (g_strcmp0 (self->name, name) == 0)
		return;
	g_free (self->name);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
}

const gchar *
lrg_skeleton2d_get_base_dir (LrgSkeleton2D *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	return self->base_dir;
}

void
lrg_skeleton2d_set_base_dir (LrgSkeleton2D *self,
                             const gchar   *base_dir)
{
	g_return_if_fail (LRG_IS_SKELETON2D (self));

	if (g_strcmp0 (self->base_dir, base_dir) == 0)
		return;
	g_free (self->base_dir);
	self->base_dir = g_strdup (base_dir);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BASE_DIR]);
}

/* ------------------------------------------------------------------ */
/* Bones                                                               */
/* ------------------------------------------------------------------ */

static Bone *
get_bone (LrgSkeleton2D *self,
          gint           bone)
{
	if (bone < 0 || (guint)bone >= self->bones->len)
		return NULL;
	return &g_array_index (self->bones, Bone, bone);
}

static Slot *
get_slot (LrgSkeleton2D *self,
          gint           slot)
{
	if (slot < 0 || (guint)slot >= self->slots->len)
		return NULL;
	return &g_array_index (self->slots, Slot, slot);
}

/* Records name -> index in a lookup table.  The key is the bone's or
 * slot's own name string: GArray growth moves the structs but not the
 * strings they point at, and bones and slots are never removed or
 * renamed, so the borrowed keys stay valid. */
static void
index_name (GHashTable  *table,
            const gchar *name,
            guint        index)
{
	g_hash_table_insert (table, (gpointer)name, GUINT_TO_POINTER (index + 1));
}

gint
lrg_skeleton2d_add_bone (LrgSkeleton2D *self,
                         const gchar   *name,
                         gint           parent,
                         gfloat         x,
                         gfloat         y,
                         gfloat         rotation,
                         gfloat         scale_x,
                         gfloat         scale_y,
                         gfloat         length)
{
	Bone b;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), -1);

	if (name == NULL || name[0] == '\0' || g_hash_table_contains (self->bone_index, name))
		return -1;
	if (parent < -1 || parent >= (gint)self->bones->len)
		return -1;

	memset (&b, 0, sizeof b);
	b.name = g_strdup (name);
	b.parent = parent;
	b.setup[P_X] = x;
	b.setup[P_Y] = y;
	b.setup[P_ROT] = rotation;
	b.setup[P_SX] = scale_x;
	b.setup[P_SY] = scale_y;
	memcpy (b.local, b.setup, sizeof b.local);
	b.length = MAX (length, 0.0f);
	affine_identity (b.world);
	affine_identity (b.inv_bind);
	g_array_append_val (self->bones, b);
	index_name (self->bone_index, b.name, self->bones->len - 1);
	self->bind_dirty = TRUE;
	emit_changed (self);
	return (gint)self->bones->len - 1;
}

guint
lrg_skeleton2d_get_bone_count (LrgSkeleton2D *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), 0);

	return self->bones->len;
}

gint
lrg_skeleton2d_find_bone (LrgSkeleton2D *self,
                          const gchar   *name)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), -1);

	if (name == NULL)
		return -1;
	return (gint)GPOINTER_TO_UINT (g_hash_table_lookup (self->bone_index, name)) - 1;
}

const gchar *
lrg_skeleton2d_get_bone_name (LrgSkeleton2D *self,
                              gint           bone)
{
	Bone *b;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	b = get_bone (self, bone);
	return b != NULL ? b->name : NULL;
}

gint
lrg_skeleton2d_get_bone_parent (LrgSkeleton2D *self,
                                gint           bone)
{
	Bone *b;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), -1);

	b = get_bone (self, bone);
	return b != NULL ? b->parent : -1;
}

gfloat
lrg_skeleton2d_get_bone_length (LrgSkeleton2D *self,
                                gint           bone)
{
	Bone *b;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), 0.0f);

	b = get_bone (self, bone);
	return b != NULL ? b->length : 0.0f;
}

void
lrg_skeleton2d_set_bone_length (LrgSkeleton2D *self,
                                gint           bone,
                                gfloat         length)
{
	Bone *b;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	b = get_bone (self, bone);
	if (b == NULL)
		return;
	b->length = MAX (length, 0.0f);
	emit_changed (self);
}

static void
read_pose (const gfloat *p,
           gfloat       *x,
           gfloat       *y,
           gfloat       *rotation,
           gfloat       *scale_x,
           gfloat       *scale_y)
{
	if (x != NULL)
		*x = p[P_X];
	if (y != NULL)
		*y = p[P_Y];
	if (rotation != NULL)
		*rotation = p[P_ROT];
	if (scale_x != NULL)
		*scale_x = p[P_SX];
	if (scale_y != NULL)
		*scale_y = p[P_SY];
}

gboolean
lrg_skeleton2d_get_bone_setup (LrgSkeleton2D *self,
                               gint           bone,
                               gfloat        *x,
                               gfloat        *y,
                               gfloat        *rotation,
                               gfloat        *scale_x,
                               gfloat        *scale_y)
{
	Bone *b;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);

	b = get_bone (self, bone);
	if (b == NULL)
		return FALSE;
	read_pose (b->setup, x, y, rotation, scale_x, scale_y);
	return TRUE;
}

void
lrg_skeleton2d_set_bone_setup (LrgSkeleton2D *self,
                               gint           bone,
                               gfloat         x,
                               gfloat         y,
                               gfloat         rotation,
                               gfloat         scale_x,
                               gfloat         scale_y)
{
	Bone *b;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	b = get_bone (self, bone);
	if (b == NULL)
		return;
	b->setup[P_X] = x;
	b->setup[P_Y] = y;
	b->setup[P_ROT] = rotation;
	b->setup[P_SX] = scale_x;
	b->setup[P_SY] = scale_y;
	self->bind_dirty = TRUE;
	emit_changed (self);
}

gboolean
lrg_skeleton2d_get_bone_local (LrgSkeleton2D *self,
                               gint           bone,
                               gfloat        *x,
                               gfloat        *y,
                               gfloat        *rotation,
                               gfloat        *scale_x,
                               gfloat        *scale_y)
{
	Bone *b;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);

	b = get_bone (self, bone);
	if (b == NULL)
		return FALSE;
	read_pose (b->local, x, y, rotation, scale_x, scale_y);
	return TRUE;
}

void
lrg_skeleton2d_set_bone_local (LrgSkeleton2D *self,
                               gint           bone,
                               gfloat         x,
                               gfloat         y,
                               gfloat         rotation,
                               gfloat         scale_x,
                               gfloat         scale_y)
{
	Bone *b;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	b = get_bone (self, bone);
	if (b == NULL)
		return;
	b->local[P_X] = x;
	b->local[P_Y] = y;
	b->local[P_ROT] = rotation;
	b->local[P_SX] = scale_x;
	b->local[P_SY] = scale_y;
}

void
lrg_skeleton2d_set_root_transform (LrgSkeleton2D *self,
                                   gfloat         x,
                                   gfloat         y,
                                   gfloat         rotation,
                                   gfloat         scale_x,
                                   gfloat         scale_y)
{
	g_return_if_fail (LRG_IS_SKELETON2D (self));

	affine_from_trs (self->root, x, y, rotation, scale_x, scale_y);
}

/* Inverse bind matrices from the setup pose without the root transform
 * (the space mesh vertices are authored in). */
static void
update_bind (LrgSkeleton2D *self)
{
	guint i;

	for (i = 0; i < self->bones->len; i++)
	{
		Bone   *b = &g_array_index (self->bones, Bone, i);
		gfloat  local[6];
		gfloat  bind[6];

		affine_from_trs (local, b->setup[P_X], b->setup[P_Y], b->setup[P_ROT],
		                 b->setup[P_SX], b->setup[P_SY]);
		if (b->parent >= 0)
		{
			/* the parent's bind is world-from-bind; its inverse is stored,
			 * so recompute the forward one from the stored inverse */
			const Bone *p = &g_array_index (self->bones, Bone, b->parent);
			gfloat      pw[6];

			affine_invert (pw, p->inv_bind);
			affine_multiply (bind, pw, local);
		}
		else
		{
			memcpy (bind, local, sizeof bind);
		}
		affine_invert (b->inv_bind, bind);
	}
	self->bind_dirty = FALSE;
}

void
lrg_skeleton2d_update_world (LrgSkeleton2D *self)
{
	gfloat *pal;
	guint   i;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	if (self->bind_dirty)
		update_bind (self);

	g_array_set_size (self->palette, self->bones->len * 6);
	pal = (gfloat *)(gpointer)self->palette->data;

	/* parents precede children, so one pass in index order suffices */
	for (i = 0; i < self->bones->len; i++)
	{
		Bone         *b = &g_array_index (self->bones, Bone, i);
		const gfloat *parent;
		gfloat        local[6];

		parent = b->parent >= 0 ? g_array_index (self->bones, Bone, b->parent).world : self->root;
		affine_from_trs (local, b->local[P_X], b->local[P_Y], b->local[P_ROT],
		                 b->local[P_SX], b->local[P_SY]);
		affine_multiply (b->world, parent, local);
		affine_multiply (pal + i * 6, b->world, b->inv_bind);
	}
}

gboolean
lrg_skeleton2d_get_bone_world_matrix (LrgSkeleton2D *self,
                                      gint           bone,
                                      gfloat        *matrix)
{
	Bone *b;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);
	g_return_val_if_fail (matrix != NULL, FALSE);

	b = get_bone (self, bone);
	if (b == NULL)
		return FALSE;
	memcpy (matrix, b->world, sizeof b->world);
	return TRUE;
}

gboolean
lrg_skeleton2d_get_bone_world_position (LrgSkeleton2D *self,
                                        gint           bone,
                                        gfloat        *x,
                                        gfloat        *y)
{
	Bone *b;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);

	b = get_bone (self, bone);
	if (b == NULL)
		return FALSE;
	if (x != NULL)
		*x = b->world[4];
	if (y != NULL)
		*y = b->world[5];
	return TRUE;
}

gboolean
lrg_skeleton2d_get_bone_world_tip (LrgSkeleton2D *self,
                                   gint           bone,
                                   gfloat        *x,
                                   gfloat        *y)
{
	Bone *b;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);

	b = get_bone (self, bone);
	if (b == NULL)
		return FALSE;
	if (x != NULL)
		*x = b->world[0] * b->length + b->world[4];
	if (y != NULL)
		*y = b->world[1] * b->length + b->world[5];
	return TRUE;
}

gfloat
lrg_skeleton2d_get_bone_world_rotation (LrgSkeleton2D *self,
                                        gint           bone)
{
	Bone *b;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), 0.0f);

	b = get_bone (self, bone);
	if (b == NULL)
		return 0.0f;
	return atan2f (b->world[1], b->world[0]) * RAD2DEG_F;
}

const gfloat *
lrg_skeleton2d_get_palette (LrgSkeleton2D *self,
                            guint         *n_floats)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	if (self->palette->len != self->bones->len * 6)
		lrg_skeleton2d_update_world (self);
	if (n_floats != NULL)
		*n_floats = self->palette->len;
	return (const gfloat *)(gconstpointer)self->palette->data;
}

/* distance from (px, py) to segment (ax, ay)-(bx, by) */
static gfloat
point_segment_distance (gfloat px,
                        gfloat py,
                        gfloat ax,
                        gfloat ay,
                        gfloat bx,
                        gfloat by)
{
	gfloat dx = bx - ax;
	gfloat dy = by - ay;
	gfloat len2 = dx * dx + dy * dy;
	gfloat t = 0.0f;
	gfloat cx;
	gfloat cy;

	if (len2 > 1e-12f)
		t = CLAMP (((px - ax) * dx + (py - ay) * dy) / len2, 0.0f, 1.0f);
	cx = ax + t * dx - px;
	cy = ay + t * dy - py;
	return sqrtf (cx * cx + cy * cy);
}

gint
lrg_skeleton2d_pick_bone (LrgSkeleton2D *self,
                          gfloat         x,
                          gfloat         y,
                          gfloat         radius)
{
	gint   best = -1;
	gfloat best_d = radius;
	guint  i;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), -1);

	for (i = 0; i < self->bones->len; i++)
	{
		gfloat ox;
		gfloat oy;
		gfloat tx;
		gfloat ty;
		gfloat d;

		lrg_skeleton2d_get_bone_world_position (self, (gint)i, &ox, &oy);
		lrg_skeleton2d_get_bone_world_tip (self, (gint)i, &tx, &ty);
		d = point_segment_distance (x, y, ox, oy, tx, ty);
		if (d <= best_d)
		{
			best_d = d;
			best = (gint)i;
		}
	}
	return best;
}

/* ------------------------------------------------------------------ */
/* Slots and draw order                                                */
/* ------------------------------------------------------------------ */

gint
lrg_skeleton2d_add_slot (LrgSkeleton2D *self,
                         const gchar   *name,
                         gint           bone,
                         const gchar   *attachment)
{
	Slot s;
	gint index;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), -1);

	if (name == NULL || name[0] == '\0' || g_hash_table_contains (self->slot_index, name))
		return -1;
	if (get_bone (self, bone) == NULL)
		return -1;

	memset (&s, 0, sizeof s);
	s.name = g_strdup (name);
	s.bone = bone;
	s.setup_attachment = g_strdup (attachment);
	s.attachment = g_strdup (attachment);
	s.setup_color[0] = s.setup_color[1] = s.setup_color[2] = s.setup_color[3] = 1.0f;
	memcpy (s.color, s.setup_color, sizeof s.color);
	g_array_append_val (self->slots, s);
	index = (gint)self->slots->len - 1;
	index_name (self->slot_index, s.name, (guint)index);
	g_array_append_val (self->draw_order, index);
	emit_changed (self);
	return index;
}

guint
lrg_skeleton2d_get_slot_count (LrgSkeleton2D *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), 0);

	return self->slots->len;
}

gint
lrg_skeleton2d_find_slot (LrgSkeleton2D *self,
                          const gchar   *name)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), -1);

	if (name == NULL)
		return -1;
	return (gint)GPOINTER_TO_UINT (g_hash_table_lookup (self->slot_index, name)) - 1;
}

const gchar *
lrg_skeleton2d_get_slot_name (LrgSkeleton2D *self,
                              gint           slot)
{
	Slot *s;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	s = get_slot (self, slot);
	return s != NULL ? s->name : NULL;
}

gint
lrg_skeleton2d_get_slot_bone (LrgSkeleton2D *self,
                              gint           slot)
{
	Slot *s;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), -1);

	s = get_slot (self, slot);
	return s != NULL ? s->bone : -1;
}

const gchar *
lrg_skeleton2d_get_slot_attachment (LrgSkeleton2D *self,
                                    gint           slot)
{
	Slot *s;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	s = get_slot (self, slot);
	return s != NULL ? s->attachment : NULL;
}

/* replaces *dst with a copy of @src unless equal (no allocation when the
 * same attachment is keyed every frame) */
static void
set_name (gchar       **dst,
          const gchar  *src)
{
	if (g_strcmp0 (*dst, src) == 0)
		return;
	g_free (*dst);
	*dst = g_strdup (src);
}

void
lrg_skeleton2d_set_slot_attachment (LrgSkeleton2D *self,
                                    gint           slot,
                                    const gchar   *attachment)
{
	Slot *s;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	s = get_slot (self, slot);
	if (s != NULL)
		set_name (&s->attachment, attachment);
}

const gchar *
lrg_skeleton2d_get_slot_setup_attachment (LrgSkeleton2D *self,
                                          gint           slot)
{
	Slot *s;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	s = get_slot (self, slot);
	return s != NULL ? s->setup_attachment : NULL;
}

void
lrg_skeleton2d_set_slot_setup_attachment (LrgSkeleton2D *self,
                                          gint           slot,
                                          const gchar   *attachment)
{
	Slot *s;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	s = get_slot (self, slot);
	if (s == NULL)
		return;
	set_name (&s->setup_attachment, attachment);
	set_name (&s->attachment, attachment);
	emit_changed (self);
}

gboolean
lrg_skeleton2d_get_slot_color (LrgSkeleton2D *self,
                               gint           slot,
                               gfloat        *rgba)
{
	Slot *s;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);
	g_return_val_if_fail (rgba != NULL, FALSE);

	s = get_slot (self, slot);
	if (s == NULL)
		return FALSE;
	memcpy (rgba, s->color, sizeof s->color);
	return TRUE;
}

void
lrg_skeleton2d_set_slot_color (LrgSkeleton2D *self,
                               gint           slot,
                               gfloat         r,
                               gfloat         g,
                               gfloat         b,
                               gfloat         a)
{
	Slot *s;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	s = get_slot (self, slot);
	if (s == NULL)
		return;
	s->color[0] = r;
	s->color[1] = g;
	s->color[2] = b;
	s->color[3] = a;
}

void
lrg_skeleton2d_set_slot_setup_color (LrgSkeleton2D *self,
                                     gint           slot,
                                     gfloat         r,
                                     gfloat         g,
                                     gfloat         b,
                                     gfloat         a)
{
	Slot *s;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	s = get_slot (self, slot);
	if (s == NULL)
		return;
	s->setup_color[0] = r;
	s->setup_color[1] = g;
	s->setup_color[2] = b;
	s->setup_color[3] = a;
	memcpy (s->color, s->setup_color, sizeof s->color);
	emit_changed (self);
}

const gint *
lrg_skeleton2d_get_draw_order (LrgSkeleton2D *self,
                               guint         *n_slots)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	if (n_slots != NULL)
		*n_slots = self->draw_order->len;
	return (const gint *)(gconstpointer)self->draw_order->data;
}

gboolean
lrg_skeleton2d_set_draw_order (LrgSkeleton2D *self,
                               const gint    *order,
                               guint          n_slots)
{
	g_autofree gboolean *seen = NULL;
	guint                i;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);

	if (n_slots != self->slots->len || (n_slots > 0 && order == NULL))
		return FALSE;
	seen = g_new0 (gboolean, n_slots + 1);
	for (i = 0; i < n_slots; i++)
	{
		if (order[i] < 0 || (guint)order[i] >= n_slots || seen[order[i]])
			return FALSE;
		seen[order[i]] = TRUE;
	}
	g_array_set_size (self->draw_order, 0);
	g_array_append_vals (self->draw_order, order, n_slots);
	return TRUE;
}

void
lrg_skeleton2d_reset_pose (LrgSkeleton2D *self)
{
	guint i;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	for (i = 0; i < self->bones->len; i++)
	{
		Bone *b = &g_array_index (self->bones, Bone, i);

		memcpy (b->local, b->setup, sizeof b->local);
	}
	for (i = 0; i < self->slots->len; i++)
	{
		Slot *s = &g_array_index (self->slots, Slot, i);

		set_name (&s->attachment, s->setup_attachment);
		memcpy (s->color, s->setup_color, sizeof s->color);
		g_array_index (self->draw_order, gint, i) = (gint)i;
	}
}

/* ------------------------------------------------------------------ */
/* Skins                                                               */
/* ------------------------------------------------------------------ */

static Skin *
find_skin (LrgSkeleton2D *self,
           const gchar   *name,
           guint         *index)
{
	guint i;

	if (name == NULL)
		name = LRG_SKELETON2D_DEFAULT_SKIN;
	for (i = 0; i < self->skins->len; i++)
	{
		Skin *s = g_ptr_array_index (self->skins, i);

		if (g_strcmp0 (s->name, name) == 0)
		{
			if (index != NULL)
				*index = i;
			return s;
		}
	}
	return NULL;
}

static SkinEntry *
skin_lookup (Skin        *skin,
             gint         slot,
             const gchar *name)
{
	GHashTable *table;

	if (name == NULL || slot < 0 || (guint)slot >= skin->per_slot->len)
		return NULL;
	table = g_ptr_array_index (skin->per_slot, slot);
	return table != NULL ? g_hash_table_lookup (table, name) : NULL;
}

gboolean
lrg_skeleton2d_add_skin (LrgSkeleton2D *self,
                         const gchar   *name)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);

	if (name == NULL || name[0] == '\0')
		return FALSE;
	if (find_skin (self, name, NULL) != NULL)
		return TRUE;
	g_ptr_array_add (self->skins, skin_new (name));
	emit_changed (self);
	return TRUE;
}

guint
lrg_skeleton2d_get_skin_count (LrgSkeleton2D *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), 0);

	return self->skins->len;
}

const gchar *
lrg_skeleton2d_get_skin_name (LrgSkeleton2D *self,
                              guint          index)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	if (index >= self->skins->len)
		return NULL;
	return ((Skin *)g_ptr_array_index (self->skins, index))->name;
}

gboolean
lrg_skeleton2d_set_skin (LrgSkeleton2D *self,
                         const gchar   *name)
{
	guint index;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);

	if (find_skin (self, name, &index) == NULL)
		return FALSE;
	if (index != self->active_skin)
	{
		self->active_skin = index;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SKIN]);
	}
	return TRUE;
}

const gchar *
lrg_skeleton2d_get_skin (LrgSkeleton2D *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	return ((Skin *)g_ptr_array_index (self->skins, self->active_skin))->name;
}

gboolean
lrg_skeleton2d_set_attachment (LrgSkeleton2D    *self,
                               const gchar      *skin,
                               gint              slot,
                               const gchar      *name,
                               LrgSkinnedSprite *sprite)
{
	Skin       *s;
	SkinEntry  *e;
	GHashTable *table;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);
	g_return_val_if_fail (sprite == NULL || LRG_IS_SKINNED_SPRITE (sprite), FALSE);

	if (get_slot (self, slot) == NULL || name == NULL || name[0] == '\0')
		return FALSE;
	if (!lrg_skeleton2d_add_skin (self, skin != NULL ? skin : LRG_SKELETON2D_DEFAULT_SKIN))
		return FALSE;
	s = find_skin (self, skin, NULL);
	if (s == NULL)
		return FALSE;

	e = skin_lookup (s, slot, name);
	if (e != NULL)
	{
		if (sprite != NULL)
		{
			/* replace in place, keeping the insertion order */
			g_set_object (&e->sprite, sprite);
		}
		else
		{
			table = g_ptr_array_index (s->per_slot, slot);
			g_hash_table_remove (table, name);
			g_ptr_array_remove (s->entries, e);
		}
		emit_changed (self);
		return TRUE;
	}
	if (sprite == NULL)
		return TRUE;

	e = g_new0 (SkinEntry, 1);
	e->slot = slot;
	e->name = g_strdup (name);
	e->sprite = g_object_ref (sprite);
	g_ptr_array_add (s->entries, e);
	if (s->per_slot->len <= (guint)slot)
		g_ptr_array_set_size (s->per_slot, slot + 1);
	table = g_ptr_array_index (s->per_slot, slot);
	if (table == NULL)
	{
		table = g_hash_table_new (g_str_hash, g_str_equal);
		g_ptr_array_index (s->per_slot, slot) = table;
	}
	g_hash_table_insert (table, e->name, e);
	emit_changed (self);
	return TRUE;
}

LrgSkinnedSprite *
lrg_skeleton2d_get_skin_attachment (LrgSkeleton2D *self,
                                    const gchar   *skin,
                                    gint           slot,
                                    const gchar   *name)
{
	Skin      *s;
	SkinEntry *e;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	s = find_skin (self, skin, NULL);
	if (s == NULL)
		return NULL;
	e = skin_lookup (s, slot, name);
	return e != NULL ? e->sprite : NULL;
}

/* active skin, then the default skin */
static LrgSkinnedSprite *
resolve_attachment (LrgSkeleton2D *self,
                    gint           slot,
                    const gchar   *name)
{
	SkinEntry *e = NULL;

	if (name == NULL)
		return NULL;
	if (self->active_skin != 0)
		e = skin_lookup (g_ptr_array_index (self->skins, self->active_skin), slot, name);
	if (e == NULL)
		e = skin_lookup (g_ptr_array_index (self->skins, 0), slot, name);
	return e != NULL ? e->sprite : NULL;
}

LrgSkinnedSprite *
lrg_skeleton2d_get_attachment (LrgSkeleton2D *self,
                               gint           slot,
                               const gchar   *name)
{
	Slot *s;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	s = get_slot (self, slot);
	if (s == NULL)
		return NULL;
	return resolve_attachment (self, slot, name != NULL ? name : s->attachment);
}

gchar **
lrg_skeleton2d_list_attachments (LrgSkeleton2D *self,
                                 const gchar   *skin,
                                 gint           slot)
{
	GStrvBuilder *b;
	Skin         *s;
	gchar       **out;
	guint         i;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	b = g_strv_builder_new ();
	s = find_skin (self, skin, NULL);
	for (i = 0; s != NULL && i < s->entries->len; i++)
	{
		SkinEntry *e = g_ptr_array_index (s->entries, i);

		if (e->slot == slot)
			g_strv_builder_add (b, e->name);
	}
	out = g_strv_builder_end (b);
	g_strv_builder_unref (b);
	return out;
}

gboolean
lrg_skeleton2d_compute_auto_weights (LrgSkeleton2D    *self,
                                     LrgSkinnedSprite *sprite,
                                     const gint       *bones,
                                     guint             n_bones,
                                     gfloat            falloff,
                                     guint             max_influences,
                                     guint             smooth_iterations)
{
	g_autofree gfloat *segs = NULL;
	g_autofree gint   *ids = NULL;
	guint              n;
	guint              i;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);
	g_return_val_if_fail (LRG_IS_SKINNED_SPRITE (sprite), FALSE);

	if (self->bind_dirty)
		update_bind (self);

	n = bones != NULL ? n_bones : self->bones->len;
	if (n == 0)
		return FALSE;
	segs = g_new (gfloat, (gsize)n * 4);
	ids = g_new (gint, n);
	for (i = 0; i < n; i++)
	{
		gint        id = bones != NULL ? bones[i] : (gint)i;
		const Bone *b = get_bone (self, id);
		gfloat      bind[6];

		if (b == NULL)
			return FALSE;
		/* setup-space segment: bind matrix applied to (0,0) and (len,0) */
		affine_invert (bind, b->inv_bind);
		segs[i * 4] = bind[4];
		segs[i * 4 + 1] = bind[5];
		segs[i * 4 + 2] = bind[0] * b->length + bind[4];
		segs[i * 4 + 3] = bind[1] * b->length + bind[5];
		ids[i] = id;
	}
	return lrg_skinned_sprite_compute_auto_weights (sprite, segs, n * 4, ids, n, falloff,
	                                                max_influences, smooth_iterations);
}

/* ------------------------------------------------------------------ */
/* Clips                                                               */
/* ------------------------------------------------------------------ */

static gint
clip_index (LrgSkeleton2D *self,
            const gchar   *name)
{
	guint i;

	for (i = 0; i < self->clips->len; i++)
	{
		if (g_strcmp0 (lrg_skeleton2d_clip_get_name (g_ptr_array_index (self->clips, i)), name) == 0)
			return (gint)i;
	}
	return -1;
}

void
lrg_skeleton2d_add_clip (LrgSkeleton2D     *self,
                         LrgSkeleton2DClip *clip)
{
	gint at;

	g_return_if_fail (LRG_IS_SKELETON2D (self));
	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (clip));

	at = clip_index (self, clip->name);
	g_object_ref (clip);
	if (at >= 0)
	{
		g_object_unref (g_ptr_array_index (self->clips, at));
		g_ptr_array_index (self->clips, at) = clip;
	}
	else
	{
		g_ptr_array_add (self->clips, clip);
	}
	emit_changed (self);
}

gboolean
lrg_skeleton2d_remove_clip (LrgSkeleton2D *self,
                            const gchar   *name)
{
	gint at;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);

	at = clip_index (self, name);
	if (at < 0)
		return FALSE;
	g_ptr_array_remove_index (self->clips, (guint)at);
	emit_changed (self);
	return TRUE;
}

LrgSkeleton2DClip *
lrg_skeleton2d_get_clip (LrgSkeleton2D *self,
                         const gchar   *name)
{
	gint at;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	at = clip_index (self, name);
	return at >= 0 ? g_ptr_array_index (self->clips, at) : NULL;
}

guint
lrg_skeleton2d_get_clip_count (LrgSkeleton2D *self)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), 0);

	return self->clips->len;
}

LrgSkeleton2DClip *
lrg_skeleton2d_get_clip_at (LrgSkeleton2D *self,
                            guint          index)
{
	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	return index < self->clips->len ? g_ptr_array_index (self->clips, index) : NULL;
}

/* ---- applying a clip ---- */

void
lrg_skeleton2d_clip_apply (LrgSkeleton2DClip *self,
                           LrgSkeleton2D     *skeleton,
                           gfloat             time,
                           gfloat             alpha)
{
	guint i;

	g_return_if_fail (LRG_IS_SKELETON2D_CLIP (self));
	g_return_if_fail (LRG_IS_SKELETON2D (skeleton));

	alpha = CLAMP (alpha, 0.0f, 1.0f);
	if (alpha <= 0.0f)
		return;
	time = CLAMP (time, 0.0f, self->duration);

	/* bones: offsets/factors relative to the setup pose, mixed into the
	 * current pose by alpha */
	for (i = 0; i < self->bone_tracks->len; i++)
	{
		BoneTrack *t = g_ptr_array_index (self->bone_tracks, i);
		Bone      *b = get_bone (skeleton, lrg_skeleton2d_find_bone (skeleton, t->name));
		gfloat     v[4];

		if (b == NULL)
			continue;
		if (t->ch[0] != NULL && t->ch[0]->len > 0)
		{
			sample_value_keys (t->ch[0], time, 2, v);
			b->local[P_X] += (b->setup[P_X] + v[0] - b->local[P_X]) * alpha;
			b->local[P_Y] += (b->setup[P_Y] + v[1] - b->local[P_Y]) * alpha;
		}
		if (t->ch[1] != NULL && t->ch[1]->len > 0)
		{
			gfloat target;

			sample_value_keys (t->ch[1], time, 1, v);
			target = b->setup[P_ROT] + v[0];
			if (alpha >= 1.0f)
				b->local[P_ROT] = target;
			else
				b->local[P_ROT] += wrap_degrees (target - b->local[P_ROT]) * alpha;
		}
		if (t->ch[2] != NULL && t->ch[2]->len > 0)
		{
			sample_value_keys (t->ch[2], time, 2, v);
			b->local[P_SX] += (b->setup[P_SX] * v[0] - b->local[P_SX]) * alpha;
			b->local[P_SY] += (b->setup[P_SY] * v[1] - b->local[P_SY]) * alpha;
		}
	}

	/* slots: colour mixes, attachments switch at half weight */
	for (i = 0; i < self->slot_tracks->len; i++)
	{
		SlotTrack *t = g_ptr_array_index (self->slot_tracks, i);
		Slot      *s = get_slot (skeleton, lrg_skeleton2d_find_slot (skeleton, t->name));

		if (s == NULL)
			continue;
		if (t->color != NULL && t->color->len > 0)
		{
			gfloat v[4];
			guint  c;

			sample_value_keys (t->color, time, 4, v);
			for (c = 0; c < 4; c++)
				s->color[c] += (v[c] - s->color[c]) * alpha;
		}
		if (t->attach != NULL && alpha >= 0.5f)
		{
			gint k = last_key_at (t->attach, sizeof (NameKey), time);

			if (k >= 0)
				set_name (&s->attachment, g_array_index (t->attach, NameKey, k).name);
		}
	}

	/* draw order: stepped, at half weight; a key that is not a full
	 * permutation of the skeleton's slots is skipped */
	if (self->order->len > 0 && alpha >= 0.5f)
	{
		gint k = last_key_at (self->order, sizeof (OrderKey), time);

		if (k >= 0)
		{
			const OrderKey *ok = &g_array_index (self->order, OrderKey, k);
			guint           n = skeleton->slots->len;

			if (ok->slots == NULL)
			{
				for (i = 0; i < n; i++)
					g_array_index (skeleton->draw_order, gint, i) = (gint)i;
			}
			else if (g_strv_length (ok->slots) == n)
			{
				g_autofree gint *order = g_new (gint, MAX (n, 1u));

				for (i = 0; i < n; i++)
					order[i] = lrg_skeleton2d_find_slot (skeleton, ok->slots[i]);
				lrg_skeleton2d_set_draw_order (skeleton, order, n);
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

void
lrg_skeleton2d_deform (LrgSkeleton2D *self)
{
	const gfloat *pal;
	guint         n_pal;
	guint         i;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	pal = lrg_skeleton2d_get_palette (self, &n_pal);
	for (i = 0; i < self->draw_order->len; i++)
	{
		gint              si = g_array_index (self->draw_order, gint, i);
		Slot             *s = &g_array_index (self->slots, Slot, si);
		LrgSkinnedSprite *sprite = resolve_attachment (self, si, s->attachment);

		if (sprite != NULL)
			lrg_skinned_sprite_deform (sprite, pal, n_pal, s->bone);
	}
}

/* 0 .. 1 float times 0 .. 255 byte, rounded */
static guint8
mul_channel (gfloat f,
             guint8 b)
{
	gfloat v = CLAMP (f, 0.0f, 1.0f) * (gfloat)b;

	return (guint8)(v + 0.5f);
}

void
lrg_skeleton2d_draw (LrgSkeleton2D  *self,
                     const GrlColor *tint)
{
	const gfloat *pal;
	guint         n_pal;
	guint         i;
	GrlColor      t;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	if (tint != NULL)
		t = *tint;
	else
		t.r = t.g = t.b = t.a = 255;

	pal = lrg_skeleton2d_get_palette (self, &n_pal);
	for (i = 0; i < self->draw_order->len; i++)
	{
		gint              si = g_array_index (self->draw_order, gint, i);
		Slot             *s = &g_array_index (self->slots, Slot, si);
		LrgSkinnedSprite *sprite = resolve_attachment (self, si, s->attachment);
		GrlColor          c;

		if (sprite == NULL || s->color[3] <= 0.0f)
			continue;
		c.r = mul_channel (s->color[0], t.r);
		c.g = mul_channel (s->color[1], t.g);
		c.b = mul_channel (s->color[2], t.b);
		c.a = mul_channel (s->color[3], t.a);
		lrg_skinned_sprite_deform (sprite, pal, n_pal, s->bone);
		lrg_skinned_sprite_draw (sprite, &c);
	}
}

void
lrg_skeleton2d_draw_bones (LrgSkeleton2D  *self,
                           const GrlColor *color)
{
	Color c = { 255, 220, 0, 255 };
	guint i;

	g_return_if_fail (LRG_IS_SKELETON2D (self));

	if (color != NULL)
	{
		c.r = color->r;
		c.g = color->g;
		c.b = color->b;
		c.a = color->a;
	}
	for (i = 0; i < self->bones->len; i++)
	{
		Vector2 a;
		Vector2 b;

		lrg_skeleton2d_get_bone_world_position (self, (gint)i, &a.x, &a.y);
		lrg_skeleton2d_get_bone_world_tip (self, (gint)i, &b.x, &b.y);
		DrawLineEx (a, b, 2.0f, c);
		DrawCircleV (a, 3.0f, c);
	}
}

gboolean
lrg_skeleton2d_load_textures (LrgSkeleton2D  *self,
                              GError        **error)
{
	g_autoptr(GHashTable) cache = NULL;
	guint                 i;
	guint                 j;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* one texture per distinct image path */
	cache = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < self->skins->len; i++)
	{
		Skin *s = g_ptr_array_index (self->skins, i);

		for (j = 0; j < s->entries->len; j++)
		{
			SkinEntry   *e = g_ptr_array_index (s->entries, j);
			const gchar *image = lrg_skinned_sprite_get_image (e->sprite);
			GrlTexture  *tex;

			if (image == NULL || lrg_skinned_sprite_get_texture (e->sprite) != NULL)
				continue;
			tex = g_hash_table_lookup (cache, image);
			if (tex != NULL)
			{
				lrg_skinned_sprite_set_texture (e->sprite, tex);
				continue;
			}
			if (!lrg_skinned_sprite_load_texture (e->sprite, self->base_dir, error))
				return FALSE;
			g_hash_table_insert (cache, (gpointer)image, lrg_skinned_sprite_get_texture (e->sprite));
		}
	}
	return TRUE;
}

/* ================================================================== */
/* .rskel2d reader                                                     */
/* ================================================================== */

typedef struct
{
	const gchar  *source;
	GError      **error;
} Reader;

static gboolean rd_fail (Reader      *rd,
                         gint         code,
                         const gchar *path,
                         const gchar *format,
                         ...) G_GNUC_PRINTF (4, 5);

/* sets "source: path: message" and returns FALSE */
static gboolean
rd_fail (Reader      *rd,
         gint         code,
         const gchar *path,
         const gchar *format,
         ...)
{
	g_autofree gchar *msg = NULL;
	va_list           args;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error (rd->error, LRG_SKELETON2D_ERROR, code, "%s: %s: %s", rd->source, path, msg);
	return FALSE;
}

static const gchar *
node_kind (YamlNode *node)
{
	switch (yaml_node_get_node_type (node))
	{
	case YAML_NODE_MAPPING:
		return "a mapping";
	case YAML_NODE_SEQUENCE:
		return "a list";
	case YAML_NODE_NULL:
		return "null";
	case YAML_NODE_SCALAR:
	default:
		return "a scalar";
	}
}

static gboolean
rd_mapping (Reader       *rd,
            YamlNode     *node,
            const gchar  *path,
            YamlMapping **out)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return rd_fail (rd, LRG_SKELETON2D_ERROR_PARSE, path, "expected a mapping, got %s",
		                node != NULL ? node_kind (node) : "nothing");
	*out = yaml_node_get_mapping (node);
	return TRUE;
}

static gboolean
rd_sequence (Reader        *rd,
             YamlNode      *node,
             const gchar   *path,
             YamlSequence **out)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return rd_fail (rd, LRG_SKELETON2D_ERROR_PARSE, path, "expected a list, got %s",
		                node != NULL ? node_kind (node) : "nothing");
	*out = yaml_node_get_sequence (node);
	return TRUE;
}

/* every key of @map must be in @allowed (NULL-terminated) */
static gboolean
rd_keys (Reader             *rd,
         YamlMapping        *map,
         const gchar        *path,
         const gchar * const *allowed)
{
	guint i;

	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);

		if (!g_strv_contains (allowed, key))
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path, "unknown key \"%s\"", key);
	}
	return TRUE;
}

static gboolean
rd_number (Reader      *rd,
           YamlNode    *node,
           const gchar *path,
           gfloat      *out)
{
	const gchar *text;
	gchar       *end = NULL;
	gdouble      v;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return rd_fail (rd, LRG_SKELETON2D_ERROR_PARSE, path, "expected a number, got %s",
		                node != NULL ? node_kind (node) : "nothing");
	text = yaml_node_get_scalar (node);
	if (text == NULL || text[0] == '\0')
		return rd_fail (rd, LRG_SKELETON2D_ERROR_PARSE, path, "expected a number");
	v = g_ascii_strtod (text, &end);
	if (end == NULL || *end != '\0' || !isfinite (v))
		return rd_fail (rd, LRG_SKELETON2D_ERROR_PARSE, path, "expected a number, got \"%s\"", text);
	*out = (gfloat)v;
	return TRUE;
}

static gboolean
rd_index (Reader      *rd,
          YamlNode    *node,
          const gchar *path,
          guint        limit,
          guint       *out)
{
	gfloat v;

	if (!rd_number (rd, node, path, &v))
		return FALSE;
	if (v != floorf (v) || v < 0.0f || v >= (gfloat)limit)
		return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path,
		                "expected a vertex index from 0 to %u", limit > 0 ? limit - 1 : 0);
	*out = (guint)v;
	return TRUE;
}

static gboolean
rd_string (Reader       *rd,
           YamlNode     *node,
           const gchar  *path,
           const gchar **out)
{
	const gchar *text;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return rd_fail (rd, LRG_SKELETON2D_ERROR_PARSE, path, "expected a string, got %s",
		                node != NULL ? node_kind (node) : "nothing");
	text = yaml_node_get_scalar (node);
	if (text == NULL || text[0] == '\0')
		return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path, "expected a non-empty string");
	*out = text;
	return TRUE;
}

/* a string or null (*out NULL) */
static gboolean
rd_string_or_null (Reader       *rd,
                   YamlNode     *node,
                   const gchar  *path,
                   const gchar **out)
{
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_NULL)
	{
		*out = NULL;
		return TRUE;
	}
	return rd_string (rd, node, path, out);
}

static gboolean
rd_boolean (Reader      *rd,
            YamlNode    *node,
            const gchar *path,
            gboolean    *out)
{
	const gchar *text = NULL;

	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
		text = yaml_node_get_scalar (node);
	if (g_strcmp0 (text, "true") == 0)
		*out = TRUE;
	else if (g_strcmp0 (text, "false") == 0)
		*out = FALSE;
	else
		return rd_fail (rd, LRG_SKELETON2D_ERROR_PARSE, path, "expected true or false");
	return TRUE;
}

/* a list of exactly @n numbers */
static gboolean
rd_numbers (Reader      *rd,
            YamlNode    *node,
            const gchar *path,
            guint        n,
            gfloat      *out)
{
	YamlSequence *seq;
	guint         i;

	if (!rd_sequence (rd, node, path, &seq))
		return FALSE;
	if (yaml_sequence_get_length (seq) != n)
		return rd_fail (rd, LRG_SKELETON2D_ERROR_PARSE, path, "expected a list of %u numbers", n);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *p = g_strdup_printf ("%s/%u", path, i);

		if (!rd_number (rd, yaml_sequence_get_element (seq, i), p, &out[i]))
			return FALSE;
	}
	return TRUE;
}

static gboolean
rd_time (Reader      *rd,
         YamlMapping *map,
         const gchar *path,
         gfloat      *out)
{
	g_autofree gchar *p = g_strdup_printf ("%s/time", path);

	if (!yaml_mapping_has_member (map, "time"))
		return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path, "missing key \"time\"");
	if (!rd_number (rd, yaml_mapping_get_member (map, "time"), p, out))
		return FALSE;
	if (*out < 0.0f)
		return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "a time must be 0 or more");
	return TRUE;
}

/* optional "curve": stepped | linear | [cx1, cy1, cx2, cy2] */
static gboolean
rd_curve (Reader             *rd,
          YamlMapping        *map,
          const gchar        *path,
          LrgSkeleton2DCurve *curve,
          gfloat             *bez)
{
	YamlNode         *node = yaml_mapping_get_member (map, "curve");
	g_autofree gchar *p = g_strdup_printf ("%s/curve", path);

	*curve = LRG_SKELETON2D_CURVE_LINEAR;
	if (node == NULL)
		return TRUE;
	if (yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		*curve = LRG_SKELETON2D_CURVE_BEZIER;
		if (!rd_numbers (rd, node, p, 4, bez))
			return FALSE;
		if (bez[0] < 0.0f || bez[0] > 1.0f || bez[2] < 0.0f || bez[2] > 1.0f)
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "control point X values must be from 0 to 1");
		return TRUE;
	}
	if (yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
	{
		const gchar *text = yaml_node_get_scalar (node);

		if (g_strcmp0 (text, "stepped") == 0)
		{
			*curve = LRG_SKELETON2D_CURVE_STEPPED;
			return TRUE;
		}
		if (g_strcmp0 (text, "linear") == 0)
			return TRUE;
	}
	return rd_fail (rd, LRG_SKELETON2D_ERROR_PARSE, p, "expected linear, stepped or [x1, y1, x2, y2]");
}

static gboolean
read_bones (Reader        *rd,
            LrgSkeleton2D *skel,
            YamlNode      *node)
{
	static const gchar * const keys[] = { "name", "parent", "x", "y", "rotation", "scale", "length", NULL };
	YamlSequence *seq;
	guint         i;

	if (!rd_sequence (rd, node, "/bones", &seq))
		return FALSE;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar *path = g_strdup_printf ("/bones/%u", i);
		g_autofree gchar *p = NULL;
		YamlMapping      *map;
		const gchar      *name;
		gint              parent = -1;
		gfloat            v[P_N] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f };
		gfloat            length = 0.0f;
		YamlNode         *m;

		if (!rd_mapping (rd, yaml_sequence_get_element (seq, i), path, &map) ||
		    !rd_keys (rd, map, path, keys))
			return FALSE;
		p = g_strdup_printf ("%s/name", path);
		if (!yaml_mapping_has_member (map, "name"))
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path, "missing key \"name\"");
		if (!rd_string (rd, yaml_mapping_get_member (map, "name"), p, &name))
			return FALSE;
		if (lrg_skeleton2d_find_bone (skel, name) >= 0)
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "duplicate bone \"%s\"", name);

		if ((m = yaml_mapping_get_member (map, "parent")) != NULL &&
		    yaml_node_get_node_type (m) != YAML_NODE_NULL)
		{
			const gchar *pname;

			g_free (p);
			p = g_strdup_printf ("%s/parent", path);
			if (!rd_string (rd, m, p, &pname))
				return FALSE;
			parent = lrg_skeleton2d_find_bone (skel, pname);
			if (parent < 0)
				return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p,
				                "unknown bone \"%s\" (a parent must come before its children)", pname);
		}
		if ((m = yaml_mapping_get_member (map, "x")) != NULL)
		{
			g_free (p);
			p = g_strdup_printf ("%s/x", path);
			if (!rd_number (rd, m, p, &v[P_X]))
				return FALSE;
		}
		if ((m = yaml_mapping_get_member (map, "y")) != NULL)
		{
			g_free (p);
			p = g_strdup_printf ("%s/y", path);
			if (!rd_number (rd, m, p, &v[P_Y]))
				return FALSE;
		}
		if ((m = yaml_mapping_get_member (map, "rotation")) != NULL)
		{
			g_free (p);
			p = g_strdup_printf ("%s/rotation", path);
			if (!rd_number (rd, m, p, &v[P_ROT]))
				return FALSE;
		}
		if ((m = yaml_mapping_get_member (map, "scale")) != NULL)
		{
			g_free (p);
			p = g_strdup_printf ("%s/scale", path);
			if (!rd_numbers (rd, m, p, 2, &v[P_SX]))
				return FALSE;
		}
		if ((m = yaml_mapping_get_member (map, "length")) != NULL)
		{
			g_free (p);
			p = g_strdup_printf ("%s/length", path);
			if (!rd_number (rd, m, p, &length))
				return FALSE;
			if (length < 0.0f)
				return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "a length must be 0 or more");
		}
		lrg_skeleton2d_add_bone (skel, name, parent, v[P_X], v[P_Y], v[P_ROT], v[P_SX], v[P_SY], length);
	}
	return TRUE;
}

static gboolean
read_slots (Reader        *rd,
            LrgSkeleton2D *skel,
            YamlNode      *node)
{
	static const gchar * const keys[] = { "name", "bone", "attachment", "color", NULL };
	YamlSequence *seq;
	guint         i;

	if (!rd_sequence (rd, node, "/slots", &seq))
		return FALSE;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar *path = g_strdup_printf ("/slots/%u", i);
		g_autofree gchar *p = NULL;
		YamlMapping      *map;
		const gchar      *name;
		const gchar      *bone_name;
		const gchar      *attachment = NULL;
		gint              bone;
		gint              slot;
		YamlNode         *m;

		if (!rd_mapping (rd, yaml_sequence_get_element (seq, i), path, &map) ||
		    !rd_keys (rd, map, path, keys))
			return FALSE;
		if (!yaml_mapping_has_member (map, "name"))
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path, "missing key \"name\"");
		p = g_strdup_printf ("%s/name", path);
		if (!rd_string (rd, yaml_mapping_get_member (map, "name"), p, &name))
			return FALSE;
		if (lrg_skeleton2d_find_slot (skel, name) >= 0)
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "duplicate slot \"%s\"", name);
		if (!yaml_mapping_has_member (map, "bone"))
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path, "missing key \"bone\"");
		g_free (p);
		p = g_strdup_printf ("%s/bone", path);
		if (!rd_string (rd, yaml_mapping_get_member (map, "bone"), p, &bone_name))
			return FALSE;
		bone = lrg_skeleton2d_find_bone (skel, bone_name);
		if (bone < 0)
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "unknown bone \"%s\"", bone_name);
		if ((m = yaml_mapping_get_member (map, "attachment")) != NULL)
		{
			g_free (p);
			p = g_strdup_printf ("%s/attachment", path);
			if (!rd_string_or_null (rd, m, p, &attachment))
				return FALSE;
		}
		slot = lrg_skeleton2d_add_slot (skel, name, bone, attachment);
		if ((m = yaml_mapping_get_member (map, "color")) != NULL)
		{
			gfloat c[4];

			g_free (p);
			p = g_strdup_printf ("%s/color", path);
			if (!rd_numbers (rd, m, p, 4, c))
				return FALSE;
			lrg_skeleton2d_set_slot_setup_color (skel, slot, c[0], c[1], c[2], c[3]);
		}
	}
	return TRUE;
}

static gboolean
read_attachment (Reader        *rd,
                 LrgSkeleton2D *skel,
                 const gchar   *skin,
                 YamlNode      *node,
                 const gchar   *path)
{
	static const gchar * const keys[] = { "slot", "name", "image", "region", "vertices", "weights", "triangles", NULL };
	g_autoptr(LrgSkinnedSprite) sprite = lrg_skinned_sprite_new ();
	g_autofree gchar           *p = NULL;
	YamlMapping                *map;
	YamlSequence               *seq;
	const gchar                *slot_name;
	const gchar                *name;
	const gchar                *image;
	gint                        slot;
	YamlNode                   *m;
	guint                       i;

	if (!rd_mapping (rd, node, path, &map) || !rd_keys (rd, map, path, keys))
		return FALSE;
	if (!yaml_mapping_has_member (map, "slot"))
		return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path, "missing key \"slot\"");
	p = g_strdup_printf ("%s/slot", path);
	if (!rd_string (rd, yaml_mapping_get_member (map, "slot"), p, &slot_name))
		return FALSE;
	slot = lrg_skeleton2d_find_slot (skel, slot_name);
	if (slot < 0)
		return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "unknown slot \"%s\"", slot_name);
	if (!yaml_mapping_has_member (map, "name"))
		return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path, "missing key \"name\"");
	g_free (p);
	p = g_strdup_printf ("%s/name", path);
	if (!rd_string (rd, yaml_mapping_get_member (map, "name"), p, &name))
		return FALSE;
	if (lrg_skeleton2d_get_skin_attachment (skel, skin, slot, name) != NULL)
		return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p,
		                "duplicate attachment \"%s\" for slot \"%s\"", name, slot_name);

	if ((m = yaml_mapping_get_member (map, "image")) != NULL)
	{
		g_free (p);
		p = g_strdup_printf ("%s/image", path);
		if (!rd_string (rd, m, p, &image))
			return FALSE;
		lrg_skinned_sprite_set_image (sprite, image);
	}
	if ((m = yaml_mapping_get_member (map, "region")) != NULL)
	{
		gfloat r[4];

		g_free (p);
		p = g_strdup_printf ("%s/region", path);
		if (!rd_numbers (rd, m, p, 4, r))
			return FALSE;
		if (r[2] < 0.0f || r[3] < 0.0f)
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "a region size must be 0 or more");
		lrg_skinned_sprite_set_region (sprite, r[0], r[1], r[2], r[3]);
	}

	/* vertices: [x, y, u, v] rows */
	if ((m = yaml_mapping_get_member (map, "vertices")) != NULL)
	{
		g_free (p);
		p = g_strdup_printf ("%s/vertices", path);
		if (!rd_sequence (rd, m, p, &seq))
			return FALSE;
		if (yaml_sequence_get_length (seq) > LRG_SKINNED_SPRITE_MAX_VERTICES)
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "more than %d vertices",
			                LRG_SKINNED_SPRITE_MAX_VERTICES);
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *vp = g_strdup_printf ("%s/%u", p, i);
			gfloat            v[4];

			if (!rd_numbers (rd, yaml_sequence_get_element (seq, i), vp, 4, v))
				return FALSE;
			lrg_skinned_sprite_add_vertex (sprite, v[0], v[1], v[2], v[3]);
		}
	}

	/* weights: one row per vertex, [bone, weight, bone, weight...] */
	if ((m = yaml_mapping_get_member (map, "weights")) != NULL)
	{
		g_free (p);
		p = g_strdup_printf ("%s/weights", path);
		if (!rd_sequence (rd, m, p, &seq))
			return FALSE;
		if (yaml_sequence_get_length (seq) != lrg_skinned_sprite_get_vertex_count (sprite))
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p,
			                "expected one row per vertex (%u), got %u",
			                lrg_skinned_sprite_get_vertex_count (sprite), yaml_sequence_get_length (seq));
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *wp = g_strdup_printf ("%s/%u", p, i);
			YamlSequence     *row;
			gint              bones[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
			gfloat            weights[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
			guint             n;
			guint             k;

			if (!rd_sequence (rd, yaml_sequence_get_element (seq, i), wp, &row))
				return FALSE;
			n = yaml_sequence_get_length (row);
			if (n % 2 != 0 || n / 2 > LRG_SKINNED_SPRITE_MAX_WEIGHTS)
				return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, wp,
				                "expected up to %d [bone, weight] pairs", LRG_SKINNED_SPRITE_MAX_WEIGHTS);
			for (k = 0; k < n / 2; k++)
			{
				g_autofree gchar *bp = g_strdup_printf ("%s/%u", wp, k * 2);
				g_autofree gchar *vp = g_strdup_printf ("%s/%u", wp, k * 2 + 1);
				const gchar      *bone_name;

				if (!rd_string (rd, yaml_sequence_get_element (row, k * 2), bp, &bone_name))
					return FALSE;
				bones[k] = lrg_skeleton2d_find_bone (skel, bone_name);
				if (bones[k] < 0)
					return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, bp, "unknown bone \"%s\"", bone_name);
				if (!rd_number (rd, yaml_sequence_get_element (row, k * 2 + 1), vp, &weights[k]))
					return FALSE;
				if (weights[k] < 0.0f)
					return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, vp, "a weight must be 0 or more");
			}
			lrg_skinned_sprite_set_weights (sprite, i, n / 2, bones, weights);
		}
	}

	/* triangles: [a, b, c] rows of vertex indices */
	if ((m = yaml_mapping_get_member (map, "triangles")) != NULL)
	{
		guint nv = lrg_skinned_sprite_get_vertex_count (sprite);

		g_free (p);
		p = g_strdup_printf ("%s/triangles", path);
		if (!rd_sequence (rd, m, p, &seq))
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *tp = g_strdup_printf ("%s/%u", p, i);
			YamlSequence     *row;
			guint             t[3];
			guint             k;

			if (!rd_sequence (rd, yaml_sequence_get_element (seq, i), tp, &row))
				return FALSE;
			if (yaml_sequence_get_length (row) != 3)
				return rd_fail (rd, LRG_SKELETON2D_ERROR_PARSE, tp, "expected three vertex indices");
			for (k = 0; k < 3; k++)
			{
				g_autofree gchar *ip = g_strdup_printf ("%s/%u", tp, k);

				if (!rd_index (rd, yaml_sequence_get_element (row, k), ip, nv, &t[k]))
					return FALSE;
			}
			lrg_skinned_sprite_add_triangle (sprite, t[0], t[1], t[2]);
		}
	}

	lrg_skeleton2d_set_attachment (skel, skin, slot, name, sprite);
	return TRUE;
}

static gboolean
read_skins (Reader        *rd,
            LrgSkeleton2D *skel,
            YamlNode      *node)
{
	static const gchar * const keys[] = { "name", "attachments", NULL };
	YamlSequence *seq;
	guint         i;

	if (!rd_sequence (rd, node, "/skins", &seq))
		return FALSE;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar *path = g_strdup_printf ("/skins/%u", i);
		g_autofree gchar *p = NULL;
		YamlMapping      *map;
		YamlSequence     *atts;
		const gchar      *name;
		YamlNode         *m;
		guint             k;

		if (!rd_mapping (rd, yaml_sequence_get_element (seq, i), path, &map) ||
		    !rd_keys (rd, map, path, keys))
			return FALSE;
		if (!yaml_mapping_has_member (map, "name"))
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path, "missing key \"name\"");
		p = g_strdup_printf ("%s/name", path);
		if (!rd_string (rd, yaml_mapping_get_member (map, "name"), p, &name))
			return FALSE;
		/* the default skin pre-exists; any other name may appear once */
		if (g_strcmp0 (name, LRG_SKELETON2D_DEFAULT_SKIN) != 0 && find_skin (skel, name, NULL) != NULL)
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "duplicate skin \"%s\"", name);
		lrg_skeleton2d_add_skin (skel, name);
		if ((m = yaml_mapping_get_member (map, "attachments")) == NULL)
			continue;
		g_free (p);
		p = g_strdup_printf ("%s/attachments", path);
		if (!rd_sequence (rd, m, p, &atts))
			return FALSE;
		for (k = 0; k < yaml_sequence_get_length (atts); k++)
		{
			g_autofree gchar *ap = g_strdup_printf ("%s/%u", p, k);

			if (!read_attachment (rd, skel, name, yaml_sequence_get_element (atts, k), ap))
				return FALSE;
		}
	}
	return TRUE;
}

/* reads a list of {time, <value keys>, curve} into the clip through
 * @add (channel-specific) */
typedef enum
{
	KEYS_TRANSLATE,
	KEYS_ROTATE,
	KEYS_SCALE,
	KEYS_COLOR
} KeysKind;

static gboolean
read_value_keys (Reader            *rd,
                 LrgSkeleton2DClip *clip,
                 const gchar       *target,
                 KeysKind           kind,
                 YamlNode          *node,
                 const gchar       *path)
{
	static const gchar * const xy_keys[] = { "time", "x", "y", "curve", NULL };
	static const gchar * const rot_keys[] = { "time", "angle", "curve", NULL };
	static const gchar * const col_keys[] = { "time", "color", "curve", NULL };
	YamlSequence *seq;
	guint         i;

	if (!rd_sequence (rd, node, path, &seq))
		return FALSE;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar   *kp = g_strdup_printf ("%s/%u", path, i);
		g_autofree gchar   *p = NULL;
		YamlMapping        *map;
		gfloat              time;
		gfloat              v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		gfloat              bez[4];
		LrgSkeleton2DCurve  curve;
		LrgSkeleton2DChannel channel;
		YamlNode           *m;
		guint               idx;

		if (!rd_mapping (rd, yaml_sequence_get_element (seq, i), kp, &map))
			return FALSE;
		if (!rd_keys (rd, map, kp, kind == KEYS_ROTATE ? rot_keys : kind == KEYS_COLOR ? col_keys : xy_keys))
			return FALSE;
		if (!rd_time (rd, map, kp, &time) || !rd_curve (rd, map, kp, &curve, bez))
			return FALSE;

		switch (kind)
		{
		case KEYS_ROTATE:
			channel = LRG_SKELETON2D_CHANNEL_ROTATE;
			if (!yaml_mapping_has_member (map, "angle"))
				return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, kp, "missing key \"angle\"");
			p = g_strdup_printf ("%s/angle", kp);
			if (!rd_number (rd, yaml_mapping_get_member (map, "angle"), p, &v[0]))
				return FALSE;
			lrg_skeleton2d_clip_add_rotate_key (clip, target, time, v[0], curve);
			break;
		case KEYS_COLOR:
			channel = LRG_SKELETON2D_CHANNEL_COLOR;
			if (!yaml_mapping_has_member (map, "color"))
				return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, kp, "missing key \"color\"");
			p = g_strdup_printf ("%s/color", kp);
			if (!rd_numbers (rd, yaml_mapping_get_member (map, "color"), p, 4, v))
				return FALSE;
			lrg_skeleton2d_clip_add_color_key (clip, target, time, v[0], v[1], v[2], v[3], curve);
			break;
		case KEYS_TRANSLATE:
		case KEYS_SCALE:
		default:
			channel = kind == KEYS_SCALE ? LRG_SKELETON2D_CHANNEL_SCALE : LRG_SKELETON2D_CHANNEL_TRANSLATE;
			if (kind == KEYS_SCALE)
				v[0] = v[1] = 1.0f;
			if ((m = yaml_mapping_get_member (map, "x")) != NULL)
			{
				p = g_strdup_printf ("%s/x", kp);
				if (!rd_number (rd, m, p, &v[0]))
					return FALSE;
			}
			if ((m = yaml_mapping_get_member (map, "y")) != NULL)
			{
				g_free (p);
				p = g_strdup_printf ("%s/y", kp);
				if (!rd_number (rd, m, p, &v[1]))
					return FALSE;
			}
			if (kind == KEYS_SCALE)
				lrg_skeleton2d_clip_add_scale_key (clip, target, time, v[0], v[1], curve);
			else
				lrg_skeleton2d_clip_add_translate_key (clip, target, time, v[0], v[1], curve);
			break;
		}

		if (curve == LRG_SKELETON2D_CURVE_BEZIER)
		{
			/* the key just added: find it by time */
			for (idx = 0; idx < lrg_skeleton2d_clip_get_key_count (clip, target, channel); idx++)
			{
				gfloat kt;

				lrg_skeleton2d_clip_get_key (clip, target, channel, idx, &kt, NULL, NULL);
				if (fabsf (kt - time) <= KEY_TIME_EPSILON)
				{
					lrg_skeleton2d_clip_set_key_bezier (clip, target, channel, idx,
					                                    bez[0], bez[1], bez[2], bez[3]);
					break;
				}
			}
		}
	}
	return TRUE;
}

static gboolean
read_animation (Reader        *rd,
                LrgSkeleton2D *skel,
                YamlNode      *node,
                const gchar   *path)
{
	static const gchar * const keys[] = { "name", "duration", "loop", "bones", "slots", "draw_order", "events", NULL };
	static const gchar * const bone_keys[] = { "bone", "translate", "rotate", "scale", NULL };
	static const gchar * const slot_keys[] = { "slot", "attachment", "color", NULL };
	static const gchar * const att_keys[] = { "time", "name", NULL };
	static const gchar * const order_keys[] = { "time", "order", NULL };
	g_autoptr(LrgSkeleton2DClip) clip = NULL;
	g_autofree gchar            *p = NULL;
	YamlMapping                 *map;
	YamlSequence                *seq;
	const gchar                 *name;
	YamlNode                    *m;
	gfloat                       duration = 0.0f;
	guint                        i;

	if (!rd_mapping (rd, node, path, &map) || !rd_keys (rd, map, path, keys))
		return FALSE;
	if (!yaml_mapping_has_member (map, "name"))
		return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, path, "missing key \"name\"");
	p = g_strdup_printf ("%s/name", path);
	if (!rd_string (rd, yaml_mapping_get_member (map, "name"), p, &name))
		return FALSE;
	if (lrg_skeleton2d_get_clip (skel, name) != NULL)
		return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "duplicate animation \"%s\"", name);
	clip = lrg_skeleton2d_clip_new (name);

	if ((m = yaml_mapping_get_member (map, "duration")) != NULL)
	{
		g_free (p);
		p = g_strdup_printf ("%s/duration", path);
		if (!rd_number (rd, m, p, &duration))
			return FALSE;
		if (duration < 0.0f)
			return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, p, "a duration must be 0 or more");
		lrg_skeleton2d_clip_set_duration (clip, duration);
	}
	if ((m = yaml_mapping_get_member (map, "loop")) != NULL)
	{
		gboolean loop;

		g_free (p);
		p = g_strdup_printf ("%s/loop", path);
		if (!rd_boolean (rd, m, p, &loop))
			return FALSE;
		lrg_skeleton2d_clip_set_loop (clip, loop);
	}

	/* bone timelines */
	if ((m = yaml_mapping_get_member (map, "bones")) != NULL)
	{
		g_free (p);
		p = g_strdup_printf ("%s/bones", path);
		if (!rd_sequence (rd, m, p, &seq))
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *bp = g_strdup_printf ("%s/%u", p, i);
			g_autofree gchar *np = g_strdup_printf ("%s/bone", bp);
			YamlMapping      *bm;
			const gchar      *bone;
			static const gchar * const ch_names[] = { "translate", "rotate", "scale" };
			guint             c;

			if (!rd_mapping (rd, yaml_sequence_get_element (seq, i), bp, &bm) ||
			    !rd_keys (rd, bm, bp, bone_keys))
				return FALSE;
			if (!yaml_mapping_has_member (bm, "bone"))
				return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, bp, "missing key \"bone\"");
			if (!rd_string (rd, yaml_mapping_get_member (bm, "bone"), np, &bone))
				return FALSE;
			if (lrg_skeleton2d_find_bone (skel, bone) < 0)
				return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, np, "unknown bone \"%s\"", bone);
			for (c = 0; c < 3; c++)
			{
				YamlNode         *kn = yaml_mapping_get_member (bm, ch_names[c]);
				g_autofree gchar *cp = NULL;

				if (kn == NULL)
					continue;
				cp = g_strdup_printf ("%s/%s", bp, ch_names[c]);
				if (!read_value_keys (rd, clip, bone, (KeysKind)c, kn, cp))
					return FALSE;
			}
		}
	}

	/* slot timelines */
	if ((m = yaml_mapping_get_member (map, "slots")) != NULL)
	{
		g_free (p);
		p = g_strdup_printf ("%s/slots", path);
		if (!rd_sequence (rd, m, p, &seq))
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *sp = g_strdup_printf ("%s/%u", p, i);
			g_autofree gchar *np = g_strdup_printf ("%s/slot", sp);
			YamlMapping      *sm;
			const gchar      *slot;
			YamlNode         *kn;

			if (!rd_mapping (rd, yaml_sequence_get_element (seq, i), sp, &sm) ||
			    !rd_keys (rd, sm, sp, slot_keys))
				return FALSE;
			if (!yaml_mapping_has_member (sm, "slot"))
				return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, sp, "missing key \"slot\"");
			if (!rd_string (rd, yaml_mapping_get_member (sm, "slot"), np, &slot))
				return FALSE;
			if (lrg_skeleton2d_find_slot (skel, slot) < 0)
				return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, np, "unknown slot \"%s\"", slot);
			if ((kn = yaml_mapping_get_member (sm, "attachment")) != NULL)
			{
				g_autofree gchar *ap = g_strdup_printf ("%s/attachment", sp);
				YamlSequence     *aseq;
				guint             k;

				if (!rd_sequence (rd, kn, ap, &aseq))
					return FALSE;
				for (k = 0; k < yaml_sequence_get_length (aseq); k++)
				{
					g_autofree gchar *kp = g_strdup_printf ("%s/%u", ap, k);
					g_autofree gchar *nkp = g_strdup_printf ("%s/name", kp);
					YamlMapping      *km;
					gfloat            time;
					const gchar      *att = NULL;

					if (!rd_mapping (rd, yaml_sequence_get_element (aseq, k), kp, &km) ||
					    !rd_keys (rd, km, kp, att_keys) || !rd_time (rd, km, kp, &time))
						return FALSE;
					if (!yaml_mapping_has_member (km, "name"))
						return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, kp, "missing key \"name\"");
					if (!rd_string_or_null (rd, yaml_mapping_get_member (km, "name"), nkp, &att))
						return FALSE;
					lrg_skeleton2d_clip_add_attachment_key (clip, slot, time, att);
				}
			}
			if ((kn = yaml_mapping_get_member (sm, "color")) != NULL)
			{
				g_autofree gchar *cp = g_strdup_printf ("%s/color", sp);

				if (!read_value_keys (rd, clip, slot, KEYS_COLOR, kn, cp))
					return FALSE;
			}
		}
	}

	/* draw order keys */
	if ((m = yaml_mapping_get_member (map, "draw_order")) != NULL)
	{
		g_free (p);
		p = g_strdup_printf ("%s/draw_order", path);
		if (!rd_sequence (rd, m, p, &seq))
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *kp = g_strdup_printf ("%s/%u", p, i);
			g_autofree gchar *op = g_strdup_printf ("%s/order", kp);
			YamlMapping      *km;
			YamlNode         *on;
			gfloat            time;

			if (!rd_mapping (rd, yaml_sequence_get_element (seq, i), kp, &km) ||
			    !rd_keys (rd, km, kp, order_keys) || !rd_time (rd, km, kp, &time))
				return FALSE;
			on = yaml_mapping_get_member (km, "order");
			if (on == NULL || yaml_node_get_node_type (on) == YAML_NODE_NULL)
			{
				lrg_skeleton2d_clip_add_draw_order_key (clip, time, NULL);
			}
			else
			{
				YamlSequence        *oseq;
				guint                n;
				guint                k;
				g_autofree gboolean *seen = NULL;
				g_autofree const gchar **names = NULL;

				if (!rd_sequence (rd, on, op, &oseq))
					return FALSE;
				n = yaml_sequence_get_length (oseq);
				if (n != lrg_skeleton2d_get_slot_count (skel))
					return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, op,
					                "a draw order must name every slot once (%u), got %u",
					                lrg_skeleton2d_get_slot_count (skel), n);
				seen = g_new0 (gboolean, n + 1);
				names = g_new0 (const gchar *, n + 1);
				for (k = 0; k < n; k++)
				{
					g_autofree gchar *sp = g_strdup_printf ("%s/%u", op, k);
					gint              slot;

					if (!rd_string (rd, yaml_sequence_get_element (oseq, k), sp, &names[k]))
						return FALSE;
					slot = lrg_skeleton2d_find_slot (skel, names[k]);
					if (slot < 0)
						return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, sp, "unknown slot \"%s\"", names[k]);
					if (seen[slot])
						return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, sp, "slot \"%s\" named twice", names[k]);
					seen[slot] = TRUE;
				}
				lrg_skeleton2d_clip_add_draw_order_key (clip, time, names);
			}
		}
	}

	/* events */
	if ((m = yaml_mapping_get_member (map, "events")) != NULL)
	{
		g_free (p);
		p = g_strdup_printf ("%s/events", path);
		if (!rd_sequence (rd, m, p, &seq))
			return FALSE;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *kp = g_strdup_printf ("%s/%u", p, i);
			g_autofree gchar *np = g_strdup_printf ("%s/name", kp);
			YamlMapping      *km;
			gfloat            time;
			const gchar      *ev;

			if (!rd_mapping (rd, yaml_sequence_get_element (seq, i), kp, &km) ||
			    !rd_keys (rd, km, kp, att_keys) || !rd_time (rd, km, kp, &time))
				return FALSE;
			if (!yaml_mapping_has_member (km, "name"))
				return rd_fail (rd, LRG_SKELETON2D_ERROR_INVALID, kp, "missing key \"name\"");
			if (!rd_string (rd, yaml_mapping_get_member (km, "name"), np, &ev))
				return FALSE;
			lrg_skeleton2d_clip_add_event_key (clip, time, ev);
		}
	}

	lrg_skeleton2d_add_clip (skel, clip);
	return TRUE;
}

static LrgSkeleton2D *
read_document (Reader   *rd,
               YamlNode *root)
{
	static const gchar * const keys[] = { "rskel2d", "name", "bones", "slots", "skins", "animations", NULL };
	g_autoptr(LrgSkeleton2D) skel = lrg_skeleton2d_new ();
	YamlMapping             *map;
	YamlNode                *m;
	gfloat                   version;

	if (!rd_mapping (rd, root, "/", &map))
		return NULL;
	if (!yaml_mapping_has_member (map, "rskel2d"))
	{
		rd_fail (rd, LRG_SKELETON2D_ERROR_VERSION, "/", "missing \"rskel2d: %d\" (not a .rskel2d document?)",
		         LRG_SKELETON2D_FORMAT_VERSION);
		return NULL;
	}
	if (!rd_number (rd, yaml_mapping_get_member (map, "rskel2d"), "/rskel2d", &version))
		return NULL;
	if (version != (gfloat)LRG_SKELETON2D_FORMAT_VERSION)
	{
		rd_fail (rd, LRG_SKELETON2D_ERROR_VERSION, "/rskel2d", "unsupported version %g (this library reads %d)",
		         (gdouble)version, LRG_SKELETON2D_FORMAT_VERSION);
		return NULL;
	}
	if (!rd_keys (rd, map, "/", keys))
		return NULL;

	if ((m = yaml_mapping_get_member (map, "name")) != NULL)
	{
		const gchar *name;

		if (!rd_string (rd, m, "/name", &name))
			return NULL;
		lrg_skeleton2d_set_name (skel, name);
	}
	if ((m = yaml_mapping_get_member (map, "bones")) != NULL && !read_bones (rd, skel, m))
		return NULL;
	if ((m = yaml_mapping_get_member (map, "slots")) != NULL && !read_slots (rd, skel, m))
		return NULL;
	if ((m = yaml_mapping_get_member (map, "skins")) != NULL && !read_skins (rd, skel, m))
		return NULL;
	if ((m = yaml_mapping_get_member (map, "animations")) != NULL)
	{
		YamlSequence *seq;
		guint         i;

		if (!rd_sequence (rd, m, "/animations", &seq))
			return NULL;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar *ap = g_strdup_printf ("/animations/%u", i);

			if (!read_animation (rd, skel, yaml_sequence_get_element (seq, i), ap))
				return NULL;
		}
	}
	lrg_skeleton2d_update_world (skel);
	return g_steal_pointer (&skel);
}

LrgSkeleton2D *
lrg_skeleton2d_new_from_data (const gchar  *data,
                              gssize        length,
                              GError      **error)
{
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	g_autoptr(GError)     local = NULL;
	Reader                rd;

	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!yaml_parser_load_from_data (parser, data != NULL ? data : "", length, &local))
	{
		g_set_error (error, LRG_SKELETON2D_ERROR, LRG_SKELETON2D_ERROR_PARSE, "<data>: %s", local->message);
		return NULL;
	}
	rd.source = "<data>";
	rd.error = error;
	return read_document (&rd, yaml_parser_get_root (parser));
}

LrgSkeleton2D *
lrg_skeleton2d_new_from_file (const gchar  *path,
                              GError      **error)
{
	g_autofree gchar     *text = NULL;
	g_autofree gchar     *dir = NULL;
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	g_autoptr(GError)     local = NULL;
	LrgSkeleton2D        *skel;
	gsize                 len;
	Reader                rd;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, LRG_SKELETON2D_ERROR, LRG_SKELETON2D_ERROR_IO, "%s: %s", path, local->message);
		return NULL;
	}
	if (!yaml_parser_load_from_data (parser, text, (gssize)len, &local))
	{
		g_set_error (error, LRG_SKELETON2D_ERROR, LRG_SKELETON2D_ERROR_PARSE, "%s: %s", path, local->message);
		return NULL;
	}
	rd.source = path;
	rd.error = error;
	skel = read_document (&rd, yaml_parser_get_root (parser));
	if (skel != NULL)
	{
		dir = g_path_get_dirname (path);
		lrg_skeleton2d_set_base_dir (skel, dir);
	}
	return skel;
}

/* ================================================================== */
/* .rskel2d writer                                                     */
/* ================================================================== */

/* shortest float text that reads back exactly (the data loader's rule) */
static void
w_num (GString *out,
       gfloat   value)
{
	g_autoptr(YamlNode) node = lrg_data_yaml_float (value);

	g_string_append (out, yaml_node_get_scalar (node));
}

static gboolean
is_reserved_word (const gchar *s)
{
	static const gchar * const words[] = { "true", "false", "yes", "no", "on", "off", "null", "y", "n", NULL };
	guint i;

	for (i = 0; words[i] != NULL; i++)
	{
		if (g_ascii_strcasecmp (s, words[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

/* a string scalar: plain when it is an identifier-like word that YAML
 * reads back as the same string, double-quoted (escaped) otherwise */
static void
w_str (GString     *out,
       const gchar *s)
{
	const gchar *c;
	gboolean     plain;

	plain = s[0] != '\0' && (g_ascii_isalpha (s[0]) || s[0] == '_') && !is_reserved_word (s);
	for (c = s; plain && *c != '\0'; c++)
	{
		if (!(g_ascii_isalnum (*c) || *c == '_' || *c == '-' || *c == '.' || *c == '/'))
			plain = FALSE;
	}
	if (plain)
	{
		g_string_append (out, s);
		return;
	}
	g_string_append_c (out, '"');
	for (c = s; *c != '\0'; c++)
	{
		guchar ch = (guchar)*c;

		if (ch == '"' || ch == '\\')
		{
			g_string_append_c (out, '\\');
			g_string_append_c (out, (gchar)ch);
		}
		else if (ch == '\n')
			g_string_append (out, "\\n");
		else if (ch == '\t')
			g_string_append (out, "\\t");
		else if (ch < 0x20 || ch == 0x7f)
			g_string_append_printf (out, "\\x%02x", ch);
		else
			g_string_append_c (out, (gchar)ch);
	}
	g_string_append_c (out, '"');
}

static void
w_indent (GString *out,
          guint    level)
{
	guint i;

	for (i = 0; i < level; i++)
		g_string_append (out, "  ");
}

/* "[a, b, c]" */
static void
w_flow_numbers (GString      *out,
                const gfloat *v,
                guint         n)
{
	guint i;

	g_string_append_c (out, '[');
	for (i = 0; i < n; i++)
	{
		if (i > 0)
			g_string_append (out, ", ");
		w_num (out, v[i]);
	}
	g_string_append_c (out, ']');
}

/* "<indent>key: value-number\n" */
static void
w_key_num (GString     *out,
           guint        level,
           const gchar *key,
           gfloat       value)
{
	w_indent (out, level);
	g_string_append_printf (out, "%s: ", key);
	w_num (out, value);
	g_string_append_c (out, '\n');
}

static void
w_key_str (GString     *out,
           guint        level,
           const gchar *key,
           const gchar *value)
{
	w_indent (out, level);
	g_string_append_printf (out, "%s: ", key);
	w_str (out, value);
	g_string_append_c (out, '\n');
}

/* ", curve: ..." for a non-linear key */
static void
w_curve (GString        *out,
         const ValueKey *k)
{
	if (k->curve == LRG_SKELETON2D_CURVE_STEPPED)
	{
		g_string_append (out, ", curve: stepped");
	}
	else if (k->curve == LRG_SKELETON2D_CURVE_BEZIER)
	{
		g_string_append (out, ", curve: ");
		w_flow_numbers (out, k->bez, 4);
	}
}

static void
w_value_keys (GString     *out,
              guint        level,
              const gchar *key,
              GArray      *keys,
              KeysKind     kind)
{
	guint i;

	if (keys == NULL || keys->len == 0)
		return;
	w_indent (out, level);
	g_string_append_printf (out, "%s:\n", key);
	for (i = 0; i < keys->len; i++)
	{
		const ValueKey *k = &g_array_index (keys, ValueKey, i);

		w_indent (out, level + 1);
		g_string_append (out, "- {time: ");
		w_num (out, k->time);
		switch (kind)
		{
		case KEYS_ROTATE:
			g_string_append (out, ", angle: ");
			w_num (out, k->v[0]);
			break;
		case KEYS_COLOR:
			g_string_append (out, ", color: ");
			w_flow_numbers (out, k->v, 4);
			break;
		case KEYS_TRANSLATE:
		case KEYS_SCALE:
		default:
			g_string_append (out, ", x: ");
			w_num (out, k->v[0]);
			g_string_append (out, ", y: ");
			w_num (out, k->v[1]);
			break;
		}
		w_curve (out, k);
		g_string_append (out, "}\n");
	}
}

static void
write_attachment (GString       *out,
                  LrgSkeleton2D *self,
                  SkinEntry     *e)
{
	LrgSkinnedSprite *sp = e->sprite;
	guint             nv = lrg_skinned_sprite_get_vertex_count (sp);
	guint             nt = lrg_skinned_sprite_get_triangle_count (sp);
	gfloat            region[4];
	guint             i;

	w_indent (out, 3);
	g_string_append (out, "- slot: ");
	w_str (out, g_array_index (self->slots, Slot, e->slot).name);
	g_string_append_c (out, '\n');
	w_key_str (out, 4, "name", e->name);
	if (lrg_skinned_sprite_get_image (sp) != NULL)
		w_key_str (out, 4, "image", lrg_skinned_sprite_get_image (sp));
	lrg_skinned_sprite_get_region (sp, &region[0], &region[1], &region[2], &region[3]);
	if (region[0] != 0.0f || region[1] != 0.0f || region[2] != 0.0f || region[3] != 0.0f)
	{
		w_indent (out, 4);
		g_string_append (out, "region: ");
		w_flow_numbers (out, region, 4);
		g_string_append_c (out, '\n');
	}
	if (nv > 0)
	{
		w_indent (out, 4);
		g_string_append (out, "vertices:\n");
		for (i = 0; i < nv; i++)
		{
			gfloat v[4];

			lrg_skinned_sprite_get_vertex (sp, i, &v[0], &v[1], &v[2], &v[3]);
			w_indent (out, 5);
			g_string_append (out, "- ");
			w_flow_numbers (out, v, 4);
			g_string_append_c (out, '\n');
		}
	}
	if (lrg_skinned_sprite_has_weights (sp))
	{
		w_indent (out, 4);
		g_string_append (out, "weights:\n");
		for (i = 0; i < nv; i++)
		{
			gint   bones[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
			gfloat weights[LRG_SKINNED_SPRITE_MAX_WEIGHTS];
			guint  n = lrg_skinned_sprite_get_weights (sp, i, bones, weights);
			guint  k;

			w_indent (out, 5);
			g_string_append (out, "- [");
			for (k = 0; k < n; k++)
			{
				const gchar *bn = lrg_skeleton2d_get_bone_name (self, bones[k]);

				if (k > 0)
					g_string_append (out, ", ");
				/* a weight on a bone the skeleton lacks cannot be named; it
				 * is written as bone "?" which loading rejects loudly */
				w_str (out, bn != NULL ? bn : "?");
				g_string_append (out, ", ");
				w_num (out, weights[k]);
			}
			g_string_append (out, "]\n");
		}
	}
	if (nt > 0)
	{
		w_indent (out, 4);
		g_string_append (out, "triangles:\n");
		for (i = 0; i < nt; i++)
		{
			guint a;
			guint b;
			guint c;

			lrg_skinned_sprite_get_triangle (sp, i, &a, &b, &c);
			w_indent (out, 5);
			g_string_append_printf (out, "- [%u, %u, %u]\n", a, b, c);
		}
	}
}

static void
write_clip (GString           *out,
            LrgSkeleton2DClip *clip)
{
	guint i;

	w_indent (out, 1);
	g_string_append (out, "- name: ");
	w_str (out, clip->name);
	g_string_append_c (out, '\n');
	if (clip->duration > 0.0f)
		w_key_num (out, 2, "duration", clip->duration);
	if (clip->loop)
	{
		w_indent (out, 2);
		g_string_append (out, "loop: true\n");
	}

	if (clip->bone_tracks->len > 0)
	{
		w_indent (out, 2);
		g_string_append (out, "bones:\n");
		for (i = 0; i < clip->bone_tracks->len; i++)
		{
			BoneTrack *t = g_ptr_array_index (clip->bone_tracks, i);

			w_indent (out, 3);
			g_string_append (out, "- bone: ");
			w_str (out, t->name);
			g_string_append_c (out, '\n');
			w_value_keys (out, 4, "translate", t->ch[0], KEYS_TRANSLATE);
			w_value_keys (out, 4, "rotate", t->ch[1], KEYS_ROTATE);
			w_value_keys (out, 4, "scale", t->ch[2], KEYS_SCALE);
		}
	}

	if (clip->slot_tracks->len > 0)
	{
		w_indent (out, 2);
		g_string_append (out, "slots:\n");
		for (i = 0; i < clip->slot_tracks->len; i++)
		{
			SlotTrack *t = g_ptr_array_index (clip->slot_tracks, i);

			w_indent (out, 3);
			g_string_append (out, "- slot: ");
			w_str (out, t->name);
			g_string_append_c (out, '\n');
			if (t->attach != NULL && t->attach->len > 0)
			{
				guint k;

				w_indent (out, 4);
				g_string_append (out, "attachment:\n");
				for (k = 0; k < t->attach->len; k++)
				{
					const NameKey *nk = &g_array_index (t->attach, NameKey, k);

					w_indent (out, 5);
					g_string_append (out, "- {time: ");
					w_num (out, nk->time);
					g_string_append (out, ", name: ");
					if (nk->name != NULL)
						w_str (out, nk->name);
					else
						g_string_append (out, "null");
					g_string_append (out, "}\n");
				}
			}
			w_value_keys (out, 4, "color", t->color, KEYS_COLOR);
		}
	}

	if (clip->order->len > 0)
	{
		w_indent (out, 2);
		g_string_append (out, "draw_order:\n");
		for (i = 0; i < clip->order->len; i++)
		{
			const OrderKey *ok = &g_array_index (clip->order, OrderKey, i);

			w_indent (out, 3);
			g_string_append (out, "- {time: ");
			w_num (out, ok->time);
			g_string_append (out, ", order: ");
			if (ok->slots == NULL)
			{
				g_string_append (out, "null");
			}
			else
			{
				guint k;

				g_string_append_c (out, '[');
				for (k = 0; ok->slots[k] != NULL; k++)
				{
					if (k > 0)
						g_string_append (out, ", ");
					w_str (out, ok->slots[k]);
				}
				g_string_append_c (out, ']');
			}
			g_string_append (out, "}\n");
		}
	}

	if (clip->events->len > 0)
	{
		w_indent (out, 2);
		g_string_append (out, "events:\n");
		for (i = 0; i < clip->events->len; i++)
		{
			const NameKey *ev = &g_array_index (clip->events, NameKey, i);

			w_indent (out, 3);
			g_string_append (out, "- {time: ");
			w_num (out, ev->time);
			g_string_append (out, ", name: ");
			w_str (out, ev->name);
			g_string_append (out, "}\n");
		}
	}
}

gchar *
lrg_skeleton2d_save_to_data (LrgSkeleton2D *self,
                             gsize         *length)
{
	GString *out;
	guint    i;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), NULL);

	out = g_string_new (NULL);
	g_string_append_printf (out, "rskel2d: %d\n", LRG_SKELETON2D_FORMAT_VERSION);
	if (self->name != NULL)
		w_key_str (out, 0, "name", self->name);

	/* bones, defaults left out */
	if (self->bones->len > 0)
	{
		g_string_append (out, "bones:\n");
		for (i = 0; i < self->bones->len; i++)
		{
			const Bone *b = &g_array_index (self->bones, Bone, i);

			w_indent (out, 1);
			g_string_append (out, "- name: ");
			w_str (out, b->name);
			g_string_append_c (out, '\n');
			if (b->parent >= 0)
				w_key_str (out, 2, "parent", g_array_index (self->bones, Bone, b->parent).name);
			if (b->setup[P_X] != 0.0f)
				w_key_num (out, 2, "x", b->setup[P_X]);
			if (b->setup[P_Y] != 0.0f)
				w_key_num (out, 2, "y", b->setup[P_Y]);
			if (b->setup[P_ROT] != 0.0f)
				w_key_num (out, 2, "rotation", b->setup[P_ROT]);
			if (b->setup[P_SX] != 1.0f || b->setup[P_SY] != 1.0f)
			{
				w_indent (out, 2);
				g_string_append (out, "scale: ");
				w_flow_numbers (out, &b->setup[P_SX], 2);
				g_string_append_c (out, '\n');
			}
			if (b->length != 0.0f)
				w_key_num (out, 2, "length", b->length);
		}
	}

	/* slots in setup draw order */
	if (self->slots->len > 0)
	{
		g_string_append (out, "slots:\n");
		for (i = 0; i < self->slots->len; i++)
		{
			const Slot *s = &g_array_index (self->slots, Slot, i);

			w_indent (out, 1);
			g_string_append (out, "- name: ");
			w_str (out, s->name);
			g_string_append_c (out, '\n');
			w_key_str (out, 2, "bone", g_array_index (self->bones, Bone, s->bone).name);
			if (s->setup_attachment != NULL)
				w_key_str (out, 2, "attachment", s->setup_attachment);
			if (s->setup_color[0] != 1.0f || s->setup_color[1] != 1.0f ||
			    s->setup_color[2] != 1.0f || s->setup_color[3] != 1.0f)
			{
				w_indent (out, 2);
				g_string_append (out, "color: ");
				w_flow_numbers (out, s->setup_color, 4);
				g_string_append_c (out, '\n');
			}
		}
	}

	/* skins: the default skin only when it has attachments, every other
	 * skin always (an empty named skin is still a skin) */
	{
		gboolean any = FALSE;

		for (i = 0; i < self->skins->len; i++)
		{
			Skin *s = g_ptr_array_index (self->skins, i);
			guint k;

			if (i == 0 && s->entries->len == 0)
				continue;
			if (!any)
			{
				g_string_append (out, "skins:\n");
				any = TRUE;
			}
			w_indent (out, 1);
			g_string_append (out, "- name: ");
			w_str (out, s->name);
			g_string_append_c (out, '\n');
			if (s->entries->len == 0)
				continue;
			w_indent (out, 2);
			g_string_append (out, "attachments:\n");
			for (k = 0; k < s->entries->len; k++)
				write_attachment (out, self, g_ptr_array_index (s->entries, k));
		}
	}

	if (self->clips->len > 0)
	{
		g_string_append (out, "animations:\n");
		for (i = 0; i < self->clips->len; i++)
			write_clip (out, g_ptr_array_index (self->clips, i));
	}

	if (length != NULL)
		*length = out->len;
	return g_string_free (out, FALSE);
}

gboolean
lrg_skeleton2d_save_to_file (LrgSkeleton2D  *self,
                             const gchar    *path,
                             GError        **error)
{
	g_autofree gchar  *text = NULL;
	g_autoptr(GError)  local = NULL;
	gsize              len;

	g_return_val_if_fail (LRG_IS_SKELETON2D (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	text = lrg_skeleton2d_save_to_data (self, &len);
	if (!g_file_set_contents (path, text, (gssize)len, &local))
	{
		g_set_error (error, LRG_SKELETON2D_ERROR, LRG_SKELETON2D_ERROR_IO, "%s: %s", path, local->message);
		return FALSE;
	}
	return TRUE;
}
