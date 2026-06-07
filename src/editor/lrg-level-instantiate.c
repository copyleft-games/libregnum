/* lrg-level-instantiate.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Bridges from the authoring #LrgLevel to the runtime layers.
 */

#include "lrg-level-instantiate.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-component-desc.h"
#include "lrg-script-binding.h"
#include "../ecs/components/lrg-script-component.h"
#include "../scene/lrg-scene.h"
#include "../scene/lrg-scene-entity.h"
#include "../scene/lrg-scene-object.h"
#include "../scene/lrg-material3d.h"
#include "../ecs/lrg-world.h"
#include "../ecs/lrg-game-object.h"
#include "../ecs/lrg-component.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"
#include "../lrg-enums.h"
#include <graylib.h>

/* ==========================================================================
 * Bake: level -> scene
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

static void
bake_node (LrgScene *scene,
           LrgNode  *node)
{
	LrgNodeVisual *visual = lrg_node_get_visual (node);
	GPtrArray     *children;
	guint          i;

	if (visual != NULL && lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_PRIMITIVE)
	{
		LrgSceneEntity          *entity = lrg_scene_entity_new (lrg_node_get_guid (node));
		LrgSceneObject          *object;
		g_autoptr(LrgMaterial3D) material = NULL;
		GrlVector3              *loc = lrg_node_get_location (node);
		GrlVector3              *rot = lrg_node_get_rotation (node);
		GrlVector3              *scl = lrg_node_get_scale (node);
		const gchar             *obj_name = lrg_node_get_name (node);

		lrg_scene_entity_set_location_xyz (entity,
		                                   loc != NULL ? loc->x : 0.0f,
		                                   loc != NULL ? loc->y : 0.0f,
		                                   loc != NULL ? loc->z : 0.0f);
		lrg_scene_entity_set_rotation_xyz (entity,
		                                   rot != NULL ? rot->x : 0.0f,
		                                   rot != NULL ? rot->y : 0.0f,
		                                   rot != NULL ? rot->z : 0.0f);
		lrg_scene_entity_set_scale_xyz (entity,
		                                scl != NULL ? scl->x : 1.0f,
		                                scl != NULL ? scl->y : 1.0f,
		                                scl != NULL ? scl->z : 1.0f);

		object = lrg_scene_object_new (obj_name != NULL ? obj_name : "object",
		                               lrg_node_visual_get_primitive (visual));

		material = copy_material (lrg_node_visual_get_material (visual));
		if (material != NULL)
			lrg_scene_object_set_material (object, material);

		lrg_scene_entity_add_object (entity, object);
		lrg_scene_add_entity (scene, entity);

		g_object_unref (object);
		g_object_unref (entity);
	}

	children = lrg_node_get_children (node);
	for (i = 0; children != NULL && i < children->len; i++)
		bake_node (scene, g_ptr_array_index (children, i));
}

/**
 * lrg_level_to_scene:
 * @level: an #LrgLevel
 *
 * Bakes the renderable nodes of @level into a new #LrgScene.
 *
 * Returns: (transfer full): a new #LrgScene
 */
LrgScene *
lrg_level_to_scene (LrgLevel *level)
{
	LrgScene  *scene;
	LrgNode   *root;
	GPtrArray *children;
	guint      i;

	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);

	scene = lrg_scene_new (lrg_level_get_name (level));
	lrg_scene_set_exported_from (scene, "libregnum-editor");

	root = lrg_level_get_root (level);
	children = lrg_node_get_children (root);
	for (i = 0; children != NULL && i < children->len; i++)
		bake_node (scene, g_ptr_array_index (children, i));

	return scene;
}

/* ==========================================================================
 * Instantiate: level -> world
 * ========================================================================== */

