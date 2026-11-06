/* lrg-style.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Part looks, style rules and hex colour parsing.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <string.h>

#include "lrg-style.h"
#include "../lrg-log.h"

struct _LrgStyleRule
{
	gint            ref_count;
	const gchar    *widget_type;    /* interned, NULL = any */
	GType           resolved_type;  /* cached g_type_from_name () */
	GQuark          style_class;    /* 0 = any */
	LrgUIPart       part;
	LrgWidgetState  state;
	LrgPartLook     look;
};

G_DEFINE_BOXED_TYPE (LrgPartLook, lrg_part_look, lrg_part_look_copy, lrg_part_look_free)
G_DEFINE_BOXED_TYPE (LrgStyleRule, lrg_style_rule, lrg_style_rule_ref, lrg_style_rule_unref)

GType
lrg_part_look_fields_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_PART_LOOK_NONE, "LRG_PART_LOOK_NONE", "none" },
			{ LRG_PART_LOOK_BACKGROUND, "LRG_PART_LOOK_BACKGROUND", "background" },
			{ LRG_PART_LOOK_FOREGROUND, "LRG_PART_LOOK_FOREGROUND", "foreground" },
			{ LRG_PART_LOOK_BORDER, "LRG_PART_LOOK_BORDER", "border" },
			{ LRG_PART_LOOK_BORDER_WIDTH, "LRG_PART_LOOK_BORDER_WIDTH", "border-width" },
			{ LRG_PART_LOOK_RADII, "LRG_PART_LOOK_RADII", "radii" },
			{ LRG_PART_LOOK_SHADOW, "LRG_PART_LOOK_SHADOW", "shadow" },
			{ LRG_PART_LOOK_SHADOW_BLUR, "LRG_PART_LOOK_SHADOW_BLUR", "shadow-blur" },
			{ LRG_PART_LOOK_SHADOW_OFFSET, "LRG_PART_LOOK_SHADOW_OFFSET", "shadow-offset" },
			{ LRG_PART_LOOK_GRADIENT, "LRG_PART_LOOK_GRADIENT", "gradient" },
			{ LRG_PART_LOOK_OPACITY, "LRG_PART_LOOK_OPACITY", "opacity" },
			{ LRG_PART_LOOK_SIZE, "LRG_PART_LOOK_SIZE", "size" },
			{ LRG_PART_LOOK_GLOW, "LRG_PART_LOOK_GLOW", "glow" },
			{ LRG_PART_LOOK_GLOW_SIZE, "LRG_PART_LOOK_GLOW_SIZE", "glow-size" },
			{ LRG_PART_LOOK_MATERIAL, "LRG_PART_LOOK_MATERIAL", "material" },
			{ LRG_PART_LOOK_GLASS, "LRG_PART_LOOK_GLASS", "glass" },
			{ LRG_PART_LOOK_EFFECT, "LRG_PART_LOOK_EFFECT", "effect" },
			{ LRG_PART_LOOK_EFFECT_COLORS, "LRG_PART_LOOK_EFFECT_COLORS", "effect-colors" },
			{ LRG_PART_LOOK_EFFECT_INTENSITY, "LRG_PART_LOOK_EFFECT_INTENSITY", "effect-intensity" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgPartLookFields"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * LrgPartLook
 * ========================================================================== */

LrgPartLook *
lrg_part_look_new (void)
{
	LrgPartLook *self;

	self = g_new (LrgPartLook, 1);
	lrg_part_look_init (self);

	return self;
}

LrgPartLook *
lrg_part_look_copy (const LrgPartLook *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return (LrgPartLook *) g_memdup2 (self, sizeof (LrgPartLook));
}

void
lrg_part_look_free (LrgPartLook *self)
{
	g_free (self);
}

void
lrg_part_look_init (LrgPartLook *self)
{
	g_return_if_fail (self != NULL);

	memset (self, 0, sizeof (LrgPartLook));
	self->set = LRG_PART_LOOK_NONE;
	self->opacity = 1.0f;
	self->gradient.kind = LRG_PAINT_GRADIENT_NONE;
	self->material = LRG_PAINT_MATERIAL_NONE;
	self->material_opacity = 1.0f;
	lrg_paint_glass_init_default (&self->glass);
	self->effect = 0;
	lrg_paint_effect_params_init_default (&self->effect_params);
}

void
lrg_part_look_merge (LrgPartLook       *self,
                     const LrgPartLook *overlay)
{
	LrgPartLookFields f;

	g_return_if_fail (self != NULL);
	g_return_if_fail (overlay != NULL);

	f = overlay->set;

	if (f & LRG_PART_LOOK_BACKGROUND)
		self->background = overlay->background;
	if (f & LRG_PART_LOOK_FOREGROUND)
		self->foreground = overlay->foreground;
	if (f & LRG_PART_LOOK_BORDER)
		self->border = overlay->border;
	if (f & LRG_PART_LOOK_BORDER_WIDTH)
		self->border_width = overlay->border_width;
	if (f & LRG_PART_LOOK_RADII)
		self->radii = overlay->radii;
	if (f & LRG_PART_LOOK_SHADOW)
		self->shadow = overlay->shadow;
	if (f & LRG_PART_LOOK_SHADOW_BLUR)
		self->shadow_blur = overlay->shadow_blur;
	if (f & LRG_PART_LOOK_SHADOW_OFFSET)
		self->shadow_offset_y = overlay->shadow_offset_y;
	if (f & LRG_PART_LOOK_GRADIENT)
		self->gradient = overlay->gradient;
	if (f & LRG_PART_LOOK_OPACITY)
		self->opacity = overlay->opacity;
	if (f & LRG_PART_LOOK_SIZE)
		self->size = overlay->size;
	if (f & LRG_PART_LOOK_GLOW)
		self->glow = overlay->glow;
	if (f & LRG_PART_LOOK_GLOW_SIZE)
		self->glow_size = overlay->glow_size;
	if (f & LRG_PART_LOOK_MATERIAL)
	{
		self->material = overlay->material;
		self->material_opacity = overlay->material_opacity;
	}
	if (f & LRG_PART_LOOK_GLASS)
		self->glass = overlay->glass;
	if (f & LRG_PART_LOOK_EFFECT)
		self->effect = overlay->effect;
	if (f & LRG_PART_LOOK_EFFECT_COLORS)
	{
		self->effect_params.color1 = overlay->effect_params.color1;
		self->effect_params.color2 = overlay->effect_params.color2;
		self->effect_params.color3 = overlay->effect_params.color3;
	}
	if (f & LRG_PART_LOOK_EFFECT_INTENSITY)
		self->effect_params.intensity = overlay->effect_params.intensity;

	self->set |= f;
}

static gfloat
lerpf (gfloat a,
       gfloat b,
       gfloat t)
{
	return a + (b - a) * t;
}

/*
 * gradient_or_solid:
 *
 * A look's fill expressed as a gradient: its own gradient, or a flat
 * "gradient" of its background colour, so fills can always be blended
 * as two gradients.
 */
static LrgPaintGradient
gradient_or_solid (const LrgPartLook *look,
                   const LrgPaintGradient *shape)
{
	LrgPaintGradient g;

	if (look->gradient.kind != LRG_PAINT_GRADIENT_NONE)
		return look->gradient;

	g = *shape;
	g.start = look->background;
	g.end = look->background;
	return g;
}

void
lrg_part_look_lerp (const LrgPartLook *a,
                    const LrgPartLook *b,
                    gfloat             t,
                    LrgPartLook       *out)
{
	LrgPartLook result;

	g_return_if_fail (a != NULL);
	g_return_if_fail (b != NULL);
	g_return_if_fail (out != NULL);

	t = CLAMP (t, 0.0f, 1.0f);
	if (t <= 0.0f)
	{
		*out = *a;
		return;
	}
	if (t >= 1.0f)
	{
		*out = *b;
		return;
	}

	result = *a;
	result.set = a->set | b->set;

	lrg_paint_color_mix (&a->background, &b->background, t, &result.background);
	lrg_paint_color_mix (&a->foreground, &b->foreground, t, &result.foreground);
	lrg_paint_color_mix (&a->border, &b->border, t, &result.border);
	lrg_paint_color_mix (&a->shadow, &b->shadow, t, &result.shadow);
	lrg_paint_color_mix (&a->glow, &b->glow, t, &result.glow);

	result.border_width = lerpf (a->border_width, b->border_width, t);
	result.radii.top_left = lerpf (a->radii.top_left, b->radii.top_left, t);
	result.radii.top_right = lerpf (a->radii.top_right, b->radii.top_right, t);
	result.radii.bottom_right = lerpf (a->radii.bottom_right, b->radii.bottom_right, t);
	result.radii.bottom_left = lerpf (a->radii.bottom_left, b->radii.bottom_left, t);
	result.shadow_blur = lerpf (a->shadow_blur, b->shadow_blur, t);
	result.shadow_offset_y = lerpf (a->shadow_offset_y, b->shadow_offset_y, t);
	result.opacity = lerpf (a->opacity, b->opacity, t);
	result.size = lerpf (a->size, b->size, t);
	result.glow_size = lerpf (a->glow_size, b->glow_size, t);

	/* Gradients: when either side has one, blend both as gradients of
	 * the same shape (the other side contributes a flat one) */
	if (a->gradient.kind != LRG_PAINT_GRADIENT_NONE ||
	    b->gradient.kind != LRG_PAINT_GRADIENT_NONE)
	{
		const LrgPaintGradient *shape;
		LrgPaintGradient        ga;
		LrgPaintGradient        gb;

		shape = b->gradient.kind != LRG_PAINT_GRADIENT_NONE ? &b->gradient : &a->gradient;
		ga = gradient_or_solid (a, shape);
		gb = gradient_or_solid (b, shape);

		result.gradient = *shape;
		lrg_paint_color_mix (&ga.start, &gb.start, t, &result.gradient.start);
		lrg_paint_color_mix (&ga.end, &gb.end, t, &result.gradient.end);
		result.gradient.angle = lerpf (ga.angle, gb.angle, t);
		result.gradient.center_x = lerpf (ga.center_x, gb.center_x, t);
		result.gradient.center_y = lerpf (ga.center_y, gb.center_y, t);
		result.gradient.radius = lerpf (ga.radius, gb.radius, t);
	}

	/* Materials fade through their strength: a side without one counts
	 * as strength 0, so NONE -> GLASS fades the glass in over the fill.
	 * Between two materials the glass parameters blend and the target
	 * material is used. */
	if (a->material != LRG_PAINT_MATERIAL_NONE || b->material != LRG_PAINT_MATERIAL_NONE)
	{
		LrgPaintGlass ga;
		LrgPaintGlass gb;
		gfloat        sa;
		gfloat        sb;

		lrg_part_look_get_glass (a->material != LRG_PAINT_MATERIAL_NONE ? a : b, &ga);
		lrg_part_look_get_glass (b->material != LRG_PAINT_MATERIAL_NONE ? b : a, &gb);
		sa = a->material != LRG_PAINT_MATERIAL_NONE ? a->material_opacity : 0.0f;
		sb = b->material != LRG_PAINT_MATERIAL_NONE ? b->material_opacity : 0.0f;

		result.material = b->material != LRG_PAINT_MATERIAL_NONE ? b->material : a->material;
		result.material_opacity = lerpf (sa, sb, t);
		lrg_paint_glass_lerp (&ga, &gb, t, &result.glass);
		result.set |= LRG_PART_LOOK_MATERIAL | LRG_PART_LOOK_GLASS;
	}

	/* Effects fade through their intensity the same way */
	if (a->effect != 0 || b->effect != 0)
	{
		gfloat ia = a->effect != 0 ? a->effect_params.intensity : 0.0f;
		gfloat ib = b->effect != 0 ? b->effect_params.intensity : 0.0f;
		const LrgPaintEffectParams *pa = a->effect != 0 ? &a->effect_params : &b->effect_params;
		const LrgPaintEffectParams *pb = b->effect != 0 ? &b->effect_params : &a->effect_params;

		result.effect = b->effect != 0 ? b->effect : a->effect;
		result.effect_params = *pb;
		lrg_paint_color_mix (&pa->color1, &pb->color1, t, &result.effect_params.color1);
		lrg_paint_color_mix (&pa->color2, &pb->color2, t, &result.effect_params.color2);
		lrg_paint_color_mix (&pa->color3, &pb->color3, t, &result.effect_params.color3);
		result.effect_params.intensity = lerpf (ia, ib, t);
		result.set |= LRG_PART_LOOK_EFFECT | LRG_PART_LOOK_EFFECT_INTENSITY;
	}

	*out = result;
}

void
lrg_part_look_set_background (LrgPartLook    *self,
                              const GrlColor *color)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (color != NULL);

	self->background = *color;
	self->set |= LRG_PART_LOOK_BACKGROUND;
}

