/* lrg-scene-arrangement.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Strategy interface that lays out an #Lrg3DSurface's panels: given the panel
 * list and the captured frame dimensions, it assigns each panel a target
 * transform.  Implementing this interface is how new "modes" (single-panel,
 * per-window, free, carousel, git-depth, or third-party) are added without
 * touching the surface.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCENE_ARRANGEMENT (lrg_scene_arrangement_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgSceneArrangement, lrg_scene_arrangement, LRG, SCENE_ARRANGEMENT, GObject)

/**
 * LrgSceneArrangementInterface:
 * @parent_iface: the parent interface
 * @get_id: return the stable string id of this arrangement
 * @layout: assign every panel its target transform
 * @wants_continuous: whether this arrangement animates on its own each frame
 *   (e.g. a spinning carousel) even when nothing else changed
 *
 * Since: 1.0
 */
struct _LrgSceneArrangementInterface
{
    GTypeInterface parent_iface;

    const gchar * (*get_id) (LrgSceneArrangement *self);
    void          (*layout) (LrgSceneArrangement *self,
                             GPtrArray           *panels,
                             gint                 frame_width,
                             gint                 frame_height,
                             gfloat               scale);
    gboolean      (*wants_continuous) (LrgSceneArrangement *self);
};

LRG_AVAILABLE_IN_ALL
const gchar * lrg_scene_arrangement_get_id (LrgSceneArrangement *self);

/**
 * lrg_scene_arrangement_layout:
 * @self: a #LrgSceneArrangement
 * @panels: (element-type LrgScenePanel): the panels to place
 * @frame_width: captured frame width in pixels
 * @frame_height: captured frame height in pixels
 * @scale: DPI scale factor
 *
 * Sets each panel's target transform. Panels already carry their source
 * rectangle (the originating window's pixel geometry).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_scene_arrangement_layout (LrgSceneArrangement *self,
                                   GPtrArray           *panels,
                                   gint                 frame_width,
                                   gint                 frame_height,
                                   gfloat               scale);

LRG_AVAILABLE_IN_ALL
gboolean lrg_scene_arrangement_wants_continuous (LrgSceneArrangement *self);

G_END_DECLS
