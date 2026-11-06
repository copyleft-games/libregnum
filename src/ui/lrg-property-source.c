/* lrg-property-source.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPropertySource: the reflected property interface edited by
 * LrgPropertyGrid, its LrgPropertyInfo descriptions, hint parsing,
 * value comparison and text conversion, and LrgObjectPropertySource,
 * which exposes any GObject's properties.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <graylib.h>

#include "lrg-property-source.h"
#include "lrg-gradient-edit.h"
#include "lrg-style.h"
#include "../tween/lrg-keyframe-curve.h"
#include "../lrg-log.h"

/* ==========================================================================
 * LrgPropertyState
 * ========================================================================== */

GType
lrg_property_state_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_PROPERTY_STATE_NONE, "LRG_PROPERTY_STATE_NONE", "none" },
			{ LRG_PROPERTY_STATE_DEFAULT, "LRG_PROPERTY_STATE_DEFAULT", "default" },
			{ LRG_PROPERTY_STATE_OVERRIDDEN, "LRG_PROPERTY_STATE_OVERRIDDEN", "overridden" },
			{ LRG_PROPERTY_STATE_ANIMATED, "LRG_PROPERTY_STATE_ANIMATED", "animated" },
			{ LRG_PROPERTY_STATE_INVALID, "LRG_PROPERTY_STATE_INVALID", "invalid" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgPropertyState"), values);

		g_once_init_leave (&type_id, id);
	}
	return type_id;
}

/* ==========================================================================
 * Hints
 * ========================================================================== */

G_DEFINE_QUARK (lrg-property-hint, lrg_property_hint)
G_DEFINE_QUARK (lrg-property-group, lrg_property_group)

void
lrg_property_hint_split (const gchar  *hint,
                         gchar       **kind,
                         gchar       **arg)
{
	const gchar *colon;

	if (kind != NULL)
		*kind = NULL;
	if (arg != NULL)
		*arg = NULL;
	if (hint == NULL)
		return;

	colon = strchr (hint, ':');
	if (kind != NULL)
		*kind = colon != NULL ? g_strndup (hint, (gsize) (colon - hint)) : g_strdup (hint);
	if (arg != NULL && colon != NULL)
		*arg = g_strdup (colon + 1);
}

gboolean
lrg_property_hint_parse_range (const gchar *hint,
                               gdouble     *min,
                               gdouble     *max,
                               gdouble     *step)
{
	g_auto(GStrv) parts = NULL;
	gdouble       v[3] = { 0.0, 0.0, 0.0 };
	guint         n;
	guint         i;

	if (hint == NULL || !g_str_has_prefix (hint, "range:"))
		return FALSE;

	/* "range:min,max[,step]" with C-locale numbers */
	parts = g_strsplit (hint + strlen ("range:"), ",", 4);
	n = g_strv_length (parts);
	if (n < 2 || n > 3)
		return FALSE;
	for (i = 0; i < n; i++)
	{
		gchar *end = NULL;
		gchar *p = g_strstrip (parts[i]);

		v[i] = g_ascii_strtod (p, &end);
		if (end == p || *end != '\0' || !isfinite (v[i]))
			return FALSE;
	}
	if (v[1] < v[0])
		return FALSE;

	if (min != NULL)
		*min = v[0];
	if (max != NULL)
		*max = v[1];
	if (step != NULL)
		*step = n == 3 ? v[2] : 0.0;
	return TRUE;
}

/* ==========================================================================
 * Value helpers
 * ========================================================================== */

/* curves_equal: same key count, times, values and easings */
static gboolean
curves_equal (LrgKeyframeCurve *a,
              LrgKeyframeCurve *b)
{
	guint n;
	guint i;

	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL)
		return FALSE;
	n = lrg_keyframe_curve_get_key_count (a);
	if (n != lrg_keyframe_curve_get_key_count (b))
		return FALSE;
	for (i = 0; i < n; i++)
	{
		gfloat        ta;
		gfloat        tb;
		gfloat        va;
		gfloat        vb;
		LrgEasingType ea;
		LrgEasingType eb;

		lrg_keyframe_curve_get_key (a, i, &ta, &va, &ea);
		lrg_keyframe_curve_get_key (b, i, &tb, &vb, &eb);
		if (ta != tb || va != vb || ea != eb)
			return FALSE;
	}
	return TRUE;
}