void
lrg_part_look_set_foreground (LrgPartLook    *self,
                              const GrlColor *color)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (color != NULL);

	self->foreground = *color;
	self->set |= LRG_PART_LOOK_FOREGROUND;
}

void
lrg_part_look_set_border (LrgPartLook    *self,
                          const GrlColor *color,
                          gfloat          width)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (color != NULL);

	self->border = *color;
	self->border_width = width;
	self->set |= LRG_PART_LOOK_BORDER | LRG_PART_LOOK_BORDER_WIDTH;
}

void
lrg_part_look_set_radius (LrgPartLook *self,
                          gfloat       radius)
{
	g_return_if_fail (self != NULL);

	lrg_corner_radii_init_uniform (&self->radii, radius);
	self->set |= LRG_PART_LOOK_RADII;
}

void
lrg_part_look_set_shadow (LrgPartLook    *self,
                          const GrlColor *color,
                          gfloat          blur,
                          gfloat          offset_y)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (color != NULL);

	self->shadow = *color;
	self->shadow_blur = blur;
	self->shadow_offset_y = offset_y;
	self->set |= LRG_PART_LOOK_SHADOW | LRG_PART_LOOK_SHADOW_BLUR | LRG_PART_LOOK_SHADOW_OFFSET;
}

void
lrg_part_look_set_gradient (LrgPartLook            *self,
                            const LrgPaintGradient *gradient)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (gradient != NULL);

	self->gradient = *gradient;
	self->set |= LRG_PART_LOOK_GRADIENT;
}

