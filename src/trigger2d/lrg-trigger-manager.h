/* lrg-trigger-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manager for 2D triggers.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include "lrg-trigger2d.h"
#include "lrg-trigger-event.h"

G_BEGIN_DECLS

#define LRG_TYPE_TRIGGER_MANAGER (lrg_trigger_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTriggerManager, lrg_trigger_manager, LRG, TRIGGER_MANAGER, GObject)

/**
 * LrgTriggerCheckFunc:
 * @trigger: The trigger being tested
 * @entity: The entity to test against
 * @x: X position of the entity
 * @y: Y position of the entity
 * @user_data: User-provided data
 *
 * Callback function used to test if an entity should be processed
 * for trigger detection.
 *
 * Returns: %TRUE if the entity should trigger events
 *
 * Since: 1.0
 */
typedef gboolean (*LrgTriggerCheckFunc) (LrgTrigger2D *trigger,
                                         gpointer      entity,
                                         gfloat        x,
                                         gfloat        y,
                                         gpointer      user_data);

/**
 * lrg_trigger_manager_new:
 *
 * Creates a new trigger manager.
 *
 * Returns: (transfer full): A new #LrgTriggerManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTriggerManager * lrg_trigger_manager_new             (void);

/* Trigger registration */

/**
 * lrg_trigger_manager_add_trigger:
 * @self: A #LrgTriggerManager
 * @trigger: The trigger to add
 *
 * Adds a trigger to the manager.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_manager_add_trigger     (LrgTriggerManager *self,
                                                         LrgTrigger2D      *trigger);

/**
 * lrg_trigger_manager_remove_trigger:
 * @self: A #LrgTriggerManager
 * @trigger: The trigger to remove
 *
 * Removes a trigger from the manager.
 *
 * Returns: %TRUE if the trigger was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger_manager_remove_trigger  (LrgTriggerManager *self,
                                                         LrgTrigger2D      *trigger);

/**
 * lrg_trigger_manager_remove_trigger_by_id:
 * @self: A #LrgTriggerManager
 * @id: The trigger ID to remove
 *
 * Removes a trigger by its ID.
 *
 * Returns: %TRUE if a trigger was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger_manager_remove_trigger_by_id (LrgTriggerManager *self,
                                                              const gchar       *id);

/**
 * lrg_trigger_manager_get_trigger:
 * @self: A #LrgTriggerManager
 * @id: The trigger ID to find
 *
 * Gets a trigger by its ID.
 *
 * Returns: (transfer none) (nullable): The trigger, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTrigger2D *      lrg_trigger_manager_get_trigger     (LrgTriggerManager *self,
                                                         const gchar       *id);

/**
 * lrg_trigger_manager_get_triggers:
 * @self: A #LrgTriggerManager
 *
 * Gets all registered triggers.
 *
 * Returns: (transfer none) (element-type LrgTrigger2D): Array of triggers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_trigger_manager_get_triggers    (LrgTriggerManager *self);

/**
 * lrg_trigger_manager_get_trigger_count:
 * @self: A #LrgTriggerManager
 *
 * Gets the number of registered triggers.
 *
 * Returns: The trigger count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_trigger_manager_get_trigger_count (LrgTriggerManager *self);

/**
 * lrg_trigger_manager_clear:
 * @self: A #LrgTriggerManager
 *
 * Removes all triggers from the manager.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_manager_clear           (LrgTriggerManager *self);

/* Entity tracking */