gboolean
lrg_property_values_equal (const GValue *a,
                           const GValue *b)
{
	GType type;

	g_return_val_if_fail (G_IS_VALUE (a), FALSE);
	g_return_val_if_fail (G_IS_VALUE (b), FALSE);

	type = G_VALUE_TYPE (a);
	if (type != G_VALUE_TYPE (b))
		return FALSE;

	/* Boxed types the grid knows: compare contents, not pointers */
	if (type == GRL_TYPE_COLOR)
	{
		const GrlColor *ca = g_value_get_boxed (a);
		const GrlColor *cb = g_value_get_boxed (b);

		if (ca == NULL || cb == NULL)
			return ca == cb;
		return ca->r == cb->r && ca->g == cb->g && ca->b == cb->b && ca->a == cb->a;
	}
	if (type == GRL_TYPE_VECTOR2)
	{
		const GrlVector2 *va = g_value_get_boxed (a);
		const GrlVector2 *vb = g_value_get_boxed (b);

		if (va == NULL || vb == NULL)
			return va == vb;
		return va->x == vb->x && va->y == vb->y;
	}
	if (type == GRL_TYPE_VECTOR3)
	{
		const GrlVector3 *va = g_value_get_boxed (a);
		const GrlVector3 *vb = g_value_get_boxed (b);

		if (va == NULL || vb == NULL)
			return va == vb;
		return va->x == vb->x && va->y == vb->y && va->z == vb->z;
	}
	if (type == GRL_TYPE_VECTOR4)
	{
		const GrlVector4 *va = g_value_get_boxed (a);
		const GrlVector4 *vb = g_value_get_boxed (b);

		if (va == NULL || vb == NULL)
			return va == vb;
		return va->x == vb->x && va->y == vb->y && va->z == vb->z && va->w == vb->w;
	}
	if (type == LRG_TYPE_GRADIENT_STOPS)
		return lrg_gradient_stops_equal (g_value_get_boxed (a), g_value_get_boxed (b));
	if (type == G_TYPE_STRV)
	{
		const gchar * const *sa = g_value_get_boxed (a);
		const gchar * const *sb = g_value_get_boxed (b);

		if (sa == NULL || sb == NULL)
			return (sa == NULL || sa[0] == NULL) && (sb == NULL || sb[0] == NULL);
		return g_strv_equal (sa, sb);
	}
	if (type == G_TYPE_VARIANT)
	{
		GVariant *va = g_value_get_variant (a);
		GVariant *vb = g_value_get_variant (b);

		if (va == NULL || vb == NULL)
			return va == vb;
		return g_variant_equal (va, vb);
	}
	if (g_type_is_a (type, LRG_TYPE_KEYFRAME_CURVE))
		return curves_equal (g_value_get_object (a), g_value_get_object (b));

	/* Everything else: by fundamental type */
	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_BOOLEAN:
		return g_value_get_boolean (a) == g_value_get_boolean (b);
	case G_TYPE_CHAR:
		return g_value_get_schar (a) == g_value_get_schar (b);
	case G_TYPE_UCHAR:
		return g_value_get_uchar (a) == g_value_get_uchar (b);
	case G_TYPE_INT:
		return g_value_get_int (a) == g_value_get_int (b);
	case G_TYPE_UINT:
		return g_value_get_uint (a) == g_value_get_uint (b);
	case G_TYPE_LONG:
		return g_value_get_long (a) == g_value_get_long (b);
	case G_TYPE_ULONG:
		return g_value_get_ulong (a) == g_value_get_ulong (b);
	case G_TYPE_INT64:
		return g_value_get_int64 (a) == g_value_get_int64 (b);
	case G_TYPE_UINT64:
		return g_value_get_uint64 (a) == g_value_get_uint64 (b);
	case G_TYPE_FLOAT:
		return g_value_get_float (a) == g_value_get_float (b);
	case G_TYPE_DOUBLE:
		return g_value_get_double (a) == g_value_get_double (b);
	case G_TYPE_ENUM:
		return g_value_get_enum (a) == g_value_get_enum (b);
	case G_TYPE_FLAGS:
		return g_value_get_flags (a) == g_value_get_flags (b);
	case G_TYPE_STRING:
		return g_strcmp0 (g_value_get_string (a), g_value_get_string (b)) == 0;
	case G_TYPE_OBJECT:
	case G_TYPE_INTERFACE:
		return g_value_get_object (a) == g_value_get_object (b);
	case G_TYPE_BOXED:
	case G_TYPE_POINTER:
		return g_value_peek_pointer (a) == g_value_peek_pointer (b);
	default:
		return FALSE;
	}
}

/* format_double: shortest round-tripping C-locale text */
static gchar *
format_double (gdouble v)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	g_ascii_formatd (buf, sizeof (buf), "%.9g", v);
	return g_strdup (buf);
}

