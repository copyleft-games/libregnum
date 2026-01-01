/* lrg-status-effect-registry.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgStatusEffectRegistry - Registry for status effect definitions.
 *
 * The status effect registry manages all registered status effect definitions.
 * Games register their status effects at startup, and the registry provides
 * lookup and iteration facilities.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-status-effect-def.h"
#include "lrg-status-effect-instance.h"

G_BEGIN_DECLS

#define LRG_TYPE_STATUS_EFFECT_REGISTRY (lrg_status_effect_registry_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgStatusEffectRegistry, lrg_status_effect_registry, LRG, STATUS_EFFECT_REGISTRY, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_status_effect_registry_get_default:
 *
 * Gets the default status effect registry singleton.
 *
 * Returns: (transfer none): the default #LrgStatusEffectRegistry
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStatusEffectRegistry * lrg_status_effect_registry_get_default (void);

/* ==========================================================================
 * Registration
 * ========================================================================== */

/**
 * lrg_status_effect_registry_register:
 * @self: a #LrgStatusEffectRegistry
 * @def: (transfer none): the status effect definition to register
 *
 * Registers a status effect definition.
 *
 * Returns: %TRUE if registered successfully, %FALSE if ID already exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_registry_register (LrgStatusEffectRegistry *self,
                                               LrgStatusEffectDef      *def);

/**
 * lrg_status_effect_registry_unregister:
 * @self: a #LrgStatusEffectRegistry
 * @id: the status effect ID to unregister
 *
 * Unregisters a status effect definition.
 *
 * Returns: %TRUE if unregistered, %FALSE if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_registry_unregister (LrgStatusEffectRegistry *self,
                                                 const gchar             *id);

/**
 * lrg_status_effect_registry_is_registered:
 * @self: a #LrgStatusEffectRegistry
 * @id: the status effect ID to check
 *
 * Checks if a status effect is registered.
 *
 * Returns: %TRUE if registered
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_status_effect_registry_is_registered (LrgStatusEffectRegistry *self,
                                                    const gchar             *id);

/* ==========================================================================
 * Lookup
 * ========================================================================== */

/**
 * lrg_status_effect_registry_lookup:
 * @self: a #LrgStatusEffectRegistry
 * @id: the status effect ID
 *
 * Looks up a status effect definition by ID.
 *
 * Returns: (transfer none) (nullable): the status effect definition, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStatusEffectDef * lrg_status_effect_registry_lookup (LrgStatusEffectRegistry *self,
                                                         const gchar             *id);

/**
 * lrg_status_effect_registry_create_instance:
 * @self: a #LrgStatusEffectRegistry
 * @id: the status effect ID
 * @stacks: initial stack count
 *
 * Creates a new status effect instance from a registered definition.
 *
 * Returns: (transfer full) (nullable): a new instance, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgStatusEffectInstance * lrg_status_effect_registry_create_instance (LrgStatusEffectRegistry *self,
                                                                       const gchar             *id,
                                                                       gint                     stacks);

/**
 * lrg_status_effect_registry_get_all:
 * @self: a #LrgStatusEffectRegistry
 *
 * Gets all registered status effect definitions.
 *
 * Returns: (transfer container) (element-type LrgStatusEffectDef): list of definitions
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_status_effect_registry_get_all (LrgStatusEffectRegistry *self);

/**
 * lrg_status_effect_registry_get_buffs:
 * @self: a #LrgStatusEffectRegistry
 *
 * Gets all registered buff definitions.
 *
 * Returns: (transfer container) (element-type LrgStatusEffectDef): list of buffs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_status_effect_registry_get_buffs (LrgStatusEffectRegistry *self);

/**
 * lrg_status_effect_registry_get_debuffs:
 * @self: a #LrgStatusEffectRegistry
 *
 * Gets all registered debuff definitions.
 *
 * Returns: (transfer container) (element-type LrgStatusEffectDef): list of debuffs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_status_effect_registry_get_debuffs (LrgStatusEffectRegistry *self);

/**
 * lrg_status_effect_registry_get_count:
 * @self: a #LrgStatusEffectRegistry
 *
 * Gets the number of registered status effects.
 *
 * Returns: the count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_status_effect_registry_get_count (LrgStatusEffectRegistry *self);

/* ==========================================================================
 * Utility
 * ========================================================================== */

/**
 * lrg_status_effect_registry_clear:
 * @self: a #LrgStatusEffectRegistry
 *
 * Removes all registered status effects.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_registry_clear (LrgStatusEffectRegistry *self);

/**
 * lrg_status_effect_registry_foreach:
 * @self: a #LrgStatusEffectRegistry
 * @func: (scope call): callback for each status effect
 * @user_data: (closure): data to pass to callback
 *
 * Calls a function for each registered status effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_status_effect_registry_foreach (LrgStatusEffectRegistry *self,
                                          GFunc                    func,
                                          gpointer                 user_data);

G_END_DECLS
