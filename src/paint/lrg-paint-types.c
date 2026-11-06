/* lrg-paint-types.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Value types shared by the paint list and renderer, plus colour
 * helpers used for state blending and theme derivation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PAINT

#include <math.h>
#include <string.h>

#include "lrg-paint-types.h"
#include "../lrg-log.h"

G_DEFINE_BOXED_TYPE (LrgCornerRadii, lrg_corner_radii,
                     lrg_corner_radii_copy, lrg_corner_radii_free)
G_DEFINE_BOXED_TYPE (LrgPaintGradient, lrg_paint_gradient,
                     lrg_paint_gradient_copy, lrg_paint_gradient_free)
G_DEFINE_BOXED_TYPE (LrgPaintInstance, lrg_paint_instance,
                     lrg_paint_instance_copy, lrg_paint_instance_free)

/* ==========================================================================
 * LrgCornerRadii
 * ========================================================================== */

LrgCornerRadii *
lrg_corner_radii_new (gfloat top_left,
                      gfloat top_right,
                      gfloat bottom_right,
                      gfloat bottom_left)
{
	LrgCornerRadii *self;

	self = g_new (LrgCornerRadii, 1);
	self->top_left = top_left;
	self->top_right = top_right;
	self->bottom_right = bottom_right;
	self->bottom_left = bottom_left;

	return self;
}

LrgCornerRadii *
lrg_corner_radii_copy (const LrgCornerRadii *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return (LrgCornerRadii *) g_memdup2 (self, sizeof (LrgCornerRadii));
}

void
lrg_corner_radii_free (LrgCornerRadii *self)
{
	g_free (self);
}

void
lrg_corner_radii_init_uniform (LrgCornerRadii *self,
                               gfloat          radius)
{
	g_return_if_fail (self != NULL);

	self->top_left = radius;
	self->top_right = radius;
	self->bottom_right = radius;
	self->bottom_left = radius;
}

void
lrg_corner_radii_scale (LrgCornerRadii *self,
                        gfloat          factor)
{
	g_return_if_fail (self != NULL);

	self->top_left *= factor;
	self->top_right *= factor;
	self->bottom_right *= factor;
	self->bottom_left *= factor;
}

/* ==========================================================================
 * LrgPaintGradient
 * ========================================================================== */

LrgPaintGradient *
lrg_paint_gradient_copy (const LrgPaintGradient *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return (LrgPaintGradient *) g_memdup2 (self, sizeof (LrgPaintGradient));
}

void
lrg_paint_gradient_free (LrgPaintGradient *self)
{
	g_free (self);
}

void
lrg_paint_gradient_init_linear (LrgPaintGradient *self,
                                const GrlColor   *start,
                                const GrlColor   *end,
                                gfloat            angle)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (start != NULL);
	g_return_if_fail (end != NULL);

	memset (self, 0, sizeof (LrgPaintGradient));
	self->kind = LRG_PAINT_GRADIENT_LINEAR;
	self->start = *start;
	self->end = *end;
	self->angle = angle;
}

void
lrg_paint_gradient_init_radial (LrgPaintGradient *self,
                                const GrlColor   *start,
                                const GrlColor   *end,
                                gfloat            center_x,
                                gfloat            center_y,
                                gfloat            radius)
{
	g_return_if_fail (self != NULL);
	g_return_if_fail (start != NULL);
	g_return_if_fail (end != NULL);

	memset (self, 0, sizeof (LrgPaintGradient));
	self->kind = LRG_PAINT_GRADIENT_RADIAL;
	self->start = *start;
	self->end = *end;
	self->center_x = center_x;
	self->center_y = center_y;
	self->radius = radius;
}

/* ==========================================================================
 * LrgPaintInstance
 * ========================================================================== */

LrgPaintInstance *
lrg_paint_instance_copy (const LrgPaintInstance *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return (LrgPaintInstance *) g_memdup2 (self, sizeof (LrgPaintInstance));
}

void
lrg_paint_instance_free (LrgPaintInstance *self)
{
	g_free (self);
}

LrgPaintPrim
lrg_paint_instance_get_prim (const LrgPaintInstance *self)
{
	g_return_val_if_fail (self != NULL, LRG_PAINT_PRIM_RECT);

	return (LrgPaintPrim) (gint) (self->params[2] + 0.5f);
}

/* ==========================================================================
 * Colour helpers
 * ========================================================================== */

/* channel_mix: one 8-bit channel, rounded */
static guint8
channel_mix (guint8 a,
             guint8 b,
             gfloat t)
{
	gfloat v;

	v = (gfloat) a + ((gfloat) b - (gfloat) a) * t;
	return (guint8) CLAMP ((gint) (v + 0.5f), 0, 255);
}

void
lrg_paint_color_mix (const GrlColor *a,
                     const GrlColor *b,
                     gfloat          t,
                     GrlColor       *out)
{
	g_return_if_fail (a != NULL);
	g_return_if_fail (b != NULL);
	g_return_if_fail (out != NULL);

	t = CLAMP (t, 0.0f, 1.0f);
	out->r = channel_mix (a->r, b->r, t);
	out->g = channel_mix (a->g, b->g, t);
	out->b = channel_mix (a->b, b->b, t);
	out->a = channel_mix (a->a, b->a, t);
}

void
lrg_paint_color_shade (const GrlColor *color,
                       gfloat          amount,
                       GrlColor       *out)
{
	GrlColor goal;
	guint8   alpha;

	g_return_if_fail (color != NULL);
	g_return_if_fail (out != NULL);

	/* Mix towards white or black, keeping the original alpha */
	alpha = color->a;
	goal.r = goal.g = goal.b = amount >= 0.0f ? 255 : 0;
	goal.a = alpha;
	lrg_paint_color_mix (color, &goal, ABS (amount), out);
	out->a = alpha;
}

void
lrg_paint_color_with_alpha (const GrlColor *color,
                            gfloat          alpha,
                            GrlColor       *out)
{
	g_return_if_fail (color != NULL);
	g_return_if_fail (out != NULL);

	*out = *color;
	out->a = (guint8) CLAMP ((gint) (alpha * 255.0f + 0.5f), 0, 255);
}

/* linear_channel: sRGB 8-bit channel to linear light (WCAG formula) */
static gdouble
linear_channel (guint8 c)
{
	gdouble v;

	v = (gdouble) c / 255.0;
	if (v <= 0.04045)
		return v / 12.92;
	return pow ((v + 0.055) / 1.055, 2.4);
}

gfloat
lrg_paint_color_luminance (const GrlColor *color)
{
	g_return_val_if_fail (color != NULL, 0.0f);

	return (gfloat) (0.2126 * linear_channel (color->r) +
	                 0.7152 * linear_channel (color->g) +
	                 0.0722 * linear_channel (color->b));
}

gfloat
lrg_paint_color_contrast (const GrlColor *a,
                          const GrlColor *b)
{
	gfloat la;
	gfloat lb;

	g_return_val_if_fail (a != NULL, 1.0f);
	g_return_val_if_fail (b != NULL, 1.0f);

	la = lrg_paint_color_luminance (a);
	lb = lrg_paint_color_luminance (b);

	return (MAX (la, lb) + 0.05f) / (MIN (la, lb) + 0.05f);
}
