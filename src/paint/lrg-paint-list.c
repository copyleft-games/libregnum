/* lrg-paint-list.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A recorded frame of 2D UI drawing: a flat array of GPU instances
 * plus a short command stream of instance batches and callbacks.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PAINT

#include <math.h>
#include <string.h>
#include <raylib.h>

#include "lrg-paint-list.h"
#include "lrg-paint-private.h"
#include "../lrg-log.h"

/* Clip rectangle used when nothing is pushed */
#define UNCLIPPED (1.0e7f)

/* Line advance as a multiple of the font size */
#define LINE_HEIGHT_FACTOR (1.2f)

typedef struct
{
	LrgPaintCommandKind kind;

	/* LRG_PAINT_COMMAND_INSTANCES */
	guint               first;
	guint               count;
	guint               texture;

	/* LRG_PAINT_COMMAND_CALLBACK */
	LrgPaintCallback    func;
	gpointer            user_data;
	GDestroyNotify      destroy;
	gfloat              clip[4];
} PaintCommand;

typedef struct
{
	gfloat x0;
	gfloat y0;
	gfloat x1;
	gfloat y1;
} ClipRect;

struct _LrgPaintList
{
	GObject    parent_instance;

	GArray    *instances;   /* LrgPaintInstance */
	GArray    *commands;    /* PaintCommand */
	GArray    *clip_stack;  /* ClipRect */
	GArray    *alpha_stack; /* gfloat */
	GPtrArray *refs;        /* GObjects kept alive until clear */

	ClipRect   clip;
	gfloat     opacity;
};

G_DEFINE_FINAL_TYPE (LrgPaintList, lrg_paint_list, G_TYPE_OBJECT)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* color_to_bytes: GrlColor -> RGBA8 with the current opacity applied */
static void
color_to_bytes (LrgPaintList   *self,
                const GrlColor *color,
                guint8         *out)
{
	if (color == NULL)
	{
		out[0] = out[1] = out[2] = out[3] = 0;
		return;
	}

	out[0] = color->r;
	out[1] = color->g;
	out[2] = color->b;
	out[3] = (guint8) CLAMP ((gint) ((gfloat) color->a * self->opacity + 0.5f), 0, 255);
}

/* clamp_radius: a corner radius may not exceed half the shorter side */
static gfloat
clamp_radius (gfloat radius,
              gfloat width,
              gfloat height)
{
	gfloat limit;

	limit = MIN (width, height) * 0.5f;
	return CLAMP (radius, 0.0f, MAX (limit, 0.0f));
}

/* bounds_outside_clip: TRUE if the box can't touch the clip at all */
static gboolean
bounds_outside_clip (LrgPaintList *self,
                     gfloat        x0,
                     gfloat        y0,
                     gfloat        x1,
                     gfloat        y1)
{
	return x1 < self->clip.x0 || y1 < self->clip.y0 ||
	       x0 > self->clip.x1 || y0 > self->clip.y1;
}

/* init_instance: zero an instance and stamp the current clip */
static void
init_instance (LrgPaintList     *self,
               LrgPaintInstance *inst,
               LrgPaintPrim      prim)
{
	memset (inst, 0, sizeof (LrgPaintInstance));
	inst->clip[0] = self->clip.x0;
	inst->clip[1] = self->clip.y0;
	inst->clip[2] = self->clip.x1;
	inst->clip[3] = self->clip.y1;
	inst->params[2] = (gfloat) prim;
	inst->params[3] = (gfloat) LRG_PAINT_GRADIENT_NONE;
}

/*
 * append_instance:
 *
 * Adds an instance and extends the last batch when it can: any number
 * of untextured instances may join a batch, plus instances of at most
 * one texture (the shader only samples for image instances).
 */
