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
#include "lrg-level-serializer.h"
#include "lrg-node-tilemap.h"
#include "../ecs/components/lrg-script-component.h"
#include "../scene/lrg-scene.h"
#include "../scene/lrg-scene-entity.h"
#include "../scene/lrg-scene-object.h"
#include "../scene/lrg-material3d.h"
#include "../ecs/lrg-world.h"
#include "../ecs/lrg-game-object.h"
#include "../ecs/lrg-component.h"
#include "../world3d/lrg-entity3d.h"
#include "../world3d/lrg-world3d.h"
#include "../world3d/components/lrg-mesh-renderer-component.h"
#include "../world3d/components/lrg-light-component.h"
#include "../world3d/components/lrg-camera-component.h"
#include "../world3d/components/lrg-audio-source-component.h"
#include "../world3d/components/lrg-billboard-component.h"
#include "../world3d/components/lrg-tilemap-component.h"
#include "../world3d/components/lrg-decal-component.h"
#include "../world3d/components/lrg-reflection-probe-component.h"
#include "../world3d/components/lrg-lod-component.h"
#include "../lighting/lrg-environment.h"
#include "../lighting/lrg-probe-grid.h"
#include "../postprocess/lrg-post-stack.h"
#include "../pathfinding/lrg-nav-bake.h"
#include "../pathfinding/lrg-nav-poly-mesh.h"
#include "../core/lrg-asset-manager.h"
#include "../core/lrg-asset-meta.h"
#include "../core/lrg-engine.h"
#include "../core/lrg-registry.h"
#include "../lrg-enums.h"
#include "../ui/lrg-property-source.h"
#include <graylib.h>
#include <math.h>

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

/*
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

/*
 * set_property_converted:
 * @obj: the object to set a property on
 * @pspec: the property
 * @src: the stored value (the type a YAML load produced)
 *
 * Sets @pspec from @src, converting the way level files need: enum and
 * flags properties accept nicks ("melee", "fire|ice") and plain numbers,
 * everything else goes through g_value_transform().
 *
 * Returns: %TRUE if the property was set
 */
static gboolean
set_property_converted (GObject      *obj,
                        GParamSpec   *pspec,
                        const GValue *src)
{
	GValue   dst = G_VALUE_INIT;
	gboolean ok = FALSE;

	g_value_init (&dst, pspec->value_type);
	if (G_VALUE_TYPE (src) == pspec->value_type)
	{
		g_object_set_property (obj, pspec->name, src);
		ok = TRUE;
	}
	else if ((G_TYPE_IS_ENUM (pspec->value_type) || G_TYPE_IS_FLAGS (pspec->value_type)) &&
	         G_VALUE_HOLDS_STRING (src))
	{
		/* Enum and flags values are saved as nicks ("melee",
		 * "fire|ice"); a YAML load keeps them as strings */
		if (lrg_property_value_from_string (&dst, g_value_get_string (src)))
		{
			g_object_set_property (obj, pspec->name, &dst);
			ok = TRUE;
		}
		else
			g_warning ("Component property '%s': '%s' is not a value of %s", pspec->name,
			           g_value_get_string (src), g_type_name (pspec->value_type));
	}
	else if ((G_TYPE_IS_ENUM (pspec->value_type) || G_TYPE_IS_FLAGS (pspec->value_type)) &&
	         (G_VALUE_HOLDS_INT64 (src) || G_VALUE_HOLDS_INT (src) || G_VALUE_HOLDS_UINT (src)))
	{
		/* A plain number (hand-written YAML, older files): the
		 * numeric value of the enum or the flags mask */
		gint64 n = G_VALUE_HOLDS_INT64 (src) ? g_value_get_int64 (src)
		         : G_VALUE_HOLDS_INT (src) ? (gint64) g_value_get_int (src)
		         : (gint64) g_value_get_uint (src);

		if (G_TYPE_IS_ENUM (pspec->value_type))
			g_value_set_enum (&dst, (gint) n);
		else
			g_value_set_flags (&dst, (guint) n);
		g_object_set_property (obj, pspec->name, &dst);
		ok = TRUE;
	}
	else if (g_value_transform (src, &dst))
	{
		g_object_set_property (obj, pspec->name, &dst);
		ok = TRUE;
	}
	g_value_unset (&dst);
	return ok;
}

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

		if (src == NULL)
			continue;

		pspec = g_object_class_find_property (klass, name);
		if (pspec == NULL)
			continue;

		set_property_converted (obj, pspec, src);
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

	/* Tag with the node GUID so tools (play-in-editor) can map runtime
	 * objects back to their authoring nodes, then let node subclasses add
	 * their own runtime pieces. */
	grl_entity_set_tag (GRL_ENTITY (object), lrg_node_get_guid (node));
	lrg_node_instantiate (node, object);

	lrg_world_add_object (world, object);
	g_object_unref (object);

	children = lrg_node_get_children (node);
	for (i = 0; children != NULL && i < children->len; i++)
		instantiate_node (g_ptr_array_index (children, i), world, registry);
}

/*
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


/* ==========================================================================
 * Instantiate: level -> 3D world
 * ========================================================================== */

/* State shared by one lrg_level_instantiate_3d() call. */
typedef struct
{
	LrgWorld3D  *world;
	LrgRegistry *registry;    /* nullable */
	LrgAssetManager *assets;  /* nullable: material references load through it */
	const gchar *base_dir;    /* nullable: relative references are tried here first */
	GHashTable  *used_guids;  /* GUIDs given out by this call (set) */
	gboolean     probe_volumes;  /* probe-volume nodes add unbaked grids */
	gfloat       probe_spacing;  /* the level's probe_spacing (0 = unset) */
} Instantiate3D;

/*
 * create_registry_components:
 *
 * Every component description of @node whose type name the registry knows
 * becomes a component on @entity, its property bag applied with the same
 * rules as the 2D path (apply_component_props()).
 */
static void
create_registry_components (Instantiate3D *ctx,
                            LrgNode       *node,
                            LrgEntity3D   *entity)
{
	GPtrArray *components = lrg_node_get_components (node);
	guint      i;

	for (i = 0; ctx->registry != NULL && components != NULL && i < components->len; i++)
	{
		LrgComponentDesc   *desc = g_ptr_array_index (components, i);
		const gchar        *type_name = lrg_component_desc_get_type_name (desc);
		g_autoptr(GObject)  comp = NULL;

		if (type_name == NULL || !lrg_registry_is_registered (ctx->registry, type_name))
		{
			g_debug ("lrg_level_instantiate_3d: node '%s': component type '%s' is not registered",
			         lrg_node_get_name (node), type_name != NULL ? type_name : "(null)");
			continue;
		}

		comp = lrg_registry_create (ctx->registry, type_name, NULL);
		if (comp == NULL || !LRG_IS_COMPONENT (comp))
			continue;

		apply_component_props (comp, desc);
		lrg_entity3d_add_component (entity, LRG_COMPONENT (comp));
	}
}

/*
 * create_script_components:
 *
 * Enabled script bindings become LrgScriptComponents (same as 2D).
 */
static void
create_script_components (LrgNode     *node,
                          LrgEntity3D *entity)
{
	GPtrArray *scripts = lrg_node_get_scripts (node);
	guint      i;

	for (i = 0; scripts != NULL && i < scripts->len; i++)
	{
		LrgScriptBinding              *binding = g_ptr_array_index (scripts, i);
		g_autoptr(LrgScriptComponent)  sc = NULL;

		if (!lrg_script_binding_get_enabled (binding))
			continue;
		sc = lrg_script_component_new (lrg_script_binding_get_language (binding),
		                               lrg_script_binding_get_script (binding));
		lrg_entity3d_add_component (entity, LRG_COMPONENT (sc));
	}
}

/*
 * assign_guid:
 *
 * The entity keeps the node's GUID unless that GUID is already taken in
 * the world (the same level instantiated twice) or was given out earlier in
 * this call (a level with duplicate GUIDs); then it gets a fresh one so
 * the world's GUID map stays 1:1.
 */