void
lrg_part_look_set_opacity (LrgPartLook *self,
                           gfloat       opacity)
{
	g_return_if_fail (self != NULL);

	self->opacity = CLAMP (opacity, 0.0f, 1.0f);
	self->set |= LRG_PART_LOOK_OPACITY;
}

void
lrg_part_look_set_size (LrgPartLook *self,
                        gfloat       size)
{
	g_return_if_fail (self != NULL);

	self->size = size;
	self->set |= LRG_PART_LOOK_SIZE;
}

void
lrg_part_look_set_glow (LrgPartLook    *self,
                        const GrlColor *color,
                        gfloat          size)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (color != NULL);

	self->glow = *color;
	self->glow_size = size;
	self->set |= LRG_PART_LOOK_GLOW | LRG_PART_LOOK_GLOW_SIZE;
}

void
lrg_part_look_set_material (LrgPartLook      *self,
                            LrgPaintMaterial  material)
{
	g_return_if_fail (self != NULL);

	self->material = material;
	self->material_opacity = 1.0f;
	self->set |= LRG_PART_LOOK_MATERIAL;
}

void
lrg_part_look_set_glass (LrgPartLook         *self,
                         const LrgPaintGlass *glass)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (glass != NULL);

	self->glass = *glass;
	self->set |= LRG_PART_LOOK_GLASS;
}

