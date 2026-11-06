/* lrg-paint-fx.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Effects and materials: backdrop blur, glass and effect shaders.
 *
 * This file holds the parameter value types and the effect registry.
 * The GLSL that draws the effects lives in lrg-paint-renderer.c; user
 * effects registered here are spliced into a variant of that shader
 * by _lrg_paint_effect_build_user_source().
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PAINT

#include <string.h>
#include <gio/gio.h>

#include "lrg-paint-fx.h"
#include "lrg-paint-private.h"
#include "../lrg-log.h"

G_DEFINE_BOXED_TYPE (LrgPaintGlass, lrg_paint_glass,
                     lrg_paint_glass_copy, lrg_paint_glass_free)
G_DEFINE_BOXED_TYPE (LrgPaintEffectParams, lrg_paint_effect_params,
                     lrg_paint_effect_params_copy, lrg_paint_effect_params_free)
G_DEFINE_BOXED_TYPE (LrgPaintCurveStyle, lrg_paint_curve_style,
                     lrg_paint_curve_style_copy, lrg_paint_curve_style_free)

/* ==========================================================================
 * LrgPaintGlass
 * ========================================================================== */

LrgPaintGlass *
lrg_paint_glass_copy (const LrgPaintGlass *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return (LrgPaintGlass *) g_memdup2 (self, sizeof (LrgPaintGlass));
}

void
lrg_paint_glass_free (LrgPaintGlass *self)
{
	g_free (self);
}

void
lrg_paint_glass_init_default (LrgPaintGlass *self)
{
	g_return_if_fail (self != NULL);

	self->frost = 0.35f;
	self->refraction = 8.0f;
	self->saturation = 1.2f;
	self->tint.r = 255;
	self->tint.g = 255;
	self->tint.b = 255;
	self->tint.a = 18;
	self->rim_light = 0.6f;
	self->dispersion = 0.25f;
	self->blur_radius = 12.0f;
}

static gfloat
lerpf (gfloat a,
       gfloat b,
       gfloat t)
{
	return a + (b - a) * t;
}

void
lrg_paint_glass_lerp (const LrgPaintGlass *a,
                      const LrgPaintGlass *b,
                      gfloat               t,
                      LrgPaintGlass       *out)
{
	LrgPaintGlass r;

	g_return_if_fail (a != NULL);
	g_return_if_fail (b != NULL);
	g_return_if_fail (out != NULL);

	t = CLAMP (t, 0.0f, 1.0f);
	r.frost = lerpf (a->frost, b->frost, t);
	r.refraction = lerpf (a->refraction, b->refraction, t);
	r.saturation = lerpf (a->saturation, b->saturation, t);
	lrg_paint_color_mix (&a->tint, &b->tint, t, &r.tint);
	r.rim_light = lerpf (a->rim_light, b->rim_light, t);
	r.dispersion = lerpf (a->dispersion, b->dispersion, t);
	r.blur_radius = lerpf (a->blur_radius, b->blur_radius, t);
	*out = r;
}

/* ==========================================================================
 * LrgPaintEffectParams
 * ========================================================================== */

LrgPaintEffectParams *
lrg_paint_effect_params_copy (const LrgPaintEffectParams *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return (LrgPaintEffectParams *) g_memdup2 (self, sizeof (LrgPaintEffectParams));
}

void
lrg_paint_effect_params_free (LrgPaintEffectParams *self)
{
	g_free (self);
}

void
lrg_paint_effect_params_init_default (LrgPaintEffectParams *self)
{
	static const GrlColor base = { 18, 20, 40, 255 };
	static const GrlColor violet = { 124, 108, 255, 255 };
	static const GrlColor cyan = { 64, 220, 255, 255 };

	g_return_if_fail (self != NULL);

	self->color1 = base;
	self->color2 = violet;
	self->color3 = cyan;
	self->intensity = 1.0f;
	self->hover = 0.0f;
	self->press = 0.0f;
	self->speed = 1.0f;
	self->scale = 1.0f;
}

/* ==========================================================================
 * LrgPaintCurveStyle
 * ========================================================================== */

LrgPaintCurveStyle *
lrg_paint_curve_style_copy (const LrgPaintCurveStyle *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return (LrgPaintCurveStyle *) g_memdup2 (self, sizeof (LrgPaintCurveStyle));
}