gchar *
lrg_property_value_to_string (const GValue *value)
{
	GType type;

	g_return_val_if_fail (G_IS_VALUE (value), NULL);

	type = G_VALUE_TYPE (value);
	if (type == GRL_TYPE_COLOR)
	{
		const GrlColor *c = g_value_get_boxed (value);

		return c != NULL ? g_strdup_printf ("#%02x%02x%02x%02x", c->r, c->g, c->b, c->a) : g_strdup ("");
	}
	if (type == GRL_TYPE_VECTOR2 || type == GRL_TYPE_VECTOR3 || type == GRL_TYPE_VECTOR4)
	{
		const gfloat *f = g_value_get_boxed (value);
		guint         n = type == GRL_TYPE_VECTOR2 ? 2 : type == GRL_TYPE_VECTOR3 ? 3 : 4;
		GString      *out = g_string_new (NULL);
		guint         i;

		/* The GrlVectorN structs are plain runs of floats */
		for (i = 0; f != NULL && i < n; i++)
		{
			g_autofree gchar *num = format_double ((gdouble) f[i]);

			g_string_append_printf (out, "%s%s", i > 0 ? ", " : "", num);
		}
		return g_string_free (out, FALSE);
	}
	if (type == LRG_TYPE_GRADIENT_STOPS)
	{
		const LrgGradientStops *s = g_value_get_boxed (value);

		return s != NULL ? lrg_gradient_stops_to_string (s) : g_strdup ("");
	}
	if (type == G_TYPE_STRV)
	{
		const gchar * const *v = g_value_get_boxed (value);

		return v != NULL ? g_strjoinv ("\n", (gchar **) v) : g_strdup ("");
	}
	if (type == G_TYPE_VARIANT)
	{
		GVariant *v = g_value_get_variant (value);

		return v != NULL ? g_variant_print (v, FALSE) : g_strdup ("");
	}

	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_BOOLEAN:
		return g_strdup (g_value_get_boolean (value) ? "true" : "false");
	case G_TYPE_FLOAT:
		return format_double ((gdouble) g_value_get_float (value));
	case G_TYPE_DOUBLE:
		return format_double (g_value_get_double (value));
	case G_TYPE_STRING:
		return g_strdup (g_value_get_string (value) != NULL ? g_value_get_string (value) : "");
	case G_TYPE_ENUM:
		{
			g_autoptr(GEnumClass) klass = g_type_class_ref (type);
			GEnumValue           *ev = g_enum_get_value (klass, g_value_get_enum (value));

			return ev != NULL ? g_strdup (ev->value_nick) : g_strdup_printf ("%d", g_value_get_enum (value));
		}
	case G_TYPE_FLAGS:
		{
			g_autoptr(GFlagsClass) klass = g_type_class_ref (type);
			GString               *out = g_string_new (NULL);
			guint                  bits = g_value_get_flags (value);
			guint                  i;

			for (i = 0; i < klass->n_values; i++)
			{
				GFlagsValue *fv = &klass->values[i];

				if (fv->value != 0 && (bits & fv->value) == fv->value)
					g_string_append_printf (out, "%s%s", out->len > 0 ? "|" : "", fv->value_nick);
			}
			return g_string_free (out, FALSE);
		}
	case G_TYPE_OBJECT:
		{
			GObject *obj = g_value_get_object (value);

			return obj != NULL ? g_strdup (G_OBJECT_TYPE_NAME (obj)) : g_strdup ("");
		}
	default:
		break;
	}

	/* Integers and anything else transformable to a string */
	if (g_value_type_transformable (type, G_TYPE_STRING))
	{
		g_auto(GValue) s = G_VALUE_INIT;

		g_value_init (&s, G_TYPE_STRING);
		g_value_transform (value, &s);
		return g_value_dup_string (&s);
	}
	return g_strdup_value_contents (value);
}

/* parse_floats: "x, y[, z ...]" into @out, exactly @n numbers */
static gboolean
parse_floats (const gchar *text,
              gfloat      *out,
              guint        n)
{
	g_auto(GStrv) parts = g_strsplit_set (text, ", \t", -1);
	guint         got = 0;
	guint         i;

	for (i = 0; parts[i] != NULL; i++)
	{
		gchar  *end = NULL;
		gdouble v;

		if (*parts[i] == '\0')
			continue;
		if (got == n)
			return FALSE;
		v = g_ascii_strtod (parts[i], &end);
		if (end == parts[i] || *end != '\0' || !isfinite (v))
			return FALSE;
		out[got++] = (gfloat) v;
	}
	return got == n;
}

