/* lrg-arrangement-single.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-arrangement-single.h"
#include "lrg-scene-arrangement.h"
#include "lrg-scene-panel.h"

/* World height the frame fills at the default camera distance; width follows
   the captured frame's aspect ratio. */
#define LRG_SINGLE_HEIGHT 4.6f

struct _LrgArrangementSingle
{
	GObject parent_instance;
};

static void lrg_arrangement_single_iface_init (LrgSceneArrangementInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgArrangementSingle, lrg_arrangement_single, G_TYPE_OBJECT,
							   G_IMPLEMENT_INTERFACE (LRG_TYPE_SCENE_ARRANGEMENT,
													  lrg_arrangement_single_iface_init))

static const gchar *
single_get_id (LrgSceneArrangement *self)
{
	(void) self;
	return "single-panel";
}

static void
single_layout (LrgSceneArrangement *self,
			   GPtrArray           *panels,
			   gint                 frame_width,
			   gint                 frame_height,
			   gfloat               scale)
{
	/* Always-fit: the whole frame fills the camera view at a fixed world height
	   with the width following the frame aspect, so the panel keeps a consistent
	   on-screen framing at every window size (re-fits on resize) and never
	   stretches (world aspect == frame aspect == camera aspect). */
	gfloat aspect = frame_height > 0
						? (gfloat) frame_width / (gfloat) frame_height : 1.6f;
	gfloat h = LRG_SINGLE_HEIGHT;
	gfloat w = h * aspect;
	guint i;

	(void) self;
	(void) scale;

	for (i = 0; i < panels->len; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (panels, i);

		if (i == 0)
			lrg_scene_panel_set_target (p, 0.0f, 0.0f, 0.0f, 0.0f, w, h);
		else
			/* Single-panel mode normally has exactly one panel; park any
			   extras far behind, tiny, so they neither show nor pick. */
			lrg_scene_panel_set_target (p, 0.0f, 0.0f, -100.0f, 0.0f, 0.001f, 0.001f);
	}
}

static void
lrg_arrangement_single_iface_init (LrgSceneArrangementInterface *iface)
{
	iface->get_id = single_get_id;
	iface->layout = single_layout;
	iface->wants_continuous = NULL;
}

static void
lrg_arrangement_single_class_init (LrgArrangementSingleClass *klass)
{
	(void) klass;
}

static void
lrg_arrangement_single_init (LrgArrangementSingle *self)
{
	(void) self;
}

LrgArrangementSingle *
lrg_arrangement_single_new (void)
{
	return g_object_new (LRG_TYPE_ARRANGEMENT_SINGLE, NULL);
}
