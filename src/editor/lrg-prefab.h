/* lrg-prefab.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Prefabs: reusable saved node subtrees.
 *
 * A prefab is a single #LrgNode subtree saved to a `.rprefab` file (reusing the
 * `.rlevel` serializer). lrg_prefab_clone() deep-copies a subtree with fresh
 * guids; lrg_prefab_instantiate() produces a placeable copy of a prefab; and
 * lrg_prefab_save()/lrg_prefab_load() round-trip a prefab to disk.
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
 * lrg_prefab_clone:
 * @node: the node subtree to copy
 *
 * Deep-copies @node and all descendants, assigning fresh guids to every copied
 * node. The copy is parentless.
 *
 * Returns: (transfer full): a new #LrgNode tree
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_clone (LrgNode *node);

/**
 * lrg_prefab_instantiate:
 * @prefab: a prefab root node
 *
 * Produces a placeable instance of @prefab (a fresh-guid deep copy).
 *
 * Returns: (transfer full): a new #LrgNode tree
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_instantiate (LrgNode *prefab);

/**
 * lrg_prefab_save:
 * @node: the prefab root to save
 * @path: destination `.rprefab` path
 * @error: (nullable): return location for an error
 *
 * Saves @node (and its subtree) as a prefab.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prefab_save (LrgNode      *node,
                          const gchar  *path,
                          GError      **error);

/**
 * lrg_prefab_load:
 * @path: a `.rprefab` path
 * @error: (nullable): return location for an error
 *
 * Loads a prefab root node from disk.
 *
 * Returns: (transfer full) (nullable): the prefab root node, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_prefab_load (const gchar  *path,
                           GError      **error);

G_END_DECLS