gboolean
lrg_property_value_from_string (GValue      *value,
                                const gchar *text)
{
	GType type;

	g_return_val_if_fail (G_IS_VALUE (value), FALSE);
	g_return_val_if_fail (text != NULL, FALSE);

	type = G_VALUE_TYPE (value);
	if (type == GRL_TYPE_COLOR)
	{
		g_autofree gchar *trimmed = g_strstrip (g_strdup (text));
		GrlColor          c;

		if (!lrg_color_parse (trimmed, &c))
			return FALSE;
		g_value_set_boxed (value, &c);
		return TRUE;
	}
	if (type == GRL_TYPE_VECTOR2 || type == GRL_TYPE_VECTOR3 || type == GRL_TYPE_VECTOR4)
	{
		gfloat f[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		guint  n = type == GRL_TYPE_VECTOR2 ? 2 : type == GRL_TYPE_VECTOR3 ? 3 : 4;

		if (!parse_floats (text, f, n))
			return FALSE;
		g_value_set_boxed (value, f);
		return TRUE;
	}
	if (type == LRG_TYPE_GRADIENT_STOPS)
	{
		LrgGradientStops *s = lrg_gradient_stops_new_from_string (text);

		if (s == NULL)
			return FALSE;
		g_value_take_boxed (value, s);
		return TRUE;
	}
	if (type == G_TYPE_STRV)
	{
		g_value_take_boxed (value, *text != '\0' ? g_strsplit (text, "\n", -1) : g_new0 (gchar *, 1));
		return TRUE;
	}
	if (type == G_TYPE_VARIANT)
	{
		GVariant *v = g_variant_parse (NULL, text, NULL, NULL, NULL);

		if (v == NULL)
			return FALSE;
		g_value_take_variant (value, g_variant_ref_sink (v));
		return TRUE;
	}

	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_BOOLEAN:
		if (g_ascii_strcasecmp (text, "true") == 0 || g_strcmp0 (text, "1") == 0)
			g_value_set_boolean (value, TRUE);
		else if (g_ascii_strcasecmp (text, "false") == 0 || g_strcmp0 (text, "0") == 0)
			g_value_set_boolean (value, FALSE);
		else
			return FALSE;
		return TRUE;
	case G_TYPE_STRING:
		g_value_set_string (value, text);
		return TRUE;
	case G_TYPE_ENUM:
		{
			g_autoptr(GEnumClass) klass = g_type_class_ref (type);
			GEnumValue           *ev = g_enum_get_value_by_nick (klass, text);

			if (ev == NULL)
				ev = g_enum_get_value_by_name (klass, text);
			if (ev == NULL)
				return FALSE;
			g_value_set_enum (value, ev->value);
			return TRUE;
		}
	case G_TYPE_FLAGS:
		{
			g_autoptr(GFlagsClass) klass = g_type_class_ref (type);
			g_auto(GStrv)          parts = g_strsplit (text, "|", -1);
			guint                  bits = 0;
			guint                  i;

			for (i = 0; parts[i] != NULL; i++)
			{
				GFlagsValue *fv;
				gchar       *p = g_strstrip (parts[i]);

				if (*p == '\0')
					continue;
				fv = g_flags_get_value_by_nick (klass, p);
				if (fv == NULL)
					fv = g_flags_get_value_by_name (klass, p);
				if (fv == NULL)
					return FALSE;
				bits |= fv->value;
			}
			g_value_set_flags (value, bits);
			return TRUE;
		}
	case G_TYPE_CHAR:
	case G_TYPE_UCHAR:
	case G_TYPE_INT:
	case G_TYPE_UINT:
	case G_TYPE_LONG:
	case G_TYPE_ULONG:
	case G_TYPE_INT64:
	case G_TYPE_UINT64:
	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
		{
			g_auto(GValue) d = G_VALUE_INIT;
			gchar         *end = NULL;
			gdouble        v = g_ascii_strtod (text, &end);

			/* Numbers go through a double, then GLib's transforms */
			if (end == text || *end != '\0' || !isfinite (v))
				return FALSE;
			if (G_TYPE_FUNDAMENTAL (type) != G_TYPE_FLOAT && G_TYPE_FUNDAMENTAL (type) != G_TYPE_DOUBLE)
				v = round (v);
			g_value_init (&d, G_TYPE_DOUBLE);
			g_value_set_double (&d, v);
			return g_value_transform (&d, value);
		}
	default:
		return FALSE;
	}
}

/* ==========================================================================
 * LrgPropertyInfo
 * ========================================================================== */

struct _LrgPropertyInfo
{
	gint        ref_count;
	gchar      *name;
	gchar      *label;
	gchar      *group;
	GParamSpec *pspec;
	gchar      *hint;
	gchar      *tooltip;
	gboolean    read_only;
};

G_DEFINE_BOXED_TYPE (LrgPropertyInfo, lrg_property_info, lrg_property_info_ref, lrg_property_info_unref)

/* pretty_label: "max-hp" -> "Max hp" */
static gchar *
pretty_label (const gchar *name)
{
	gchar *label = g_strdup (name);
	gchar *p;

	for (p = label; *p != '\0'; p++)
		if (*p == '-' || *p == '_')
			*p = ' ';
	if (label[0] != '\0')
		label[0] = g_ascii_toupper (label[0]);
	return label;
}

