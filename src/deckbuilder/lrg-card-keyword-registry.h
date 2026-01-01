/* lrg-card-keyword-registry.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardKeywordRegistry - Registry for custom keywords.
 *
 * The keyword registry manages custom keyword definitions added by mods.
 * Built-in keywords (LrgCardKeyword flags) are handled separately.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-card-keyword-def.h"

G_BEGIN_DECLS

#define LRG_TYPE_CARD_KEYWORD_REGISTRY (lrg_card_keyword_registry_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCardKeywordRegistry, lrg_card_keyword_registry, LRG, CARD_KEYWORD_REGISTRY, GObject)

/* ==========================================================================
 * Singleton Access
 * ========================================================================== */

/**
 * lrg_card_keyword_registry_get_default:
 *
 * Gets the default keyword registry singleton.
 *
 * Returns: (transfer none): the default #LrgCardKeywordRegistry
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardKeywordRegistry * lrg_card_keyword_registry_get_default (void);

/* ==========================================================================
 * Registration
 * ========================================================================== */

/**
 * lrg_card_keyword_registry_register:
 * @self: a #LrgCardKeywordRegistry
 * @keyword: (transfer none): the keyword definition to register
 *
 * Registers a custom keyword definition.
 *
 * Returns: %TRUE if registered successfully, %FALSE if ID already exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_keyword_registry_register (LrgCardKeywordRegistry *self,
                                             LrgCardKeywordDef      *keyword);

/**
 * lrg_card_keyword_registry_unregister:
 * @self: a #LrgCardKeywordRegistry
 * @id: the keyword ID to unregister
 *
 * Unregisters a custom keyword definition.
 *
 * Returns: %TRUE if unregistered, %FALSE if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_keyword_registry_unregister (LrgCardKeywordRegistry *self,
                                               const gchar            *id);

/**
 * lrg_card_keyword_registry_is_registered:
 * @self: a #LrgCardKeywordRegistry
 * @id: the keyword ID to check
 *
 * Checks if a custom keyword is registered.
 *
 * Returns: %TRUE if registered
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_keyword_registry_is_registered (LrgCardKeywordRegistry *self,
                                                  const gchar            *id);

/* ==========================================================================
 * Lookup
 * ========================================================================== */

/**
 * lrg_card_keyword_registry_lookup:
 * @self: a #LrgCardKeywordRegistry
 * @id: the keyword ID
 *
 * Looks up a custom keyword definition by ID.
 *
 * Returns: (transfer none) (nullable): the keyword definition, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardKeywordDef * lrg_card_keyword_registry_lookup (LrgCardKeywordRegistry *self,
                                                      const gchar            *id);

/**
 * lrg_card_keyword_registry_get_all:
 * @self: a #LrgCardKeywordRegistry
 *
 * Gets all registered custom keyword definitions.
 *
 * Returns: (transfer container) (element-type LrgCardKeywordDef): list of keywords
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_card_keyword_registry_get_all (LrgCardKeywordRegistry *self);

/**
 * lrg_card_keyword_registry_get_count:
 * @self: a #LrgCardKeywordRegistry
 *
 * Gets the number of registered custom keywords.
 *
 * Returns: the count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_card_keyword_registry_get_count (LrgCardKeywordRegistry *self);

/* ==========================================================================
 * Utility
 * ========================================================================== */

/**
 * lrg_card_keyword_registry_clear:
 * @self: a #LrgCardKeywordRegistry
 *
 * Removes all registered custom keywords.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_registry_clear (LrgCardKeywordRegistry *self);

/**
 * lrg_card_keyword_registry_foreach:
 * @self: a #LrgCardKeywordRegistry
 * @func: (scope call): callback for each keyword
 * @user_data: (closure): data to pass to callback
 *
 * Calls a function for each registered keyword.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_registry_foreach (LrgCardKeywordRegistry *self,
                                        GFunc                   func,
                                        gpointer                user_data);

G_END_DECLS