static void
append_instance (LrgPaintList           *self,
                 const LrgPaintInstance *inst)
{
	PaintCommand *last = NULL;

	g_array_append_val (self->instances, *inst);

	if (self->commands->len > 0)
		last = &g_array_index (self->commands, PaintCommand, self->commands->len - 1);

	if (last != NULL &&
	    last->kind == LRG_PAINT_COMMAND_INSTANCES &&
	    (inst->texture == 0 || last->texture == 0 || last->texture == inst->texture))
	{
		last->count++;
		if (inst->texture != 0)
			last->texture = inst->texture;
		return;
	}

	{
		PaintCommand cmd;

		memset (&cmd, 0, sizeof (PaintCommand));
		cmd.kind = LRG_PAINT_COMMAND_INSTANCES;
		cmd.first = self->instances->len - 1;
		cmd.count = 1;
		cmd.texture = inst->texture;
		g_array_append_val (self->commands, cmd);
	}
}

/* keep_ref: hold @object until the list is cleared */
static void
keep_ref (LrgPaintList *self,
          gpointer      object)
{
	/* Text and images often reuse the same object call after call */
	if (self->refs->len > 0 &&
	    g_ptr_array_index (self->refs, self->refs->len - 1) == object)
		return;

	g_ptr_array_add (self->refs, g_object_ref (object));
}

/* resolve_font: the raylib font behind @font, or raylib's default */
static const Font *
resolve_font (GrlFont *font,
              Font    *storage)
{
	if (font != NULL)
	{
		const Font *handle = grl_font_get_handle (font);

		if (handle != NULL)
			return handle;
	}

	*storage = GetFontDefault ();
	return storage;
}

