/* lrg-scene-arrangement.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-scene-arrangement.h"

G_DEFINE_INTERFACE (LrgSceneArrangement, lrg_scene_arrangement, G_TYPE_OBJECT)

static void
lrg_scene_arrangement_default_init (LrgSceneArrangementInterface *iface)
{
	(void) iface;
}

const gchar *
lrg_scene_arrangement_get_id (LrgSceneArrangement *self)
{
	LrgSceneArrangementInterface *iface;

	g_return_val_if_fail (LRG_IS_SCENE_ARRANGEMENT (self), NULL);

	iface = LRG_SCENE_ARRANGEMENT_GET_IFACE (self);
	return iface->get_id != NULL ? iface->get_id (self) : NULL;
}

void
lrg_scene_arrangement_layout (LrgSceneArrangement *self,
							  GPtrArray           *panels,
							  gint                 frame_width,
							  gint                 frame_height,
							  gfloat               scale)
{
	LrgSceneArrangementInterface *iface;

	g_return_if_fail (LRG_IS_SCENE_ARRANGEMENT (self));
	g_return_if_fail (panels != NULL);

	iface = LRG_SCENE_ARRANGEMENT_GET_IFACE (self);
	if (iface->layout != NULL)
		iface->layout (self, panels, frame_width, frame_height, scale);
}

gboolean
lrg_scene_arrangement_wants_continuous (LrgSceneArrangement *self)
{
	LrgSceneArrangementInterface *iface;

	g_return_val_if_fail (LRG_IS_SCENE_ARRANGEMENT (self), FALSE);

	iface = LRG_SCENE_ARRANGEMENT_GET_IFACE (self);
	return iface->wants_continuous != NULL ? iface->wants_continuous (self) : FALSE;
}