void
lrg_part_look_get_glass (const LrgPartLook *self,
                         LrgPaintGlass     *out)
{
	gboolean explicit_glass;

	g_return_if_fail (self != NULL);
	g_return_if_fail (out != NULL);

	explicit_glass = (self->set & LRG_PART_LOOK_GLASS) != 0;
	if (explicit_glass)
		*out = self->glass;
	else
		lrg_paint_glass_init_default (out);

	switch (self->material)
	{
	case LRG_PAINT_MATERIAL_BLUR:
		/* Only the blur: none of the slab's optics */
		out->frost = 1.0f;
		out->refraction = 0.0f;
		out->rim_light = 0.0f;
		out->dispersion = 0.0f;
		if (!explicit_glass)
		{
			out->saturation = 1.0f;
			out->tint.a = 0;
		}
		break;
	case LRG_PAINT_MATERIAL_FROSTED:
		out->frost = 1.0f;
		break;
	case LRG_PAINT_MATERIAL_TINTED:
		out->frost = 1.0f;
		out->tint = self->background;
		break;
	case LRG_PAINT_MATERIAL_GLASS:
	case LRG_PAINT_MATERIAL_NONE:
	default:
		break;
	}
}

void
lrg_part_look_set_effect (LrgPartLook *self,
                          const gchar *name)
{
	g_return_if_fail (self != NULL);

	self->effect = (name != NULL && name[0] != '\0') ? g_quark_from_string (name) : 0;
	self->set |= LRG_PART_LOOK_EFFECT;
}

