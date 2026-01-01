/* lrg-relic-registry.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRelicRegistry - Registry for relic definitions.
 *
 * The relic registry manages all registered relic definitions.
 * Games register their relics at startup, and the registry provides
 * lookup and iteration facilities.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-relic-def.h"
#include "lrg-relic-instance.h"

G_BEGIN_DECLS

#define LRG_TYPE_RELIC_REGISTRY (lrg_relic_registry_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRelicRegistry, lrg_relic_registry, LRG, RELIC_REGISTRY, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_relic_registry_get_default:
 *
 * Gets the default relic registry singleton.
 *
 * Returns: (transfer none): the default #LrgRelicRegistry
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRelicRegistry * lrg_relic_registry_get_default (void);

/* ==========================================================================
 * Registration
 * ========================================================================== */

/**
 * lrg_relic_registry_register:
 * @self: a #LrgRelicRegistry
 * @def: (transfer none): the relic definition to register
 *
 * Registers a relic definition.
 *
 * Returns: %TRUE if registered successfully, %FALSE if ID already exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_relic_registry_register (LrgRelicRegistry *self,
                                       LrgRelicDef      *def);

/**
 * lrg_relic_registry_unregister:
 * @self: a #LrgRelicRegistry
 * @id: the relic ID to unregister
 *
 * Unregisters a relic definition.
 *
 * Returns: %TRUE if unregistered, %FALSE if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_relic_registry_unregister (LrgRelicRegistry *self,
                                         const gchar      *id);

/**
 * lrg_relic_registry_is_registered:
 * @self: a #LrgRelicRegistry
 * @id: the relic ID to check
 *
 * Checks if a relic is registered.
 *
 * Returns: %TRUE if registered
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_relic_registry_is_registered (LrgRelicRegistry *self,
                                            const gchar      *id);

/* ==========================================================================
 * Lookup
 * ========================================================================== */

/**
 * lrg_relic_registry_lookup:
 * @self: a #LrgRelicRegistry
 * @id: the relic ID
 *
 * Looks up a relic definition by ID.
 *
 * Returns: (transfer none) (nullable): the relic definition, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRelicDef * lrg_relic_registry_lookup (LrgRelicRegistry *self,
                                          const gchar      *id);

/**
 * lrg_relic_registry_create_instance:
 * @self: a #LrgRelicRegistry
 * @id: the relic ID
 *
 * Creates a new relic instance from a registered definition.
 *
 * Returns: (transfer full) (nullable): a new instance, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRelicInstance * lrg_relic_registry_create_instance (LrgRelicRegistry *self,
                                                        const gchar      *id);

/**
 * lrg_relic_registry_get_all:
 * @self: a #LrgRelicRegistry
 *
 * Gets all registered relic definitions.
 *
 * Returns: (transfer container) (element-type LrgRelicDef): list of definitions
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_relic_registry_get_all (LrgRelicRegistry *self);

/**
 * lrg_relic_registry_get_by_rarity:
 * @self: a #LrgRelicRegistry
 * @rarity: the rarity to filter by
 *
 * Gets all relics of a specific rarity.
 *
 * Returns: (transfer container) (element-type LrgRelicDef): list of relics
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_relic_registry_get_by_rarity (LrgRelicRegistry *self,
                                           LrgRelicRarity    rarity);

/**
 * lrg_relic_registry_get_count:
 * @self: a #LrgRelicRegistry
 *
 * Gets the number of registered relics.
 *
 * Returns: the count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_relic_registry_get_count (LrgRelicRegistry *self);

/* ==========================================================================
 * Utility
 * ========================================================================== */

/**
 * lrg_relic_registry_clear:
 * @self: a #LrgRelicRegistry
 *
 * Removes all registered relics.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_registry_clear (LrgRelicRegistry *self);

/**
 * lrg_relic_registry_foreach:
 * @self: a #LrgRelicRegistry
 * @func: (scope call): callback for each relic
 * @user_data: (closure): data to pass to callback
 *
 * Calls a function for each registered relic.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_relic_registry_foreach (LrgRelicRegistry *self,
                                  GFunc             func,
                                  gpointer          user_data);

G_END_DECLS
