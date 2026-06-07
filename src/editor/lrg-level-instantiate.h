/* lrg-level-instantiate.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Bridges from the authoring #LrgLevel to the runtime layers.
 *
 * lrg_level_to_scene() bakes the renderable nodes of a level into an #LrgScene
 * for geometry rendering. lrg_level_instantiate() materializes a level into a
 * runtime #LrgWorld of #LrgGameObject instances with their components, for
 * play-in-editor. Neither mutates the source #LrgLevel.
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
 * lrg_level_to_scene:
 * @level: an #LrgLevel
 *
 * Bakes the renderable (primitive-visual) nodes of @level into a new #LrgScene.
 * Each such node becomes an #LrgSceneEntity (keyed by the node guid) carrying
 * the node's local transform and a single primitive #LrgSceneObject.
 *
 * Note: in this MVP intermediate group transforms are not composed into child
 * world transforms; flat or single-level hierarchies bake exactly.
 *
 * Returns: (transfer full): a new #LrgScene
 */
LRG_AVAILABLE_IN_ALL
LrgScene * lrg_level_to_scene (LrgLevel *level);

/**
 * lrg_level_instantiate:
 * @level: an #LrgLevel
 * @world: the #LrgWorld to populate
 * @engine: (nullable): the engine whose #LrgRegistry resolves component types;
 *   if %NULL, the default engine is used
 * @error: (nullable): return location for an error
 *
 * Creates one #LrgGameObject per node in @level, applies the node's component
 * descriptions via the engine registry, and adds the objects to @world.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_instantiate (LrgLevel   *level,
                                LrgWorld   *world,
                                LrgEngine  *engine,
                                GError    **error);

G_END_DECLS