const gchar *
lrg_part_look_get_effect_name (const LrgPartLook *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return self->effect != 0 ? g_quark_to_string (self->effect) : NULL;
}

void
lrg_part_look_set_effect_colors (LrgPartLook    *self,
                                 const GrlColor *color1,
                                 const GrlColor *color2,
                                 const GrlColor *color3)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (color1 != NULL);

	if (color2 == NULL)
		color2 = color1;
	if (color3 == NULL)
		color3 = color2;

	self->effect_params.color1 = *color1;
	self->effect_params.color2 = *color2;
	self->effect_params.color3 = *color3;
	self->set |= LRG_PART_LOOK_EFFECT_COLORS;
}

void
lrg_part_look_set_effect_intensity (LrgPartLook *self,
                                    gfloat       intensity)
{
	g_return_if_fail (self != NULL);

	self->effect_params.intensity = MAX (intensity, 0.0f);
	self->set |= LRG_PART_LOOK_EFFECT_INTENSITY;
}

/* ==========================================================================
 * LrgStyleRule
 * ========================================================================== */

LrgStyleRule *
lrg_style_rule_new (const gchar    *widget_type,
                    const gchar    *style_class,
                    LrgUIPart       part,
                    LrgWidgetState  state)
{
	LrgStyleRule *self;

	self = g_new0 (LrgStyleRule, 1);
	self->ref_count = 1;
	self->widget_type = (widget_type != NULL && widget_type[0] != '\0')
	                    ? g_intern_string (widget_type)
	                    : NULL;
	self->resolved_type = G_TYPE_INVALID;
	self->style_class = (style_class != NULL && style_class[0] != '\0')
	                    ? g_quark_from_string (style_class)
	                    : 0;
	self->part = part;
	self->state = state;
	lrg_part_look_init (&self->look);

	return self;
}

LrgStyleRule *
lrg_style_rule_ref (LrgStyleRule *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	g_atomic_int_inc (&self->ref_count);
	return self;
}

void
lrg_style_rule_unref (LrgStyleRule *self)
{
	g_return_if_fail (self != NULL);

	if (g_atomic_int_dec_and_test (&self->ref_count))
		g_free (self);
}

LrgPartLook *
lrg_style_rule_get_look (LrgStyleRule *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return &self->look;
}

const gchar *
lrg_style_rule_get_widget_type (LrgStyleRule *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return self->widget_type;
}

const gchar *
lrg_style_rule_get_style_class (LrgStyleRule *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return self->style_class != 0 ? g_quark_to_string (self->style_class) : NULL;
}

