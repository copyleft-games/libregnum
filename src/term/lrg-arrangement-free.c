/* lrg-arrangement-free.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-arrangement-free.h"
#include "lrg-scene-arrangement.h"
#include "lrg-scene-panel.h"

/* World height of a panel in the default spread; horizontal step between them. */
#define LRG_FREE_HEIGHT 4.0f
#define LRG_FREE_STEP   1.25f

struct _LrgArrangementFree
{
	GObject parent_instance;
};

static void lrg_arrangement_free_iface_init (LrgSceneArrangementInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgArrangementFree, lrg_arrangement_free, G_TYPE_OBJECT,
							   G_IMPLEMENT_INTERFACE (LRG_TYPE_SCENE_ARRANGEMENT,
													  lrg_arrangement_free_iface_init))

static const gchar *
free_get_id (LrgSceneArrangement *self)
{
	(void) self;
	return "free";
}

/* Spread panels in a loose horizontal row centred on the origin, each sized to
   its source aspect.  This is only the *default* placement: the user then
   grabs/moves panels (pinned ones are skipped by lrg_scene_panel_set_target). */
static void
free_layout (LrgSceneArrangement *self,
			 GPtrArray           *panels,
			 gint                 frame_width,
			 gint                 frame_height,
			 gfloat               scale)
{
	guint n = panels->len;
	guint i;

	(void) self;
	(void) frame_width;
	(void) frame_height;
	(void) scale;

	for (i = 0; i < n; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (panels, i);
		gint sx, sy, sw, sh;
		gfloat aspect, w, x;

		lrg_scene_panel_get_source_rect (p, &sx, &sy, &sw, &sh);
		aspect = (sh > 0) ? (gfloat) sw / (gfloat) sh : 1.6f;
		w = LRG_FREE_HEIGHT * aspect;
		x = ((gfloat) i - ((gfloat) n - 1.0f) * 0.5f) * (LRG_FREE_HEIGHT * LRG_FREE_STEP);

		lrg_scene_panel_set_target (p, x, 0.0f, 0.0f, 0.0f, w, LRG_FREE_HEIGHT);
	}
}

static void
lrg_arrangement_free_iface_init (LrgSceneArrangementInterface *iface)
{
	iface->get_id = free_get_id;
	iface->layout = free_layout;
	iface->wants_continuous = NULL;
}

static void
lrg_arrangement_free_class_init (LrgArrangementFreeClass *klass)
{
	(void) klass;
}

static void
lrg_arrangement_free_init (LrgArrangementFree *self)
{
	(void) self;
}

LrgArrangementFree *
lrg_arrangement_free_new (void)
{
	return g_object_new (LRG_TYPE_ARRANGEMENT_FREE, NULL);
}
