/* lrg-audio-param-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Table-driven numeric parameters for audio objects (effects, synthesizers,
 * drum machines).  Each parameter is a gdouble field of the instance
 * struct, exposed as a GObject double property with a range and default.
 * The .rmixer and .rsong formats, mixer ramps, automation, the editor's
 * property grids and the IPC all set parameters by name through these
 * properties, so the clamps are in one place.
 *
 * Property ids are the table index + 1.
 */

#pragma once

#include <glib-object.h>
#include <math.h>

G_BEGIN_DECLS

typedef struct
{
	const gchar *name;       /* property name, dashed ("cut-off") */
	const gchar *blurb;      /* one-line description (editor tooltip) */
	gsize        offset;     /* G_STRUCT_OFFSET of the gdouble field */
	gdouble      min;
	gdouble      max;
	gdouble      def;
} LrgAudioParam;

/*
 * lrg_audio_params_install:
 * Installs one G_TYPE_DOUBLE property per entry of @params (ids 1 .. n)
 * on @klass and stores the specs in @specs (n + 1 entries, [0] unused).
 */
static inline void
lrg_audio_params_install (GObjectClass        *klass,
                          const LrgAudioParam *params,
                          guint                n,
                          GParamSpec         **specs)
{
	guint i;

	specs[0] = NULL;
	for (i = 0; i < n; i++)
		specs[i + 1] = g_param_spec_double (params[i].name, params[i].name, params[i].blurb,
		                                    params[i].min, params[i].max, params[i].def,
		                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		                                    | G_PARAM_EXPLICIT_NOTIFY);
	g_object_class_install_properties (klass, n + 1, specs);
}

/* sets every parameter of @instance to its default */
static inline void
lrg_audio_params_defaults (gpointer             instance,
                           const LrgAudioParam *params,
                           guint                n)
{
	guint i;

	for (i = 0; i < n; i++)
		G_STRUCT_MEMBER (gdouble, instance, params[i].offset) = params[i].def;
}

/* get_property for a table property; FALSE when @prop_id is not one */
static inline gboolean
lrg_audio_params_get (GObject             *object,
                      const LrgAudioParam *params,
                      guint                n,
                      guint                prop_id,
                      GValue              *value)
{
	if (prop_id == 0 || prop_id > n)
		return FALSE;
	g_value_set_double (value, G_STRUCT_MEMBER (gdouble, object, params[prop_id - 1].offset));
	return TRUE;
}

/* set_property for a table property: clamps and notifies on change */
static inline gboolean
lrg_audio_params_set (GObject             *object,
                      const LrgAudioParam *params,
                      guint                n,
                      guint                prop_id,
                      const GValue        *value,
                      GParamSpec          *pspec)
{
	const LrgAudioParam *p;
	gdouble             *field;
	gdouble              v;

	if (prop_id == 0 || prop_id > n)
		return FALSE;
	p = &params[prop_id - 1];
	field = &G_STRUCT_MEMBER (gdouble, object, p->offset);
	v = g_value_get_double (value);
	if (!isfinite (v))
		return TRUE;
	v = CLAMP (v, p->min, p->max);
	if (v != *field)
	{
		*field = v;
		g_object_notify_by_pspec (object, pspec);
	}
	return TRUE;
}

/*
 * lrg_audio_params_set_clamped:
 * Sets numeric property @name of @object to @value clamped to the
 * property's range (g_object_set() refuses an out-of-range value with a
 * warning).  Returns FALSE for an unknown or non-numeric property or a
 * value that is not finite.
 */
static inline gboolean
lrg_audio_params_set_clamped (GObject     *object,
                              const gchar *name,
                              gdouble      value)
{
	GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), name);
	GValue      v = G_VALUE_INIT;

	if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE) || !isfinite (value))
		return FALSE;
	if (G_IS_PARAM_SPEC_DOUBLE (pspec))
	{
		GParamSpecDouble *d = G_PARAM_SPEC_DOUBLE (pspec);

		g_value_init (&v, G_TYPE_DOUBLE);
		g_value_set_double (&v, CLAMP (value, d->minimum, d->maximum));
	}
	else if (G_IS_PARAM_SPEC_FLOAT (pspec))
	{
		GParamSpecFloat *d = G_PARAM_SPEC_FLOAT (pspec);

		g_value_init (&v, G_TYPE_FLOAT);
		g_value_set_float (&v, (gfloat) CLAMP (value, (gdouble) d->minimum, (gdouble) d->maximum));
	}
	else if (G_IS_PARAM_SPEC_INT (pspec))
	{
		GParamSpecInt *d = G_PARAM_SPEC_INT (pspec);

		g_value_init (&v, G_TYPE_INT);
		g_value_set_int (&v, (gint) CLAMP (round (value), (gdouble) d->minimum, (gdouble) d->maximum));
	}
	else if (G_IS_PARAM_SPEC_UINT (pspec))
	{
		GParamSpecUInt *d = G_PARAM_SPEC_UINT (pspec);

		g_value_init (&v, G_TYPE_UINT);
		g_value_set_uint (&v, (guint) CLAMP (round (value), (gdouble) d->minimum, (gdouble) d->maximum));
	}
	else if (G_IS_PARAM_SPEC_BOOLEAN (pspec))
	{
		g_value_init (&v, G_TYPE_BOOLEAN);
		g_value_set_boolean (&v, value != 0.0);
	}
	else if (G_IS_PARAM_SPEC_ENUM (pspec))
	{
		GEnumClass *klass = G_PARAM_SPEC_ENUM (pspec)->enum_class;

		if (g_enum_get_value (klass, (gint) round (value)) == NULL)
			return FALSE;
		g_value_init (&v, pspec->value_type);
		g_value_set_enum (&v, (gint) round (value));
	}
	else
		return FALSE;
	g_object_set_property (object, name, &v);
	g_value_unset (&v);
	return TRUE;
}

G_END_DECLS