LrgUIPart
lrg_style_rule_get_part (LrgStyleRule *self)
{
	g_return_val_if_fail (self != NULL, LRG_UI_PART_BOX);

	return self->part;
}

LrgWidgetState
lrg_style_rule_get_state (LrgStyleRule *self)
{
	g_return_val_if_fail (self != NULL, LRG_WIDGET_STATE_NONE);

	return self->state;
}

guint
lrg_style_rule_get_specificity (LrgStyleRule *self)
{
	guint spec = 0;
	guint bits;

	g_return_val_if_fail (self != NULL, 0);

	if (self->widget_type != NULL)
		spec += 1;
	if (self->style_class != 0)
		spec += 2;

	/* each required state counts like a class (CSS pseudo-classes) */
	for (bits = (guint) self->state; bits != 0; bits &= bits - 1)
		spec += 2;

	return spec;
}

gboolean
lrg_style_rule_matches (LrgStyleRule   *self,
                        GType           widget_type,
                        const GQuark   *classes,
                        guint           n_classes,
                        LrgUIPart       part,
                        LrgWidgetState  state)
{
	g_return_val_if_fail (self != NULL, FALSE);

	if (self->part != part)
		return FALSE;

	/* every required state must be active */
	if ((state & self->state) != self->state)
		return FALSE;

	if (self->style_class != 0)
	{
		guint    i;
		gboolean found = FALSE;

		for (i = 0; i < n_classes && !found; i++)
			found = classes[i] == self->style_class;
		if (!found)
			return FALSE;
	}

	if (self->widget_type != NULL)
	{
		/* Resolve lazily: the type may not exist when the theme loads */
		if (self->resolved_type == G_TYPE_INVALID)
			self->resolved_type = g_type_from_name (self->widget_type);
		if (self->resolved_type == G_TYPE_INVALID)
			return FALSE;
		if (!g_type_is_a (widget_type, self->resolved_type))
			return FALSE;
	}

	return TRUE;
}

/* ==========================================================================
 * Colour parsing
 * ========================================================================== */

/* hex_digit: value of one hex character, or -1 */
static gint
hex_digit (gchar c)
{
	return g_ascii_xdigit_value (c);
}

gboolean
lrg_color_parse (const gchar *text,
                 GrlColor    *out)
{
	const gchar *p;
	gsize        len;
	gint         v[8];
	gsize        i;

	g_return_val_if_fail (out != NULL, FALSE);

	if (text == NULL)
		return FALSE;

	p = text;
	while (g_ascii_isspace (*p))
		p++;
	if (*p == '#')
		p++;

	len = strlen (p);
	while (len > 0 && g_ascii_isspace (p[len - 1]))
		len--;

	if (len != 3 && len != 4 && len != 6 && len != 8)
		return FALSE;

	for (i = 0; i < len; i++)
	{
		v[i] = hex_digit (p[i]);
		if (v[i] < 0)
			return FALSE;
	}

	if (len <= 4)
	{
		/* Short form: each digit doubles, "#f80" == "#ff8800" */
		out->r = (guint8) (v[0] * 17);
		out->g = (guint8) (v[1] * 17);
		out->b = (guint8) (v[2] * 17);
		out->a = (guint8) (len == 4 ? v[3] * 17 : 255);
	}
	else
	{
		out->r = (guint8) (v[0] * 16 + v[1]);
		out->g = (guint8) (v[2] * 16 + v[3]);
		out->b = (guint8) (v[4] * 16 + v[5]);
		out->a = (guint8) (len == 8 ? v[6] * 16 + v[7] : 255);
	}

	return TRUE;
}

gchar *
lrg_color_to_string (const GrlColor *color)
{
	g_return_val_if_fail (color != NULL, NULL);

	if (color->a == 255)
		return g_strdup_printf ("#%02x%02x%02x", color->r, color->g, color->b);
	return g_strdup_printf ("#%02x%02x%02x%02x", color->r, color->g, color->b, color->a);
}
