/* lrg-potion-instance.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPotionInstance - Runtime instance of a potion.
 *
 * Each LrgPotionInstance represents a consumable potion in a player's
 * potion slots, providing access to its definition and use functionality.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-potion-def.h"

G_BEGIN_DECLS

#define LRG_TYPE_POTION_INSTANCE (lrg_potion_instance_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPotionInstance, lrg_potion_instance, LRG, POTION_INSTANCE, GObject)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_potion_instance_new:
 * @def: the potion definition
 *
 * Creates a new potion instance from a definition.
 *
 * Returns: (transfer full): a new #LrgPotionInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPotionInstance * lrg_potion_instance_new (LrgPotionDef *def);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_potion_instance_get_def:
 * @self: a #LrgPotionInstance
 *
 * Gets the potion's definition.
 *
 * Returns: (transfer none): the definition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPotionDef * lrg_potion_instance_get_def (LrgPotionInstance *self);

/**
 * lrg_potion_instance_get_id:
 * @self: a #LrgPotionInstance
 *
 * Gets the potion's ID (from definition).
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_potion_instance_get_id (LrgPotionInstance *self);

/**
 * lrg_potion_instance_get_name:
 * @self: a #LrgPotionInstance
 *
 * Gets the potion's name (from definition).
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_potion_instance_get_name (LrgPotionInstance *self);

/* ==========================================================================
 * Actions
 * ========================================================================== */

/**
 * lrg_potion_instance_can_use:
 * @self: a #LrgPotionInstance
 * @context: (nullable): game/combat context
 *
 * Checks if the potion can be used.
 *
 * Returns: %TRUE if can use
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_potion_instance_can_use (LrgPotionInstance *self,
                                       gpointer           context);

/**
 * lrg_potion_instance_use:
 * @self: a #LrgPotionInstance
 * @context: (nullable): game/combat context
 * @target: (nullable): target for targeted potions
 *
 * Uses the potion, consuming it.
 *
 * Returns: %TRUE if used successfully
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_potion_instance_use (LrgPotionInstance *self,
                                   gpointer           context,
                                   gpointer           target);

/**
 * lrg_potion_instance_discard:
 * @self: a #LrgPotionInstance
 *
 * Discards the potion without using it.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_potion_instance_discard (LrgPotionInstance *self);

/**
 * lrg_potion_instance_is_consumed:
 * @self: a #LrgPotionInstance
 *
 * Checks if the potion has been consumed.
 *
 * Returns: %TRUE if consumed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_potion_instance_is_consumed (LrgPotionInstance *self);

G_END_DECLS
