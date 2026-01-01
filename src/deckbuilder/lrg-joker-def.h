/* lrg-joker-def.h
 *
 * Copyright 2025 Libregnum Authors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_JOKER_DEF (lrg_joker_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgJokerDef, lrg_joker_def, LRG, JOKER_DEF, GObject)

/**
 * LrgJokerDefClass:
 * @apply_effect: Virtual method to apply the joker's effect to scoring
 * @can_trigger: Virtual method to check if joker triggers in current context
 * @get_description: Virtual method to get dynamic description
 *
 * Class structure for #LrgJokerDef.
 *
 * Subclasses can override methods to implement custom joker behaviors.
 *
 * Since: 1.0
 */
struct _LrgJokerDefClass
{
    GObjectClass parent_class;

    /**
     * LrgJokerDefClass::apply_effect:
     * @self: a #LrgJokerDef
     * @ctx: the scoring context
     * @instance: the joker instance (for tracking state)
     *
     * Applies the joker's effect to the scoring context.
     *
     * Since: 1.0
     */
    void (*apply_effect) (LrgJokerDef       *self,
                          LrgScoringContext *ctx,
                          LrgJokerInstance  *instance);

    /**
     * LrgJokerDefClass::can_trigger:
     * @self: a #LrgJokerDef
     * @ctx: the scoring context
     * @instance: the joker instance
     *
     * Checks if the joker can trigger in the current context.
     *
     * Returns: %TRUE if the joker should trigger
     *
     * Since: 1.0
     */
    gboolean (*can_trigger) (LrgJokerDef       *self,
                             LrgScoringContext *ctx,
                             LrgJokerInstance  *instance);

    /**
     * LrgJokerDefClass::get_description:
     * @self: a #LrgJokerDef
     * @instance: (nullable): the joker instance for dynamic values
     *
     * Gets the description, potentially with dynamic values.
     *
     * Returns: (transfer none): the description
     *
     * Since: 1.0
     */
    const gchar * (*get_description) (LrgJokerDef      *self,
                                      LrgJokerInstance *instance);

    /*< private >*/
    gpointer _reserved[8];
};

/* Construction */

/**
 * lrg_joker_def_new:
 * @id: unique identifier for the joker
 * @name: display name
 *
 * Creates a new joker definition with default values.
 *
 * Returns: (transfer full): A new #LrgJokerDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgJokerDef *
lrg_joker_def_new (const gchar *id,
                   const gchar *name);

/* Identification */

/**
 * lrg_joker_def_get_id:
 * @self: a #LrgJokerDef
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): the ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_joker_def_get_id (LrgJokerDef *self);

/**
 * lrg_joker_def_get_name:
 * @self: a #LrgJokerDef
 *
 * Gets the display name.
 *
 * Returns: (transfer none): the name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_joker_def_get_name (LrgJokerDef *self);

/**
 * lrg_joker_def_set_description:
 * @self: a #LrgJokerDef
 * @description: the description text
 *
 * Sets the description text.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_def_set_description (LrgJokerDef *self,
                               const gchar *description);

/**
 * lrg_joker_def_get_description:
 * @self: a #LrgJokerDef
 * @instance: (nullable): optional instance for dynamic values
 *
 * Gets the description, potentially with dynamic values from the instance.
 *
 * Returns: (transfer none): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_joker_def_get_description (LrgJokerDef      *self,
                               LrgJokerInstance *instance);

/* Rarity and Cost */

/**
 * lrg_joker_def_set_rarity:
 * @self: a #LrgJokerDef
 * @rarity: the #LrgJokerRarity
 *
 * Sets the rarity tier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_def_set_rarity (LrgJokerDef    *self,
                          LrgJokerRarity  rarity);

/**
 * lrg_joker_def_get_rarity:
 * @self: a #LrgJokerDef
 *
 * Gets the rarity tier.
 *
 * Returns: the #LrgJokerRarity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgJokerRarity
lrg_joker_def_get_rarity (LrgJokerDef *self);

/**
 * lrg_joker_def_set_cost:
 * @self: a #LrgJokerDef
 * @cost: the cost in gold
 *
 * Sets the base shop cost.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_def_set_cost (LrgJokerDef *self,
                        gint         cost);

/**
 * lrg_joker_def_get_cost:
 * @self: a #LrgJokerDef
 *
 * Gets the base shop cost.
 *
 * Returns: the cost in gold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_joker_def_get_cost (LrgJokerDef *self);

/**
 * lrg_joker_def_set_sell_value:
 * @self: a #LrgJokerDef
 * @value: the sell value
 *
 * Sets the sell value (usually cost/2).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_def_set_sell_value (LrgJokerDef *self,
                              gint         value);

/**
 * lrg_joker_def_get_sell_value:
 * @self: a #LrgJokerDef
 *
 * Gets the sell value.
 *
 * Returns: the sell value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_joker_def_get_sell_value (LrgJokerDef *self);

/* Simple Effect Values */