/*
 * parse_blurb:
 *
 * Strips leading "[key:value]" prefixes from @blurb: "[hint:...]" and
 * "[group:...]" fill @hint and @group, any other key is dropped. The
 * rest, trimmed, is the tooltip (NULL when empty).
 */
static void
parse_blurb (const gchar  *blurb,
             gchar       **hint,
             gchar       **group,
             gchar       **tooltip)
{
	const gchar *p = blurb;

	*hint = NULL;
	*group = NULL;
	*tooltip = NULL;
	if (blurb == NULL)
		return;

	while (*p == '[')
	{
		const gchar *close = strchr (p, ']');
		const gchar *colon = strchr (p, ':');

		if (close == NULL || colon == NULL || colon > close)
			break;
		if (g_str_has_prefix (p, "[hint:"))
		{
			g_free (*hint);
			*hint = g_strndup (colon + 1, (gsize) (close - colon - 1));
		}
		else if (g_str_has_prefix (p, "[group:"))
		{
			g_free (*group);
			*group = g_strndup (colon + 1, (gsize) (close - colon - 1));
		}
		p = close + 1;
		while (*p == ' ')
			p++;
	}

	if (*p != '\0')
		*tooltip = g_strstrip (g_strdup (p));
}

LrgPropertyInfo *
lrg_property_info_new (GParamSpec *pspec)
{
	LrgPropertyInfo *self;
	const gchar     *nick;
	const gchar     *q_hint;
	const gchar     *q_group;
	gchar           *b_hint = NULL;
	gchar           *b_group = NULL;
	gchar           *tooltip = NULL;

	g_return_val_if_fail (G_IS_PARAM_SPEC (pspec), NULL);

	self = g_new0 (LrgPropertyInfo, 1);
	self->ref_count = 1;
	self->pspec = g_param_spec_ref_sink (pspec);
	self->name = g_strdup (g_param_spec_get_name (pspec));

	/* Label: an explicit nick, else the prettified name */
	nick = g_param_spec_get_nick (pspec);
	if (nick != NULL && g_strcmp0 (nick, self->name) != 0)
		self->label = g_strdup (nick);
	else
		self->label = pretty_label (self->name);

	/* Hint and group: qdata wins over blurb prefixes */
	parse_blurb (g_param_spec_get_blurb (pspec), &b_hint, &b_group, &tooltip);
	q_hint = g_param_spec_get_qdata (pspec, lrg_property_hint_quark ());
	q_group = g_param_spec_get_qdata (pspec, lrg_property_group_quark ());
	self->hint = q_hint != NULL ? g_strdup (q_hint) : g_steal_pointer (&b_hint);
	self->group = q_group != NULL ? g_strdup (q_group) : g_steal_pointer (&b_group);
	self->tooltip = tooltip;
	g_free (b_hint);
	g_free (b_group);

	self->read_only = !(pspec->flags & G_PARAM_WRITABLE) || (pspec->flags & G_PARAM_CONSTRUCT_ONLY);
	return self;
}

LrgPropertyInfo *
lrg_property_info_ref (LrgPropertyInfo *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	g_atomic_int_inc (&self->ref_count);
	return self;
}

void
lrg_property_info_unref (LrgPropertyInfo *self)
{
	g_return_if_fail (self != NULL);

	if (!g_atomic_int_dec_and_test (&self->ref_count))
		return;
	g_free (self->name);
	g_free (self->label);
	g_free (self->group);
	g_free (self->hint);
	g_free (self->tooltip);
	g_param_spec_unref (self->pspec);
	g_free (self);
}

const gchar *
lrg_property_info_get_name (const LrgPropertyInfo *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->name;
}

const gchar *
lrg_property_info_get_label (const LrgPropertyInfo *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->label;
}

void
lrg_property_info_set_label (LrgPropertyInfo *self,
                             const gchar     *label)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (label != NULL);
	g_free (self->label);
	self->label = g_strdup (label);
}

const gchar *
lrg_property_info_get_group (const LrgPropertyInfo *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->group;
}

void
lrg_property_info_set_group (LrgPropertyInfo *self,
                             const gchar     *group)
{
	g_return_if_fail (self != NULL);
	g_free (self->group);
	self->group = g_strdup (group);
}

GParamSpec *
lrg_property_info_get_pspec (const LrgPropertyInfo *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->pspec;
}

GType
lrg_property_info_get_value_type (const LrgPropertyInfo *self)
{
	g_return_val_if_fail (self != NULL, G_TYPE_INVALID);
	return G_PARAM_SPEC_VALUE_TYPE (self->pspec);
}

const gchar *
lrg_property_info_get_hint (const LrgPropertyInfo *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->hint;
}