/* glyph_advance: horizontal advance of glyph @index at @scale */
static gfloat
glyph_advance (const Font *font,
               gint        index,
               gfloat      scale)
{
	if (font->glyphs[index].advanceX != 0)
		return (gfloat) font->glyphs[index].advanceX * scale;
	return font->recs[index].width * scale;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
clear_command (gpointer data)
{
	PaintCommand *cmd = data;

	if (cmd->kind == LRG_PAINT_COMMAND_CALLBACK && cmd->destroy != NULL)
		cmd->destroy (cmd->user_data);
	cmd->destroy = NULL;
}

static void
lrg_paint_list_finalize (GObject *object)
{
	LrgPaintList *self = LRG_PAINT_LIST (object);

	g_array_unref (self->instances);
	g_array_unref (self->commands);
	g_array_unref (self->clip_stack);
	g_array_unref (self->alpha_stack);
	g_ptr_array_unref (self->refs);

	G_OBJECT_CLASS (lrg_paint_list_parent_class)->finalize (object);
}

static void
lrg_paint_list_class_init (LrgPaintListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_paint_list_finalize;
}

static void
lrg_paint_list_init (LrgPaintList *self)
{
	self->instances = g_array_sized_new (FALSE, FALSE, sizeof (LrgPaintInstance), 256);
	self->commands = g_array_sized_new (FALSE, FALSE, sizeof (PaintCommand), 8);
	g_array_set_clear_func (self->commands, clear_command);
	self->clip_stack = g_array_new (FALSE, FALSE, sizeof (ClipRect));
	self->alpha_stack = g_array_new (FALSE, FALSE, sizeof (gfloat));
	self->refs = g_ptr_array_new_with_free_func (g_object_unref);

	self->clip.x0 = -UNCLIPPED;
	self->clip.y0 = -UNCLIPPED;
	self->clip.x1 = UNCLIPPED;
	self->clip.y1 = UNCLIPPED;
	self->opacity = 1.0f;
}

LrgPaintList *
lrg_paint_list_new (void)
{
	return g_object_new (LRG_TYPE_PAINT_LIST, NULL);
}

void
lrg_paint_list_clear (LrgPaintList *self)
{
	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	g_array_set_size (self->instances, 0);
	g_array_set_size (self->commands, 0);
	g_array_set_size (self->clip_stack, 0);
	g_array_set_size (self->alpha_stack, 0);
	g_ptr_array_set_size (self->refs, 0);

	self->clip.x0 = -UNCLIPPED;
	self->clip.y0 = -UNCLIPPED;
	self->clip.x1 = UNCLIPPED;
	self->clip.y1 = UNCLIPPED;
	self->opacity = 1.0f;
}

/* ==========================================================================
 * State stacks
 * ========================================================================== */

void
lrg_paint_list_push_clip (LrgPaintList *self,
                          gfloat        x,
                          gfloat        y,
                          gfloat        width,
                          gfloat        height)
{
	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	g_array_append_val (self->clip_stack, self->clip);

	/* Intersect; an empty intersection collapses to a zero box */
	self->clip.x0 = MAX (self->clip.x0, x);
	self->clip.y0 = MAX (self->clip.y0, y);
	self->clip.x1 = MIN (self->clip.x1, x + MAX (width, 0.0f));
	self->clip.y1 = MIN (self->clip.y1, y + MAX (height, 0.0f));
	if (self->clip.x1 < self->clip.x0)
		self->clip.x1 = self->clip.x0;
	if (self->clip.y1 < self->clip.y0)
		self->clip.y1 = self->clip.y0;
}

void
lrg_paint_list_pop_clip (LrgPaintList *self)
{
	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	if (self->clip_stack->len == 0)
	{
		lrg_warning (LRG_LOG_DOMAIN_PAINT, "pop_clip without a matching push_clip");
		return;
	}

	self->clip = g_array_index (self->clip_stack, ClipRect, self->clip_stack->len - 1);
	g_array_set_size (self->clip_stack, self->clip_stack->len - 1);
}

void
lrg_paint_list_get_clip (LrgPaintList *self,
                         gfloat       *x,
                         gfloat       *y,
                         gfloat       *width,
                         gfloat       *height)
{
	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	if (x != NULL)
		*x = self->clip.x0;
	if (y != NULL)
		*y = self->clip.y0;
	if (width != NULL)
		*width = self->clip.x1 - self->clip.x0;
	if (height != NULL)
		*height = self->clip.y1 - self->clip.y0;
}

void
lrg_paint_list_push_opacity (LrgPaintList *self,
                             gfloat        opacity)
{
	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	g_array_append_val (self->alpha_stack, self->opacity);
	self->opacity *= CLAMP (opacity, 0.0f, 1.0f);
}

void
lrg_paint_list_pop_opacity (LrgPaintList *self)
{
	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	if (self->alpha_stack->len == 0)
	{
		lrg_warning (LRG_LOG_DOMAIN_PAINT, "pop_opacity without a matching push_opacity");
		return;
	}

	self->opacity = g_array_index (self->alpha_stack, gfloat, self->alpha_stack->len - 1);
	g_array_set_size (self->alpha_stack, self->alpha_stack->len - 1);
}

gfloat
lrg_paint_list_get_opacity (LrgPaintList *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_LIST (self), 1.0f);

	return self->opacity;
}

/* ==========================================================================
 * Shapes
 * ========================================================================== */

void
lrg_paint_list_rect (LrgPaintList   *self,
                     gfloat          x,
                     gfloat          y,
                     gfloat          width,
                     gfloat          height,
                     gfloat          radius,
                     const GrlColor *color)
{
	LrgCornerRadii radii;

	lrg_corner_radii_init_uniform (&radii, radius);
	lrg_paint_list_rect_ex (self, x, y, width, height, &radii, color, NULL, 0.0f, NULL);
}

