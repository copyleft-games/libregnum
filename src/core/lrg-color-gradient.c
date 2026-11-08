/* lrg-color-gradient.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColorGradient: sorted colour stops with an interpolation mode.
 *
 * Stops live in a GArray kept sorted by offset (insertion after equal
 * offsets, so two stops at one offset make a hard edge).  The exact
 * evaluator binary-searches the segment; the fast evaluator samples a
 * 256-entry table that is rebuilt lazily after any change (the "dirty"
 * flag), which is what the particle modules call per particle.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include "lrg-color-gradient.h"

typedef struct
{
	gfloat offset;
	gfloat rgba[4];
} Stop;

struct _LrgColorGradient
{
	GObject              parent_instance;

	GArray              *stops;   /* Stop, sorted by offset */
	LrgColorGradientMode mode;

	/* lazily baked table for evaluate_fast() */
	gfloat               lut[LRG_COLOR_GRADIENT_LUT_SIZE][4];
	gboolean             lut_dirty;
};

G_DEFINE_FINAL_TYPE (LrgColorGradient, lrg_color_gradient, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_MODE,
	PROP_N_STOPS,
	N_PROPS
};

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

GType
lrg_color_gradient_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_COLOR_GRADIENT_LINEAR, "LRG_COLOR_GRADIENT_LINEAR", "linear" },
			{ LRG_COLOR_GRADIENT_STEP, "LRG_COLOR_GRADIENT_STEP", "step" },
			{ LRG_COLOR_GRADIENT_SMOOTH, "LRG_COLOR_GRADIENT_SMOOTH", "smooth" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static ("LrgColorGradientMode", values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

G_DEFINE_QUARK (lrg-color-gradient-error-quark, lrg_color_gradient_error)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gfloat
clamp01 (gfloat v)
{
	if (!(v > 0.0f))   /* also catches NaN */
		return 0.0f;
	return v > 1.0f ? 1.0f : v;
}

/* Marks the table stale and tells listeners (the "changed" signal). */
static void
touch (LrgColorGradient *self)
{
	self->lut_dirty = TRUE;
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/* Index of the first stop whose offset is strictly greater than @offset:
 * where a new stop at @offset goes (after equal ones). */
static guint
upper_bound (LrgColorGradient *self,
             gfloat            offset)
{
	guint lo = 0, hi = self->stops->len;

	while (lo < hi)
	{
		guint mid = (lo + hi) / 2;

		if (g_array_index (self->stops, Stop, mid).offset <= offset)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static guint
insert_stop (LrgColorGradient *self,
             gfloat            offset,
             const gfloat     *rgba)
{
	Stop  s;
	guint at;
	gint  k;

	s.offset = clamp01 (offset);
	for (k = 0; k < 4; k++)
		s.rgba[k] = clamp01 (rgba[k]);
	at = upper_bound (self, s.offset);
	g_array_insert_val (self->stops, at, s);
	return at;
}

/* Two hex digits to a byte; FALSE when not hex */
static gboolean
hex_pair (const gchar *p,
          guint8      *out)
{
	gint hi = g_ascii_xdigit_value (p[0]);
	gint lo = g_ascii_xdigit_value (p[1]);

	if (hi < 0 || lo < 0)
		return FALSE;
	*out = (guint8) (hi * 16 + lo);
	return TRUE;
}

/* "#rgb", "#rrggbb" or "#rrggbbaa" to RGBA floats */
static gboolean
parse_hex_color (const gchar *text,
                 gfloat      *rgba)
{
	gsize  len;
	guint8 c[4] = { 255, 255, 255, 255 };
	gint   k;

	if (text == NULL || text[0] != '#')
		return FALSE;
	text++;
	len = strlen (text);
	if (len == 3)
	{
		for (k = 0; k < 3; k++)
		{
			gint v = g_ascii_xdigit_value (text[k]);

			if (v < 0)
				return FALSE;
			c[k] = (guint8) (v * 17);
		}
	}
	else if (len == 6 || len == 8)
	{
		for (k = 0; k < (gint) len / 2; k++)
			if (!hex_pair (text + k * 2, &c[k]))
				return FALSE;
	}
	else
		return FALSE;
	for (k = 0; k < 4; k++)
		rgba[k] = (gfloat) c[k] / 255.0f;
	return TRUE;
}

static guint8
to_byte (gfloat v)
{
	return (guint8) lrintf (clamp01 (v) * 255.0f);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_color_gradient_finalize (GObject *object)
{
	LrgColorGradient *self = LRG_COLOR_GRADIENT (object);

	g_clear_pointer (&self->stops, g_array_unref);

	G_OBJECT_CLASS (lrg_color_gradient_parent_class)->finalize (object);
}

static void
lrg_color_gradient_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	LrgColorGradient *self = LRG_COLOR_GRADIENT (object);

	switch (prop_id)
	{
	case PROP_MODE:
		g_value_set_enum (value, (gint) self->mode);
		break;
	case PROP_N_STOPS:
		g_value_set_uint (value, self->stops->len);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_color_gradient_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	LrgColorGradient *self = LRG_COLOR_GRADIENT (object);

	switch (prop_id)
	{
	case PROP_MODE:
		lrg_color_gradient_set_mode (self, (LrgColorGradientMode) g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_color_gradient_class_init (LrgColorGradientClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_color_gradient_finalize;
	object_class->get_property = lrg_color_gradient_get_property;
	object_class->set_property = lrg_color_gradient_set_property;

	/**
	 * LrgColorGradient:mode:
	 *
	 * How the space between two stops is filled.
	 */
	properties[PROP_MODE] =
		g_param_spec_enum ("mode", "Mode", "Interpolation between stops",
		                   LRG_TYPE_COLOR_GRADIENT_MODE, LRG_COLOR_GRADIENT_LINEAR,
		                   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgColorGradient:n-stops:
	 *
	 * The number of stops (read-only).
	 */
	properties[PROP_N_STOPS] =
		g_param_spec_uint ("n-stops", "Stops", "Number of stops", 0, G_MAXUINT, 0,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgColorGradient::changed:
	 * @self: the gradient
	 *
	 * Emitted after any change of the stops or the mode.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_color_gradient_init (LrgColorGradient *self)
{
	self->stops = g_array_new (FALSE, FALSE, sizeof (Stop));
	self->mode = LRG_COLOR_GRADIENT_LINEAR;
	self->lut_dirty = TRUE;
}

/* ==========================================================================
 * Construction and conversion
 * ========================================================================== */

LrgColorGradient *
lrg_color_gradient_new (void)
{
	return g_object_new (LRG_TYPE_COLOR_GRADIENT, NULL);
}

LrgColorGradient *
lrg_color_gradient_new_two (const gfloat *start,
                            const gfloat *end)
{
	LrgColorGradient *self;

	g_return_val_if_fail (start != NULL && end != NULL, NULL);

	self = lrg_color_gradient_new ();
	insert_stop (self, 0.0f, start);
	insert_stop (self, 1.0f, end);
	return self;
}

LrgColorGradient *
lrg_color_gradient_new_from_string (const gchar  *text,
                                    GError      **error)
{
	g_autoptr(LrgColorGradient) self = NULL;
	g_auto(GStrv)               parts = NULL;
	const gchar                *colon;
	const gchar                *body;
	guint                       i;

	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	self = lrg_color_gradient_new ();
	body = text;

	/* an optional "mode:" prefix */
	colon = strchr (text, ':');
	if (colon != NULL)
	{
		g_autofree gchar *mode = g_strstrip (g_strndup (text, (gsize) (colon - text)));

		if (g_strcmp0 (mode, "linear") == 0)
			self->mode = LRG_COLOR_GRADIENT_LINEAR;
		else if (g_strcmp0 (mode, "step") == 0)
			self->mode = LRG_COLOR_GRADIENT_STEP;
		else if (g_strcmp0 (mode, "smooth") == 0)
			self->mode = LRG_COLOR_GRADIENT_SMOOTH;
		else
		{
			g_set_error (error, LRG_COLOR_GRADIENT_ERROR, LRG_COLOR_GRADIENT_ERROR_PARSE,
			             "unknown gradient mode '%s' (expected linear, step or smooth)", mode);
			return NULL;
		}
		body = colon + 1;
	}

	/* "offset colour" pairs separated by ';', empty pieces skipped */
	parts = g_strsplit (body, ";", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		g_auto(GStrv) fields = NULL;
		gchar        *piece = g_strstrip (parts[i]);
		gchar        *end = NULL;
		gdouble       offset;
		gfloat        rgba[4];

		if (*piece == '\0')
			continue;
		fields = g_strsplit_set (piece, " \t", 2);
		if (fields[0] == NULL || fields[1] == NULL)
		{
			g_set_error (error, LRG_COLOR_GRADIENT_ERROR, LRG_COLOR_GRADIENT_ERROR_PARSE,
			             "stop %u: expected \"offset #rrggbb\", got '%s'", i, piece);
			return NULL;
		}
		offset = g_ascii_strtod (fields[0], &end);
		if (end == fields[0] || *end != '\0' || !isfinite (offset) || offset < 0.0 || offset > 1.0)
		{
			g_set_error (error, LRG_COLOR_GRADIENT_ERROR, LRG_COLOR_GRADIENT_ERROR_PARSE,
			             "stop %u: offset '%s' is not a number from 0 to 1", i, fields[0]);
			return NULL;
		}
		if (!parse_hex_color (g_strstrip (fields[1]), rgba))
		{
			g_set_error (error, LRG_COLOR_GRADIENT_ERROR, LRG_COLOR_GRADIENT_ERROR_PARSE,
			             "stop %u: '%s' is not a #rgb, #rrggbb or #rrggbbaa colour", i, fields[1]);
			return NULL;
		}
		insert_stop (self, (gfloat) offset, rgba);
	}
	self->lut_dirty = TRUE;
	return g_steal_pointer (&self);
}

LrgColorGradient *
lrg_color_gradient_new_from_stops (const LrgGradientStops *stops)
{
	LrgColorGradient *self;

	g_return_val_if_fail (stops != NULL, NULL);

	self = lrg_color_gradient_new ();
	lrg_color_gradient_set_from_stops (self, stops);
	return self;
}

void
lrg_color_gradient_set_from_stops (LrgColorGradient       *self,
                                   const LrgGradientStops *stops)
{
	guint i, n;

	g_return_if_fail (LRG_IS_COLOR_GRADIENT (self));

	g_array_set_size (self->stops, 0);
	n = stops != NULL ? lrg_gradient_stops_get_n_stops (stops) : 0;
	for (i = 0; i < n; i++)
	{
		gfloat   offset = 0.0f;
		GrlColor c;
		gfloat   rgba[4];

		lrg_gradient_stops_get_stop (stops, i, &offset, &c);
		rgba[0] = (gfloat) c.r / 255.0f;
		rgba[1] = (gfloat) c.g / 255.0f;
		rgba[2] = (gfloat) c.b / 255.0f;
		rgba[3] = (gfloat) c.a / 255.0f;
		insert_stop (self, offset, rgba);
	}
	touch (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_STOPS]);
}

LrgGradientStops *
lrg_color_gradient_to_stops (LrgColorGradient *self)
{
	LrgGradientStops *stops;
	guint             i;

	g_return_val_if_fail (LRG_IS_COLOR_GRADIENT (self), NULL);

	stops = lrg_gradient_stops_new ();
	for (i = 0; i < self->stops->len; i++)
	{
		const Stop *s = &g_array_index (self->stops, Stop, i);
		GrlColor    c;

		c.r = to_byte (s->rgba[0]);
		c.g = to_byte (s->rgba[1]);
		c.b = to_byte (s->rgba[2]);
		c.a = to_byte (s->rgba[3]);
		lrg_gradient_stops_add (stops, s->offset, &c);
	}
	return stops;
}

gchar *
lrg_color_gradient_to_string (LrgColorGradient *self)
{
	GString *out;
	guint    i;

	g_return_val_if_fail (LRG_IS_COLOR_GRADIENT (self), NULL);

	out = g_string_new (NULL);
	if (self->mode == LRG_COLOR_GRADIENT_STEP)
		g_string_append (out, "step: ");
	else if (self->mode == LRG_COLOR_GRADIENT_SMOOTH)
		g_string_append (out, "smooth: ");
	for (i = 0; i < self->stops->len; i++)
	{
		const Stop *s = &g_array_index (self->stops, Stop, i);
		gchar       num[G_ASCII_DTOSTR_BUF_SIZE];

		/* %.9g reads back to the same float */
		g_ascii_formatd (num, sizeof num, "%.9g", (gdouble) s->offset);
		g_string_append_printf (out, "%s%s #%02x%02x%02x%02x", i > 0 ? "; " : "", num,
		                        to_byte (s->rgba[0]), to_byte (s->rgba[1]),
		                        to_byte (s->rgba[2]), to_byte (s->rgba[3]));
	}
	return g_string_free (out, FALSE);
}

LrgColorGradient *
lrg_color_gradient_copy (LrgColorGradient *self)
{
	LrgColorGradient *copy;

	g_return_val_if_fail (LRG_IS_COLOR_GRADIENT (self), NULL);

	copy = lrg_color_gradient_new ();
	copy->mode = self->mode;
	g_array_append_vals (copy->stops, self->stops->data, self->stops->len);
	return copy;
}

gboolean
lrg_color_gradient_equal (LrgColorGradient *a,
                          LrgColorGradient *b)
{
	guint i;

	if (a == NULL || b == NULL)
		return a == b;
	g_return_val_if_fail (LRG_IS_COLOR_GRADIENT (a) && LRG_IS_COLOR_GRADIENT (b), FALSE);

	if (a->mode != b->mode || a->stops->len != b->stops->len)
		return FALSE;
	for (i = 0; i < a->stops->len; i++)
	{
		const Stop *sa = &g_array_index (a->stops, Stop, i);
		const Stop *sb = &g_array_index (b->stops, Stop, i);

		if (sa->offset != sb->offset || memcmp (sa->rgba, sb->rgba, sizeof sa->rgba) != 0)
			return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Stops
 * ========================================================================== */

LrgColorGradientMode
lrg_color_gradient_get_mode (LrgColorGradient *self)
{
	g_return_val_if_fail (LRG_IS_COLOR_GRADIENT (self), LRG_COLOR_GRADIENT_LINEAR);
	return self->mode;
}

void
lrg_color_gradient_set_mode (LrgColorGradient     *self,
                             LrgColorGradientMode  mode)
{
	g_return_if_fail (LRG_IS_COLOR_GRADIENT (self));

	if (self->mode == mode)
		return;
	self->mode = mode;
	touch (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

guint
lrg_color_gradient_get_n_stops (LrgColorGradient *self)
{
	g_return_val_if_fail (LRG_IS_COLOR_GRADIENT (self), 0);
	return self->stops->len;
}

gboolean
lrg_color_gradient_get_stop (LrgColorGradient *self,
                             guint             index,
                             gfloat           *offset,
                             gfloat           *color)
{
	const Stop *s;

	g_return_val_if_fail (LRG_IS_COLOR_GRADIENT (self), FALSE);

	if (index >= self->stops->len)
	{
		if (offset != NULL)
			*offset = 0.0f;
		if (color != NULL)
			memset (color, 0, 4 * sizeof (gfloat));
		return FALSE;
	}
	s = &g_array_index (self->stops, Stop, index);
	if (offset != NULL)
		*offset = s->offset;
	if (color != NULL)
		memcpy (color, s->rgba, sizeof s->rgba);
	return TRUE;
}

guint
lrg_color_gradient_add_stop (LrgColorGradient *self,
                             gfloat            offset,
                             const gfloat     *color)
{
	guint at;

	g_return_val_if_fail (LRG_IS_COLOR_GRADIENT (self), 0);
	g_return_val_if_fail (color != NULL, 0);

	at = insert_stop (self, offset, color);
	touch (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_STOPS]);
	return at;
}

gint
lrg_color_gradient_set_stop (LrgColorGradient *self,
                             guint             index,
                             gfloat            offset,
                             const gfloat     *color)
{
	Stop  s;
	guint at;

	g_return_val_if_fail (LRG_IS_COLOR_GRADIENT (self), -1);

	if (index >= self->stops->len)
		return -1;
	s = g_array_index (self->stops, Stop, index);
	g_array_remove_index (self->stops, index);
	at = insert_stop (self, offset, color != NULL ? color : s.rgba);
	touch (self);
	return (gint) at;
}

gboolean
lrg_color_gradient_remove_stop (LrgColorGradient *self,
                                guint             index)
{
	g_return_val_if_fail (LRG_IS_COLOR_GRADIENT (self), FALSE);

	if (index >= self->stops->len)
		return FALSE;
	g_array_remove_index (self->stops, index);
	touch (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_STOPS]);
	return TRUE;
}

void
lrg_color_gradient_clear (LrgColorGradient *self)
{
	g_return_if_fail (LRG_IS_COLOR_GRADIENT (self));

	if (self->stops->len == 0)
		return;
	g_array_set_size (self->stops, 0);
	touch (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_STOPS]);
}

/* ==========================================================================
 * Evaluation
 * ========================================================================== */

void
lrg_color_gradient_evaluate (LrgColorGradient *self,
                             gfloat            t,
                             gfloat           *out)
{
	const Stop *a;
	const Stop *b;
	guint       hi;
	gfloat      f;
	gint        k;

	g_return_if_fail (LRG_IS_COLOR_GRADIENT (self));
	g_return_if_fail (out != NULL);

	/* Empty: the neutral multiplier */
	if (self->stops->len == 0)
	{
		out[0] = out[1] = out[2] = out[3] = 1.0f;
		return;
	}

	t = clamp01 (t);
	a = &g_array_index (self->stops, Stop, 0);
	if (self->stops->len == 1 || t <= a->offset)
	{
		memcpy (out, a->rgba, 4 * sizeof (gfloat));
		return;
	}
	b = &g_array_index (self->stops, Stop, self->stops->len - 1);
	if (t >= b->offset)
	{
		memcpy (out, b->rgba, 4 * sizeof (gfloat));
		return;
	}

	/* The segment [hi - 1, hi] holds t: hi is the first stop after t */
	hi = upper_bound (self, t);
	a = &g_array_index (self->stops, Stop, hi - 1);
	b = &g_array_index (self->stops, Stop, hi);
	if (self->mode == LRG_COLOR_GRADIENT_STEP || b->offset <= a->offset)
	{
		memcpy (out, a->rgba, 4 * sizeof (gfloat));
		return;
	}
	f = (t - a->offset) / (b->offset - a->offset);
	if (self->mode == LRG_COLOR_GRADIENT_SMOOTH)
		f = f * f * (3.0f - 2.0f * f);
	for (k = 0; k < 4; k++)
		out[k] = a->rgba[k] + (b->rgba[k] - a->rgba[k]) * f;
}

void
lrg_color_gradient_evaluate_fast (LrgColorGradient *self,
                                  gfloat            t,
                                  gfloat           *out)
{
	gfloat x;
	gint   i0;
	gfloat f;
	gint   k;

	g_return_if_fail (LRG_IS_COLOR_GRADIENT (self));

	/* Rebake the table after a change */
	if (self->lut_dirty)
	{
		gint i;

		for (i = 0; i < LRG_COLOR_GRADIENT_LUT_SIZE; i++)
			lrg_color_gradient_evaluate (self, (gfloat) i / (gfloat) (LRG_COLOR_GRADIENT_LUT_SIZE - 1),
			                             self->lut[i]);
		self->lut_dirty = FALSE;
	}

	/* Blend the two nearest entries (steps stay hard: nearest lower) */
	x = clamp01 (t) * (gfloat) (LRG_COLOR_GRADIENT_LUT_SIZE - 1);
	i0 = (gint) x;
	if (i0 >= LRG_COLOR_GRADIENT_LUT_SIZE - 1)
	{
		memcpy (out, self->lut[LRG_COLOR_GRADIENT_LUT_SIZE - 1], 4 * sizeof (gfloat));
		return;
	}
	f = self->mode == LRG_COLOR_GRADIENT_STEP ? 0.0f : x - (gfloat) i0;
	for (k = 0; k < 4; k++)
		out[k] = self->lut[i0][k] + (self->lut[i0 + 1][k] - self->lut[i0][k]) * f;
}

void
lrg_color_gradient_evaluate_color (LrgColorGradient *self,
                                   gfloat            t,
                                   GrlColor         *out)
{
	gfloat rgba[4];

	g_return_if_fail (out != NULL);

	lrg_color_gradient_evaluate (self, t, rgba);
	out->r = to_byte (rgba[0]);
	out->g = to_byte (rgba[1]);
	out->b = to_byte (rgba[2]);
	out->a = to_byte (rgba[3]);
}