void
lrg_paint_curve_style_free (LrgPaintCurveStyle *self)
{
	g_free (self);
}

void
lrg_paint_curve_style_init (LrgPaintCurveStyle *self,
                            const GrlColor     *color)
{
	static const GrlColor white = { 255, 255, 255, 255 };

	g_return_if_fail (self != NULL);
	g_return_if_fail (color != NULL);

	memset (self, 0, sizeof (LrgPaintCurveStyle));
	self->color_start = *color;
	self->color_end = *color;
	self->pulse_color = white;
	self->pulse_speed = 0.5f;
}

/* ==========================================================================
 * Effect registry
 * ========================================================================== */

/* Built-in effects; the id of each is its index + 1 and must match the
 * switch in the renderer's fragment shader */
static const gchar * const builtin_effects[] = {
	"shimmer",
	"aurora",
	"neon",
	"spotlight",
	"ripple",
	"grain",
	"plasma",
	"mesh-gradient",
	NULL
};

typedef struct
{
	gchar *name;
	gchar *glsl;
} UserEffect;

G_LOCK_DEFINE_STATIC (registry);
static GPtrArray *user_effects = NULL;  /* UserEffect*, index = id - FIRST_USER */
static guint      registry_serial = 0;

static void
user_effect_free (gpointer data)
{
	UserEffect *fx = data;

	g_free (fx->name);
	g_free (fx->glsl);
	g_free (fx);
}

/* builtin_id: id of a built-in effect, or -1 */
static gint
builtin_id (const gchar *name)
{
	guint i;

	for (i = 0; builtin_effects[i] != NULL; i++)
		if (g_strcmp0 (builtin_effects[i], name) == 0)
			return (gint) i + 1;
	return -1;
}

/* user_id_locked: id of a user effect, or -1; registry lock held */
static gint
user_id_locked (const gchar *name)
{
	guint i;

	if (user_effects == NULL)
		return -1;
	for (i = 0; i < user_effects->len; i++)
	{
		UserEffect *fx = g_ptr_array_index (user_effects, i);

		if (g_strcmp0 (fx->name, name) == 0)
			return LRG_PAINT_EFFECT_FIRST_USER + (gint) i;
	}
	return -1;
}

gint
lrg_paint_effect_lookup (const gchar *name)
{
	gint id;

	if (name == NULL || name[0] == '\0')
		return -1;

	id = builtin_id (name);
	if (id >= 0)
		return id;

	G_LOCK (registry);
	id = user_id_locked (name);
	G_UNLOCK (registry);

	return id;
}

const gchar *
lrg_paint_effect_get_name (gint effect_id)
{
	const gchar *name = NULL;

	if (effect_id >= 1 && effect_id <= (gint) G_N_ELEMENTS (builtin_effects) - 1)
		return builtin_effects[effect_id - 1];

	G_LOCK (registry);
	if (user_effects != NULL && effect_id >= LRG_PAINT_EFFECT_FIRST_USER &&
	    effect_id < LRG_PAINT_EFFECT_FIRST_USER + (gint) user_effects->len)
	{
		UserEffect *fx = g_ptr_array_index (user_effects,
		                                    (guint) (effect_id - LRG_PAINT_EFFECT_FIRST_USER));

		/* Names are never freed while the process runs, so the
		 * pointer stays valid after the lock is released */
		name = fx->name;
	}
	G_UNLOCK (registry);

	return name;
}

gchar **
lrg_paint_effect_list_names (void)
{
	GStrvBuilder *builder = g_strv_builder_new ();
	gchar       **names;
	guint         i;

	for (i = 0; builtin_effects[i] != NULL; i++)
		g_strv_builder_add (builder, builtin_effects[i]);

	G_LOCK (registry);
	if (user_effects != NULL)
		for (i = 0; i < user_effects->len; i++)
			g_strv_builder_add (builder, ((UserEffect *) g_ptr_array_index (user_effects, i))->name);
	G_UNLOCK (registry);

	names = g_strv_builder_end (builder);
	g_strv_builder_unref (builder);
	return names;
}

