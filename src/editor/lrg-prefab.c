/* lrg-prefab.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Prefabs: reusable saved node subtrees.
 */

#include "lrg-prefab.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-component-desc.h"
#include "lrg-script-binding.h"
#include "lrg-level.h"
#include "lrg-level-serializer.h"
#include "../scene/lrg-material3d.h"
#include <graylib.h>

/* ==========================================================================
 * Deep clone
 * ========================================================================== */

LrgNode *
lrg_prefab_clone (LrgNode *node)
{
	LrgNode   *dst;
	GPtrArray *children;
	guint      i;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	/* Own state (name, flags, transform, visual, components, scripts) is
	 * deep-copied; the clone keeps its own freshly generated GUID. */
	dst = lrg_node_new (lrg_node_get_name (node));
	lrg_node_copy_state (dst, node);

	children = lrg_node_get_children (node);
	for (i = 0; children != NULL && i < children->len; i++)
	{
		g_autoptr(LrgNode) child = lrg_prefab_clone (g_ptr_array_index (children, i));
		lrg_node_add_child (dst, child);
	}

	return dst;
}

LrgNode *
lrg_prefab_instantiate (LrgNode *prefab)
{
	g_return_val_if_fail (LRG_IS_NODE (prefab), NULL);

	return lrg_prefab_clone (prefab);
}

/* ==========================================================================
 * Persistence (reuses the .rlevel serializer)
 * ========================================================================== */

gboolean
lrg_prefab_save (LrgNode      *node,
                 const gchar  *path,
                 GError      **error)
{
	g_autoptr(LrgLevel)           level = NULL;
	g_autoptr(LrgNode)            clone = NULL;
	g_autoptr(LrgLevelSerializer) ser = NULL;

	g_return_val_if_fail (LRG_IS_NODE (node), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	/* Clone so we never mutate the source node's parent linkage. */
	clone = lrg_prefab_clone (node);

	level = lrg_level_new ("prefab");
	lrg_level_add_node (level, clone, NULL);

	ser = lrg_level_serializer_new ();
	return lrg_level_serializer_save_to_file (ser, level, path, error);
}

LrgNode *
lrg_prefab_load (const gchar  *path,
                 GError      **error)
{
	g_autoptr(LrgLevelSerializer) ser = NULL;
	g_autoptr(LrgLevel)           level = NULL;
	LrgNode                      *root;
	GPtrArray                    *children;
	LrgNode                      *node;

	g_return_val_if_fail (path != NULL, NULL);

	ser = lrg_level_serializer_new ();
	level = lrg_level_serializer_load_from_file (ser, path, error);
	if (level == NULL)
		return NULL;

	root = lrg_level_get_root (level);
	children = lrg_node_get_children (root);
	if (children == NULL || children->len == 0)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
		             "Prefab file contains no node");
		return NULL;
	}

	/* Detach the first node from the (soon-discarded) level and return it. */
	node = g_object_ref (g_ptr_array_index (children, 0));
	lrg_level_remove_node (level, node);

	return node;
}