void
lrg_paint_list_rect_ex (LrgPaintList           *self,
                        gfloat                  x,
                        gfloat                  y,
                        gfloat                  width,
                        gfloat                  height,
                        const LrgCornerRadii   *radii,
                        const GrlColor         *fill,
                        const LrgPaintGradient *gradient,
                        gfloat                  border_width,
                        const GrlColor         *border)
{
	LrgPaintInstance inst;
	gboolean         has_gradient;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	if (width <= 0.0f || height <= 0.0f)
		return;
	if (bounds_outside_clip (self, x, y, x + width, y + height))
		return;

	has_gradient = gradient != NULL && gradient->kind != LRG_PAINT_GRADIENT_NONE;

	/* Nothing visible: no fill, no gradient, no border */
	if (!has_gradient && (fill == NULL || fill->a == 0) &&
	    (border == NULL || border->a == 0 || border_width <= 0.0f))
		return;

	init_instance (self, &inst, LRG_PAINT_PRIM_RECT);
	inst.rect[0] = x;
	inst.rect[1] = y;
	inst.rect[2] = x + width;
	inst.rect[3] = y + height;

	if (radii != NULL)
	{
		inst.radii[0] = clamp_radius (radii->top_left, width, height);
		inst.radii[1] = clamp_radius (radii->top_right, width, height);
		inst.radii[2] = clamp_radius (radii->bottom_right, width, height);
		inst.radii[3] = clamp_radius (radii->bottom_left, width, height);
	}

	if (has_gradient)
	{
		color_to_bytes (self, &gradient->start, inst.color);
		color_to_bytes (self, &gradient->end, inst.color2);
		inst.params[3] = (gfloat) gradient->kind;

		if (gradient->kind == LRG_PAINT_GRADIENT_LINEAR)
		{
			gdouble rad = (gdouble) gradient->angle * G_PI / 180.0;

			inst.extra[0] = (gfloat) cos (rad);
			inst.extra[1] = (gfloat) sin (rad);
		}
		else
		{
			inst.extra[0] = gradient->center_x;
			inst.extra[1] = gradient->center_y;
			inst.extra[2] = gradient->radius > 0.0f
			                ? gradient->radius
			                : sqrtf (width * width + height * height) * 0.5f;
		}
	}
	else
	{
		color_to_bytes (self, fill, inst.color);
	}

	if (border != NULL && border_width > 0.0f)
	{
		inst.params[0] = MIN (border_width, MIN (width, height) * 0.5f);
		color_to_bytes (self, border, inst.border);
	}

	append_instance (self, &inst);
}

void
lrg_paint_list_border (LrgPaintList   *self,
                       gfloat          x,
                       gfloat          y,
                       gfloat          width,
                       gfloat          height,
                       gfloat          radius,
                       gfloat          border_width,
                       const GrlColor *color)
{
	LrgCornerRadii radii;

	lrg_corner_radii_init_uniform (&radii, radius);
	lrg_paint_list_rect_ex (self, x, y, width, height, &radii, NULL, NULL,
	                        border_width, color);
}

void
lrg_paint_list_shadow (LrgPaintList   *self,
                       gfloat          x,
                       gfloat          y,
                       gfloat          width,
                       gfloat          height,
                       gfloat          radius,
                       gfloat          blur,
                       gfloat          spread,
                       gfloat          offset_x,
                       gfloat          offset_y,
                       const GrlColor *color)
{
	LrgPaintInstance inst;
	gfloat           sx;
	gfloat           sy;
	gfloat           sw;
	gfloat           sh;
	gfloat           r;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (color != NULL);

	if (color->a == 0)
		return;

	/* Shadow shape: the caster, moved and grown by the spread */
	sx = x + offset_x - spread;
	sy = y + offset_y - spread;
	sw = width + spread * 2.0f;
	sh = height + spread * 2.0f;
	if (sw <= 0.0f || sh <= 0.0f)
		return;

	blur = MAX (blur, 0.0f);
	if (bounds_outside_clip (self, sx - blur, sy - blur, sx + sw + blur, sy + sh + blur))
		return;

	r = clamp_radius (radius + spread, sw, sh);

	init_instance (self, &inst, LRG_PAINT_PRIM_SHADOW);
	inst.rect[0] = sx;
	inst.rect[1] = sy;
	inst.rect[2] = sx + sw;
	inst.rect[3] = sy + sh;
	inst.radii[0] = inst.radii[1] = inst.radii[2] = inst.radii[3] = r;
	inst.params[1] = blur;
	color_to_bytes (self, color, inst.color);

	append_instance (self, &inst);
}