static void
assign_guid (Instantiate3D *ctx,
             LrgNode       *node,
             LrgEntity3D   *entity)
{
	const gchar *guid = lrg_node_get_guid (node);

	if (guid != NULL && *guid != '\0' && g_hash_table_contains (ctx->used_guids, guid))
	{
		g_warning ("lrg_level_instantiate_3d: GUID %s is used by more than one node; "
		           "node '%s' gets a fresh GUID", guid, lrg_node_get_name (node));
		guid = NULL;
	}
	else if (guid != NULL && *guid != '\0' && lrg_world3d_find_by_guid (ctx->world, guid) != NULL)
		guid = NULL;   /* already in the world: spawned twice */

	if (guid != NULL && *guid != '\0')
		lrg_entity3d_set_guid (entity, guid);
	else
		guid = lrg_entity3d_ensure_guid (entity);

	g_hash_table_add (ctx->used_guids, g_strdup (guid));
}

/*
 * apply_node_transform:
 *
 * The node's local TRS becomes the entity's local TRS. The Euler rotation
 * goes in through set_rotation_euler() so the entity keeps the authored
 * angles (370 degrees stays 370) and composes the same R = Rz * Ry * Rx.
 */
static void
apply_node_transform (LrgNode     *node,
                      LrgEntity3D *entity)
{
	GrlVector3 *loc = lrg_node_get_location (node);
	GrlVector3 *rot = lrg_node_get_rotation (node);
	GrlVector3 *scl = lrg_node_get_scale (node);
	gfloat      v[3];

	v[0] = loc != NULL ? loc->x : 0.0f;
	v[1] = loc != NULL ? loc->y : 0.0f;
	v[2] = loc != NULL ? loc->z : 0.0f;
	lrg_entity3d_set_position (entity, v);

	v[0] = rot != NULL ? rot->x : 0.0f;
	v[1] = rot != NULL ? rot->y : 0.0f;
	v[2] = rot != NULL ? rot->z : 0.0f;
	lrg_entity3d_set_rotation_euler (entity, v);

	v[0] = scl != NULL ? scl->x : 1.0f;
	v[1] = scl != NULL ? scl->y : 1.0f;
	v[2] = scl != NULL ? scl->z : 1.0f;
	lrg_entity3d_set_scale (entity, v);
}

/* --------------------------------------------------------------------------
 * Visual -> component property map
 *
 * A visual becomes one component (lrg_level_visual_get_component_type()).
 * Its parts map onto that component's GObject properties:
 *
 * - explicit rows (VISUAL_MAP below): the asset, the primitive, the
 *   material colour / roughness / metallic, and params whose property has
 *   a different name (aliases such as light_type);
 * - every other param maps by name, underscores becoming dashes
 *   ("cast_shadows" -> "cast-shadows"), when the component has such a
 *   writable property.
 *
 * lrg_level_visual_store_property() walks the same map backwards, so a
 * value applied from play-in-editor lands where instantiation reads it.
 * -------------------------------------------------------------------------- */

typedef enum
{
	MAP_PARAM,        /* @key is a param-bag key */
	MAP_COLOR,        /* material colour channel @index (0..3) */
	MAP_ROUGHNESS,    /* material roughness */
	MAP_METALLIC,     /* material metallic */
	MAP_EMISSION,     /* material emission channel @index (0..2), 3 = strength */
	MAP_ASSET,        /* the visual's asset path / GUID */
	MAP_PRIMITIVE,    /* the visual's LrgPrimitiveType */
	MAP_IGNORE        /* @key is a param the runtime ignores (as the editor does) */
} MapSource;

typedef struct
{
	LrgNodeVisualKind  kind;
	MapSource          source;
	const gchar       *key;       /* MAP_PARAM: the param key */
	gint               index;     /* MAP_COLOR: the channel */
	const gchar       *property;  /* the component property */
	gdouble            factor;    /* MAP_PARAM: property = param * factor */
} VisualMap;

static const VisualMap visual_map[] = {
	/* primitive and mesh asset -> LrgMeshRendererComponent */
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_PRIMITIVE, NULL, 0, "primitive", 1.0 },
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_COLOR,     NULL, 0, "color-r", 1.0 },
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_COLOR,     NULL, 1, "color-g", 1.0 },
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_COLOR,     NULL, 2, "color-b", 1.0 },
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_COLOR,     NULL, 3, "color-a", 1.0 },
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_ROUGHNESS, NULL, 0, "roughness", 1.0 },
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_METALLIC,  NULL, 0, "metallic", 1.0 },
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_EMISSION,  NULL, 0, "emission-r", 1.0 },
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_EMISSION,  NULL, 1, "emission-g", 1.0 },
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_EMISSION,  NULL, 2, "emission-b", 1.0 },
	{ LRG_NODE_VISUAL_PRIMITIVE,  MAP_EMISSION,  NULL, 3, "emission-strength", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_ASSET,     NULL, 0, "mesh-asset", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_COLOR,     NULL, 0, "color-r", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_COLOR,     NULL, 1, "color-g", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_COLOR,     NULL, 2, "color-b", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_COLOR,     NULL, 3, "color-a", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_ROUGHNESS, NULL, 0, "roughness", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_METALLIC,  NULL, 0, "metallic", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_EMISSION,  NULL, 0, "emission-r", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_EMISSION,  NULL, 1, "emission-g", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_EMISSION,  NULL, 2, "emission-b", 1.0 },
	{ LRG_NODE_VISUAL_MESH_ASSET, MAP_EMISSION,  NULL, 3, "emission-strength", 1.0 },

	/* light -> LrgLightComponent */
	{ LRG_NODE_VISUAL_LIGHT,      MAP_PARAM,     "light_type", 0, "kind", 1.0 },
	/* the editor's spot_angle is the full cone, outer-angle a half-angle */
	{ LRG_NODE_VISUAL_LIGHT,      MAP_PARAM,     "spot_angle", 0, "outer-angle", 0.5 },
	/* the editor's spot_inner_angle is the full inner cone, inner-angle a half-angle */
	{ LRG_NODE_VISUAL_LIGHT,      MAP_PARAM,     "spot_inner_angle", 0, "inner-angle", 0.5 },
	{ LRG_NODE_VISUAL_LIGHT,      MAP_COLOR,     NULL, 0, "color-r", 1.0 },
	{ LRG_NODE_VISUAL_LIGHT,      MAP_COLOR,     NULL, 1, "color-g", 1.0 },
	{ LRG_NODE_VISUAL_LIGHT,      MAP_COLOR,     NULL, 2, "color-b", 1.0 },

	/* sprite -> LrgBillboardComponent */
	{ LRG_NODE_VISUAL_SPRITE,     MAP_ASSET,     NULL, 0, "texture-path", 1.0 },
	/* the editor draws sprites as an aspect-fit 1x1 quad and ignores
	 * pixels_per_unit; the runtime matches it */
	{ LRG_NODE_VISUAL_SPRITE,     MAP_IGNORE,    "pixels_per_unit", 0, NULL, 1.0 },
	{ LRG_NODE_VISUAL_SPRITE,     MAP_COLOR,     NULL, 0, "color-r", 1.0 },
	{ LRG_NODE_VISUAL_SPRITE,     MAP_COLOR,     NULL, 1, "color-g", 1.0 },
	{ LRG_NODE_VISUAL_SPRITE,     MAP_COLOR,     NULL, 2, "color-b", 1.0 },
	{ LRG_NODE_VISUAL_SPRITE,     MAP_COLOR,     NULL, 3, "color-a", 1.0 },

	/* audio emitter -> LrgAudioSourceComponent */
	{ LRG_NODE_VISUAL_AUDIO_EMITTER, MAP_ASSET,  NULL, 0, "sound-path", 1.0 },
	{ LRG_NODE_VISUAL_AUDIO_EMITTER, MAP_PARAM,  "radius", 0, "max-distance", 1.0 },

	/* tilemap -> LrgTilemapComponent (the grid itself is converted
	 * with lrg_node_tilemap_to_runtime(), see configure_tilemap()) */
	{ LRG_NODE_VISUAL_TILEMAP,    MAP_ASSET,     NULL, 0, "tileset-path", 1.0 },

	/* decal -> LrgDecalComponent: the asset is the albedo texture, the
	 * material colour the tint; size_x, opacity, fade_angle, ... map by
	 * name (size-x, opacity, fade-angle, ...) */
	{ LRG_NODE_VISUAL_DECAL,      MAP_ASSET,     NULL, 0, "albedo-path", 1.0 },
	{ LRG_NODE_VISUAL_DECAL,      MAP_PARAM,     "normal", 0, "normal-path", 1.0 },
	{ LRG_NODE_VISUAL_DECAL,      MAP_PARAM,     "orm", 0, "orm-path", 1.0 },
	{ LRG_NODE_VISUAL_DECAL,      MAP_COLOR,     NULL, 0, "color-r", 1.0 },
	{ LRG_NODE_VISUAL_DECAL,      MAP_COLOR,     NULL, 1, "color-g", 1.0 },
	{ LRG_NODE_VISUAL_DECAL,      MAP_COLOR,     NULL, 2, "color-b", 1.0 },
	{ LRG_NODE_VISUAL_DECAL,      MAP_COLOR,     NULL, 3, "color-a", 1.0 },

	/* reflection probe -> LrgReflectionProbeComponent: every param maps
	 * by name (extents_x -> extents-x, blend_distance, intensity,
	 * box_projection, resolution, near_clip, far_clip, auto_recapture) */
};