static void
apply_component_props (GObject          *obj,
                       LrgComponentDesc *desc)
{
	GObjectClass *klass = G_OBJECT_GET_CLASS (obj);
	GList        *keys = lrg_component_desc_get_keys (desc);
	GList        *iter;

	for (iter = keys; iter != NULL; iter = iter->next)
	{
		const gchar  *name = iter->data;
		const GValue *src = lrg_component_desc_get_value (desc, name);
		GParamSpec   *pspec;
		GValue        dst = G_VALUE_INIT;

		if (src == NULL)
			continue;

		pspec = g_object_class_find_property (klass, name);
		if (pspec == NULL)
			continue;

		g_value_init (&dst, pspec->value_type);
		if (g_value_transform (src, &dst))
			g_object_set_property (obj, name, &dst);
		else if (G_VALUE_TYPE (src) == pspec->value_type)
			g_object_set_property (obj, name, src);
		g_value_unset (&dst);
	}

	g_list_free (keys);
}

static void
instantiate_node (LrgNode     *node,
                  LrgWorld    *world,
                  LrgRegistry *registry)
{
	LrgGameObject *object = lrg_game_object_new ();
	GrlVector3    *loc = lrg_node_get_location (node);
	GPtrArray     *components;
	GPtrArray     *scripts;
	GPtrArray     *children;
	guint          i;

	if (loc != NULL)
		grl_entity_set_position_xy (GRL_ENTITY (object), loc->x, loc->y);

	components = lrg_node_get_components (node);
	for (i = 0; registry != NULL && components != NULL && i < components->len; i++)
	{
		LrgComponentDesc *desc = g_ptr_array_index (components, i);
		const gchar      *type_name = lrg_component_desc_get_type_name (desc);
		GObject          *comp;

		if (type_name == NULL || !lrg_registry_is_registered (registry, type_name))
			continue;

		comp = lrg_registry_create (registry, type_name, NULL);
		if (comp == NULL)
			continue;

		if (LRG_IS_COMPONENT (comp))
		{
			apply_component_props (comp, desc);
			lrg_game_object_add_component (object, LRG_COMPONENT (comp));
		}
		g_object_unref (comp);
	}

	/* Materialise script bindings as LrgScriptComponents so they run when the
	 * world ticks (the component's attached/update calls the scripting backend). */
	scripts = lrg_node_get_scripts (node);
	for (i = 0; scripts != NULL && i < scripts->len; i++)
	{
		LrgScriptBinding   *binding = g_ptr_array_index (scripts, i);
		LrgScriptComponent *sc;

		if (!lrg_script_binding_get_enabled (binding))
			continue;
		sc = lrg_script_component_new
		         (lrg_script_binding_get_language (binding),
		          lrg_script_binding_get_script (binding));
		lrg_game_object_add_component (object, LRG_COMPONENT (sc));
		g_object_unref (sc);
	}

	lrg_world_add_object (world, object);
	g_object_unref (object);

	children = lrg_node_get_children (node);
	for (i = 0; children != NULL && i < children->len; i++)
		instantiate_node (g_ptr_array_index (children, i), world, registry);
}

/**
 * lrg_level_instantiate:
 * @level: an #LrgLevel
 * @world: the #LrgWorld to populate
 * @engine: (nullable): the engine whose #LrgRegistry resolves component types
 * @error: (nullable): return location for an error
 *
 * Creates one #LrgGameObject per node and adds them to @world.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_level_instantiate (LrgLevel   *level,
                       LrgWorld   *world,
                       LrgEngine  *engine,
                       GError    **error)
{
	LrgRegistry *registry = NULL;
	LrgNode     *root;
	GPtrArray   *children;
	guint        i;

	g_return_val_if_fail (LRG_IS_LEVEL (level), FALSE);
	g_return_val_if_fail (LRG_IS_WORLD (world), FALSE);

	if (engine == NULL)
		engine = lrg_engine_get_default ();
	if (engine != NULL)
		registry = lrg_engine_get_registry (engine);

	root = lrg_level_get_root (level);
	children = lrg_node_get_children (root);
	for (i = 0; children != NULL && i < children->len; i++)
		instantiate_node (g_ptr_array_index (children, i), world, registry);

	return TRUE;
}