/**
 * lrg_joker_def_set_plus_chips:
 * @self: a #LrgJokerDef
 * @chips: bonus chips to add
 *
 * Sets the +Chips value this joker provides.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_def_set_plus_chips (LrgJokerDef *self,
                              gint64       chips);

/**
 * lrg_joker_def_get_plus_chips:
 * @self: a #LrgJokerDef
 *
 * Gets the +Chips value.
 *
 * Returns: the bonus chips
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_joker_def_get_plus_chips (LrgJokerDef *self);

/**
 * lrg_joker_def_set_plus_mult:
 * @self: a #LrgJokerDef
 * @mult: bonus mult to add
 *
 * Sets the +Mult value this joker provides.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_def_set_plus_mult (LrgJokerDef *self,
                             gint64       mult);

/**
 * lrg_joker_def_get_plus_mult:
 * @self: a #LrgJokerDef
 *
 * Gets the +Mult value.
 *
 * Returns: the bonus mult
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_joker_def_get_plus_mult (LrgJokerDef *self);

/**
 * lrg_joker_def_set_x_mult:
 * @self: a #LrgJokerDef
 * @x_mult: the X multiplier
 *
 * Sets the X-Mult value this joker provides.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_def_set_x_mult (LrgJokerDef *self,
                          gdouble      x_mult);

/**
 * lrg_joker_def_get_x_mult:
 * @self: a #LrgJokerDef
 *
 * Gets the X-Mult value.
 *
 * Returns: the X multiplier (1.0 = no effect)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_joker_def_get_x_mult (LrgJokerDef *self);

/* Triggering */

/**
 * lrg_joker_def_can_trigger:
 * @self: a #LrgJokerDef
 * @ctx: the scoring context
 * @instance: the joker instance
 *
 * Checks if this joker can trigger in the current context.
 *
 * Returns: %TRUE if the joker should trigger
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_joker_def_can_trigger (LrgJokerDef       *self,
                           LrgScoringContext *ctx,
                           LrgJokerInstance  *instance);

/**
 * lrg_joker_def_apply_effect:
 * @self: a #LrgJokerDef
 * @ctx: the scoring context to modify
 * @instance: the joker instance
 *
 * Applies this joker's effect to the scoring context.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_def_apply_effect (LrgJokerDef       *self,
                            LrgScoringContext *ctx,
                            LrgJokerInstance  *instance);

/* Condition Types */

/**
 * lrg_joker_def_set_required_hand:
 * @self: a #LrgJokerDef
 * @hand_type: the required hand type, or NONE for any
 *
 * Sets a required hand type for triggering.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_def_set_required_hand (LrgJokerDef *self,
                                 LrgHandType  hand_type);

/**
 * lrg_joker_def_get_required_hand:
 * @self: a #LrgJokerDef
 *
 * Gets the required hand type.
 *
 * Returns: the required #LrgHandType, or NONE for any
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgHandType
lrg_joker_def_get_required_hand (LrgJokerDef *self);

/**
 * lrg_joker_def_set_required_suit:
 * @self: a #LrgJokerDef
 * @suit: the required suit, or NONE for any
 *
 * Sets a required suit for triggering (on scoring cards).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_joker_def_set_required_suit (LrgJokerDef *self,
                                 LrgCardSuit  suit);

/**
 * lrg_joker_def_get_required_suit:
 * @self: a #LrgJokerDef
 *
 * Gets the required suit.
 *
 * Returns: the required #LrgCardSuit, or NONE for any
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardSuit
lrg_joker_def_get_required_suit (LrgJokerDef *self);

G_END_DECLS
