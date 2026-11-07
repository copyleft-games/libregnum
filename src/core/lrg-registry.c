/* lrg-registry.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Type registry implementation.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_CORE

#include "lrg-registry.h"
#include "../lrg-log.h"

#include "../ecs/lrg-game-object.h"
#include "../ecs/lrg-world.h"
#include "../ecs/components/lrg-transform-component.h"
#include "../ecs/components/lrg-sprite-component.h"
#include "../ecs/components/lrg-collider-component.h"
#include "../ecs/components/lrg-animator-component.h"
#include "../scene/lrg-scene.h"
#include "../scene/lrg-scene-entity.h"
#include "../scene/lrg-scene-object.h"
#include "../world3d/lrg-level3d.h"
#include "../physics/lrg-physics-world.h"
#include "../physics/lrg-rigid-body.h"
#include "../physics/lrg-character-controller.h"
#include "../physics/lrg-wall-set.h"
#include "../physics/lrg-collision-mesh.h"
#include "../physics/lrg-rope.h"
#include "../trigger2d/lrg-trigger-rect.h"
#include "../trigger2d/lrg-trigger-circle.h"
#include "../trigger2d/lrg-trigger-polygon.h"
#include "../trigger2d/lrg-trigger-manager.h"
#include "../lighting/lrg-point-light2d.h"
#include "../lighting/lrg-spot-light2d.h"
#include "../lighting/lrg-directional-light2d.h"
#include "../graphics/lrg-light-set.h"
#include "../world3d/components/lrg-mesh-renderer-component.h"
#include "../world3d/components/lrg-skinned-model-component.h"
#include "../world3d/components/lrg-light-component.h"
#include "../world3d/components/lrg-camera-component.h"
#include "../world3d/components/lrg-audio-source-component.h"
#include "../world3d/components/lrg-audio-listener-component.h"
#include "../world3d/components/lrg-billboard-component.h"
#include "../world3d/components/lrg-tilemap-component.h"
#include "../world3d/components/lrg-spawn-component.h"
#include "../world3d/components/lrg-trigger-component.h"
#include "../world3d/components/lrg-decal-component.h"
#include "../world3d/components/lrg-lod-component.h"
#include "../world3d/components/lrg-reflection-probe-component.h"
#include "../world3d/components/lrg-sequence-component.h"
#include "../ecs/components/lrg-skeleton2d-component.h"
#include "../particles/lrg-particle-system.h"
#include "../particles/lrg-particle-emitter.h"
#include "../particles/lrg-particle-force.h"
#include "../weather/lrg-weather.h"
#include "../weather/lrg-rain.h"
#include "../weather/lrg-snow.h"
#include "../weather/lrg-fog.h"
#include "../weather/lrg-lightning.h"
#include "../weather/lrg-day-night-cycle.h"
#include "../shapes/lrg-rectangle2d.h"
#include "../shapes/lrg-circle2d.h"
#include "../shapes/lrg-text2d.h"
#include "../shapes/lrg-cube3d.h"
#include "../shapes/lrg-sphere3d.h"
#include "../shapes/lrg-plane3d.h"
#include "../shapes/lrg-cylinder3d.h"
#include "../shapes/lrg-cone3d.h"
#include "../shapes/lrg-torus3d.h"
#include "../shapes/lrg-icosphere3d.h"
#include "../shapes/lrg-circle3d.h"
#include "../shapes/lrg-line3d.h"
#include "../shapes/lrg-grid3d.h"
#include "../inventory/lrg-item-def.h"
#include "../inventory/lrg-inventory.h"
#include "../inventory/lrg-equipment.h"
#include "../quest/lrg-quest-def.h"
#include "../dialog/lrg-dialog-tree.h"
#include "../dialog/lrg-dialog-node.h"
#include "../ai/lrg-behavior-tree.h"
#include "../ai/lrg-blackboard.h"
#include "../ai/lrg-bt-composite.h"
#include "../ai/lrg-bt-decorator.h"
#include "../ai/lrg-bt-leaf.h"

#include <stdarg.h>
#include <string.h>

struct _LrgRegistry
{
    GObject     parent_instance;

    /* Hash table mapping string names to GTypes */
    GHashTable *name_to_type;
};

