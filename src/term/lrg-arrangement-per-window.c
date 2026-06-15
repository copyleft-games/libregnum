/* lrg-arrangement-per-window.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-arrangement-per-window.h"
#include "lrg-scene-arrangement.h"
#include "lrg-scene-panel.h"

/* The captured frame maps to a world plane this many units tall; width follows
   the aspect ratio.  Each window panel takes its proportional share. */
#define LRG_PW_HEIGHT 4.6f
/* Shrink each panel slightly so neighbours read as separate planes. */
#define LRG_PW_GAP 0.97f

struct _LrgArrangementPerWindow
{
	GObject parent_instance;
};

static void lrg_arrangement_per_window_iface_init (LrgSceneArrangementInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgArrangementPerWindow, lrg_arrangement_per_window, G_TYPE_OBJECT,
							   G_IMPLEMENT_INTERFACE (LRG_TYPE_SCENE_ARRANGEMENT,
													  lrg_arrangement_per_window_iface_init))

static const gchar *
per_window_get_id (LrgSceneArrangement *self)
{
	(void) self;
	return "per-window";
}

static void
per_window_layout (LrgSceneArrangement *self,
				   GPtrArray           *panels,
				   gint                 frame_width,
				   gint                 frame_height,
				   gfloat               scale)
{
	/* Always-fit world plane: a fixed world height with width following the
	   frame aspect, so the layout keeps a consistent on-screen framing at every
	   window size (re-fits on resize) and never stretches. */
	gfloat aspect = frame_height > 0
						? (gfloat) frame_width / (gfloat) frame_height : 1.6f;
	gfloat world_h = LRG_PW_HEIGHT;
	gfloat world_w = world_h * aspect;
	gfloat fw = frame_width > 0 ? (gfloat) frame_width : 1.0f;
	gfloat fh = frame_height > 0 ? (gfloat) frame_height : 1.0f;
	guint i;

	(void) self;
	(void) scale;

	for (i = 0; i < panels->len; i++)
	{
		LrgScenePanel *p = g_ptr_array_index (panels, i);
		gint sx, sy, sw, sh;
		gfloat cx, cy, nx, ny, wx, wy, pw, ph;

		lrg_scene_panel_get_source_rect (p, &sx, &sy, &sw, &sh);

		if (sw <= 0 || sh <= 0)
		{
			/* No geometry yet: fall back to filling the whole plane. */
			lrg_scene_panel_set_target (p, 0.0f, 0.0f, 0.0f, 0.0f, world_w, world_h);
			continue;
		}

		cx = (gfloat) sx + (gfloat) sw * 0.5f;
		cy = (gfloat) sy + (gfloat) sh * 0.5f;
		nx = cx / fw;
		ny = cy / fh;

		/* Pixel space has +y downward; world space has +y up. */
		wx = (nx - 0.5f) * world_w;
		wy = (0.5f - ny) * world_h;
		pw = ((gfloat) sw / fw) * world_w * LRG_PW_GAP;
		ph = ((gfloat) sh / fh) * world_h * LRG_PW_GAP;

		lrg_scene_panel_set_target (p, wx, wy, 0.0f, 0.0f, pw, ph);
	}
}

static void
lrg_arrangement_per_window_iface_init (LrgSceneArrangementInterface *iface)
{
	iface->get_id = per_window_get_id;
	iface->layout = per_window_layout;
	iface->wants_continuous = NULL;
}

static void
lrg_arrangement_per_window_class_init (LrgArrangementPerWindowClass *klass)
{
	(void) klass;
}

static void
lrg_arrangement_per_window_init (LrgArrangementPerWindow *self)
{
	(void) self;
}

LrgArrangementPerWindow *
lrg_arrangement_per_window_new (void)
{
	return g_object_new (LRG_TYPE_ARRANGEMENT_PER_WINDOW, NULL);
}