void
lrg_paint_list_circle (LrgPaintList   *self,
                       gfloat          center_x,
                       gfloat          center_y,
                       gfloat          radius,
                       const GrlColor *color)
{
	lrg_paint_list_rect (self, center_x - radius, center_y - radius,
	                     radius * 2.0f, radius * 2.0f, radius, color);
}

void
lrg_paint_list_line (LrgPaintList   *self,
                     gfloat          x0,
                     gfloat          y0,
                     gfloat          x1,
                     gfloat          y1,
                     gfloat          thickness,
                     const GrlColor *color)
{
	LrgPaintInstance inst;
	gfloat           pad;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (color != NULL);

	if (thickness <= 0.0f || color->a == 0)
		return;

	pad = thickness * 0.5f;
	if (bounds_outside_clip (self, MIN (x0, x1) - pad, MIN (y0, y1) - pad,
	                         MAX (x0, x1) + pad, MAX (y0, y1) + pad))
		return;

	init_instance (self, &inst, LRG_PAINT_PRIM_LINE);
	inst.rect[0] = x0;
	inst.rect[1] = y0;
	inst.rect[2] = x1;
	inst.rect[3] = y1;
	inst.params[1] = thickness;
	color_to_bytes (self, color, inst.color);

	append_instance (self, &inst);
}

void
lrg_paint_list_triangle (LrgPaintList   *self,
                         gfloat          x0,
                         gfloat          y0,
                         gfloat          x1,
                         gfloat          y1,
                         gfloat          x2,
                         gfloat          y2,
                         const GrlColor *color)
{
	LrgPaintInstance inst;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (color != NULL);

	if (color->a == 0)
		return;
	if (bounds_outside_clip (self, MIN (x0, MIN (x1, x2)), MIN (y0, MIN (y1, y2)),
	                         MAX (x0, MAX (x1, x2)), MAX (y0, MAX (y1, y2))))
		return;

	/* Two vertices in rect, the third in extra */
	init_instance (self, &inst, LRG_PAINT_PRIM_TRIANGLE);
	inst.rect[0] = x0;
	inst.rect[1] = y0;
	inst.rect[2] = x1;
	inst.rect[3] = y1;
	inst.extra[0] = x2;
	inst.extra[1] = y2;
	color_to_bytes (self, color, inst.color);

	append_instance (self, &inst);
}

void
lrg_paint_list_image (LrgPaintList       *self,
                      GrlTexture         *texture,
                      const GrlRectangle *source,
                      gfloat              x,
                      gfloat              y,
                      gfloat              width,
                      gfloat              height,
                      gfloat              radius,
                      const GrlColor     *tint)
{
	static const GrlColor white = { 255, 255, 255, 255 };
	LrgPaintInstance      inst;
	const Texture2D      *tex;
	gfloat                tw;
	gfloat                th;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (GRL_IS_TEXTURE (texture));

	if (width <= 0.0f || height <= 0.0f)
		return;
	if (bounds_outside_clip (self, x, y, x + width, y + height))
		return;

	tex = grl_texture_get_handle (texture);
	if (tex == NULL || tex->id == 0)
		return;

	tw = (gfloat) MAX (tex->width, 1);
	th = (gfloat) MAX (tex->height, 1);

	init_instance (self, &inst, LRG_PAINT_PRIM_IMAGE);
	inst.rect[0] = x;
	inst.rect[1] = y;
	inst.rect[2] = x + width;
	inst.rect[3] = y + height;
	inst.radii[0] = inst.radii[1] = inst.radii[2] = inst.radii[3] =
		clamp_radius (radius, width, height);

	if (source != NULL)
	{
		inst.extra[0] = source->x / tw;
		inst.extra[1] = source->y / th;
		inst.extra[2] = (source->x + source->width) / tw;
		inst.extra[3] = (source->y + source->height) / th;
	}
	else
	{
		inst.extra[2] = 1.0f;
		inst.extra[3] = 1.0f;
	}

	color_to_bytes (self, tint != NULL ? tint : &white, inst.color);
	inst.texture = tex->id;

	keep_ref (self, texture);
	append_instance (self, &inst);
}

