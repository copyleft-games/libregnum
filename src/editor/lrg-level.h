/* lrg-level.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Top-level editable level document.
 *
 * LrgLevel is the serializable authoring document at the heart of the editor.
 * It owns a root #LrgNode whose descendants form the editable scene tree, plus
 * level-wide settings and metadata. A level is host-agnostic data: it is baked
 * into an #LrgScene for rendering (lrg_level_to_scene()) and instantiated into
 * a runtime #LrgWorld for play-in-editor (lrg_level_instantiate()), and it
 * round-trips to disk as a `.rlevel` file via #LrgLevelSerializer.
 *
 * The level emits #LrgLevel::node-added, #LrgLevel::node-removed and
 * #LrgLevel::node-changed so a host IDE can keep its panels in sync.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_LEVEL (lrg_level_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLevel, lrg_level, LRG, LEVEL, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_level_new:
 * @name: (nullable): the level name
 *
 * Creates a new, empty #LrgLevel with a single empty root node.
 *
 * Returns: (transfer full): a new #LrgLevel
 */
LRG_AVAILABLE_IN_ALL
LrgLevel * lrg_level_new (const gchar *name);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_level_get_name:
 * @self: an #LrgLevel
 *
 * Returns: (transfer none) (nullable): the level name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_level_get_name (LrgLevel *self);

/**
 * lrg_level_set_name:
 * @self: an #LrgLevel
 * @name: (nullable): the new name
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_name (LrgLevel    *self,
                         const gchar *name);

/**
 * lrg_level_get_default_2d:
 * @self: an #LrgLevel
 *
 * Gets whether the level defaults to a 2D editing view.
 *
 * Returns: %TRUE if the default view is 2D
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_get_default_2d (LrgLevel *self);

/**
 * lrg_level_set_default_2d:
 * @self: an #LrgLevel
 * @default_2d: whether the default view is 2D
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_set_default_2d (LrgLevel *self,
                               gboolean  default_2d);

/**
 * lrg_level_get_root:
 * @self: an #LrgLevel
 *
 * Gets the root node. All level content hangs off the root; the root itself is
 * an empty group node that is not serialized as content.
 *
 * Returns: (transfer none): the root #LrgNode
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_level_get_root (LrgLevel *self);

/* ==========================================================================
 * Node Management
 * ========================================================================== */

/**
 * lrg_level_add_node:
 * @self: an #LrgLevel
 * @node: (transfer none): the node to add
 * @parent: (nullable): the parent node, or %NULL to add under the root
 *
 * Adds @node under @parent (or the root). A reference is taken on @node.
 * Emits #LrgLevel::node-added.
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_add_node (LrgLevel *self,
                         LrgNode  *node,
                         LrgNode  *parent);

/**
 * lrg_level_remove_node:
 * @self: an #LrgLevel
 * @node: the node to remove
 *
 * Removes @node from its parent. Emits #LrgLevel::node-removed.
 *
 * Returns: %TRUE if the node was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_remove_node (LrgLevel *self,
                                LrgNode  *node);

/**
 * lrg_level_find_node:
 * @self: an #LrgLevel
 * @guid: the node guid to find
 *
 * Searches the whole tree for a node with @guid.
 *
 * Returns: (transfer none) (nullable): the matching node, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_level_find_node (LrgLevel    *self,
                               const gchar *guid);

/**
 * lrg_level_notify_node_changed:
 * @self: an #LrgLevel
 * @node: the node that changed
 *
 * Emits #LrgLevel::node-changed for @node. Editor operations call this after
 * mutating a node so the host IDE can refresh its panels.
 */
LRG_AVAILABLE_IN_ALL
void lrg_level_notify_node_changed (LrgLevel *self,
                                    LrgNode  *node);

G_END_DECLS
