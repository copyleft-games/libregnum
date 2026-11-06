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
#include "lrg-paint-font.h"
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

/*
 * ClipNode:
 *
 * One lrg_paint_list_push_clip() call, kept until the list is cleared
 * so group transforms can move clips pushed inside the group. Node ids
 * are index + 1; id 0 means "nothing pushed".
 */
typedef struct
{
	gfloat x0;      /* the rectangle as pushed (not intersected) */
	gfloat y0;
	gfloat x1;
	gfloat y1;
	guint  parent;  /* node active when this one was pushed, or 0 */
	guint  depth;   /* clip stack depth while this node is active (1 = outermost) */
	guint  stamp;   /* last transform_group call that moved it */
} ClipNode;

struct _LrgPaintList
{
	GObject    parent_instance;

	GArray    *instances;   /* LrgPaintInstance */
	GArray    *commands;    /* PaintCommand */
	GArray    *clip_stack;  /* ClipRect */
	GArray    *clip_nodes;  /* ClipNode, every push since the last clear */
	GArray    *inst_clip;   /* guint per instance: its clip node id */
	guint      clip_node;   /* node of the current clip, 0 = none */
	guint      group_stamp; /* counts transform_group calls */
	GArray    *alpha_stack; /* gfloat */
	GPtrArray *refs;        /* GObjects kept alive until clear */

	ClipRect   clip;
	gfloat     opacity;

	/* Frame inputs for animated primitives; kept across clear () */
	gdouble    time;
	gfloat     pointer_x;
	gfloat     pointer_y;
	gboolean   has_pointer;
};

/* A backdrop command merges at most this many instances, which bounds
 * the overlap test in append_instance () */
#define MAX_BACKDROP_BATCH (256)

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

/* prim_of: the primitive of an instance */
static LrgPaintPrim
prim_of (const LrgPaintInstance *inst)
{
	return (LrgPaintPrim) (gint) (inst->params[2] + 0.5f);
}

/* needs_backdrop: TRUE for primitives that sample what is behind them */
static gboolean
needs_backdrop (const LrgPaintInstance *inst)
{
	LrgPaintPrim prim = prim_of (inst);

	return prim == LRG_PAINT_PRIM_BLUR || prim == LRG_PAINT_PRIM_GLASS;
}

void
_lrg_paint_instance_get_bounds (const LrgPaintInstance *inst,
                                gfloat                 *x0,
                                gfloat                 *y0,
                                gfloat                 *x1,
                                gfloat                 *y1)
{
	gfloat lo_x;
	gfloat lo_y;
	gfloat hi_x;
	gfloat hi_y;
	gfloat pad = 1.0f;

	/* Same expansion as the vertex shader, so the box covers every
	 * pixel the instance can touch */
	switch (prim_of (inst))
	{
	case LRG_PAINT_PRIM_SHADOW:
		pad = inst->params[1] + 1.0f;
		break;
	case LRG_PAINT_PRIM_LINE:
		pad = inst->params[1] * 0.5f + 1.0f;
		break;
	default:
		break;
	}

	lo_x = MIN (inst->rect[0], inst->rect[2]);
	lo_y = MIN (inst->rect[1], inst->rect[3]);
	hi_x = MAX (inst->rect[0], inst->rect[2]);
	hi_y = MAX (inst->rect[1], inst->rect[3]);

	if (prim_of (inst) == LRG_PAINT_PRIM_TRIANGLE)
	{
		lo_x = MIN (lo_x, inst->extra[0]);
		lo_y = MIN (lo_y, inst->extra[1]);
		hi_x = MAX (hi_x, inst->extra[0]);
		hi_y = MAX (hi_y, inst->extra[1]);
	}
	else if (prim_of (inst) == LRG_PAINT_PRIM_CURVE)
	{
		/* A Bezier stays inside the hull of its control points */
		lo_x = MIN (lo_x, MIN (inst->extra[0], inst->extra[2]));
		lo_y = MIN (lo_y, MIN (inst->extra[1], inst->extra[3]));
		hi_x = MAX (hi_x, MAX (inst->extra[0], inst->extra[2]));
		hi_y = MAX (hi_y, MAX (inst->extra[1], inst->extra[3]));
		pad = inst->params[1] * 0.5f + MAX (inst->data[0], 0.0f) + 1.0f;
	}

	*x0 = MAX (lo_x - pad, inst->clip[0]);
	*y0 = MAX (lo_y - pad, inst->clip[1]);
	*x1 = MIN (hi_x + pad, inst->clip[2]);
	*y1 = MIN (hi_y + pad, inst->clip[3]);
}