G_DEFINE_TYPE (LrgRegistry, lrg_registry, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_registry_finalize (GObject *object)
{
    LrgRegistry *self = LRG_REGISTRY (object);

    g_clear_pointer (&self->name_to_type, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_registry_parent_class)->finalize (object);
}

static void
lrg_registry_class_init (LrgRegistryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_registry_finalize;
}

static void
lrg_registry_init (LrgRegistry *self)
{
    /* Create hash table with string keys (copied) and GType values */
    self->name_to_type = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                NULL);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/*
 * lrg_registry_new:
 *
 * Creates a new type registry.
 *
 * Returns: (transfer full): A new #LrgRegistry
 */
LrgRegistry *
lrg_registry_new (void)
{
    return g_object_new (LRG_TYPE_REGISTRY, NULL);
}

/* ==========================================================================
 * Public API - Type Registration
 * ========================================================================== */

/*
 * lrg_registry_register:
 * @self: an #LrgRegistry
 * @name: the string name to register (e.g., "player", "enemy")
 * @type: the #GType to associate with the name
 *
 * Registers a GType with a string name for data-driven instantiation.
 */
void
lrg_registry_register (LrgRegistry *self,
                       const gchar *name,
                       GType        type)
{
    gchar *name_copy;
    GType  old_type;

    g_return_if_fail (LRG_IS_REGISTRY (self));
    g_return_if_fail (name != NULL && name[0] != '\0');
    g_return_if_fail (type != G_TYPE_INVALID);

    /* Copy before replacing: name may be borrowed from this registry. */
    name_copy = g_strdup (name);
    old_type = lrg_registry_lookup (self, name);
    if (old_type != G_TYPE_INVALID)
        lrg_debug (LRG_LOG_DOMAIN_CORE, "Overwriting registry entry '%s'", name);

    g_hash_table_replace (self->name_to_type, name_copy, GSIZE_TO_POINTER (type));
    lrg_debug (LRG_LOG_DOMAIN_CORE, "Registered type '%s' as '%s'",
               g_type_name (type), name_copy);
}

/*
 * lrg_registry_unregister:
 * @self: an #LrgRegistry
 * @name: the string name to unregister
 *
 * Removes a type registration from the registry.
 *
 * Returns: %TRUE if the name was registered and removed
 */
gboolean
lrg_registry_unregister (LrgRegistry *self,
                         const gchar *name)
{
    GType type;

    g_return_val_if_fail (LRG_IS_REGISTRY (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    /* Check whether the name exists before removing it. */
    type = GPOINTER_TO_SIZE (g_hash_table_lookup (self->name_to_type, name));
    if (type == G_TYPE_INVALID)
    {
        return FALSE;
    }

    /* Log before removing: name may point into the table. */
    lrg_debug (LRG_LOG_DOMAIN_CORE, "Unregistered type '%s'", name);
    g_hash_table_remove (self->name_to_type, name);

    return TRUE;
}

/*
 * lrg_registry_is_registered:
 * @self: an #LrgRegistry
 * @name: the string name to check
 *
 * Checks if a name is registered in the registry.
 *
 * Returns: %TRUE if the name is registered
 */
gboolean
lrg_registry_is_registered (LrgRegistry *self,
                            const gchar *name)
{
    g_return_val_if_fail (LRG_IS_REGISTRY (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_contains (self->name_to_type, name);
}

/* ==========================================================================
 * Public API - Type Lookup
 * ========================================================================== */

/*
 * lrg_registry_lookup:
 * @self: an #LrgRegistry
 * @name: the string name to look up
 *
 * Looks up a GType by its registered string name.
 *
 * Returns: The registered #GType, or %G_TYPE_INVALID if not found
 */
GType
lrg_registry_lookup (LrgRegistry *self,
                     const gchar *name)
{
    g_return_val_if_fail (LRG_IS_REGISTRY (self), G_TYPE_INVALID);
    g_return_val_if_fail (name != NULL, G_TYPE_INVALID);

    return GPOINTER_TO_SIZE (g_hash_table_lookup (self->name_to_type, name));
}

/*
 * lrg_registry_lookup_name:
 * @self: an #LrgRegistry
 * @type: the #GType to look up
 *
 * Looks up the lexicographically smallest registered name for a GType.
 * The borrowed name remains valid until that registration is mutated or
 * the registry is cleared or destroyed.
 *
 * Returns: (nullable) (transfer none): The registered name, or %NULL
 */
const gchar *
lrg_registry_lookup_name (LrgRegistry *self,
                          GType        type)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    const gchar *name = NULL;

    g_return_val_if_fail (LRG_IS_REGISTRY (self), NULL);
    g_return_val_if_fail (type != G_TYPE_INVALID, NULL);

    /* One owning table avoids stale pointers when aliases are replaced. */
    g_hash_table_iter_init (&iter, self->name_to_type);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        if (GPOINTER_TO_SIZE (value) == type &&
            (name == NULL || strcmp (key, name) < 0))
            name = key;
    }
    return name;
}

/* ==========================================================================
 * Public API - Object Creation
 * ========================================================================== */

/*
 * lrg_registry_create:
 * @self: an #LrgRegistry
 * @name: the registered type name
 * @first_property_name: (nullable): name of first property to set
 * @...: value of first property, then additional name/value pairs, NULL-terminated
 *
 * Creates a new object of the type registered under @name.
 *
 * Returns: (transfer full) (nullable): A new #GObject instance, or %NULL
 */
GObject *
lrg_registry_create (LrgRegistry *self,
                     const gchar *name,
                     const gchar *first_property_name,
                     ...)
{
    GType    type;
    GObject *object;
    va_list  args;

    g_return_val_if_fail (LRG_IS_REGISTRY (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    type = lrg_registry_lookup (self, name);
    if (type == G_TYPE_INVALID)
    {
        lrg_warning (LRG_LOG_DOMAIN_CORE,
                     "Cannot create object: type '%s' not registered",
                     name);
        return NULL;
    }

    /* Use g_object_new_valist for varargs construction */
    va_start (args, first_property_name);
    object = g_object_new_valist (type, first_property_name, args);
    va_end (args);

    return object;
}

/*
 * lrg_registry_create_with_properties:
 * @self: an #LrgRegistry
 * @name: the registered type name
 * @n_properties: number of properties
 * @names: (array length=n_properties): property names
 * @values: (array length=n_properties): property values
 *
 * Creates a new object with the specified properties.
 *
 * Returns: (transfer full) (nullable): A new #GObject instance, or %NULL
 */
GObject *
lrg_registry_create_with_properties (LrgRegistry  *self,
                                     const gchar  *name,
                                     guint         n_properties,
                                     const gchar **names,
                                     const GValue *values)
{
    GType type;

    g_return_val_if_fail (LRG_IS_REGISTRY (self), NULL);
    g_return_val_if_fail (name != NULL, NULL);

    type = lrg_registry_lookup (self, name);
    if (type == G_TYPE_INVALID)
    {
        lrg_warning (LRG_LOG_DOMAIN_CORE,
                     "Cannot create object: type '%s' not registered",
                     name);
        return NULL;
    }

    return g_object_new_with_properties (type, n_properties, names, values);
}

/* ==========================================================================
 * Public API - Enumeration
 * ========================================================================== */

/*
 * lrg_registry_get_names:
 * @self: an #LrgRegistry
 *
 * Gets all registered type names.
 *
 * Returns: (transfer container) (element-type utf8): A list of registered names
 */
GList *
lrg_registry_get_names (LrgRegistry *self)
{
    g_return_val_if_fail (LRG_IS_REGISTRY (self), NULL);

    return g_hash_table_get_keys (self->name_to_type);
}

/*
 * lrg_registry_get_count:
 * @self: an #LrgRegistry
 *
 * Gets the number of registered types.
 *
 * Returns: The number of registered types
 */
guint
lrg_registry_get_count (LrgRegistry *self)
{
    g_return_val_if_fail (LRG_IS_REGISTRY (self), 0);

    return g_hash_table_size (self->name_to_type);
}

/*
 * lrg_registry_foreach:
 * @self: an #LrgRegistry
 * @func: (scope call): the function to call for each entry
 * @user_data: (closure): user data to pass to @func
 *
 * Calls @func for each registered type in the registry.
 */
void
lrg_registry_foreach (LrgRegistry            *self,
                      LrgRegistryForeachFunc  func,
                      gpointer                user_data)
{
    GHashTableIter iter;
    gpointer       key;
    gpointer       value;

    g_return_if_fail (LRG_IS_REGISTRY (self));
    g_return_if_fail (func != NULL);

    g_hash_table_iter_init (&iter, self->name_to_type);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        func ((const gchar *)key,
              GPOINTER_TO_SIZE (value),
              user_data);
    }
}

/* ==========================================================================
 * Public API - Bulk Operations
 * ========================================================================== */

/*
 * LrgRegistryBuiltin:
 *
 * One row of the built-in catalog: the data-file name and a function
 * returning its #GType. Storing the get_type function (rather than the
 * GType itself) keeps the table a static constant and defers type
 * registration until the catalog is actually installed.
 */
typedef struct
{
    const gchar *name;
    GType      (*get_type) (void);
} LrgRegistryBuiltin;

/*
 * builtin_catalog:
 *
 * The engine's concrete, headlessly constructible public types, keyed by
 * the kebab-case names YAML definitions and MCP spawning use. Names follow
 * the type's C prefix without "lrg_"; ECS components drop the "-component"
 * suffix ("transform", not "transform-component"). Abstract bases, device
 * resources (windows, textures, audio devices) and singletons are left out,
 * as is LrgScriptComponent: scripts attach through script bindings, and a
 * bare script component would only clutter the editor's component list.
 * Keep docs/modules/core/registry.org in sync when adding rows.
 */
static const LrgRegistryBuiltin builtin_catalog[] = {
    /* ECS: objects, worlds and components */
    { "game-object",               lrg_game_object_get_type },
    { "world",                     lrg_world_get_type },
    { "transform",                 lrg_transform_component_get_type },
    { "sprite",                    lrg_sprite_component_get_type },
    { "collider",                  lrg_collider_component_get_type },
    { "animator",                  lrg_animator_component_get_type },

    /* 3D (LrgWorld3D) components: same suffix rule as the ECS ones */
    { "mesh-renderer",             lrg_mesh_renderer_component_get_type },
    { "skinned-model",             lrg_skinned_model_component_get_type },
    { "light",                     lrg_light_component_get_type },
    { "camera",                    lrg_camera_component_get_type },
    { "audio-source",              lrg_audio_source_component_get_type },
    { "audio-listener",            lrg_audio_listener_component_get_type },
    { "billboard",                 lrg_billboard_component_get_type },
    { "tilemap",                   lrg_tilemap_component_get_type },
    { "spawn",                     lrg_spawn_component_get_type },
    { "trigger",                   lrg_trigger_component_get_type },
    { "decal",                     lrg_decal_component_get_type },
    { "lod",                       lrg_lod_component_get_type },
    { "reflection-probe",          lrg_reflection_probe_component_get_type },
    { "sequence",                  lrg_sequence_component_get_type },
    { "skeleton2d",                lrg_skeleton2d_component_get_type },

    /* Scenes and 3D levels */
    { "scene",                     lrg_scene_get_type },
    { "scene-entity",              lrg_scene_entity_get_type },
    { "scene-object",              lrg_scene_object_get_type },
    { "level3d",                   lrg_level3d_get_type },

    /* Physics */
    { "physics-world",             lrg_physics_world_get_type },
    { "rigid-body",                lrg_rigid_body_get_type },
    { "character-controller",      lrg_character_controller_get_type },
    { "wall-set",                  lrg_wall_set_get_type },
    { "collision-mesh",            lrg_collision_mesh_get_type },
    { "rope",                      lrg_rope_get_type },

    /* 2D triggers */
    { "trigger-rect",              lrg_trigger_rect_get_type },
    { "trigger-circle",            lrg_trigger_circle_get_type },
    { "trigger-polygon",           lrg_trigger_polygon_get_type },
    { "trigger-manager",           lrg_trigger_manager_get_type },

    /* Lights */
    { "point-light2d",             lrg_point_light2d_get_type },
    { "spot-light2d",              lrg_spot_light2d_get_type },
    { "directional-light2d",       lrg_directional_light2d_get_type },
    { "light-set",                 lrg_light_set_get_type },

    /* Particles */
    { "particle-system",           lrg_particle_system_get_type },
    { "particle-emitter",          lrg_particle_emitter_get_type },
    { "particle-force-gravity",    lrg_particle_force_gravity_get_type },
    { "particle-force-wind",       lrg_particle_force_wind_get_type },
    { "particle-force-attractor",  lrg_particle_force_attractor_get_type },
    { "particle-force-turbulence", lrg_particle_force_turbulence_get_type },

    /* Weather */
    { "weather",                   lrg_weather_get_type },
    { "rain",                      lrg_rain_get_type },
    { "snow",                      lrg_snow_get_type },
    { "fog",                       lrg_fog_get_type },
    { "lightning",                 lrg_lightning_get_type },
    { "day-night-cycle",           lrg_day_night_cycle_get_type },

    /* Shapes */
    { "rectangle2d",               lrg_rectangle2d_get_type },
    { "circle2d",                  lrg_circle2d_get_type },
    { "text2d",                    lrg_text2d_get_type },
    { "cube3d",                    lrg_cube3d_get_type },
    { "sphere3d",                  lrg_sphere3d_get_type },
    { "plane3d",                   lrg_plane3d_get_type },
    { "cylinder3d",                lrg_cylinder3d_get_type },
    { "cone3d",                    lrg_cone3d_get_type },
    { "torus3d",                   lrg_torus3d_get_type },
    { "icosphere3d",               lrg_icosphere3d_get_type },
    { "circle3d",                  lrg_circle3d_get_type },
    { "line3d",                    lrg_line3d_get_type },
    { "grid3d",                    lrg_grid3d_get_type },

    /* Gameplay definitions and containers */
    { "item-def",                  lrg_item_def_get_type },
    { "inventory",                 lrg_inventory_get_type },
    { "equipment",                 lrg_equipment_get_type },
    { "quest-def",                 lrg_quest_def_get_type },
    { "dialog-tree",               lrg_dialog_tree_get_type },
    { "dialog-node",               lrg_dialog_node_get_type },

    /* AI: behavior trees */
    { "behavior-tree",             lrg_behavior_tree_get_type },
    { "blackboard",                lrg_blackboard_get_type },
    { "bt-sequence",               lrg_bt_sequence_get_type },
    { "bt-selector",               lrg_bt_selector_get_type },
    { "bt-parallel",               lrg_bt_parallel_get_type },
    { "bt-inverter",               lrg_bt_inverter_get_type },
    { "bt-repeater",               lrg_bt_repeater_get_type },
    { "bt-succeeder",              lrg_bt_succeeder_get_type },
    { "bt-failer",                 lrg_bt_failer_get_type },
    { "bt-wait",                   lrg_bt_wait_get_type },
};

/*
 * lrg_registry_register_builtin:
 * @self: an #LrgRegistry
 *
 * Installs the built-in catalog (see builtin_catalog above). A name that
 * is already registered, by the application, a mod or an earlier call,
 * is left untouched, so the call is idempotent and never overrides an
 * application mapping.
 *
 * This is called automatically during engine startup but can
 * also be called manually for testing purposes.
 */
void
lrg_registry_register_builtin (LrgRegistry *self)
{
    guint added;
    guint i;

    g_return_if_fail (LRG_IS_REGISTRY (self));

    /* Fill only the gaps; existing names win. */
    added = 0;
    for (i = 0; i < G_N_ELEMENTS (builtin_catalog); i++)
    {
        if (lrg_registry_is_registered (self, builtin_catalog[i].name))
            continue;
        lrg_registry_register (self, builtin_catalog[i].name,
                               builtin_catalog[i].get_type ());
        added++;
    }

    lrg_debug (LRG_LOG_DOMAIN_CORE,
               "Built-in types registered (added: %u, total: %u)",
               added, lrg_registry_get_count (self));
}

/*
 * lrg_registry_clear:
 * @self: an #LrgRegistry
 *
 * Removes all type registrations from the registry.
 */
void
lrg_registry_clear (LrgRegistry *self)
{
    g_return_if_fail (LRG_IS_REGISTRY (self));

    g_hash_table_remove_all (self->name_to_type);

    lrg_debug (LRG_LOG_DOMAIN_CORE, "Registry cleared");
}