/* ==========================================================================
 * Text
 * ========================================================================== */

void
_lrg_paint_list_text_font (LrgPaintList   *self,
                           const Font     *rl,
                           const gchar    *text,
                           gssize          length,
                           gfloat          x,
                           gfloat          y,
                           gfloat          size,
                           gfloat          spacing,
                           const GrlColor *color)
{
	const gchar *p;
	const gchar *end;
	gfloat       scale;
	gfloat       pad;
	gfloat       pen_x;
	gfloat       pen_y;
	gfloat       tw;
	gfloat       th;
	guint8       rgba[4];

	if (text[0] == '\0' || length == 0 || size <= 0.0f || color->a == 0)
		return;
	if (rl->glyphCount == 0 || rl->texture.id == 0 || rl->baseSize == 0)
		return;

	end = length < 0 ? text + strlen (text) : text + length;
	scale = size / (gfloat) rl->baseSize;
	pad = (gfloat) rl->glyphPadding;
	tw = (gfloat) MAX (rl->texture.width, 1);
	th = (gfloat) MAX (rl->texture.height, 1);
	color_to_bytes (self, color, rgba);

	/* Snap the origin so glyph edges land on whole units */
	pen_x = floorf (x + 0.5f);
	pen_y = floorf (y + 0.5f);

	for (p = text; p < end && *p != '\0'; p = g_utf8_next_char (p))
	{
		gunichar         cp = g_utf8_get_char (p);
		gint             index;
		LrgPaintInstance inst;
		Rectangle        rec;
		gfloat           gx;
		gfloat           gy;
		gfloat           gw;
		gfloat           gh;

		if (cp == '\n')
		{
			pen_x = floorf (x + 0.5f);
			pen_y += size * LINE_HEIGHT_FACTOR;
			continue;
		}

		index = GetGlyphIndex (*rl, (gint) cp);

		if (cp != ' ' && cp != '\t')
		{
			/* Same placement raylib's DrawTextCodepoint() uses */
			rec = rl->recs[index];
			gx = pen_x + ((gfloat) rl->glyphs[index].offsetX - pad) * scale;
			gy = pen_y + ((gfloat) rl->glyphs[index].offsetY - pad) * scale;
			gw = (rec.width + 2.0f * pad) * scale;
			gh = (rec.height + 2.0f * pad) * scale;

			if (!bounds_outside_clip (self, gx, gy, gx + gw, gy + gh))
			{
				init_instance (self, &inst, LRG_PAINT_PRIM_IMAGE);
				inst.rect[0] = gx;
				inst.rect[1] = gy;
				inst.rect[2] = gx + gw;
				inst.rect[3] = gy + gh;
				inst.extra[0] = (rec.x - pad) / tw;
				inst.extra[1] = (rec.y - pad) / th;
				inst.extra[2] = (rec.x + rec.width + pad) / tw;
				inst.extra[3] = (rec.y + rec.height + pad) / th;
				memcpy (inst.color, rgba, 4);
				inst.texture = rl->texture.id;
				append_instance (self, &inst);
			}
		}

		pen_x += glyph_advance (rl, index, scale) + spacing;
	}
}

void
lrg_paint_list_text (LrgPaintList   *self,
                     GrlFont        *font,
                     const gchar    *text,
                     gfloat          x,
                     gfloat          y,
                     gfloat          size,
                     gfloat          spacing,
                     const GrlColor *color)
{
	Font        storage;
	const Font *rl;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (text != NULL);
	g_return_if_fail (color != NULL);

	rl = resolve_font (font, &storage);
	if (rl->glyphCount == 0 || rl->texture.id == 0)
		return;

	if (font != NULL)
		keep_ref (self, font);

	_lrg_paint_list_text_font (self, rl, text, -1, x, y, size, spacing, color);
}