/* valid_name: lower-case letters, digits and '-', starting with a letter */
static gboolean
valid_name (const gchar *name)
{
	const gchar *p;

	if (name == NULL || !g_ascii_islower (name[0]))
		return FALSE;
	for (p = name; *p != '\0'; p++)
		if (!g_ascii_islower (*p) && !g_ascii_isdigit (*p) && *p != '-')
			return FALSE;
	return (p - name) <= 48;
}

gint
lrg_paint_effect_register (const gchar  *name,
                           const gchar  *glsl_snippet,
                           GError      **error)
{
	UserEffect *fx;
	gint        id;

	g_return_val_if_fail (error == NULL || *error == NULL, -1);

	/* Validate the name and the obvious parts of the snippet contract;
	 * the GLSL itself is only checked by the driver at link time */
	if (!valid_name (name))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "effect name '%s' is invalid: use lower-case letters, digits and '-'",
		             name != NULL ? name : "(null)");
		return -1;
	}
	if (glsl_snippet == NULL || strstr (glsl_snippet, "lrg_effect") == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "effect '%s': the snippet must define vec4 lrg_effect (LrgEffect fx)",
		             name);
		return -1;
	}
	if (strstr (glsl_snippet, "#version") != NULL || strstr (glsl_snippet, "void main") != NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "effect '%s': the snippet must not contain #version or main()", name);
		return -1;
	}
	if (builtin_id (name) >= 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
		             "effect '%s' is a built-in effect", name);
		return -1;
	}

	G_LOCK (registry);

	if (user_effects == NULL)
		user_effects = g_ptr_array_new_with_free_func (user_effect_free);

	if (user_id_locked (name) >= 0)
	{
		G_UNLOCK (registry);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
		             "effect '%s' is already registered", name);
		return -1;
	}
	if (user_effects->len >= LRG_PAINT_EFFECT_MAX_USER)
	{
		G_UNLOCK (registry);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
		             "cannot register effect '%s': %d user effects already registered",
		             name, LRG_PAINT_EFFECT_MAX_USER);
		return -1;
	}

	fx = g_new0 (UserEffect, 1);
	fx->name = g_strdup (name);
	fx->glsl = g_strdup (glsl_snippet);
	g_ptr_array_add (user_effects, fx);
	id = LRG_PAINT_EFFECT_FIRST_USER + (gint) user_effects->len - 1;
	registry_serial++;

	G_UNLOCK (registry);

	lrg_debug (LRG_LOG_DOMAIN_PAINT, "registered user effect '%s' as id %d", name, id);
	return id;
}

guint
_lrg_paint_effect_get_serial (void)
{
	guint serial;

	G_LOCK (registry);
	serial = registry_serial;
	G_UNLOCK (registry);

	return serial;
}

/*
 * _lrg_paint_effect_build_user_source:
 *
 * Every user snippet, each wrapped so its lrg_effect() gets a unique
 * name, followed by the dispatcher the paint shader calls:
 *
 *   #define lrg_effect lrg_user_effect_0
 *   <snippet>
 *   #undef lrg_effect
 *   ...
 *   vec4 lrg_user_effect (int id, LrgEffect fx) { ... }
 *
 * Returns NULL when no user effect is registered.
 */
gchar *
_lrg_paint_effect_build_user_source (void)
{
	GString *src;
	guint    i;

	G_LOCK (registry);

	if (user_effects == NULL || user_effects->len == 0)
	{
		G_UNLOCK (registry);
		return NULL;
	}

	src = g_string_new (NULL);
	for (i = 0; i < user_effects->len; i++)
	{
		UserEffect *fx = g_ptr_array_index (user_effects, i);

		g_string_append_printf (src, "#define lrg_effect lrg_user_effect_%u\n", i);
		g_string_append (src, fx->glsl);
		g_string_append (src, "\n#undef lrg_effect\n");
	}

	g_string_append (src, "vec4 lrg_user_effect (int id, LrgEffect fx)\n{\n");
	for (i = 0; i < user_effects->len; i++)
		g_string_append_printf (src, "    if (id == %u) return lrg_user_effect_%u (fx);\n",
		                        (guint) LRG_PAINT_EFFECT_FIRST_USER + i, i);
	g_string_append (src, "    return fx.color1;\n}\n");

	G_UNLOCK (registry);

	return g_string_free (src, FALSE);
}
