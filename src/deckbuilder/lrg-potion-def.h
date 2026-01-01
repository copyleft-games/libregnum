/* lrg-potion-def.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPotionDef - Base class for potion definitions.
 *
 * Potions are consumable items that provide a one-time effect.
 * They can be used during combat (or outside in some cases).
 *
 * This is a derivable type - subclass it to create custom potions
 * with specialized behavior.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LrgPotionRarity:
 * @LRG_POTION_RARITY_COMMON: Common potion
 * @LRG_POTION_RARITY_UNCOMMON: Uncommon potion
 * @LRG_POTION_RARITY_RARE: Rare potion
 *
 * Potion rarity tiers.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_POTION_RARITY_COMMON,
    LRG_POTION_RARITY_UNCOMMON,
    LRG_POTION_RARITY_RARE
} LrgPotionRarity;

/**
 * LrgPotionTarget:
 * @LRG_POTION_TARGET_NONE: No target required
 * @LRG_POTION_TARGET_SELF: Targets self
 * @LRG_POTION_TARGET_SINGLE_ENEMY: Targets single enemy
 * @LRG_POTION_TARGET_ALL_ENEMIES: Targets all enemies
 *
 * Potion targeting types.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_POTION_TARGET_NONE,
    LRG_POTION_TARGET_SELF,
    LRG_POTION_TARGET_SINGLE_ENEMY,
    LRG_POTION_TARGET_ALL_ENEMIES
} LrgPotionTarget;

#define LRG_TYPE_POTION_DEF (lrg_potion_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgPotionDef, lrg_potion_def, LRG, POTION_DEF, GObject)

/**
 * LrgPotionDefClass:
 * @parent_class: The parent class
 * @can_use: Check if potion can be used
 * @on_use: Called when the potion is used
 * @get_tooltip: Get tooltip text
 *
 * The virtual function table for #LrgPotionDef.
 *
 * Since: 1.0
 */
struct _LrgPotionDefClass
{
    GObjectClass parent_class;

    /* Virtual methods */
    gboolean (* can_use)     (LrgPotionDef *self,
                              gpointer      context);
    void     (* on_use)      (LrgPotionDef *self,
                              gpointer      context,
                              gpointer      target);
    gchar *  (* get_tooltip) (LrgPotionDef *self,
                              gpointer      context);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_potion_def_new:
 * @id: unique identifier
 * @name: display name
 *
 * Creates a new potion definition.
 *
 * Returns: (transfer full): a new #LrgPotionDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPotionDef * lrg_potion_def_new (const gchar *id,
                                    const gchar *name);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_potion_def_get_id:
 * @self: a #LrgPotionDef
 *
 * Gets the potion's unique identifier.
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_potion_def_get_id (LrgPotionDef *self);

/**
 * lrg_potion_def_get_name:
 * @self: a #LrgPotionDef
 *
 * Gets the potion's display name.
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_potion_def_get_name (LrgPotionDef *self);

/**
 * lrg_potion_def_set_name:
 * @self: a #LrgPotionDef
 * @name: the new name
 *
 * Sets the potion's display name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_potion_def_set_name (LrgPotionDef *self,
                               const gchar  *name);

/**
 * lrg_potion_def_get_description:
 * @self: a #LrgPotionDef
 *
 * Gets the potion's description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_potion_def_get_description (LrgPotionDef *self);

/**
 * lrg_potion_def_set_description:
 * @self: a #LrgPotionDef
 * @description: (nullable): the description
 *
 * Sets the potion's description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_potion_def_set_description (LrgPotionDef *self,
                                      const gchar  *description);

/**
 * lrg_potion_def_get_icon:
 * @self: a #LrgPotionDef
 *
 * Gets the potion's icon path.
 *
 * Returns: (transfer none) (nullable): the icon path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_potion_def_get_icon (LrgPotionDef *self);

/**
 * lrg_potion_def_set_icon:
 * @self: a #LrgPotionDef
 * @icon: (nullable): the icon path
 *
 * Sets the potion's icon path.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_potion_def_set_icon (LrgPotionDef *self,
                               const gchar  *icon);

/**
 * lrg_potion_def_get_rarity:
 * @self: a #LrgPotionDef
 *
 * Gets the potion's rarity.
 *
 * Returns: the rarity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPotionRarity lrg_potion_def_get_rarity (LrgPotionDef *self);

/**
 * lrg_potion_def_set_rarity:
 * @self: a #LrgPotionDef
 * @rarity: the rarity
 *
 * Sets the potion's rarity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_potion_def_set_rarity (LrgPotionDef   *self,
                                 LrgPotionRarity rarity);

/**
 * lrg_potion_def_get_target_type:
 * @self: a #LrgPotionDef
 *
 * Gets the potion's target type.
 *
 * Returns: the target type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPotionTarget lrg_potion_def_get_target_type (LrgPotionDef *self);

/**
 * lrg_potion_def_set_target_type:
 * @self: a #LrgPotionDef
 * @target_type: the target type
 *
 * Sets the potion's target type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_potion_def_set_target_type (LrgPotionDef   *self,
                                      LrgPotionTarget target_type);

/**
 * lrg_potion_def_get_potency:
 * @self: a #LrgPotionDef
 *
 * Gets the potion's potency (effect magnitude).
 *
 * Returns: the potency
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_potion_def_get_potency (LrgPotionDef *self);

/**
 * lrg_potion_def_set_potency:
 * @self: a #LrgPotionDef
 * @potency: the potency
 *
 * Sets the potion's potency.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_potion_def_set_potency (LrgPotionDef *self,
                                  gint          potency);

/**
 * lrg_potion_def_get_combat_only:
 * @self: a #LrgPotionDef
 *
 * Gets whether the potion can only be used in combat.
 *
 * Returns: %TRUE if combat only
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_potion_def_get_combat_only (LrgPotionDef *self);

/**
 * lrg_potion_def_set_combat_only:
 * @self: a #LrgPotionDef
 * @combat_only: whether combat only
 *
 * Sets whether the potion can only be used in combat.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_potion_def_set_combat_only (LrgPotionDef *self,
                                      gboolean      combat_only);

/**
 * lrg_potion_def_get_price:
 * @self: a #LrgPotionDef
 *
 * Gets the potion's base shop price.
 *
 * Returns: the price
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_potion_def_get_price (LrgPotionDef *self);

/**
 * lrg_potion_def_set_price:
 * @self: a #LrgPotionDef
 * @price: the base price
 *
 * Sets the potion's base shop price.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_potion_def_set_price (LrgPotionDef *self,
                                gint          price);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_potion_def_can_use:
 * @self: a #LrgPotionDef
 * @context: (nullable): game/combat context
 *
 * Checks if the potion can be used.
 *
 * Returns: %TRUE if can use
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_potion_def_can_use (LrgPotionDef *self,
                                  gpointer      context);

/**
 * lrg_potion_def_on_use:
 * @self: a #LrgPotionDef
 * @context: (nullable): game/combat context
 * @target: (nullable): target for targeted potions
 *
 * Called when the potion is used.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_potion_def_on_use (LrgPotionDef *self,
                             gpointer      context,
                             gpointer      target);

/**
 * lrg_potion_def_get_tooltip:
 * @self: a #LrgPotionDef
 * @context: (nullable): game context
 *
 * Gets the potion's tooltip text.
 *
 * Returns: (transfer full): tooltip text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_potion_def_get_tooltip (LrgPotionDef *self,
                                     gpointer      context);

G_END_DECLS