void
lrg_property_info_set_hint (LrgPropertyInfo *self,
                            const gchar     *hint)
{
	g_return_if_fail (self != NULL);
	g_free (self->hint);
	self->hint = g_strdup (hint);
}

gboolean
lrg_property_info_get_read_only (const LrgPropertyInfo *self)
{
	g_return_val_if_fail (self != NULL, TRUE);
	return self->read_only;
}

void
lrg_property_info_set_read_only (LrgPropertyInfo *self,
                                 gboolean         read_only)
{
	g_return_if_fail (self != NULL);
	self->read_only = !!read_only;
}

const gchar *
lrg_property_info_get_tooltip (const LrgPropertyInfo *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return self->tooltip;
}

void
lrg_property_info_set_tooltip (LrgPropertyInfo *self,
                               const gchar     *tooltip)
{
	g_return_if_fail (self != NULL);
	g_free (self->tooltip);
	self->tooltip = g_strdup (tooltip);
}

/* ==========================================================================
 * LrgPropertySource interface
 * ========================================================================== */

G_DEFINE_INTERFACE (LrgPropertySource, lrg_property_source, G_TYPE_OBJECT)

enum
{
	SOURCE_SIGNAL_PROPERTY_CHANGED,
	SOURCE_N_SIGNALS
};

static guint source_signals[SOURCE_N_SIGNALS];

/* default_get_state: DEFAULT when the value equals the pspec default */
static LrgPropertyState
lrg_property_source_real_get_state (LrgPropertySource *self,
                                    const gchar       *name)
{
	g_autoptr(LrgPropertyInfo) info = lrg_property_source_find_property (self, name);
	g_auto(GValue)             value = G_VALUE_INIT;
	const GValue              *def;

	if (info == NULL || !lrg_property_source_get_value (self, name, &value))
		return LRG_PROPERTY_STATE_NONE;
	def = g_param_spec_get_default_value (lrg_property_info_get_pspec (info));
	if (def != NULL && G_VALUE_TYPE (def) == G_VALUE_TYPE (&value) && lrg_property_values_equal (def, &value))
		return LRG_PROPERTY_STATE_DEFAULT;
	return LRG_PROPERTY_STATE_NONE;
}

/* default_reset: store the pspec default */
static gboolean
lrg_property_source_real_reset (LrgPropertySource *self,
                                const gchar       *name)
{
	g_autoptr(LrgPropertyInfo) info = lrg_property_source_find_property (self, name);

	if (info == NULL || lrg_property_info_get_read_only (info))
		return FALSE;
	return lrg_property_source_set_value (self, name,
	                                      g_param_spec_get_default_value (lrg_property_info_get_pspec (info)));
}