/**
 * lrg_trigger_manager_register_entity:
 * @self: A #LrgTriggerManager
 * @entity: The entity to track
 * @collision_layer: The collision layer of the entity
 *
 * Registers an entity for trigger detection.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_manager_register_entity (LrgTriggerManager *self,
                                                         gpointer           entity,
                                                         guint32            collision_layer);

/**
 * lrg_trigger_manager_unregister_entity:
 * @self: A #LrgTriggerManager
 * @entity: The entity to unregister
 *
 * Unregisters an entity from trigger detection.
 * This will emit exit events for any triggers the entity was inside.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_manager_unregister_entity (LrgTriggerManager *self,
                                                           gpointer           entity);

/**
 * lrg_trigger_manager_set_entity_position:
 * @self: A #LrgTriggerManager
 * @entity: The entity
 * @x: New X position
 * @y: New Y position
 *
 * Updates an entity's position for trigger detection.
 * Call this when the entity moves.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_manager_set_entity_position (LrgTriggerManager *self,
                                                             gpointer           entity,
                                                             gfloat             x,
                                                             gfloat             y);

/**
 * lrg_trigger_manager_set_entity_layer:
 * @self: A #LrgTriggerManager
 * @entity: The entity
 * @collision_layer: New collision layer
 *
 * Updates an entity's collision layer.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_manager_set_entity_layer (LrgTriggerManager *self,
                                                          gpointer           entity,
                                                          guint32            collision_layer);

/* Processing */

/**
 * lrg_trigger_manager_update:
 * @self: A #LrgTriggerManager
 * @delta_time: Time elapsed since last update
 *
 * Updates all triggers and processes entity positions.
 * This should be called once per frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_manager_update          (LrgTriggerManager *self,
                                                         gfloat             delta_time);

/**
 * lrg_trigger_manager_check_point:
 * @self: A #LrgTriggerManager
 * @x: X coordinate to check
 * @y: Y coordinate to check
 * @collision_layer: Collision layer to check against
 *
 * Checks which triggers contain the given point.
 *
 * Returns: (transfer full) (element-type LrgTrigger2D): Array of triggers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_trigger_manager_check_point     (LrgTriggerManager *self,
                                                         gfloat             x,
                                                         gfloat             y,
                                                         guint32            collision_layer);

/**
 * lrg_trigger_manager_check_bounds:
 * @self: A #LrgTriggerManager
 * @x: X coordinate of bounds
 * @y: Y coordinate of bounds
 * @width: Width of bounds
 * @height: Height of bounds
 * @collision_layer: Collision layer to check against
 *
 * Gets triggers that might overlap with the given bounds.
 * Uses AABB intersection for broad phase.
 *
 * Returns: (transfer full) (element-type LrgTrigger2D): Array of triggers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_trigger_manager_check_bounds    (LrgTriggerManager *self,
                                                         gfloat             x,
                                                         gfloat             y,
                                                         gfloat             width,
                                                         gfloat             height,
                                                         guint32            collision_layer);

/* Queries */

/**
 * lrg_trigger_manager_get_entities_in_trigger:
 * @self: A #LrgTriggerManager
 * @trigger: The trigger to check
 *
 * Gets all entities currently inside the trigger.
 *
 * Returns: (transfer full) (element-type gpointer): Array of entity pointers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_trigger_manager_get_entities_in_trigger (LrgTriggerManager *self,
                                                                 LrgTrigger2D      *trigger);

/**
 * lrg_trigger_manager_get_triggers_containing_entity:
 * @self: A #LrgTriggerManager
 * @entity: The entity to check
 *
 * Gets all triggers that currently contain the entity.
 *
 * Returns: (transfer full) (element-type LrgTrigger2D): Array of triggers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_trigger_manager_get_triggers_containing_entity (LrgTriggerManager *self,
                                                                        gpointer           entity);

/* Debug */

/**
 * lrg_trigger_manager_set_debug_enabled:
 * @self: A #LrgTriggerManager
 * @enabled: Whether debug mode is enabled
 *
 * Enables or disables debug mode.
 * When enabled, additional logging is performed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_trigger_manager_set_debug_enabled (LrgTriggerManager *self,
                                                           gboolean           enabled);

/**
 * lrg_trigger_manager_is_debug_enabled:
 * @self: A #LrgTriggerManager
 *
 * Checks if debug mode is enabled.
 *
 * Returns: %TRUE if debug mode is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_trigger_manager_is_debug_enabled (LrgTriggerManager *self);

G_END_DECLS
