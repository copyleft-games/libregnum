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
 * runtime 2D #LrgWorld of #LrgGameObject instances with their components.
 * lrg_level_instantiate_3d() materializes it into a runtime #LrgWorld3D: one
 * #LrgEntity3D per node with the same GUID, name, hierarchy and full local
 * transform, a component for the node's visual kind, its registry
 * components and its script components. None of them mutates the source
 * #LrgLevel.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "../world3d/lrg-world3d.h"

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
 * descriptions via the engine registry, attaches enabled script bindings as
 * #LrgScriptComponent instances, runs the node's #LrgNodeClass.instantiate
 * hook and adds the objects to @world. Each object is tagged with its
 * node's GUID (lrg_world_find_by_tag()), which is how play-in-editor maps
 * runtime objects back to the authoring nodes.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_instantiate (LrgLevel   *level,
                                LrgWorld   *world,
                                LrgEngine  *engine,
                                GError    **error);

/**
 * lrg_level_instantiate_3d:
 * @level: an #LrgLevel
 * @world: the #LrgWorld3D to populate
 * @engine: (nullable): the engine whose #LrgRegistry resolves component
 *   types; if %NULL, the default engine is used
 * @error: (nullable): return location for an error
 *
 * Materializes @level into @world, one #LrgEntity3D per #LrgNode
 * (group nodes without a visual included), appended under the world root
 * in the level's order:
 *
 * - identity: the entity gets the node's GUID and name (a GUID already
 *   used in @world, for example when the same level is instantiated
 *   twice, or used twice inside @level, gets a fresh GUID instead);
 * - hierarchy: child order is the node's child order;
 * - transform: the node's location, Euler rotation (kept as the entity's
 *   authored Euler, see lrg_entity3d_has_authored_euler()) and scale as
 *   the entity's local TRS, so lrg_entity3d_get_world_matrix() equals
 *   lrg_node_get_world_matrix();
 * - flags: #LrgNode:visible becomes #LrgEntity3D:visible, the entity is
 *   active;
 * - visual: a primitive or mesh asset becomes an
 *   #LrgMeshRendererComponent, a light an #LrgLightComponent, a camera an
 *   #LrgCameraComponent, an audio emitter an #LrgAudioSourceComponent, a
 *   sprite an #LrgBillboardComponent and a tilemap an
 *   #LrgTilemapComponent, each configured from the visual's asset,
 *   material and params (prefab instances are already expanded in the
 *   level; their root carries no component of its own);
 * - components: each #LrgComponentDesc whose type name is registered in
 *   the engine's #LrgRegistry is created and its properties applied
 *   exactly as lrg_level_instantiate() does (unregistered types are
 *   skipped);
 * - scripts: each enabled #LrgScriptBinding becomes an
 *   #LrgScriptComponent;
 * - finally the node class's #LrgNodeClass.instantiate_3d hook runs.
 *
 * @world is not cleared first. lrg_level_instantiate() (2D) is unchanged.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_instantiate_3d (LrgLevel    *level,
                                   LrgWorld3D  *world,
                                   LrgEngine   *engine,
                                   GError     **error);

/**
 * lrg_level_instantiate_node_3d:
 * @node: the root of the subtree to instantiate (for example a prefab
 *   instance made with lrg_prefab_instantiate())
 * @parent: (nullable): the entity to append the new entity to; %NULL
 *   appends it under the world root
 * @world: the #LrgWorld3D to populate
 * @engine: (nullable): the engine whose #LrgRegistry resolves component
 *   types; if %NULL, the default engine is used
 * @error: (nullable): return location for an error
 *
 * Instantiates @node and its descendants the way lrg_level_instantiate_3d()
 * instantiates a whole level, which is how a game spawns a prefab at
 * runtime. @node's own transform is used as the entity's local transform
 * relative to @parent.
 *
 * Returns: (transfer none) (nullable): the entity made from @node (owned
 *   by its parent), or %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgEntity3D * lrg_level_instantiate_node_3d (LrgNode      *node,
                                             LrgEntity3D  *parent,
                                             LrgWorld3D   *world,
                                             LrgEngine    *engine,
                                             GError      **error);

/**
 * lrg_level_visual_get_component_type:
 * @kind: a visual kind
 *
 * The component type lrg_level_instantiate_3d() creates for a visual of
 * kind @kind: #LrgMeshRendererComponent for primitives and mesh assets,
 * #LrgLightComponent, #LrgCameraComponent, #LrgAudioSourceComponent,
 * #LrgBillboardComponent for sprites and #LrgTilemapComponent.
 *
 * Returns: the component #GType, or %G_TYPE_INVALID for kinds that make
 *   no component (none, prefab instance, CAD part)
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GType lrg_level_visual_get_component_type (LrgNodeVisualKind kind);

/**
 * lrg_level_visual_create_component:
 * @visual: a node visual
 *
 * Creates the component lrg_level_instantiate_3d() attaches for @visual
 * and configures it from the visual's asset, primitive, material and
 * params (see the property map in the level module documentation).
 *
 * Returns: (transfer full) (nullable): a new component, or %NULL for
 *   kinds that make none
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgComponent * lrg_level_visual_create_component (LrgNodeVisual *visual);

/**
 * lrg_level_visual_store_property:
 * @visual: a node visual
 * @property: a property name of the visual's component type
 * @value: the property value
 *
 * The inverse of lrg_level_visual_create_component(): stores @value of
 * the component property @property into @visual (its param bag,
 * material, asset or primitive), which is how play-in-editor's "Apply to
 * Level" keeps a live value of a light, camera, mesh renderer, ...
 *
 * Returns: %TRUE if @property maps to something stored in a visual of
 *   this kind and @value could be converted
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_level_visual_store_property (LrgNodeVisual *visual,
                                          const gchar   *property,
                                          const GValue  *value);

/**
 * lrg_level_load_world3d:
 * @path: path of a `.rlevel` file
 * @world: the #LrgWorld3D to populate
 * @error: (nullable): return location for an error
 *
 * Convenience for games: loads @path with #LrgLevelSerializer and
 * instantiates it into @world with lrg_level_instantiate_3d() and the
 * default engine. Register your component types in the default engine's
 * registry first.
 *
 * Returns: (transfer full) (nullable): the loaded #LrgLevel (keep it to
 *   look nodes up by GUID, or drop it: the world does not need it), or
 *   %NULL on error
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgLevel * lrg_level_load_world3d (const gchar  *path,
                                   LrgWorld3D   *world,
                                   GError      **error);

G_END_DECLS