void
lrg_paint_measure_text (GrlFont     *font,
                        const gchar *text,
                        gfloat       size,
                        gfloat       spacing,
                        gfloat      *width,
                        gfloat      *height)
{
	Font         storage;
	const Font  *rl;
	const gchar *p;
	gfloat       scale;
	gfloat       line_w = 0.0f;
	gfloat       max_w = 0.0f;
	guint        lines = 1;
	guint        glyphs_on_line = 0;

	g_return_if_fail (text != NULL);

	rl = resolve_font (font, &storage);

	for (p = text; *p != '\0'; p = g_utf8_next_char (p))
	{
		gunichar cp = g_utf8_get_char (p);

		if (cp == '\n')
		{
			/* the trailing spacing after the last glyph is not ink */
			if (glyphs_on_line > 0)
				line_w -= spacing;
			max_w = MAX (max_w, line_w);
			line_w = 0.0f;
			glyphs_on_line = 0;
			lines++;
			continue;
		}

		if (rl->glyphCount == 0 || rl->baseSize == 0)
		{
			/* No glyph data (no window yet): typical proportional width */
			line_w += size * 0.55f + spacing;
		}
		else
		{
			scale = size / (gfloat) rl->baseSize;
			line_w += glyph_advance (rl, GetGlyphIndex (*rl, (gint) cp), scale) + spacing;
		}
		glyphs_on_line++;
	}

	if (glyphs_on_line > 0)
		line_w -= spacing;
	max_w = MAX (max_w, line_w);

	if (width != NULL)
		*width = max_w;
	if (height != NULL)
		*height = size + (gfloat) (lines - 1) * size * LINE_HEIGHT_FACTOR;
}

/* ==========================================================================
 * Callbacks
 * ========================================================================== */

void
lrg_paint_list_callback (LrgPaintList     *self,
                         LrgPaintCallback  func,
                         gpointer          user_data,
                         GDestroyNotify    destroy)
{
	PaintCommand cmd;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (func != NULL);

	memset (&cmd, 0, sizeof (PaintCommand));
	cmd.kind = LRG_PAINT_COMMAND_CALLBACK;
	cmd.func = func;
	cmd.user_data = user_data;
	cmd.destroy = destroy;
	cmd.clip[0] = self->clip.x0;
	cmd.clip[1] = self->clip.y0;
	cmd.clip[2] = self->clip.x1;
	cmd.clip[3] = self->clip.y1;
	g_array_append_val (self->commands, cmd);
}

/* ==========================================================================
 * Groups
 * ========================================================================== */

guint
lrg_paint_list_begin_group (LrgPaintList *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_LIST (self), 0);

	return self->instances->len;
}

/* scale_alpha: multiply an RGBA8 alpha in place */
static void
scale_alpha (guint8 *rgba,
             gfloat  factor)
{
	rgba[3] = (guint8) CLAMP ((gint) ((gfloat) rgba[3] * factor + 0.5f), 0, 255);
}

void
lrg_paint_list_transform_group (LrgPaintList *self,
                                guint         mark,
                                gfloat        scale,
                                gfloat        pivot_x,
                                gfloat        pivot_y,
                                gfloat        translate_x,
                                gfloat        translate_y,
                                gfloat        opacity)
{
	guint i;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (mark <= self->instances->len);

	opacity = CLAMP (opacity, 0.0f, 1.0f);

	for (i = mark; i < self->instances->len; i++)
	{
		LrgPaintInstance *inst = &g_array_index (self->instances, LrgPaintInstance, i);
		guint             k;

		/* Both corners (or both line end points) move as points */
		for (k = 0; k < 4; k += 2)
		{
			inst->rect[k] = pivot_x + (inst->rect[k] - pivot_x) * scale + translate_x;
			inst->rect[k + 1] = pivot_y + (inst->rect[k + 1] - pivot_y) * scale + translate_y;
		}

		/* A triangle keeps its third vertex in extra */
		if ((gint) (inst->params[2] + 0.5f) == LRG_PAINT_PRIM_TRIANGLE)
		{
			inst->extra[0] = pivot_x + (inst->extra[0] - pivot_x) * scale + translate_x;
			inst->extra[1] = pivot_y + (inst->extra[1] - pivot_y) * scale + translate_y;
		}

		/* Sizes scale with the shape */
		for (k = 0; k < 4; k++)
			inst->radii[k] *= scale;
		inst->params[0] *= scale;
		inst->params[1] *= scale;
		if ((gint) (inst->params[3] + 0.5f) == LRG_PAINT_GRADIENT_RADIAL)
			inst->extra[2] *= scale;

		if (opacity < 1.0f)
		{
			scale_alpha (inst->color, opacity);
			scale_alpha (inst->color2, opacity);
			scale_alpha (inst->border, opacity);
		}
	}
}

