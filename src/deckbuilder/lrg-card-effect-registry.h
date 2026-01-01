/* lrg-card-effect-registry.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardEffectRegistry - Singleton registry for effect executors.
 *
 * The effect registry manages registered effect executors, mapping
 * effect type strings to their executor implementations. Games
 * register built-in effects at startup and mods can add custom
 * effect types.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-card-effect.h"
#include "lrg-card-effect-executor.h"

G_BEGIN_DECLS

#define LRG_TYPE_CARD_EFFECT_REGISTRY (lrg_card_effect_registry_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCardEffectRegistry, lrg_card_effect_registry,
                      LRG, CARD_EFFECT_REGISTRY, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_card_effect_registry_get_default:
 *
 * Gets the default effect registry singleton.
 *
 * Returns: (transfer none): the default #LrgCardEffectRegistry
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardEffectRegistry * lrg_card_effect_registry_get_default (void);

/* ==========================================================================
 * Registration
 * ========================================================================== */

/**
 * lrg_card_effect_registry_register:
 * @self: a #LrgCardEffectRegistry
 * @executor: (transfer none): the executor to register
 *
 * Registers an effect executor. The executor's effect type is
 * obtained via lrg_card_effect_executor_get_effect_type().
 *
 * If an executor for this effect type already exists, it will
 * be replaced.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_effect_registry_register (LrgCardEffectRegistry *self,
                                        LrgCardEffectExecutor *executor);

/**
 * lrg_card_effect_registry_unregister:
 * @self: a #LrgCardEffectRegistry
 * @effect_type: the effect type to unregister
 *
 * Unregisters the executor for the given effect type.
 *
 * Returns: %TRUE if an executor was removed, %FALSE if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_effect_registry_unregister (LrgCardEffectRegistry *self,
                                              const gchar           *effect_type);

/* ==========================================================================
 * Lookup
 * ========================================================================== */

/**
 * lrg_card_effect_registry_lookup:
 * @self: a #LrgCardEffectRegistry
 * @effect_type: the effect type to look up
 *
 * Looks up the executor for the given effect type.
 *
 * Returns: (transfer none) (nullable): the executor, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardEffectExecutor * lrg_card_effect_registry_lookup (LrgCardEffectRegistry *self,
                                                         const gchar           *effect_type);

/**
 * lrg_card_effect_registry_has_executor:
 * @self: a #LrgCardEffectRegistry
 * @effect_type: the effect type to check
 *
 * Checks if an executor is registered for the given effect type.
 *
 * Returns: %TRUE if an executor is registered
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_effect_registry_has_executor (LrgCardEffectRegistry *self,
                                                const gchar           *effect_type);

/* ==========================================================================
 * Enumeration
 * ========================================================================== */

/**
 * lrg_card_effect_registry_get_effect_types:
 * @self: a #LrgCardEffectRegistry
 *
 * Gets a list of all registered effect types.
 *
 * Returns: (transfer full) (element-type utf8): array of effect type strings
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_card_effect_registry_get_effect_types (LrgCardEffectRegistry *self);

/**
 * lrg_card_effect_registry_get_executor_count:
 * @self: a #LrgCardEffectRegistry
 *
 * Gets the number of registered executors.
 *
 * Returns: the number of registered executors
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_card_effect_registry_get_executor_count (LrgCardEffectRegistry *self);

/* ==========================================================================
 * Effect Execution
 * ========================================================================== */

/**
 * lrg_card_effect_registry_execute:
 * @self: a #LrgCardEffectRegistry
 * @effect: the effect to execute
 * @context: the combat context
 * @source: (nullable): the source combatant
 * @target: (nullable): the target combatant
 * @error: (optional): return location for a #GError
 *
 * Convenience method to look up the executor for an effect and
 * execute it in one call.
 *
 * Returns: %TRUE on success, %FALSE with @error set on failure
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_effect_registry_execute (LrgCardEffectRegistry  *self,
                                           LrgCardEffect          *effect,
                                           gpointer                context,
                                           gpointer                source,
                                           gpointer                target,
                                           GError                **error);

G_END_DECLS