static void
lrg_property_source_default_init (LrgPropertySourceInterface *iface)
{
	iface->get_state = lrg_property_source_real_get_state;
	iface->reset = lrg_property_source_real_reset;

	/**
	 * LrgPropertySource::property-changed:
	 * @self: the source
	 * @name: (nullable): the property that changed, or %NULL for "any"
	 *
	 * A value changed, whoever changed it. Detailed by the property
	 * name, so `property-changed::speed` only fires for "speed".
	 */
	source_signals[SOURCE_SIGNAL_PROPERTY_CHANGED] =
		g_signal_new ("property-changed", G_TYPE_FROM_INTERFACE (iface),
		              G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

GPtrArray *
lrg_property_source_list_properties (LrgPropertySource *self)
{
	LrgPropertySourceInterface *iface;

	g_return_val_if_fail (LRG_IS_PROPERTY_SOURCE (self), NULL);

	iface = LRG_PROPERTY_SOURCE_GET_IFACE (self);
	g_return_val_if_fail (iface->list_properties != NULL, NULL);
	return iface->list_properties (self);
}

LrgPropertyInfo *
lrg_property_source_find_property (LrgPropertySource *self,
                                   const gchar       *name)
{
	g_autoptr(GPtrArray) props = NULL;
	guint                i;

	g_return_val_if_fail (LRG_IS_PROPERTY_SOURCE (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	props = lrg_property_source_list_properties (self);
	for (i = 0; props != NULL && i < props->len; i++)
	{
		LrgPropertyInfo *info = g_ptr_array_index (props, i);

		if (g_strcmp0 (lrg_property_info_get_name (info), name) == 0)
			return lrg_property_info_ref (info);
	}
	return NULL;
}

gboolean
lrg_property_source_get_value (LrgPropertySource *self,
                               const gchar       *name,
                               GValue            *value)
{
	LrgPropertySourceInterface *iface;

	g_return_val_if_fail (LRG_IS_PROPERTY_SOURCE (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (value != NULL && !G_IS_VALUE (value), FALSE);

	iface = LRG_PROPERTY_SOURCE_GET_IFACE (self);
	g_return_val_if_fail (iface->get_value != NULL, FALSE);
	return iface->get_value (self, name, value);
}

gboolean
lrg_property_source_set_value (LrgPropertySource *self,
                               const gchar       *name,
                               const GValue      *value)
{
	LrgPropertySourceInterface *iface;

	g_return_val_if_fail (LRG_IS_PROPERTY_SOURCE (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);
	g_return_val_if_fail (G_IS_VALUE (value), FALSE);

	iface = LRG_PROPERTY_SOURCE_GET_IFACE (self);
	g_return_val_if_fail (iface->set_value != NULL, FALSE);
	return iface->set_value (self, name, value);
}

LrgPropertyState
lrg_property_source_get_state (LrgPropertySource *self,
                               const gchar       *name)
{
	LrgPropertySourceInterface *iface;

	g_return_val_if_fail (LRG_IS_PROPERTY_SOURCE (self), LRG_PROPERTY_STATE_NONE);
	g_return_val_if_fail (name != NULL, LRG_PROPERTY_STATE_NONE);

	iface = LRG_PROPERTY_SOURCE_GET_IFACE (self);
	return iface->get_state != NULL ? iface->get_state (self, name) : LRG_PROPERTY_STATE_NONE;
}

gboolean
lrg_property_source_reset (LrgPropertySource *self,
                           const gchar       *name)
{
	LrgPropertySourceInterface *iface;

	g_return_val_if_fail (LRG_IS_PROPERTY_SOURCE (self), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	iface = LRG_PROPERTY_SOURCE_GET_IFACE (self);
	return iface->reset != NULL ? iface->reset (self, name) : FALSE;
}

void
lrg_property_source_emit_changed (LrgPropertySource *self,
                                  const gchar       *name)
{
	g_return_if_fail (LRG_IS_PROPERTY_SOURCE (self));

	g_signal_emit (self, source_signals[SOURCE_SIGNAL_PROPERTY_CHANGED],
	               name != NULL ? g_quark_from_string (name) : 0, name);
}

/* ==========================================================================
 * LrgObjectPropertySource
 * ========================================================================== */

struct _LrgObjectPropertySource
{
	GObject     parent_instance;

	GObject    *object;
	gulong      notify_id;
	GHashTable *hidden;      /* name -> NULL */
	GType       owner_type;  /* G_TYPE_INVALID = all */
};

static void lrg_object_property_source_iface_init (LrgPropertySourceInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgObjectPropertySource, lrg_object_property_source, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_PROPERTY_SOURCE,
                                                      lrg_object_property_source_iface_init))

enum
{
	OPS_PROP_0,
	OPS_PROP_OBJECT,
	OPS_N_PROPS
};

static GParamSpec *ops_properties[OPS_N_PROPS];

/* listed: whether @pspec is shown */
static gboolean
listed (LrgObjectPropertySource *self,
        GParamSpec              *pspec)
{
	if (!(pspec->flags & G_PARAM_READABLE) || (pspec->flags & G_PARAM_CONSTRUCT_ONLY))
		return FALSE;
	if (g_hash_table_contains (self->hidden, pspec->name))
		return FALSE;
	if (self->owner_type != G_TYPE_INVALID && !g_type_is_a (pspec->owner_type, self->owner_type))
		return FALSE;
	return TRUE;
}

static GPtrArray *
ops_list_properties (LrgPropertySource *source)
{
	LrgObjectPropertySource *self = LRG_OBJECT_PROPERTY_SOURCE (source);
	GPtrArray               *out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_property_info_unref);
	g_autofree GParamSpec  **pspecs = NULL;
	guint                    n = 0;
	guint                    i;

	pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (self->object), &n);
	for (i = 0; i < n; i++)
		if (listed (self, pspecs[i]))
			g_ptr_array_add (out, lrg_property_info_new (pspecs[i]));
	return out;
}

/* find_pspec: @name if it is listed */
static GParamSpec *
find_pspec (LrgObjectPropertySource *self,
            const gchar             *name)
{
	GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (self->object), name);

	return pspec != NULL && listed (self, pspec) ? pspec : NULL;
}

static gboolean
ops_get_value (LrgPropertySource *source,
               const gchar       *name,
               GValue            *value)
{
	LrgObjectPropertySource *self = LRG_OBJECT_PROPERTY_SOURCE (source);
	GParamSpec              *pspec = find_pspec (self, name);

	if (pspec == NULL)
		return FALSE;
	g_value_init (value, pspec->value_type);
	g_object_get_property (self->object, name, value);
	return TRUE;
}

static gboolean
ops_set_value (LrgPropertySource *source,
               const gchar       *name,
               const GValue      *value)
{
	LrgObjectPropertySource *self = LRG_OBJECT_PROPERTY_SOURCE (source);
	GParamSpec              *pspec = find_pspec (self, name);

	if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE))
		return FALSE;

	/* Same type: straight through; otherwise via GLib's transforms */
	if (G_VALUE_HOLDS (value, pspec->value_type))
		g_object_set_property (self->object, name, value);
	else
	{
		g_auto(GValue) tmp = G_VALUE_INIT;

		g_value_init (&tmp, pspec->value_type);
		if (!g_value_transform (value, &tmp))
		{
			lrg_warning (LRG_LOG_DOMAIN_UI, "%s: cannot store a %s in property '%s' (%s)",
			             G_OBJECT_TYPE_NAME (self->object), G_VALUE_TYPE_NAME (value), name,
			             g_type_name (pspec->value_type));
			return FALSE;
		}
		g_object_set_property (self->object, name, &tmp);
	}
	return TRUE;
}

static void
lrg_object_property_source_iface_init (LrgPropertySourceInterface *iface)
{
	iface->list_properties = ops_list_properties;
	iface->get_value = ops_get_value;
	iface->set_value = ops_set_value;
}

/* on_notify: GObject::notify -> property-changed for listed properties */
static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           gpointer    user_data)
{
	LrgObjectPropertySource *self = LRG_OBJECT_PROPERTY_SOURCE (user_data);

	if (listed (self, pspec))
		lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self), pspec->name);
}