GType
lrg_level_visual_get_component_type (LrgNodeVisualKind kind)
{
	switch (kind)
	{
	case LRG_NODE_VISUAL_PRIMITIVE:
	case LRG_NODE_VISUAL_MESH_ASSET:
		return LRG_TYPE_MESH_RENDERER_COMPONENT;
	case LRG_NODE_VISUAL_LIGHT:
		return LRG_TYPE_LIGHT_COMPONENT;
	case LRG_NODE_VISUAL_CAMERA:
		return LRG_TYPE_CAMERA_COMPONENT;
	case LRG_NODE_VISUAL_AUDIO_EMITTER:
		return LRG_TYPE_AUDIO_SOURCE_COMPONENT;
	case LRG_NODE_VISUAL_SPRITE:
		return LRG_TYPE_BILLBOARD_COMPONENT;
	case LRG_NODE_VISUAL_TILEMAP:
		return LRG_TYPE_TILEMAP_COMPONENT;
	case LRG_NODE_VISUAL_DECAL:
		return LRG_TYPE_DECAL_COMPONENT;
	case LRG_NODE_VISUAL_REFLECTION_PROBE:
		return LRG_TYPE_REFLECTION_PROBE_COMPONENT;
	case LRG_NODE_VISUAL_NONE:
	case LRG_NODE_VISUAL_PREFAB_INSTANCE:
	case LRG_NODE_VISUAL_CAD_PART:
	default:
		return G_TYPE_INVALID;
	}
}

/* The writable property @name of @klass, NULL when there is none or it
 * belongs to LrgComponent itself ("enabled" is not a visual's business). */
static GParamSpec *
component_property (GObjectClass *klass,
                    const gchar  *name)
{
	GParamSpec *pspec = g_object_class_find_property (klass, name);

	if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE) || (pspec->flags & G_PARAM_CONSTRUCT_ONLY))
		return NULL;
	if (pspec->owner_type == LRG_TYPE_COMPONENT || !g_type_is_a (pspec->owner_type, LRG_TYPE_COMPONENT))
		return NULL;
	return pspec;
}

/* The explicit map row of (@kind, @param key), or NULL. */
static const VisualMap *
find_param_row (LrgNodeVisualKind  kind,
                const gchar       *key)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (visual_map); i++)
		if (visual_map[i].kind == kind &&
		    (visual_map[i].source == MAP_PARAM || visual_map[i].source == MAP_IGNORE) &&
		    g_strcmp0 (visual_map[i].key, key) == 0)
			return &visual_map[i];
	return NULL;
}

/* The explicit map row of (@kind, component @property), or NULL. */
static const VisualMap *
find_property_row (LrgNodeVisualKind  kind,
                   const gchar       *property)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (visual_map); i++)
		if (visual_map[i].kind == kind && g_strcmp0 (visual_map[i].property, property) == 0)
			return &visual_map[i];
	return NULL;
}

/* "cast_shadows" <-> "cast-shadows" */
static gchar *
replace_char (const gchar *s,
              gchar        from,
              gchar        to)
{
	gchar *out = g_strdup (s);
	gchar *p;

	for (p = out; *p != '\0'; p++)
		if (*p == from)
			*p = to;
	return out;
}

/* Sets @property of @comp from a gfloat, if it exists. */
static void
set_float_property (GObject     *comp,
                    const gchar *property,
                    gfloat       v)
{
	GParamSpec *pspec = component_property (G_OBJECT_GET_CLASS (comp), property);
	GValue      src = G_VALUE_INIT;

	if (pspec == NULL)
		return;
	g_value_init (&src, G_TYPE_FLOAT);
	g_value_set_float (&src, v);
	set_property_converted (comp, pspec, &src);
	g_value_unset (&src);
}

/* Sets @property of @comp from a string, if it exists. */
static void
set_string_property (GObject     *comp,
                     const gchar *property,
                     const gchar *v)
{
	GParamSpec *pspec = component_property (G_OBJECT_GET_CLASS (comp), property);
	GValue      src = G_VALUE_INIT;

	if (pspec == NULL)
		return;
	g_value_init (&src, G_TYPE_STRING);
	g_value_set_string (&src, v);
	set_property_converted (comp, pspec, &src);
	g_value_unset (&src);
}

