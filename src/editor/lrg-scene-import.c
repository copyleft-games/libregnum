/* lrg-scene-import.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Import an #LrgScene (Blender geometry export) into an editable #LrgLevel.
 */

#include "lrg-scene-import.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "../scene/lrg-scene.h"
#include "../scene/lrg-scene-entity.h"
#include "../scene/lrg-scene-object.h"
#include "../scene/lrg-material3d.h"
#include <graylib.h>

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

static void
copy_vec3 (GrlVector3 *src,
           gfloat     *x,
           gfloat     *y,
           gfloat     *z)
{
	*x = (src != NULL) ? src->x : 0.0f;
	*y = (src != NULL) ? src->y : 0.0f;
	*z = (src != NULL) ? src->z : 0.0f;
}

static LrgNode *
node_from_object (LrgSceneObject *obj)
{
	LrgNode                 *node = lrg_node_new (lrg_scene_object_get_name (obj));
	g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);
	g_autoptr(LrgMaterial3D) material = NULL;
	gfloat                   x, y, z;

	lrg_node_visual_set_primitive (visual, lrg_scene_object_get_primitive (obj));

	material = copy_material (lrg_scene_object_get_material (obj));
	if (material != NULL)
		lrg_node_visual_set_material (visual, material);

	lrg_node_set_visual (node, visual);

	copy_vec3 (lrg_scene_object_get_location (obj), &x, &y, &z);
	lrg_node_set_location_xyz (node, x, y, z);
	copy_vec3 (lrg_scene_object_get_rotation (obj), &x, &y, &z);
	lrg_node_set_rotation_xyz (node, x, y, z);
	copy_vec3 (lrg_scene_object_get_scale (obj), &x, &y, &z);
	lrg_node_set_scale_xyz (node, x, y, z);

	return node;
}

static LrgNode *
node_from_entity (LrgSceneEntity *entity)
{
	LrgNode    *node = lrg_node_new (lrg_scene_entity_get_name (entity));
	GPtrArray  *objects;
	gfloat      x, y, z;
	guint       i;

	copy_vec3 (lrg_scene_entity_get_location (entity), &x, &y, &z);
	lrg_node_set_location_xyz (node, x, y, z);
	copy_vec3 (lrg_scene_entity_get_rotation (entity), &x, &y, &z);
	lrg_node_set_rotation_xyz (node, x, y, z);
	copy_vec3 (lrg_scene_entity_get_scale (entity), &x, &y, &z);
	lrg_node_set_scale_xyz (node, x, y, z);

	objects = lrg_scene_entity_get_objects (entity);
	for (i = 0; objects != NULL && i < objects->len; i++)
	{
		LrgSceneObject     *obj = g_ptr_array_index (objects, i);
		g_autoptr(LrgNode)  child = node_from_object (obj);

		lrg_node_add_child (node, child);
	}

	return node;
}

/**
 * lrg_level_from_scene:
 * @scene: an #LrgScene to import
 *
 * Builds a new editable #LrgLevel from a geometry-only #LrgScene.
 *
 * Returns: (transfer full) (nullable): a new #LrgLevel, or %NULL if @scene is
 *   invalid
 */
LrgLevel *
lrg_level_from_scene (LrgScene *scene)
{
	LrgLevel *level;
	GList    *names;
	GList    *iter;

	g_return_val_if_fail (LRG_IS_SCENE (scene), NULL);

	level = lrg_level_new (lrg_scene_get_name (scene));

	names = lrg_scene_get_entity_names (scene);
	for (iter = names; iter != NULL; iter = iter->next)
	{
		const gchar        *name = iter->data;
		LrgSceneEntity     *entity = lrg_scene_get_entity (scene, name);
		g_autoptr(LrgNode)  node = NULL;

		if (entity == NULL)
			continue;

		node = node_from_entity (entity);
		lrg_level_add_node (level, node, NULL);
	}
	g_list_free (names);

	return level;
}