static void
lrg_object_property_source_dispose (GObject *object)
{
	LrgObjectPropertySource *self = LRG_OBJECT_PROPERTY_SOURCE (object);

	if (self->object != NULL && self->notify_id != 0)
		g_signal_handler_disconnect (self->object, self->notify_id);
	self->notify_id = 0;
	g_clear_object (&self->object);
	G_OBJECT_CLASS (lrg_object_property_source_parent_class)->dispose (object);
}

static void
lrg_object_property_source_finalize (GObject *object)
{
	LrgObjectPropertySource *self = LRG_OBJECT_PROPERTY_SOURCE (object);

	g_hash_table_unref (self->hidden);
	G_OBJECT_CLASS (lrg_object_property_source_parent_class)->finalize (object);
}

static void
lrg_object_property_source_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
	LrgObjectPropertySource *self = LRG_OBJECT_PROPERTY_SOURCE (object);

	switch (prop_id)
	{
	case OPS_PROP_OBJECT:
		g_value_set_object (value, self->object);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_object_property_source_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
	LrgObjectPropertySource *self = LRG_OBJECT_PROPERTY_SOURCE (object);

	switch (prop_id)
	{
	case OPS_PROP_OBJECT:
		/* Construct-only: follow the object's notify from now on */
		self->object = g_value_dup_object (value);
		if (self->object != NULL)
			self->notify_id = g_signal_connect (self->object, "notify", G_CALLBACK (on_notify), self);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_object_property_source_class_init (LrgObjectPropertySourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_object_property_source_dispose;
	object_class->finalize = lrg_object_property_source_finalize;
	object_class->get_property = lrg_object_property_source_get_property;
	object_class->set_property = lrg_object_property_source_set_property;

	/**
	 * LrgObjectPropertySource:object:
	 *
	 * The wrapped object (construct-only).
	 */
	ops_properties[OPS_PROP_OBJECT] =
		g_param_spec_object ("object", NULL, NULL, G_TYPE_OBJECT,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, OPS_N_PROPS, ops_properties);
}

static void
lrg_object_property_source_init (LrgObjectPropertySource *self)
{
	self->hidden = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	self->owner_type = G_TYPE_INVALID;
}

LrgObjectPropertySource *
lrg_object_property_source_new (GObject *object)
{
	g_return_val_if_fail (G_IS_OBJECT (object), NULL);

	return g_object_new (LRG_TYPE_OBJECT_PROPERTY_SOURCE, "object", object, NULL);
}

GObject *
lrg_object_property_source_get_object (LrgObjectPropertySource *self)
{
	g_return_val_if_fail (LRG_IS_OBJECT_PROPERTY_SOURCE (self), NULL);
	return self->object;
}

void
lrg_object_property_source_hide_property (LrgObjectPropertySource *self,
                                          const gchar             *name)
{
	g_return_if_fail (LRG_IS_OBJECT_PROPERTY_SOURCE (self));
	g_return_if_fail (name != NULL);

	g_hash_table_add (self->hidden, g_strdup (name));
	lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self), NULL);
}

void
lrg_object_property_source_set_owner_type (LrgObjectPropertySource *self,
                                           GType                    owner_type)
{
	g_return_if_fail (LRG_IS_OBJECT_PROPERTY_SOURCE (self));

	self->owner_type = owner_type;
	lrg_property_source_emit_changed (LRG_PROPERTY_SOURCE (self), NULL);
}