/* The explicit rows: asset, primitive, material. */
static void
apply_explicit_rows (LrgNodeVisual *visual,
                     GObject       *comp)
{
	LrgNodeVisualKind  kind = lrg_node_visual_get_kind (visual);
	LrgMaterial3D     *material = lrg_node_visual_get_material (visual);
	gfloat             rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	guint              i;

	if (material != NULL)
		lrg_material3d_get_color (material, &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
	else if (kind == LRG_NODE_VISUAL_PRIMITIVE || kind == LRG_NODE_VISUAL_MESH_ASSET)
	{
		/* The editor viewport draws meshes without a material in this
		 * grey; the mesh renderer's own default is white */
		rgba[0] = 0.75f;
		rgba[1] = 0.75f;
		rgba[2] = 0.78f;
	}

	for (i = 0; i < G_N_ELEMENTS (visual_map); i++)
	{
		const VisualMap *row = &visual_map[i];

		if (row->kind != kind)
			continue;
		switch (row->source)
		{
		case MAP_PRIMITIVE:
			{
				GParamSpec *pspec = component_property (G_OBJECT_GET_CLASS (comp), row->property);
				GValue      v = G_VALUE_INIT;

				if (pspec == NULL)
					break;
				g_value_init (&v, LRG_TYPE_PRIMITIVE_TYPE);
				g_value_set_enum (&v, (gint) lrg_node_visual_get_primitive (visual));
				set_property_converted (comp, pspec, &v);
				g_value_unset (&v);
			}
			break;
		case MAP_ASSET:
			if (lrg_node_visual_get_asset (visual) != NULL)
				set_string_property (comp, row->property, lrg_node_visual_get_asset (visual));
			break;
		case MAP_COLOR:
			if (material != NULL || kind == LRG_NODE_VISUAL_PRIMITIVE || kind == LRG_NODE_VISUAL_MESH_ASSET)
				set_float_property (comp, row->property, rgba[row->index]);
			break;
		case MAP_ROUGHNESS:
			if (material != NULL)
				set_float_property (comp, row->property, lrg_material3d_get_roughness (material));
			break;
		case MAP_METALLIC:
			if (material != NULL)
				set_float_property (comp, row->property, lrg_material3d_get_metallic (material));
			break;
		case MAP_EMISSION:
			if (material != NULL)
			{
				gfloat e[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

				lrg_material3d_get_emission_color (material, &e[0], &e[1], &e[2], NULL);
				e[3] = lrg_material3d_get_emission_strength (material);
				set_float_property (comp, row->property, e[row->index]);
			}
			break;
		case MAP_PARAM:
		case MAP_IGNORE:
		default:
			break;
		}
	}
}

/* Every param: an alias row, or the property of the same (dashed) name. */
static void
apply_params (LrgNodeVisual *visual,
              GObject       *comp)
{
	LrgNodeVisualKind  kind = lrg_node_visual_get_kind (visual);
	GList             *names = lrg_node_visual_get_param_names (visual);
	GList             *l;

	for (l = names; l != NULL; l = l->next)
	{
		const gchar      *key = l->data;
		const VisualMap  *row = find_param_row (kind, key);
		g_autofree gchar *dashed = replace_char (key, '_', '-');
		GParamSpec       *pspec;

		if (row != NULL && row->source == MAP_IGNORE)
			continue;
		pspec = component_property (G_OBJECT_GET_CLASS (comp), row != NULL ? row->property : dashed);
		if (pspec == NULL)
		{
			g_debug ("lrg_level_instantiate_3d: visual param '%s' has no %s property", key,
			         G_OBJECT_TYPE_NAME (comp));
			continue;
		}
		if (row != NULL && row->factor != 0.0 && row->factor != 1.0)
		{
			/* A unit change (the spot cone's full angle -> half-angle) */
			GValue d = G_VALUE_INIT;

			g_value_init (&d, G_TYPE_DOUBLE);
			if (g_value_transform (lrg_node_visual_get_param (visual, key), &d))
			{
				g_value_set_double (&d, g_value_get_double (&d) * row->factor);
				set_property_converted (comp, pspec, &d);
			}
			g_value_unset (&d);
			continue;
		}
		set_property_converted (comp, pspec, lrg_node_visual_get_param (visual, key));
	}
	g_list_free (names);
}

/*
 * configure_tilemap:
 *
 * The painted grid of a tilemap visual: a copy, so the runtime world does
 * not change when the level is edited. The component turns it into a
 * runtime #LrgTilemap with lrg_node_tilemap_to_runtime() once its tileset
 * (the visual's asset, "tileset-path") is loaded.
 */
static void
configure_tilemap (LrgNodeVisual *visual,
                   GObject       *comp)
{
	LrgNodeTilemap            *grid = lrg_node_visual_get_tilemap (visual);
	g_autoptr(LrgNodeTilemap)  copy = NULL;

	if (grid == NULL || !LRG_IS_TILEMAP_COMPONENT (comp))
		return;
	copy = lrg_node_tilemap_copy (grid);
	lrg_tilemap_component_set_node_tilemap (LRG_TILEMAP_COMPONENT (comp), copy);
}

/*
 * default_light_kind:
 *
 * The editor viewport's rule for a light without "light_type": a
 * positive "spot_angle" makes a spot light, anything else a point light.
 * An explicit "light_type" (applied afterwards) wins.
 */
static void
default_light_kind (LrgNodeVisual *visual,
                    GObject       *comp)
{
	GParamSpec *pspec = component_property (G_OBJECT_GET_CLASS (comp), "kind");
	GValue      v = G_VALUE_INIT;

	if (pspec == NULL || !G_TYPE_IS_ENUM (pspec->value_type))
		return;
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, lrg_node_visual_get_param_double (visual, "spot_angle", 0.0) > 0.0 ? "spot" : "point");
	set_property_converted (comp, pspec, &v);
	g_value_unset (&v);
}

/*
 * default_sprite:
 *
 * The editor draws a SPRITE as a 1x1 quad in the node's local XY plane,
 * aspect-fit to the texture: facing "fixed" with preserve-aspect on. The
 * visual's "facing" / "preserve_aspect" params (applied afterwards) win.
 */
static void
default_sprite (GObject *comp)
{
	GParamSpec *pspec;
	GValue      v = G_VALUE_INIT;

	if ((pspec = component_property (G_OBJECT_GET_CLASS (comp), "facing")) != NULL)
	{
		g_value_init (&v, G_TYPE_STRING);
		g_value_set_string (&v, "fixed");
		set_property_converted (comp, pspec, &v);
		g_value_unset (&v);
	}
	if ((pspec = component_property (G_OBJECT_GET_CLASS (comp), "preserve-aspect")) != NULL)
	{
		g_value_init (&v, G_TYPE_BOOLEAN);
		g_value_set_boolean (&v, TRUE);
		set_property_converted (comp, pspec, &v);
		g_value_unset (&v);
	}
}

/*
 * resolve_material_ref:
 *
 * The material a visual's `material: guid:/path` reference names, loaded
 * through @manager's cache (shared by every node naming it) or, without
 * a manager, from an absolute or current-directory path. NULL (with a
 * debug message) when it does not resolve: the node then draws with its
 * scalar material like before.
 *
 * Returns: (transfer full) (nullable): the material
 */
static LrgMaterial *
resolve_material_ref (const gchar     *ref,
                      LrgAssetManager *manager)
{
	g_autoptr(GError) error = NULL;
	LrgMaterial      *material = NULL;

	if (manager != NULL)
	{
		LrgMaterial *cached = lrg_asset_manager_load_material (manager, ref, &error);

		if (cached != NULL)
			return g_object_ref (cached);
	}
	else if (!g_str_has_prefix (ref, LRG_ASSET_GUID_PREFIX))
		material = lrg_material_new_from_file (ref, NULL, &error);
	if (material == NULL)
		g_debug ("lrg_level_instantiate_3d: material '%s' not loaded: %s", ref,
		         error != NULL ? error->message : "no asset manager for a guid reference");
	return material;
}

/* Fills the component's "material" slot from the visual's reference. */
static void
apply_material_ref (LrgNodeVisual   *visual,
                    GObject         *comp,
                    LrgAssetManager *manager)
{
	const gchar            *ref = lrg_node_visual_get_material_ref (visual);
	GParamSpec             *pspec;
	g_autoptr(LrgMaterial)  material = NULL;

	if (ref == NULL)
		return;
	pspec = component_property (G_OBJECT_GET_CLASS (comp), "material");
	if (pspec == NULL || !g_type_is_a (LRG_TYPE_MATERIAL, G_PARAM_SPEC_VALUE_TYPE (pspec)))
		return;
	material = resolve_material_ref (ref, manager);
	if (material != NULL)
		g_object_set (comp, "material", material, NULL);
}

/*
 * resolve_asset_ref:
 *
 * The existing file an authored reference names: `guid:` through the
 * manager's GUID index; an absolute path as is; a relative one tried
 * against @base_dir, the manager's search paths, its project root and
 * finally the working directory.
 *
 * Returns: (transfer full) (nullable): the canonical path, or NULL
 */
static gchar *
resolve_asset_ref (LrgAssetManager *manager,
                   const gchar     *base_dir,
                   const gchar     *ref)
{
	gchar *path;

	if (ref == NULL || *ref == '\0')
		return NULL;
	if (g_str_has_prefix (ref, LRG_ASSET_GUID_PREFIX))
		return manager != NULL ? lrg_asset_manager_resolve_guid (manager, ref) : NULL;
	if (g_path_is_absolute (ref))
		return g_file_test (ref, G_FILE_TEST_EXISTS) ? g_canonicalize_filename (ref, NULL) : NULL;

	/* 1. the level's (or project's) directory */
	if (base_dir != NULL)
	{
		path = g_build_filename (base_dir, ref, NULL);
		if (g_file_test (path, G_FILE_TEST_EXISTS))
		{
			gchar *canon = g_canonicalize_filename (path, NULL);

			g_free (path);
			return canon;
		}
		g_free (path);
	}

	/* 2. the asset manager: search paths, then the project root */
	if (manager != NULL)
	{
		const gchar *root = lrg_asset_manager_get_project_root (manager);

		path = lrg_asset_manager_resolve_reference (manager, ref);
		if (path != NULL)
			return path;
		if (root != NULL)
		{
			path = g_build_filename (root, ref, NULL);
			if (g_file_test (path, G_FILE_TEST_EXISTS))
			{
				gchar *canon = g_canonicalize_filename (path, NULL);

				g_free (path);
				return canon;
			}
			g_free (path);
		}
	}

	/* 3. the working directory */
	if (g_file_test (ref, G_FILE_TEST_EXISTS))
		return g_canonicalize_filename (ref, NULL);
	return NULL;
}

/*
 * lod_mesh_path:
 *
 * For a mesh asset visual whose `.meta` sidecar asks for an LOD chain
 * (`lod-levels` > 1), the resolved model path; NULL for everything else
 * (no manager to generate the chain with, import settings off, no
 * sidecar, one level).
 *
 * Returns: (transfer full) (nullable): the model path
 */
static gchar *
lod_mesh_path (LrgNodeVisual   *visual,
               LrgAssetManager *manager,
               const gchar     *base_dir)
{
	g_autofree gchar        *path = NULL;
	g_autoptr(LrgAssetMeta)  meta = NULL;

	if (lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_MESH_ASSET ||
	    lrg_node_visual_get_asset (visual) == NULL || manager == NULL ||
	    !lrg_asset_manager_get_apply_import_settings (manager))
		return NULL;
	path = resolve_asset_ref (manager, base_dir, lrg_node_visual_get_asset (visual));
	if (path == NULL)
		return NULL;
	meta = lrg_asset_meta_load_for_asset (path, NULL);
	if (meta == NULL || lrg_asset_meta_get_model_lod_levels (meta) <= 1)
		return NULL;
	return g_steal_pointer (&path);
}

/*
 * resolve_decal_paths:
 *
 * Stores the decal's texture references as absolute paths when they
 * resolve, so the textures load whatever directory the renderer
 * resolves against.  Unresolved ones are left for the renderer's
 * resolver.
 */
static void
resolve_decal_paths (GObject         *comp,
                     LrgAssetManager *manager,
                     const gchar     *base_dir)
{
	static const gchar *props[] = { "albedo-path", "normal-path", "orm-path" };
	guint               i;

	for (i = 0; i < G_N_ELEMENTS (props); i++)
	{
		g_autofree gchar *ref = NULL;
		g_autofree gchar *path = NULL;

		g_object_get (comp, props[i], &ref, NULL);
		if (ref == NULL || g_str_has_prefix (ref, LRG_ASSET_GUID_PREFIX))
			continue;   /* guid: references stay for the resolver */
		path = resolve_asset_ref (manager, base_dir, ref);
		if (path != NULL)
			g_object_set (comp, props[i], path, NULL);
	}
}

static LrgComponent *
create_visual_component (LrgNodeVisual   *visual,
                         LrgAssetManager *manager,
                         const gchar     *base_dir);

LrgComponent *
lrg_level_visual_create_component (LrgNodeVisual *visual)
{
	LrgEngine *engine;

	g_return_val_if_fail (LRG_IS_NODE_VISUAL (visual), NULL);

	engine = lrg_engine_get_default ();
	return create_visual_component (visual, engine != NULL ? lrg_engine_get_asset_manager (engine)
	                                                       : NULL, NULL);
}

/* lrg_level_visual_create_component() with an explicit asset manager and
 * base directory. */
static LrgComponent *
create_visual_component (LrgNodeVisual   *visual,
                         LrgAssetManager *manager,
                         const gchar     *base_dir)
{
	LrgNodeVisualKind  kind = lrg_node_visual_get_kind (visual);
	g_autofree gchar  *lod_path = NULL;
	GType              type;
	GObject           *comp;

	type = lrg_level_visual_get_component_type (kind);
	if (type == G_TYPE_INVALID || !g_type_is_a (type, LRG_TYPE_COMPONENT))
		return NULL;

	/* A mesh asset with an LOD chain in its sidecar draws through an
	 * LrgLodComponent, which shares the mesh renderer's property names
	 * (mesh-asset, color-*, material, cast-shadows, layer). */
	lod_path = lod_mesh_path (visual, manager, base_dir);
	if (lod_path != NULL)
		type = LRG_TYPE_LOD_COMPONENT;

	comp = g_object_new (type, NULL);
	if (kind == LRG_NODE_VISUAL_LIGHT)
		default_light_kind (visual, comp);
	else if (kind == LRG_NODE_VISUAL_SPRITE)
		default_sprite (comp);
	apply_explicit_rows (visual, comp);
	apply_params (visual, comp);
	apply_material_ref (visual, comp, manager);
	if (kind == LRG_NODE_VISUAL_TILEMAP)
		configure_tilemap (visual, comp);
	else if (kind == LRG_NODE_VISUAL_DECAL)
		resolve_decal_paths (comp, manager, base_dir);
	if (lod_path != NULL)
	{
		/* the chain is generated from the resolved model on first draw */
		lrg_lod_component_set_mesh_asset (LRG_LOD_COMPONENT (comp), lod_path);
		lrg_lod_component_set_asset_manager (LRG_LOD_COMPONENT (comp), manager);
	}
	return LRG_COMPONENT (comp);
}

/*
 * store_param:
 *
 * Stores a property value under a param key, keeping the type the param
 * already has, otherwise the types a level file round-trips: booleans,
 * strings, doubles for floating point, int64 for integers and enum /
 * flags as their nick string.
 */
static gboolean
store_param (LrgNodeVisual *visual,
             const gchar   *key,
             const GValue  *value)
{
	const GValue *stored = lrg_node_visual_get_param (visual, key);
	GType         t = G_VALUE_TYPE (value);
	GValue        out = G_VALUE_INIT;
	gboolean      ok = TRUE;

	if (stored != NULL && G_VALUE_TYPE (stored) == G_TYPE_STRING &&
	    (G_TYPE_IS_ENUM (t) || G_TYPE_IS_FLAGS (t)))
	{
		g_value_init (&out, G_TYPE_STRING);
		g_value_take_string (&out, lrg_property_value_to_string (value));
	}
	else if (stored != NULL && !G_TYPE_IS_ENUM (t) && !G_TYPE_IS_FLAGS (t))
	{
		g_value_init (&out, G_VALUE_TYPE (stored));
		ok = g_value_transform (value, &out);
	}
	else if (t == G_TYPE_BOOLEAN || t == G_TYPE_STRING || t == G_TYPE_DOUBLE || t == G_TYPE_INT64)
	{
		g_value_init (&out, t);
		g_value_copy (value, &out);
	}
	else if (t == G_TYPE_FLOAT)
	{
		g_value_init (&out, G_TYPE_DOUBLE);
		g_value_set_double (&out, (gdouble) g_value_get_float (value));
	}
	else if (G_TYPE_IS_ENUM (t) || G_TYPE_IS_FLAGS (t))
	{
		g_value_init (&out, G_TYPE_STRING);
		g_value_take_string (&out, lrg_property_value_to_string (value));
	}
	else if (t == G_TYPE_INT || t == G_TYPE_UINT || t == G_TYPE_LONG || t == G_TYPE_ULONG ||
	         t == G_TYPE_UINT64 || t == G_TYPE_CHAR || t == G_TYPE_UCHAR)
	{
		g_value_init (&out, G_TYPE_INT64);
		ok = g_value_transform (value, &out);
	}
	else
		return FALSE;

	if (ok)
		lrg_node_visual_set_param (visual, key, &out);
	g_value_unset (&out);
	return ok;
}

/* Reads a float out of @value (any numeric type). */
static gboolean
value_to_float (const GValue *value,
                gfloat       *out)
{
	GValue f = G_VALUE_INIT;

	g_value_init (&f, G_TYPE_FLOAT);
	if (!g_value_transform (value, &f))
	{
		g_value_unset (&f);
		return FALSE;
	}
	*out = g_value_get_float (&f);
	g_value_unset (&f);
	return TRUE;
}

gboolean
lrg_level_visual_store_property (LrgNodeVisual *visual,
                                 const gchar   *property,
                                 const GValue  *value)
{
	LrgNodeVisualKind  kind;
	GType              type;
	GObjectClass      *klass;
	const VisualMap   *row;
	gboolean           ok = FALSE;

	g_return_val_if_fail (LRG_IS_NODE_VISUAL (visual), FALSE);
	g_return_val_if_fail (property != NULL, FALSE);
	g_return_val_if_fail (G_IS_VALUE (value), FALSE);

	kind = lrg_node_visual_get_kind (visual);
	type = lrg_level_visual_get_component_type (kind);
	if (type == G_TYPE_INVALID)
		return FALSE;

	/* Only properties the component really has (and owns) are mapped. */
	klass = g_type_class_ref (type);
	if (component_property (klass, property) == NULL)
	{
		g_type_class_unref (klass);
		return FALSE;
	}
	g_type_class_unref (klass);

	row = find_property_row (kind, property);
	if (row == NULL)
	{
		g_autofree gchar *key = replace_char (property, '-', '_');

		return store_param (visual, key, value);
	}

	switch (row->source)
	{
	case MAP_PARAM:
		if (row->factor != 0.0 && row->factor != 1.0)
		{
			/* Undo the unit change of instantiation */
			GValue d = G_VALUE_INIT;

			g_value_init (&d, G_TYPE_DOUBLE);
			if (g_value_transform (value, &d))
			{
				g_value_set_double (&d, g_value_get_double (&d) / row->factor);
				ok = store_param (visual, row->key, &d);
			}
			g_value_unset (&d);
		}
		else
			ok = store_param (visual, row->key, value);
		break;
	case MAP_IGNORE:
		break;
	case MAP_ASSET:
		if (G_VALUE_HOLDS_STRING (value))
		{
			lrg_node_visual_set_asset (visual, g_value_get_string (value));
			ok = TRUE;
		}
		break;
	case MAP_PRIMITIVE:
		if (G_VALUE_HOLDS_ENUM (value))
		{
			lrg_node_visual_set_primitive (visual, (LrgPrimitiveType) g_value_get_enum (value));
			ok = TRUE;
		}
		break;
	case MAP_COLOR:
	case MAP_ROUGHNESS:
	case MAP_METALLIC:
	case MAP_EMISSION:
		{
			LrgMaterial3D            *material = lrg_node_visual_get_material (visual);
			g_autoptr(LrgMaterial3D)  copy = NULL;
			gfloat                    f;
			gfloat                    rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

			if (!value_to_float (value, &f))
				break;
			copy = material != NULL ? lrg_material3d_copy (material) : lrg_material3d_new ();
			lrg_material3d_get_color (copy, &rgba[0], &rgba[1], &rgba[2], &rgba[3]);
			if (row->source == MAP_COLOR)
			{
				rgba[row->index] = f;
				lrg_material3d_set_color (copy, rgba[0], rgba[1], rgba[2], rgba[3]);
			}
			else if (row->source == MAP_ROUGHNESS)
				lrg_material3d_set_roughness (copy, f);
			else if (row->source == MAP_EMISSION)
			{
				gfloat e[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

				lrg_material3d_get_emission_color (copy, &e[0], &e[1], &e[2], &e[3]);
				if (row->index == 3)
					lrg_material3d_set_emission_strength (copy, f);
				else
				{
					e[row->index] = f;
					lrg_material3d_set_emission_color (copy, e[0], e[1], e[2], e[3]);
				}
			}
			else
				lrg_material3d_set_metallic (copy, f);
			lrg_node_visual_set_material (visual, copy);
			ok = TRUE;
		}
		break;
	default:
		break;
	}
	return ok;
}

/* Attaches the component made from the node's visual, if any. */
static void
add_visual_component (Instantiate3D *ctx,
                      LrgNode       *node,
                      LrgEntity3D   *entity)
{
	LrgNodeVisual           *visual = lrg_node_get_visual (node);
	g_autoptr(LrgComponent)  comp = NULL;

	if (visual == NULL)
		return;
	comp = create_visual_component (visual, ctx->assets, ctx->base_dir);
	if (comp != NULL)
		lrg_entity3d_add_component (entity, comp);
}

static LrgEntity3D *
instantiate_node_3d (Instantiate3D *ctx,
                     LrgNode       *node,
                     LrgEntity3D   *parent);

/*
 * add_physics_components:
 *
 * The node's `physics:` block (0.9.0 M5) becomes an LrgRigidBodyComponent
 * (the body settings), one LrgColliderComponent3D per shape and one
 * LrgJointComponent per joint, in that order.  The world gets a physics
 * world (lrg_world3d_ensure_physics()) when it has none, so a level with
 * bodies simulates as soon as it is stepped.
 */
static void
add_physics_components (Instantiate3D *ctx,
                        LrgNode       *node,
                        LrgEntity3D   *entity)
{
	LrgNodePhysics                  *physics = lrg_node_get_physics (node);
	g_autoptr(LrgRigidBodyComponent) body = NULL;
	GPtrArray                       *list;
	guint                            i;

	if (physics == NULL)
		return;
	lrg_world3d_ensure_physics (ctx->world);

	body = lrg_rigid_body_component_new (lrg_node_physics_get_body_kind (physics));
	lrg_node_physics_apply (physics, body);
	lrg_entity3d_add_component (entity, LRG_COMPONENT (body));

	list = lrg_node_physics_get_shapes (physics);
	for (i = 0; i < list->len; i++)
	{
		LrgNodePhysicsShape              *desc = g_ptr_array_index (list, i);
		g_autoptr(LrgColliderComponent3D) collider = lrg_collider_component3d_new (lrg_node_physics_shape_get_kind (desc));

		lrg_node_physics_shape_apply (desc, collider);
		lrg_entity3d_add_component (entity, LRG_COMPONENT (collider));
	}

	list = lrg_node_physics_get_joints (physics);
	for (i = 0; i < list->len; i++)
	{
		LrgNodePhysicsJoint          *desc = g_ptr_array_index (list, i);
		g_autoptr(LrgJointComponent)  joint = lrg_joint_component_new (lrg_node_physics_joint_get_kind (desc));

		lrg_node_physics_joint_apply (desc, joint);
		lrg_entity3d_add_component (entity, LRG_COMPONENT (joint));
	}
}

/* --------------------------------------------------------------------------
 * Probe volumes (0.9.0 M3)
 * -------------------------------------------------------------------------- */

/* floor (extent / spacing) + 1, clamped: the lightmap baker's axis rule */
static guint
probe_axis_count (gfloat extent,
                  gfloat spacing)
{
	guint n = (guint) floorf (extent / MAX (spacing, 1e-3f)) + 1u;

	return CLAMP (n, 2u, (guint) LRG_PROBE_GRID_MAX_RESOLUTION);
}

gboolean
lrg_node_visual_get_probe_volume (LrgNodeVisual *self,
                                  const gfloat  *world,
                                  gfloat         default_spacing,
                                  gfloat        *out_min,
                                  gfloat        *out_max,
                                  guint         *out_resolution)
{
	gfloat half[3];
	gfloat spacing;
	gint   res;
	gint   k;

	g_return_val_if_fail (LRG_IS_NODE_VISUAL (self), FALSE);
	g_return_val_if_fail (world != NULL && out_min != NULL && out_max != NULL && out_resolution != NULL, FALSE);

	if (lrg_node_visual_get_kind (self) != LRG_NODE_VISUAL_PROBE_VOLUME)
		return FALSE;

	/* an axis-aligned box around the node position (rotation and scale
	 * ignored, like reflection probes and the gizmo) */
	half[0] = (gfloat) MAX (lrg_node_visual_get_param_double (self, "extents_x", 5.0), 0.01);
	half[1] = (gfloat) MAX (lrg_node_visual_get_param_double (self, "extents_y", 5.0), 0.01);
	half[2] = (gfloat) MAX (lrg_node_visual_get_param_double (self, "extents_z", 5.0), 0.01);
	spacing = (gfloat) lrg_node_visual_get_param_double (self, "spacing", 0.0);
	if (spacing <= 0.0f)
		spacing = default_spacing > 0.0f ? default_spacing : 2.0f;
	res = (gint) lrg_node_visual_get_param_double (self, "resolution", 0.0);
	for (k = 0; k < 3; k++)
	{
		out_min[k] = world[12 + k] - half[k];
		out_max[k] = world[12 + k] + half[k];
		out_resolution[k] = (res > 0) ? (guint) CLAMP (res, 2, LRG_PROBE_GRID_MAX_RESOLUTION)
		                              : probe_axis_count (2.0f * half[k], spacing);
	}
	return TRUE;
}

/*
 * add_probe_volume:
 *
 * A probe-volume node becomes an unbaked LrgProbeGrid on the world (every
 * probe invalid, so the renderer ignores it), which
 * lrg_lightmap_baker_add_world() bakes as a volume.  Skipped when the
 * level already lists baked grids (`lighting: probes:`): those stand for
 * the volumes, and the baker would bake every box twice.
 */
static void
add_probe_volume (Instantiate3D *ctx,
                  LrgNode       *node,
                  LrgEntity3D   *entity)
{
	LrgNodeVisual          *visual = lrg_node_get_visual (node);
	g_autoptr(LrgProbeGrid) grid = NULL;
	gfloat                  world[16], mn[3], mx[3];
	guint                   n[3];

	if (!ctx->probe_volumes || visual == NULL || lrg_node_visual_get_kind (visual) != LRG_NODE_VISUAL_PROBE_VOLUME)
		return;
	lrg_entity3d_get_world_matrix (entity, world);
	if (!lrg_node_visual_get_probe_volume (visual, world, ctx->probe_spacing, mn, mx, n))
		return;
	grid = lrg_probe_grid_new_with_bounds (mn, mx, n[0], n[1], n[2]);
	lrg_world3d_add_probe_grid (ctx->world, grid);
}

/*
 * instantiate_children_3d:
 *
 * Children in the node's order, appended to @entity.
 */
static void
instantiate_children_3d (Instantiate3D *ctx,
                         LrgNode       *node,
                         LrgEntity3D   *entity)
{
	GPtrArray *children = lrg_node_get_children (node);
	guint      i;

	for (i = 0; children != NULL && i < children->len; i++)
		instantiate_node_3d (ctx, g_ptr_array_index (children, i), entity);
}

/*
 * instantiate_node_3d:
 *
 * Builds the entity of @node, appends it to @parent (which is in the
 * world, so the entity joins the world before its components attach and
 * world_entered() runs for them), adds the visual, registry and script
 * components, runs the node class hook and recurses into the children.
 *
 * Returns: (transfer none): the new entity, owned by @parent
 */
static LrgEntity3D *
instantiate_node_3d (Instantiate3D *ctx,
                     LrgNode       *node,
                     LrgEntity3D   *parent)
{
	g_autoptr(LrgEntity3D) entity = lrg_entity3d_new_with_name (lrg_node_get_name (node));

	/* Identity, transform and flags first: they are what the world's
	 * GUID map, octree and render lists see when the entity joins. */
	assign_guid (ctx, node, entity);
	apply_node_transform (node, entity);
	lrg_entity3d_set_visible (entity, lrg_node_get_visible (node));
	lrg_entity3d_set_active (entity, TRUE);

	lrg_entity3d_add_child (parent, entity);

	/* Components: the visual's first (renderers, lights, cameras), then
	 * the authored registry components and the scripts, in that order, so
	 * game components can find the visual component in their attach. */
	add_visual_component (ctx, node, entity);
	add_probe_volume (ctx, node, entity);
	add_physics_components (ctx, node, entity);
	create_registry_components (ctx, node, entity);
	create_script_components (node, entity);

	/* Custom node kinds add their own runtime pieces. */
	lrg_node_instantiate_3d (node, entity, ctx->world);

	instantiate_children_3d (ctx, node, entity);
	return entity;   /* the parent holds the reference */
}

static void
instantiate_3d_init (Instantiate3D *ctx,
                     LrgWorld3D    *world,
                     LrgEngine     *engine)
{
	ctx->world = world;
	ctx->registry = NULL;
	ctx->assets = NULL;
	ctx->base_dir = NULL;
	ctx->used_guids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	ctx->probe_volumes = TRUE;
	ctx->probe_spacing = 0.0f;

	if (engine == NULL)
		engine = lrg_engine_get_default ();
	if (engine != NULL)
	{
		ctx->registry = lrg_engine_get_registry (engine);
		ctx->assets = lrg_engine_get_asset_manager (engine);
	}
}

/* ==========================================================================
 * The level's look: environment and post stack
 * ========================================================================== */

/*
 * resolve_sky_refs:
 *
 * Rewrites a sky reference of the runtime copy to its resolved absolute
 * path, so lrg_environment_load_sources() needs no base directory.
 * Unresolved references are kept (the load then reports them).
 */
static void
resolve_sky_refs (Instantiate3D  *ctx,
                  LrgEnvironment *env)
{
	g_autofree gchar *sky = NULL;
	guint             i;

	sky = resolve_asset_ref (ctx->assets, ctx->base_dir, lrg_environment_get_sky_path (env));
	if (sky != NULL)
		lrg_environment_set_sky_path (env, sky);
	for (i = 0; i < 6; i++)
	{
		g_autofree gchar *face = resolve_asset_ref (ctx->assets, ctx->base_dir,
		                                            lrg_environment_get_sky_face_path (env, i));

		if (face != NULL)
			lrg_environment_set_sky_face_path (env, i, face);
	}
}

/*
 * copy_sky_images:
 *
 * Shares the sky images @src already holds (the editor loaded them for
 * its viewport) with @dst.
 *
 * Returns: %TRUE when @dst needs no lrg_environment_load_sources()
 */
static gboolean
copy_sky_images (LrgEnvironment *src,
                 LrgEnvironment *dst)
{
	guint i;

	lrg_environment_set_sky_dome (dst, lrg_environment_get_sky_dome (src));
	switch (lrg_environment_get_sky_source (src))
	{
	case LRG_ENVIRONMENT_SKY_EQUIRECT:
		if (lrg_environment_get_equirect (src) == NULL)
			return FALSE;
		lrg_environment_set_equirect (dst, lrg_environment_get_equirect (src));
		return TRUE;
	case LRG_ENVIRONMENT_SKY_CUBEMAP:
		for (i = 0; i < 6; i++)
			if (lrg_environment_get_cubemap_face (src, i) == NULL)
				return FALSE;
		for (i = 0; i < 6; i++)
			lrg_environment_set_cubemap_face (dst, i, lrg_environment_get_cubemap_face (src, i));
		return TRUE;
	case LRG_ENVIRONMENT_SKY_COLOR:
	case LRG_ENVIRONMENT_SKY_GRADIENT:
	case LRG_ENVIRONMENT_SKY_DOME:
	default:
		return TRUE;
	}
}

/*
 * apply_level_look:
 *
 * The world gets a runtime copy of the level's environment (settings
 * through the block's YAML, sky images shared or loaded) and the post
 * stack its `post_stack:` names.  Nothing happens for a level without an
 * environment block: the world keeps what it has.
 */
static void
apply_level_look (Instantiate3D *ctx,
                  LrgLevel      *level)
{
	LrgEnvironment            *src = lrg_level_get_environment (level);
	g_autoptr(LrgEnvironment)  env = NULL;
	g_autofree gchar          *data = NULL;
	g_autoptr(GError)          error = NULL;
	const gchar               *post_ref;

	if (src == NULL)
		return;

	/* 1. the settings: the block round trip copies every one of them */
	env = lrg_environment_new ();
	data = lrg_environment_to_data (src);
	if (!lrg_environment_load_from_data (env, data, -1, &error))
	{
		g_warning ("lrg_level_instantiate_3d: cannot copy the level environment: %s", error->message);
		return;
	}

	/* 2. the sky images: shared when loaded, else loaded for the copy */
	if (!copy_sky_images (src, env))
	{
		resolve_sky_refs (ctx, env);
		if (!lrg_environment_load_sources (env, ctx->assets, ctx->base_dir, &error))
		{
			g_warning ("lrg_level_instantiate_3d: the level sky did not load: %s", error->message);
			g_clear_error (&error);
		}
	}
	lrg_world3d_set_environment (ctx->world, env);

	/* 3. the post-processing stack */
	post_ref = lrg_environment_get_post_stack (env);
	if (post_ref != NULL && *post_ref != '\0')
	{
		g_autofree gchar         *path = resolve_asset_ref (ctx->assets, ctx->base_dir, post_ref);
		g_autoptr(LrgPostStack)   stack = NULL;

		if (path != NULL)
			stack = lrg_post_stack_new_from_file (path, &error);
		if (stack != NULL)
			lrg_world3d_set_post_stack (ctx->world, stack);
		else
			g_warning ("lrg_level_instantiate_3d: post stack '%s' did not load: %s", post_ref,
			           error != NULL ? error->message : "not found");
	}
}

/*
 * apply_level_lighting:
 *
 * The level's `lighting:` block (0.9.0 M3): its lightmap becomes the
 * world's (lrg_world3d_set_lightmap()) and each probe grid is added
 * (lrg_world3d_add_probe_grid()).  References resolve like every other
 * asset reference; one that does not resolve or load is reported with a
 * warning and skipped (the scene then draws with realtime and ambient
 * light), so a level whose bake was never run still loads.
 */
static void
apply_level_lighting (Instantiate3D *ctx,
                      LrgLevel      *level)
{
	const gchar *ref = lrg_level_get_lightmap_ref (level);
	GPtrArray   *probes = lrg_level_get_probe_refs (level);
	guint        i;

	if (ref != NULL)
	{
		g_autofree gchar         *path = resolve_asset_ref (ctx->assets, ctx->base_dir, ref);
		g_autoptr(LrgLightmap3D)  lightmap = NULL;
		g_autoptr(GError)         error = NULL;

		if (path != NULL)
			lightmap = lrg_lightmap3d_new_from_file (path, &error);
		if (lightmap != NULL)
			lrg_world3d_set_lightmap (ctx->world, lightmap);
		else
			g_warning ("lrg_level_instantiate_3d: lightmap '%s' did not load: %s", ref,
			           error != NULL ? error->message : "not found");
	}
	for (i = 0; i < probes->len; i++)
	{
		const gchar             *pref = g_ptr_array_index (probes, i);
		g_autofree gchar        *path = resolve_asset_ref (ctx->assets, ctx->base_dir, pref);
		g_autoptr(LrgProbeGrid)  grid = NULL;
		g_autoptr(GError)        error = NULL;

		if (path != NULL)
			grid = lrg_probe_grid_new_from_file (path, &error);
		if (grid != NULL)
			lrg_world3d_add_probe_grid (ctx->world, grid);
		else
			g_warning ("lrg_level_instantiate_3d: probe grid '%s' did not load: %s", pref,
			           error != NULL ? error->message : "not found");
	}
}

/*
 * apply_project_physics:
 *
 * Gives the world its project's physics: block (layers, gravity, step
 * limits, iterations, sleep) before any physics component asks for a
 * physics world. The project.ryaml is found the way the level serializer
 * finds project settings: walking up from the level's directory, then
 * the asset manager's project root. A world that already has settings
 * (lrg_world3d_set_physics_settings(), e.g. from the editor's open
 * project) or a physics world of its own is left alone; no project
 * means lrg_world3d_ensure_physics() uses the defaults.
 */
static void
apply_project_physics (Instantiate3D *ctx)
{
	g_autofree gchar  *path = NULL;
	g_autoptr(GError)  error = NULL;

	if (lrg_world3d_get_physics_settings (ctx->world) != NULL || lrg_world3d_get_physics (ctx->world) != NULL)
		return;

	/* the level's own project first, then the game's project root */
	if (ctx->base_dir != NULL)
		path = lrg_physics_settings_find_project (ctx->base_dir);
	if (path == NULL && ctx->assets != NULL && lrg_asset_manager_get_project_root (ctx->assets) != NULL)
	{
		g_autofree gchar *candidate = g_build_filename (lrg_asset_manager_get_project_root (ctx->assets),
		                                                "project.ryaml", NULL);

		if (g_file_test (candidate, G_FILE_TEST_IS_REGULAR))
			path = g_steal_pointer (&candidate);
	}
	if (path == NULL)
		return;

	/* a broken block is reported and the defaults are used */
	if (!lrg_world3d_load_physics_project (ctx->world, path, &error))
		g_warning ("lrg_level_instantiate_3d: %s: physics block ignored: %s", path, error->message);
}

/*
 * apply_level_navigation:
 *
 * The level's `navigation:` block (0.9.0 M6), after the entities exist:
 * the baked mesh of `data:` is loaded (lrg_nav_poly_mesh_load()) and
 * given the area costs of the `settings:` .rnav; when there is no data,
 * or it does not load, and `bake_on_load:` is set, the mesh is baked from
 * the instantiated world with the settings.  The mesh becomes the
 * world's (lrg_world3d_set_navigation()).  Every failure is a warning:
 * the level still loads, without navigation.
 */
static void
apply_level_navigation (Instantiate3D *ctx,
                        LrgLevel      *level)
{
	const gchar              *settings_ref = lrg_level_get_nav_settings_ref (level);
	const gchar              *data_ref = lrg_level_get_nav_data_ref (level);
	g_autoptr(LrgNavBake)     settings = NULL;
	g_autoptr(LrgNavPolyMesh) mesh = NULL;

	if (!lrg_level_has_navigation (level))
		return;

	/* the .rnav settings (area costs, and the bake on load) */
	if (settings_ref != NULL)
	{
		g_autofree gchar  *path = resolve_asset_ref (ctx->assets, ctx->base_dir, settings_ref);
		g_autoptr(GError)  error = NULL;

		if (path != NULL)
			settings = lrg_nav_bake_new_from_file (path, &error);
		if (settings == NULL)
			g_warning ("lrg_level_instantiate_3d: navigation settings '%s' did not load: %s", settings_ref,
			           error != NULL ? error->message : "not found");
	}

	/* the baked mesh */
	if (data_ref != NULL)
	{
		g_autofree gchar  *path = resolve_asset_ref (ctx->assets, ctx->base_dir, data_ref);
		g_autoptr(GError)  error = NULL;
		g_autofree gchar  *bytes = NULL;
		gsize              len = 0;

		if (path != NULL && g_file_get_contents (path, &bytes, &len, &error))
		{
			g_autoptr(GBytes) data = g_bytes_new_take (g_steal_pointer (&bytes), len);

			mesh = lrg_nav_poly_mesh_new ();
			if (!lrg_nav_poly_mesh_load (mesh, data, &error))
				g_clear_object (&mesh);
		}
		if (mesh == NULL)
			g_warning ("lrg_level_instantiate_3d: navigation data '%s' did not load: %s", data_ref,
			           error != NULL ? error->message : "not found");
		else if (settings != NULL)
			lrg_nav_bake_apply_costs (settings, mesh);
	}

	/* no data: bake from what was just instantiated */
	if (mesh == NULL && lrg_level_get_nav_bake_on_load (level))
	{
		g_autoptr(GError) error = NULL;

		if (settings == NULL)
			settings = lrg_nav_bake_new ();
		mesh = lrg_nav_bake_bake (settings, ctx->world, NULL, &error);
		if (mesh == NULL)
			g_warning ("lrg_level_instantiate_3d: navigation bake on load failed: %s", error->message);
	}
	if (mesh != NULL)
		lrg_world3d_set_navigation (ctx->world, mesh);
}

/*
 * lrg_level_instantiate_3d:
 *
 * One entity per node under the world root, see the header.
 */
gboolean
lrg_level_instantiate_3d (LrgLevel    *level,
                          LrgWorld3D  *world,
                          LrgEngine   *engine,
                          GError     **error)
{
	return lrg_level_instantiate_3d_with_base_dir (level, world, engine, NULL, error);
}

gboolean
lrg_level_instantiate_3d_with_base_dir (LrgLevel     *level,
                                        LrgWorld3D   *world,
                                        LrgEngine    *engine,
                                        const gchar  *base_dir,
                                        GError      **error)
{
	Instantiate3D  ctx;
	LrgEntity3D   *world_root;

	g_return_val_if_fail (LRG_IS_LEVEL (level), FALSE);
	g_return_val_if_fail (LRG_IS_WORLD3D (world), FALSE);
	g_return_val_if_fail (engine == NULL || LRG_IS_ENGINE (engine), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	instantiate_3d_init (&ctx, world, engine);
	ctx.base_dir = base_dir;
	world_root = lrg_world3d_get_root (world);

	/* probe-volume nodes stand in for baked grids until the first bake */
	{
		gdouble spacing = 0.0;

		ctx.probe_volumes = lrg_level_get_probe_refs (level)->len == 0;
		if (lrg_level_get_bake_setting (level, "probe_spacing", &spacing))
			ctx.probe_spacing = (gfloat) spacing;
	}

	/* The look first, so probe components entering the world find the
	 * environment to register with. */
	apply_level_look (&ctx, level);
	apply_level_lighting (&ctx, level);

	/* the project's physics settings before any body is made */
	apply_project_physics (&ctx);

	/* The level's implicit root is not an entity: its children are the
	 * world's top-level entities. */
	instantiate_children_3d (&ctx, lrg_level_get_root (level), world_root);

	/* navigation last: a bake on load needs the geometry */
	apply_level_navigation (&ctx, level);

	g_hash_table_unref (ctx.used_guids);
	return TRUE;
}

/*
 * lrg_level_instantiate_node_3d:
 *
 * One subtree, for spawning prefabs at runtime.
 */
LrgEntity3D *
lrg_level_instantiate_node_3d (LrgNode      *node,
                               LrgEntity3D  *parent,
                               LrgWorld3D   *world,
                               LrgEngine    *engine,
                               GError      **error)
{
	Instantiate3D  ctx;
	LrgEntity3D   *entity;

	g_return_val_if_fail (LRG_IS_NODE (node), NULL);
	g_return_val_if_fail (LRG_IS_WORLD3D (world), NULL);
	g_return_val_if_fail (parent == NULL || LRG_IS_ENTITY3D (parent), NULL);
	g_return_val_if_fail (engine == NULL || LRG_IS_ENGINE (engine), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (parent != NULL && lrg_entity3d_get_world (parent) != world)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_FAILED,
		             "The parent entity '%s' is not in the given world",
		             lrg_entity3d_get_name (parent));
		return NULL;
	}

	instantiate_3d_init (&ctx, world, engine);
	entity = instantiate_node_3d (&ctx, node, parent != NULL ? parent : lrg_world3d_get_root (world));
	g_hash_table_unref (ctx.used_guids);
	return entity;
}

/*
 * lrg_level_load_world3d:
 *
 * Load + instantiate with the default engine.
 */
LrgLevel *
lrg_level_load_world3d (const gchar  *path,
                        LrgWorld3D   *world,
                        GError      **error)
{
	g_autoptr(LrgLevelSerializer) serializer = NULL;
	g_autoptr(LrgLevel)           level = NULL;
	g_autofree gchar             *dir = NULL;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (LRG_IS_WORLD3D (world), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	serializer = lrg_level_serializer_new ();
	level = lrg_level_serializer_load_from_file (serializer, path, error);
	if (level == NULL)
		return NULL;
	dir = g_path_get_dirname (path);
	if (!lrg_level_instantiate_3d_with_base_dir (level, world, NULL, dir, error))
		return NULL;
	return g_steal_pointer (&level);
}
