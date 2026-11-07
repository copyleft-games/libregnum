/* lrg-arrangement-stack.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include <math.h>
#include "lrg-arrangement-stack.h"
#include "lrg-scene-arrangement.h"
#include "lrg-scene-panel.h"

/* Panel height, the sideways and upward drift per depth level, the turn of a
   receding panel toward the centre, and the brightness lost per level (never
   below the floor, so deep panels stay legible enough to pick). */
#define LRG_STACK_HEIGHT     3.4f
#define LRG_STACK_DRIFT_X    1.1f
#define LRG_STACK_DRIFT_Y    0.12f
#define LRG_STACK_TURN       10.0f
#define LRG_STACK_FADE       0.16f
#define LRG_STACK_FADE_FLOOR 0.25f

struct _LrgArrangementStack
{
	GObject parent_instance;

	gfloat depth_step;
};

static void lrg_arrangement_stack_iface_init (LrgSceneArrangementInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgArrangementStack, lrg_arrangement_stack, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_SCENE_ARRANGEMENT,
                                                      lrg_arrangement_stack_iface_init))

static const gchar *
stack_get_id (LrgSceneArrangement *self)
{
	(void) self;
	return "stack";
}

/*
 * Each panel's place comes straight from its ordinal k:
 * - k = 0 is the front panel: the origin, facing +Z, full brightness.
 * - |k| levels recede |k| * depth_step along -Z and drift up a little.
 * - k > 0 drifts left and turns right (toward the centre); k < 0 mirrors it.
 * - Brightness drops by LRG_STACK_FADE per level down to a floor.
 */
static void
stack_layout (LrgSceneArrangement *arrangement,
              GPtrArray           *panels,
              gint                 frame_width,
              gint                 frame_height,
              gfloat               scale)
{
	LrgArrangementStack *self = LRG_ARRANGEMENT_STACK (arrangement);
	guint i;

	(void) frame_width;
	(void) frame_height;
	(void) scale;

	for (i = 0; i < panels->len; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (panels, i);
		gint k = lrg_scene_panel_get_ordinal (p);
		gfloat depth = (gfloat) ABS (k);
		gfloat side = k > 0 ? -1.0f : (k < 0 ? 1.0f : 0.0f);
		gint sw, sh;
		gfloat aspect;

		lrg_scene_panel_get_source_rect (p, NULL, NULL, &sw, &sh);
		aspect = (sw > 0 && sh > 0) ? (gfloat) sw / (gfloat) sh : 1.6f;

		lrg_scene_panel_set_target (p,
		                            side * depth * LRG_STACK_DRIFT_X,
		                            depth * LRG_STACK_DRIFT_Y,
		                            -depth * self->depth_step,
		                            -side * LRG_STACK_TURN,
		                            LRG_STACK_HEIGHT * aspect,
		                            LRG_STACK_HEIGHT);
		lrg_scene_panel_set_brightness (p, MAX (LRG_STACK_FADE_FLOOR,
		                                        1.0f - LRG_STACK_FADE * depth));
	}
}

static void
lrg_arrangement_stack_iface_init (LrgSceneArrangementInterface *iface)
{
	iface->get_id = stack_get_id;
	iface->layout = stack_layout;
	iface->wants_continuous = NULL;
}

static void
lrg_arrangement_stack_class_init (LrgArrangementStackClass *klass)
{
	(void) klass;
}

static void
lrg_arrangement_stack_init (LrgArrangementStack *self)
{
	self->depth_step = 1.6f;
}

LrgArrangementStack *
lrg_arrangement_stack_new (void)
{
	return g_object_new (LRG_TYPE_ARRANGEMENT_STACK, NULL);
}

void
lrg_arrangement_stack_set_depth_step (LrgArrangementStack *self,
                                      gfloat               step)
{
	g_return_if_fail (LRG_IS_ARRANGEMENT_STACK (self));
	self->depth_step = CLAMP (step, 0.2f, 10.0f);
}

gfloat
lrg_arrangement_stack_get_depth_step (LrgArrangementStack *self)
{
	g_return_val_if_fail (LRG_IS_ARRANGEMENT_STACK (self), 0.0f);
	return self->depth_step;
}
