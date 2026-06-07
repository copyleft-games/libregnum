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

static LrgMaterial3D *
copy_material (LrgMaterial3D *src)
{
	LrgMaterial3D *dst;
	gfloat         r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

	if (src == NULL)
		return NULL;

	dst = lrg_material3d_new ();
	lrg_material3d_get_color (src, &r, &g, &b, &a);
	lrg_material3d_set_color (dst, r, g, b, a);
	lrg_material3d_set_roughness (dst, lrg_material3d_get_roughness (src));
	lrg_material3d_set_metallic (dst, lrg_material3d_get_metallic (src));

	return dst;
}

static LrgNodeVisual *
copy_visual (LrgNodeVisual *src)
{
	LrgNodeVisual           *dst;
	g_autoptr(LrgMaterial3D)  material = NULL;
	GList                    *params;
	GList                    *iter;

	if (src == NULL)
		return NULL;

	dst = lrg_node_visual_new (lrg_node_visual_get_kind (src));
	lrg_node_visual_set_primitive (dst, lrg_node_visual_get_primitive (src));
	lrg_node_visual_set_asset (dst, lrg_node_visual_get_asset (src));

	material = copy_material (lrg_node_visual_get_material (src));
	if (material != NULL)
		lrg_node_visual_set_material (dst, material);

	params = lrg_node_visual_get_param_names (src);
	for (iter = params; iter != NULL; iter = iter->next)
	{
		const gchar  *name = iter->data;
		const GValue *value = lrg_node_visual_get_param (src, name);

		if (value != NULL)
			lrg_node_visual_set_param (dst, name, value);
	}
	g_list_free (params);

	return dst;
}

static LrgComponentDesc *
copy_component (LrgComponentDesc *src)
{
	LrgComponentDesc *dst = lrg_component_desc_new (lrg_component_desc_get_type_name (src));
	GList            *keys = lrg_component_desc_get_keys (src);
	GList            *iter;

	for (iter = keys; iter != NULL; iter = iter->next)
	{
		const gchar  *name = iter->data;
		const GValue *value = lrg_component_desc_get_value (src, name);

		if (value != NULL)
			lrg_component_desc_set_value (dst, name, value);
	}
	g_list_free (keys);

	return dst;
}

LrgNode *
lrg_prefab_clone (LrgNode *node)
{
	LrgNode                 *dst;
	g_autoptr(LrgNodeVisual)  visual = NULL;
	GrlVector3              *loc, *rot, *scl;
	GPtrArray               *components;
	GPtrArray               *scripts;
	GPtrArray               *children;
	guint                    i;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);

	dst = lrg_node_new (lrg_node_get_name (node));
	lrg_node_set_visible (dst, lrg_node_get_visible (node));
	lrg_node_set_locked (dst, lrg_node_get_locked (node));
	lrg_node_set_is_2d (dst, lrg_node_get_is_2d (node));

	loc = lrg_node_get_location (node);
	rot = lrg_node_get_rotation (node);
	scl = lrg_node_get_scale (node);
	lrg_node_set_location_xyz (dst, loc->x, loc->y, loc->z);
	lrg_node_set_rotation_xyz (dst, rot->x, rot->y, rot->z);
	lrg_node_set_scale_xyz (dst, scl->x, scl->y, scl->z);

	visual = copy_visual (lrg_node_get_visual (node));
	if (visual != NULL)
		lrg_node_set_visual (dst, visual);

	components = lrg_node_get_components (node);
	for (i = 0; components != NULL && i < components->len; i++)
	{
		g_autoptr(LrgComponentDesc) c = copy_component (g_ptr_array_index (components, i));
		lrg_node_add_component (dst, c);
	}

	scripts = lrg_node_get_scripts (node);
	for (i = 0; scripts != NULL && i < scripts->len; i++)
	{
		LrgScriptBinding          *s = g_ptr_array_index (scripts, i);
		g_autoptr(LrgScriptBinding) copy =
			lrg_script_binding_new (lrg_script_binding_get_language (s),
			                        lrg_script_binding_get_script (s));

		lrg_script_binding_set_enabled (copy, lrg_script_binding_get_enabled (s));
		lrg_node_add_script (dst, copy);
	}

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
