/* lrg-scene-import.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Import an #LrgScene (Blender geometry export) into an editable #LrgLevel.
 *
 * Each scene entity becomes a top-level group #LrgNode carrying the entity's
 * world transform; each of the entity's primitive objects becomes a child node
 * with a %LRG_NODE_VISUAL_PRIMITIVE visual (primitive type + a copied material)
 * and the object's local transform.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * lrg_level_from_scene:
 * @scene: an #LrgScene to import
 *
 * Builds a new editable #LrgLevel from a geometry-only #LrgScene.
 *
 * Returns: (transfer full) (nullable): a new #LrgLevel, or %NULL if @scene is
 *   invalid
 */
LRG_AVAILABLE_IN_ALL
LrgLevel * lrg_level_from_scene (LrgScene *scene);

G_END_DECLS