void
_lrg_paint_instance_get_backdrop_reach (const LrgPaintInstance *inst,
                                        gfloat                 *x0,
                                        gfloat                 *y0,
                                        gfloat                 *x1,
                                        gfloat                 *y1)
{
	gfloat reach;

	/* A blur or glass pixel reads the backdrop up to the blur radius
	 * (twice, for the Kawase taps) plus the refraction offset away */
	reach = MAX (inst->params[1], 0.0f) * 2.0f + 2.0f;
	if (prim_of (inst) == LRG_PAINT_PRIM_GLASS)
		reach += fabsf (inst->extra[1]) * (1.0f + inst->data[0]);

	*x0 = MAX (inst->rect[0] - reach, inst->clip[0] - reach);
	*y0 = MAX (inst->rect[1] - reach, inst->clip[1] - reach);
	*x1 = MIN (inst->rect[2] + reach, inst->clip[2] + reach);
	*y1 = MIN (inst->rect[3] + reach, inst->clip[3] + reach);
}

/*
 * backdrop_can_join:
 *
 * A blur/glass instance may join the backdrop batch @cmd (sharing its
 * capture of the target) only if nothing already in the batch lies
 * where the new instance reads the backdrop: everything it can see was
 * then drawn before the batch, and so is in the capture.
 */
static gboolean
backdrop_can_join (LrgPaintList           *self,
                   const PaintCommand     *cmd,
                   const LrgPaintInstance *inst)
{
	gfloat rx0;
	gfloat ry0;
	gfloat rx1;
	gfloat ry1;
	guint  i;

	if (cmd->count >= MAX_BACKDROP_BATCH)
		return FALSE;

	_lrg_paint_instance_get_backdrop_reach (inst, &rx0, &ry0, &rx1, &ry1);

	for (i = cmd->first; i < cmd->first + cmd->count; i++)
	{
		const LrgPaintInstance *other;
		gfloat                  x0;
		gfloat                  y0;
		gfloat                  x1;
		gfloat                  y1;

		other = &g_array_index (self->instances, LrgPaintInstance, i);
		_lrg_paint_instance_get_bounds (other, &x0, &y0, &x1, &y1);
		if (x0 < rx1 && x1 > rx0 && y0 < ry1 && y1 > ry0)
			return FALSE;
	}

	return TRUE;
}

/*
 * append_instance:
 *
 * Adds an instance and extends the last batch when it can: any number
 * of untextured instances may join a batch, plus instances of at most
 * one texture (the shader only samples for image instances).
 *
 * Blur and glass instances start a BACKDROP batch, for which the
 * renderer first captures the target. They join an existing backdrop
 * batch only when nothing in it overlaps what they sample (see
 * backdrop_can_join ()); plain instances join either kind of batch.
 */