/* ==========================================================================
 * Inspection
 * ========================================================================== */

guint
lrg_paint_list_get_n_instances (LrgPaintList *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_LIST (self), 0);

	return self->instances->len;
}

const LrgPaintInstance *
lrg_paint_list_get_instance (LrgPaintList *self,
                             guint         index)
{
	g_return_val_if_fail (LRG_IS_PAINT_LIST (self), NULL);
	g_return_val_if_fail (index < self->instances->len, NULL);

	return &g_array_index (self->instances, LrgPaintInstance, index);
}

const LrgPaintInstance *
lrg_paint_list_get_instances (LrgPaintList *self,
                              guint        *n_instances)
{
	g_return_val_if_fail (LRG_IS_PAINT_LIST (self), NULL);

	if (n_instances != NULL)
		*n_instances = self->instances->len;

	return (const LrgPaintInstance *) (gpointer) self->instances->data;
}

guint
lrg_paint_list_get_n_commands (LrgPaintList *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_LIST (self), 0);

	return self->commands->len;
}

LrgPaintCommandKind
lrg_paint_list_get_command (LrgPaintList *self,
                            guint         index,
                            guint        *first,
                            guint        *count,
                            guint        *texture)
{
	const PaintCommand *cmd;

	g_return_val_if_fail (LRG_IS_PAINT_LIST (self), LRG_PAINT_COMMAND_INSTANCES);
	g_return_val_if_fail (index < self->commands->len, LRG_PAINT_COMMAND_INSTANCES);

	cmd = &g_array_index (self->commands, PaintCommand, index);

	if (first != NULL)
		*first = cmd->first;
	if (count != NULL)
		*count = cmd->count;
	if (texture != NULL)
		*texture = cmd->texture;

	return cmd->kind;
}

gboolean
lrg_paint_list_get_command_clip (LrgPaintList *self,
                                 guint         index,
                                 gfloat       *x0,
                                 gfloat       *y0,
                                 gfloat       *x1,
                                 gfloat       *y1)
{
	const PaintCommand *cmd;

	g_return_val_if_fail (LRG_IS_PAINT_LIST (self), FALSE);
	g_return_val_if_fail (index < self->commands->len, FALSE);

	cmd = &g_array_index (self->commands, PaintCommand, index);

	if (x0 != NULL)
		*x0 = cmd->clip[0];
	if (y0 != NULL)
		*y0 = cmd->clip[1];
	if (x1 != NULL)
		*x1 = cmd->clip[2];
	if (y1 != NULL)
		*y1 = cmd->clip[3];

	return cmd->clip[0] > -UNCLIPPED || cmd->clip[1] > -UNCLIPPED ||
	       cmd->clip[2] < UNCLIPPED || cmd->clip[3] < UNCLIPPED;
}

void
lrg_paint_list_run_callback (LrgPaintList *self,
                             guint         index)
{
	const PaintCommand *cmd;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (index < self->commands->len);

	cmd = &g_array_index (self->commands, PaintCommand, index);
	g_return_if_fail (cmd->kind == LRG_PAINT_COMMAND_CALLBACK);

	cmd->func (cmd->user_data);
}