static void
append_instance (LrgPaintList           *self,
                 const LrgPaintInstance *inst)
{
	PaintCommand *last = NULL;
	gboolean      backdrop;
	gboolean      texture_ok;

	backdrop = needs_backdrop (inst);

	if (self->commands->len > 0)
		last = &g_array_index (self->commands, PaintCommand, self->commands->len - 1);

	texture_ok = last != NULL &&
	             (inst->texture == 0 || last->texture == 0 || last->texture == inst->texture);

	if (last != NULL && texture_ok &&
	    ((!backdrop && (last->kind == LRG_PAINT_COMMAND_INSTANCES ||
	                    last->kind == LRG_PAINT_COMMAND_BACKDROP)) ||
	     (backdrop && last->kind == LRG_PAINT_COMMAND_BACKDROP &&
	      backdrop_can_join (self, last, inst))))
	{
		g_array_append_val (self->instances, *inst);
		g_array_append_val (self->inst_clip, self->clip_node);
		last->count++;
		if (inst->texture != 0)
			last->texture = inst->texture;
		return;
	}

	g_array_append_val (self->instances, *inst);
	g_array_append_val (self->inst_clip, self->clip_node);

	{
		PaintCommand cmd;

		memset (&cmd, 0, sizeof (PaintCommand));
		cmd.kind = backdrop ? LRG_PAINT_COMMAND_BACKDROP : LRG_PAINT_COMMAND_INSTANCES;
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

void
_lrg_paint_list_keep (LrgPaintList *self,
                      gpointer      object)
{
	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (G_IS_OBJECT (object));

	keep_ref (self, object);
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
	g_array_unref (self->clip_nodes);
	g_array_unref (self->inst_clip);
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
	self->clip_nodes = g_array_new (FALSE, FALSE, sizeof (ClipNode));
	self->inst_clip = g_array_sized_new (FALSE, FALSE, sizeof (guint), 256);
	self->clip_node = 0;
	self->group_stamp = 0;
	self->alpha_stack = g_array_new (FALSE, FALSE, sizeof (gfloat));
	self->refs = g_ptr_array_new_with_free_func (g_object_unref);

	self->clip.x0 = -UNCLIPPED;
	self->clip.y0 = -UNCLIPPED;
	self->clip.x1 = UNCLIPPED;
	self->clip.y1 = UNCLIPPED;
	self->opacity = 1.0f;
	self->time = 0.0;
	self->has_pointer = FALSE;
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
	g_array_set_size (self->clip_nodes, 0);
	g_array_set_size (self->inst_clip, 0);
	self->clip_node = 0;
	self->group_stamp = 0;
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

	/* Remember the push itself so a group transform can move it later */
	{
		ClipNode node;

		node.x0 = x;
		node.y0 = y;
		node.x1 = x + MAX (width, 0.0f);
		node.y1 = y + MAX (height, 0.0f);
		node.parent = self->clip_node;
		node.depth = self->clip_stack->len;
		node.stamp = 0;
		g_array_append_val (self->clip_nodes, node);
		self->clip_node = self->clip_nodes->len;
	}

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
	if (self->clip_node != 0)
		self->clip_node = g_array_index (self->clip_nodes, ClipNode, self->clip_node - 1).parent;
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
_lrg_paint_list_glyph (LrgPaintList   *self,
                       const gfloat   *rect,
                       const gfloat   *atlas,
                       guint           texture,
                       gfloat          dist_scale,
                       gfloat          softness,
                       gfloat          dilate,
                       gfloat          outline,
                       gfloat          glow,
                       const GrlColor *color,
                       const GrlColor *outline_color,
                       const GrlColor *glow_color)
{
	LrgPaintInstance inst;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	if (bounds_outside_clip (self, rect[0], rect[1], rect[2], rect[3]))
		return;

	/*
	 * Layout of a glyph instance:
	 *   rect   quad in canvas units (the field's padding included)
	 *   extra  the field's rectangle in atlas pixels; the shader divides
	 *          by the bound texture's size, so a page can grow without
	 *          invalidating instances already recorded
	 *   params outline width, glow radius
	 *   data   distance scale (canvas units at the field's full range),
	 *          softness, dilation
	 *   color / border / color2   fill / outline / glow colours
	 */
	init_instance (self, &inst, LRG_PAINT_PRIM_GLYPH);
	memcpy (inst.rect, rect, sizeof (inst.rect));
	memcpy (inst.extra, atlas, sizeof (inst.extra));
	inst.params[0] = outline;
	inst.params[1] = glow;
	inst.data[0] = dist_scale;
	inst.data[1] = softness;
	inst.data[2] = dilate;
	color_to_bytes (self, color, inst.color);
	color_to_bytes (self, outline > 0.0f ? outline_color : NULL, inst.border);
	color_to_bytes (self, glow > 0.0f ? glow_color : NULL, inst.color2);
	inst.texture = texture;

	append_instance (self, &inst);
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
	Font          storage;
	const Font   *rl;
	LrgPaintFont *sdf;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (text != NULL);
	g_return_if_fail (color != NULL);

	/* An SDF font bound to @font (or the default for NULL) wins */
	sdf = lrg_paint_font_lookup (font);
	if (sdf != NULL)
	{
		LrgPaintTextStyle style;

		lrg_paint_text_style_init (&style, color);
		style.spacing = spacing;
		if (font != NULL)
			keep_ref (self, font);
		lrg_paint_font_draw_text (sdf, self, text, -1, x, y, size, &style);
		return;
	}

	rl = resolve_font (font, &storage);
	if (rl->glyphCount == 0 || rl->texture.id == 0)
		return;

	if (font != NULL)
		keep_ref (self, font);

	_lrg_paint_list_text_font (self, rl, text, -1, x, y, size, spacing, color);
}

void
lrg_paint_list_text_styled (LrgPaintList            *self,
                            GrlFont                 *font,
                            const gchar             *text,
                            gfloat                   x,
                            gfloat                   y,
                            gfloat                   size,
                            const LrgPaintTextStyle *style)
{
	LrgPaintFont *sdf;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (text != NULL);
	g_return_if_fail (style != NULL);

	sdf = lrg_paint_font_lookup (font);
	if (sdf == NULL)
	{
		lrg_paint_list_text (self, font, text, x, y, size, style->spacing, &style->color);
		return;
	}

	if (font != NULL)
		keep_ref (self, font);
	lrg_paint_font_draw_text (sdf, self, text, -1, x, y, size, style);
}

void
lrg_paint_measure_text (GrlFont     *font,
                        const gchar *text,
                        gfloat       size,
                        gfloat       spacing,
                        gfloat      *width,
                        gfloat      *height)
{
	Font          storage;
	const Font   *rl;
	const gchar  *p;
	LrgPaintFont *sdf;
	gfloat        scale;
	gfloat        line_w = 0.0f;
	gfloat        max_w = 0.0f;
	guint         lines = 1;
	guint         glyphs_on_line = 0;

	g_return_if_fail (text != NULL);

	/* Same font choice as lrg_paint_list_text() */
	sdf = lrg_paint_font_lookup (font);
	if (sdf != NULL)
	{
		lrg_paint_font_measure_text (sdf, text, -1, size, spacing, LRG_TEXT_DIRECTION_AUTO,
		                             width, height);
		return;
	}

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
 * Materials, effects and curves
 * ========================================================================== */

void
lrg_paint_list_set_time (LrgPaintList *self,
                         gdouble       seconds)
{
	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	self->time = seconds;
}

gdouble
lrg_paint_list_get_time (LrgPaintList *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_LIST (self), 0.0);

	return self->time;
}

void
lrg_paint_list_set_pointer (LrgPaintList *self,
                            gfloat        x,
                            gfloat        y)
{
	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	self->pointer_x = x;
	self->pointer_y = y;
	self->has_pointer = TRUE;
}

void
lrg_paint_list_unset_pointer (LrgPaintList *self)
{
	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	self->has_pointer = FALSE;
}

gboolean
lrg_paint_list_get_pointer (LrgPaintList *self,
                            gfloat       *x,
                            gfloat       *y)
{
	g_return_val_if_fail (LRG_IS_PAINT_LIST (self), FALSE);

	if (x != NULL)
		*x = self->has_pointer ? self->pointer_x : 0.0f;
	if (y != NULL)
		*y = self->has_pointer ? self->pointer_y : 0.0f;

	return self->has_pointer;
}

/* raw_color_bytes: GrlColor -> RGBA8 without the opacity stack */
static void
raw_color_bytes (const GrlColor *color,
                 guint8         *out)
{
	out[0] = color->r;
	out[1] = color->g;
	out[2] = color->b;
	out[3] = color->a;
}

/* set_radii: clamp and store per-corner radii (NULL = square) */
static void
set_radii (LrgPaintInstance     *inst,
           const LrgCornerRadii *radii,
           gfloat                width,
           gfloat                height)
{
	if (radii == NULL)
		return;

	inst->radii[0] = clamp_radius (radii->top_left, width, height);
	inst->radii[1] = clamp_radius (radii->top_right, width, height);
	inst->radii[2] = clamp_radius (radii->bottom_right, width, height);
	inst->radii[3] = clamp_radius (radii->bottom_left, width, height);
}

/*
 * record_backdrop:
 *
 * Shared body of blur and glass. Their parameters live in extra
 * (frost, refraction, saturation, rim light) and data (dispersion,
 * rim bevel width, opacity); params.y is the blur radius. The tint is
 * stored without the opacity stack because the shader fades the whole
 * slab by data.z instead.
 */
static void
record_backdrop (LrgPaintList         *self,
                 LrgPaintPrim          prim,
                 gfloat                x,
                 gfloat                y,
                 gfloat                width,
                 gfloat                height,
                 const LrgCornerRadii *radii,
                 const LrgPaintGlass  *glass)
{
	LrgPaintInstance inst;

	if (width <= 0.0f || height <= 0.0f || self->opacity <= 0.0f)
		return;
	if (bounds_outside_clip (self, x, y, x + width, y + height))
		return;

	init_instance (self, &inst, prim);
	inst.rect[0] = x;
	inst.rect[1] = y;
	inst.rect[2] = x + width;
	inst.rect[3] = y + height;
	set_radii (&inst, radii, width, height);

	inst.params[1] = MAX (glass->blur_radius, 0.0f);
	inst.extra[0] = CLAMP (glass->frost, 0.0f, 1.0f);
	inst.extra[1] = MAX (glass->refraction, 0.0f);
	inst.extra[2] = MAX (glass->saturation, 0.0f);
	inst.extra[3] = CLAMP (glass->rim_light, 0.0f, 1.0f);
	inst.data[0] = CLAMP (glass->dispersion, 0.0f, 1.0f);
	/* The rim (where refraction bends the backdrop) is a band along
	 * the edge, proportional to the slab but never huge */
	inst.data[1] = CLAMP (MIN (width, height) * 0.2f, 3.0f, 24.0f);
	inst.data[2] = self->opacity;
	raw_color_bytes (&glass->tint, inst.color);

	append_instance (self, &inst);
}

void
lrg_paint_list_blur (LrgPaintList   *self,
                     gfloat          x,
                     gfloat          y,
                     gfloat          width,
                     gfloat          height,
                     gfloat          radius,
                     gfloat          blur_radius,
                     const GrlColor *tint)
{
	LrgPaintGlass  glass;
	LrgCornerRadii radii;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	/* A blur is a glass with full frost and none of the slab effects */
	memset (&glass, 0, sizeof (LrgPaintGlass));
	glass.frost = 1.0f;
	glass.saturation = 1.0f;
	glass.blur_radius = blur_radius;
	if (tint != NULL)
		glass.tint = *tint;

	lrg_corner_radii_init_uniform (&radii, radius);
	record_backdrop (self, LRG_PAINT_PRIM_BLUR, x, y, width, height, &radii, &glass);
}

void
lrg_paint_list_glass (LrgPaintList          *self,
                      gfloat                 x,
                      gfloat                 y,
                      gfloat                 width,
                      gfloat                 height,
                      const LrgCornerRadii  *radii,
                      const LrgPaintGlass   *params)
{
	LrgPaintGlass defaults;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	if (params == NULL)
	{
		lrg_paint_glass_init_default (&defaults);
		params = &defaults;
	}

	record_backdrop (self, LRG_PAINT_PRIM_GLASS, x, y, width, height, radii, params);
}

void
lrg_paint_list_effect (LrgPaintList               *self,
                       gfloat                      x,
                       gfloat                      y,
                       gfloat                      width,
                       gfloat                      height,
                       const LrgCornerRadii       *radii,
                       gint                        effect_id,
                       const LrgPaintEffectParams *params)
{
	LrgPaintEffectParams defaults;
	LrgPaintInstance     inst;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));

	if (width <= 0.0f || height <= 0.0f || self->opacity <= 0.0f)
		return;
	if (lrg_paint_effect_get_name (effect_id) == NULL)
		return;
	if (bounds_outside_clip (self, x, y, x + width, y + height))
		return;

	if (params == NULL)
	{
		lrg_paint_effect_params_init_default (&defaults);
		params = &defaults;
	}

	/* Palette in the three colour slots, state in extra, pacing and
	 * opacity in data (colours stay unfaded: data.z fades the result) */
	init_instance (self, &inst, LRG_PAINT_PRIM_EFFECT);
	inst.rect[0] = x;
	inst.rect[1] = y;
	inst.rect[2] = x + width;
	inst.rect[3] = y + height;
	set_radii (&inst, radii, width, height);
	raw_color_bytes (&params->color1, inst.color);
	raw_color_bytes (&params->color2, inst.color2);
	raw_color_bytes (&params->color3, inst.border);
	inst.extra[0] = CLAMP (params->hover, 0.0f, 1.0f);
	inst.extra[1] = CLAMP (params->press, 0.0f, 1.0f);
	inst.extra[2] = MAX (params->intensity, 0.0f);
	inst.extra[3] = (gfloat) effect_id;
	inst.data[0] = params->speed;
	inst.data[1] = params->scale > 0.0f ? params->scale : 1.0f;
	inst.data[2] = self->opacity;

	append_instance (self, &inst);
}

void
lrg_paint_list_curve (LrgPaintList             *self,
                      gfloat                    x0,
                      gfloat                    y0,
                      gfloat                    x1,
                      gfloat                    y1,
                      gfloat                    x2,
                      gfloat                    y2,
                      gfloat                    x3,
                      gfloat                    y3,
                      gfloat                    thickness,
                      const LrgPaintCurveStyle *style)
{
	LrgPaintInstance inst;
	gfloat           pad;

	g_return_if_fail (LRG_IS_PAINT_LIST (self));
	g_return_if_fail (style != NULL);

	if (thickness <= 0.0f || self->opacity <= 0.0f)
		return;
	if (style->color_start.a == 0 && style->color_end.a == 0 &&
	    (style->glow.a == 0 || style->glow_size <= 0.0f) &&
	    (style->pulse_count == 0 || style->pulse_color.a == 0))
		return;

	pad = thickness * 0.5f + MAX (style->glow_size, 0.0f) + 1.0f;
	if (bounds_outside_clip (self,
	                         MIN (MIN (x0, x1), MIN (x2, x3)) - pad,
	                         MIN (MIN (y0, y1), MIN (y2, y3)) - pad,
	                         MAX (MAX (x0, x1), MAX (x2, x3)) + pad,
	                         MAX (MAX (y0, y1), MAX (y2, y3)) + pad))
		return;

	/* End points in rect, inner control points in extra, colours as a
	 * gradient, glow in border, pulse colour (0..1) in radii */
	init_instance (self, &inst, LRG_PAINT_PRIM_CURVE);
	inst.rect[0] = x0;
	inst.rect[1] = y0;
	inst.rect[2] = x3;
	inst.rect[3] = y3;
	inst.extra[0] = x1;
	inst.extra[1] = y1;
	inst.extra[2] = x2;
	inst.extra[3] = y2;
	inst.params[1] = thickness;
	color_to_bytes (self, &style->color_start, inst.color);
	color_to_bytes (self, &style->color_end, inst.color2);
	if (style->glow_size > 0.0f)
	{
		color_to_bytes (self, &style->glow, inst.border);
		inst.data[0] = style->glow_size;
	}
	inst.data[1] = (gfloat) MIN (style->pulse_count, 8u);
	inst.data[2] = style->pulse_speed;
	inst.data[3] = style->pulse_phase;
	inst.radii[0] = (gfloat) style->pulse_color.r / 255.0f;
	inst.radii[1] = (gfloat) style->pulse_color.g / 255.0f;
	inst.radii[2] = (gfloat) style->pulse_color.b / 255.0f;
	inst.radii[3] = (gfloat) style->pulse_color.a / 255.0f * self->opacity;

	append_instance (self, &inst);
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

/*
 * clip_node_effective:
 *
 * The clip rectangle of node @id: the intersection of every pushed
 * rectangle on its chain (as push_clip computes it), collapsed to a
 * zero box when empty. Node 0 is unclipped.
 */
static void
clip_node_effective (LrgPaintList *self,
                     guint         id,
                     gfloat       *out)
{
	out[0] = -UNCLIPPED;
	out[1] = -UNCLIPPED;
	out[2] = UNCLIPPED;
	out[3] = UNCLIPPED;

	while (id != 0)
	{
		const ClipNode *n = &g_array_index (self->clip_nodes, ClipNode, id - 1);

		out[0] = MAX (out[0], n->x0);
		out[1] = MAX (out[1], n->y0);
		out[2] = MIN (out[2], n->x1);
		out[3] = MIN (out[3], n->y1);
		id = n->parent;
	}
	if (out[2] < out[0])
		out[2] = out[0];
	if (out[3] < out[1])
		out[3] = out[1];
}

/*
 * move_group_clips:
 *
 * The clip half of lrg_paint_list_transform_group(). Clips pushed (and
 * popped) inside the group belong to it and move with it; clips that
 * were already pushed when the group began stay where they are.
 *
 * At transform time the clip stack is back at the depth it had when
 * the group began (pushes inside a group are balanced), so a node on
 * an instance's chain deeper than the current depth was pushed inside
 * the group: it cannot have existed before, because the stack was no
 * deeper than that when the group began and popped nodes are never
 * pushed again. Each such node's rectangle is transformed once per
 * call (the stamp), then every affected instance gets its clip
 * recomputed from its whole chain. Nested groups compose naturally:
 * an inner group moves its nodes first, the outer group moves them
 * again together with its own.
 */
static void
move_group_clips (LrgPaintList *self,
                  guint         mark,
                  gfloat        scale,
                  gfloat        pivot_x,
                  gfloat        pivot_y,
                  gfloat        translate_x,
                  gfloat        translate_y)
{
	guint depth = self->clip_stack->len;
	guint i;

	if (self->clip_nodes->len == 0)
		return;

	self->group_stamp++;
	if (self->group_stamp == 0)
		self->group_stamp = 1;

	for (i = mark; i < self->instances->len; i++)
	{
		guint             id = g_array_index (self->inst_clip, guint, i);
		guint             walk;
		LrgPaintInstance *inst;

		if (id == 0 || g_array_index (self->clip_nodes, ClipNode, id - 1).depth <= depth)
			continue;

		/* Move this group's nodes on the chain (once each per call) */
		for (walk = id; walk != 0; )
		{
			ClipNode *n = &g_array_index (self->clip_nodes, ClipNode, walk - 1);
			gfloat    ax;
			gfloat    ay;
			gfloat    bx;
			gfloat    by;

			if (n->depth <= depth)
				break;
			if (n->stamp != self->group_stamp)
			{
				n->stamp = self->group_stamp;
				ax = pivot_x + (n->x0 - pivot_x) * scale + translate_x;
				ay = pivot_y + (n->y0 - pivot_y) * scale + translate_y;
				bx = pivot_x + (n->x1 - pivot_x) * scale + translate_x;
				by = pivot_y + (n->y1 - pivot_y) * scale + translate_y;
				n->x0 = MIN (ax, bx);
				n->y0 = MIN (ay, by);
				n->x1 = MAX (ax, bx);
				n->y1 = MAX (ay, by);
			}
			walk = n->parent;
		}

		/* Outer nodes kept, inner ones moved: re-intersect the chain */
		inst = &g_array_index (self->instances, LrgPaintInstance, i);
		clip_node_effective (self, id, inst->clip);
	}
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
		LrgPaintPrim      prim = prim_of (inst);
		guint             k;

		/* Both corners (or both line end points) move as points */
		for (k = 0; k < 4; k += 2)
		{
			inst->rect[k] = pivot_x + (inst->rect[k] - pivot_x) * scale + translate_x;
			inst->rect[k + 1] = pivot_y + (inst->rect[k + 1] - pivot_y) * scale + translate_y;
		}

		/* A triangle keeps its third vertex in extra, a curve its two
		 * inner control points */
		if (prim == LRG_PAINT_PRIM_TRIANGLE || prim == LRG_PAINT_PRIM_CURVE)
		{
			for (k = 0; k < 4; k += 2)
			{
				if (prim == LRG_PAINT_PRIM_TRIANGLE && k > 0)
					break;
				inst->extra[k] = pivot_x + (inst->extra[k] - pivot_x) * scale + translate_x;
				inst->extra[k + 1] = pivot_y + (inst->extra[k + 1] - pivot_y) * scale + translate_y;
			}
		}

		/* Sizes scale with the shape; a curve's radii hold its pulse
		 * colour, not sizes */
		if (prim != LRG_PAINT_PRIM_CURVE)
			for (k = 0; k < 4; k++)
				inst->radii[k] *= scale;
		inst->params[0] *= scale;
		inst->params[1] *= scale;
		if (prim == LRG_PAINT_PRIM_RECT &&
		    (gint) (inst->params[3] + 0.5f) == LRG_PAINT_GRADIENT_RADIAL)
			inst->extra[2] *= scale;
		if (prim == LRG_PAINT_PRIM_GLASS)
		{
			inst->extra[1] *= scale;   /* refraction */
			inst->data[1] *= scale;    /* bevel */
		}
		if (prim == LRG_PAINT_PRIM_CURVE)
			inst->data[0] *= scale;    /* glow size */
		if (prim == LRG_PAINT_PRIM_GLYPH)
		{
			inst->data[0] *= scale;    /* distance scale */
			inst->data[1] *= scale;    /* softness */
			inst->data[2] *= scale;    /* dilation */
		}

		if (opacity < 1.0f)
		{
			/* Backdrop and effect instances carry their opacity on
			 * its own; their colours are not faded */
			if (prim == LRG_PAINT_PRIM_BLUR || prim == LRG_PAINT_PRIM_GLASS ||
			    prim == LRG_PAINT_PRIM_EFFECT)
			{
				inst->data[2] *= opacity;
				continue;
			}
			scale_alpha (inst->color, opacity);
			scale_alpha (inst->color2, opacity);
			scale_alpha (inst->border, opacity);
			if (prim == LRG_PAINT_PRIM_CURVE)
				inst->radii[3] *= opacity;
		}
	}

	move_group_clips (self, mark, scale, pivot_x, pivot_y, translate_x, translate_y);
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
