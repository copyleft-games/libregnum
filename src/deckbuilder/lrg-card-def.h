/* lrg-card-def.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardDef - Definition of a card type.
 *
 * This is a derivable class that defines the properties and behavior
 * of a card type. Actual card instances during combat are represented
 * by LrgCardInstance.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_CARD_DEF (lrg_card_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCardDef, lrg_card_def, LRG, CARD_DEF, GObject)

/* Forward declarations */
typedef struct _LrgCombatContext LrgCombatContext;
typedef struct _LrgCombatant     LrgCombatant;
typedef struct _LrgCardEffect    LrgCardEffect;

/**
 * LrgCardDefClass:
 * @parent_class: the parent class
 * @on_play: virtual function called when card is played
 * @on_discard: virtual function called when card is discarded
 * @on_exhaust: virtual function called when card is exhausted
 * @on_draw: virtual function called when card is drawn
 * @can_play: virtual function to check if card can be played
 * @calculate_cost: virtual function to calculate card cost
 * @get_tooltip: virtual function to get tooltip text
 *
 * The class structure for #LrgCardDef.
 *
 * Since: 1.0
 */
struct _LrgCardDefClass
{
    GObjectClass parent_class;

    /**
     * LrgCardDefClass::on_play:
     * @self: the card definition
     * @ctx: the combat context
     * @target: (nullable): the target combatant
     *
     * Called when the card is played. Default implementation
     * executes all effects in order via the effect stack.
     *
     * Returns: %TRUE if successfully played
     */
    gboolean (* on_play)        (LrgCardDef       *self,
                                 LrgCombatContext *ctx,
                                 LrgCombatant     *target);

    /**
     * LrgCardDefClass::on_discard:
     * @self: the card definition
     * @ctx: the combat context
     *
     * Called when the card is discarded.
     *
     * Returns: %TRUE if discard should proceed
     */
    gboolean (* on_discard)     (LrgCardDef       *self,
                                 LrgCombatContext *ctx);

    /**
     * LrgCardDefClass::on_exhaust:
     * @self: the card definition
     * @ctx: the combat context
     *
     * Called when the card is exhausted.
     *
     * Returns: %TRUE if exhaust should proceed
     */
    gboolean (* on_exhaust)     (LrgCardDef       *self,
                                 LrgCombatContext *ctx);

    /**
     * LrgCardDefClass::on_draw:
     * @self: the card definition
     * @ctx: the combat context
     *
     * Called when the card is drawn to hand.
     *
     * Returns: %TRUE if draw should proceed
     */
    gboolean (* on_draw)        (LrgCardDef       *self,
                                 LrgCombatContext *ctx);

    /**
     * LrgCardDefClass::can_play:
     * @self: the card definition
     * @ctx: the combat context
     *
     * Checks if the card can be played in the current state.
     * Default implementation checks energy cost and unplayable keyword.
     *
     * Returns: %TRUE if the card can be played
     */
    gboolean (* can_play)       (LrgCardDef       *self,
                                 LrgCombatContext *ctx);

    /**
     * LrgCardDefClass::calculate_cost:
     * @self: the card definition
     * @ctx: (nullable): the combat context
     *
     * Calculates the energy cost for playing this card.
     * Default handles X-cost and cost modifiers from context.
     *
     * Returns: the energy cost
     */
    gint     (* calculate_cost) (LrgCardDef       *self,
                                 LrgCombatContext *ctx);

    /**
     * LrgCardDefClass::get_tooltip:
     * @self: the card definition
     * @ctx: (nullable): the combat context for dynamic values
     *
     * Gets the tooltip text for this card.
     * Default implementation returns the description with variable substitution.
     *
     * Returns: (transfer full) (nullable): tooltip text, or %NULL
     */
    gchar *  (* get_tooltip)    (LrgCardDef       *self,
                                 LrgCombatContext *ctx);

    /*< private >*/
    gpointer _reserved[8];
};

/* Construction */

/**
 * lrg_card_def_new:
 * @id: unique identifier for this card type
 *
 * Creates a new card definition.
 *
 * Returns: (transfer full): A new #LrgCardDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardDef *
lrg_card_def_new (const gchar *id);

/* Properties */

/**
 * lrg_card_def_get_id:
 * @self: an #LrgCardDef
 *
 * Gets the unique identifier for this card type.
 *
 * Returns: (transfer none): the card ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_card_def_get_id (LrgCardDef *self);

/**
 * lrg_card_def_get_name:
 * @self: an #LrgCardDef
 *
 * Gets the display name.
 *
 * Returns: (transfer none) (nullable): the display name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_card_def_get_name (LrgCardDef *self);

/**
 * lrg_card_def_set_name:
 * @self: an #LrgCardDef
 * @name: (nullable): the display name
 *
 * Sets the display name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_name (LrgCardDef  *self,
                       const gchar *name);

/**
 * lrg_card_def_get_description:
 * @self: an #LrgCardDef
 *
 * Gets the card description (may contain variables like {damage}).
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_card_def_get_description (LrgCardDef *self);

/**
 * lrg_card_def_set_description:
 * @self: an #LrgCardDef
 * @description: (nullable): the description
 *
 * Sets the card description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_description (LrgCardDef  *self,
                              const gchar *description);

/**
 * lrg_card_def_get_card_type:
 * @self: an #LrgCardDef
 *
 * Gets the card type.
 *
 * Returns: the #LrgCardType
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardType
lrg_card_def_get_card_type (LrgCardDef *self);

/**
 * lrg_card_def_set_card_type:
 * @self: an #LrgCardDef
 * @card_type: the #LrgCardType
 *
 * Sets the card type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_card_type (LrgCardDef  *self,
                            LrgCardType  card_type);

/**
 * lrg_card_def_get_rarity:
 * @self: an #LrgCardDef
 *
 * Gets the card rarity.
 *
 * Returns: the #LrgCardRarity
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardRarity
lrg_card_def_get_rarity (LrgCardDef *self);

/**
 * lrg_card_def_set_rarity:
 * @self: an #LrgCardDef
 * @rarity: the #LrgCardRarity
 *
 * Sets the card rarity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_rarity (LrgCardDef    *self,
                         LrgCardRarity  rarity);

/**
 * lrg_card_def_get_base_cost:
 * @self: an #LrgCardDef
 *
 * Gets the base energy cost.
 *
 * Returns: the base cost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_card_def_get_base_cost (LrgCardDef *self);

/**
 * lrg_card_def_set_base_cost:
 * @self: an #LrgCardDef
 * @cost: the base cost
 *
 * Sets the base energy cost.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_base_cost (LrgCardDef *self,
                            gint        cost);

/**
 * lrg_card_def_get_target_type:
 * @self: an #LrgCardDef
 *
 * Gets the target type for this card.
 *
 * Returns: the #LrgCardTargetType
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardTargetType
lrg_card_def_get_target_type (LrgCardDef *self);

/**
 * lrg_card_def_set_target_type:
 * @self: an #LrgCardDef
 * @target_type: the #LrgCardTargetType
 *
 * Sets the target type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_target_type (LrgCardDef        *self,
                              LrgCardTargetType  target_type);

/**
 * lrg_card_def_get_keywords:
 * @self: an #LrgCardDef
 *
 * Gets the keyword flags.
 *
 * Returns: the #LrgCardKeyword flags
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardKeyword
lrg_card_def_get_keywords (LrgCardDef *self);

/**
 * lrg_card_def_set_keywords:
 * @self: an #LrgCardDef
 * @keywords: the #LrgCardKeyword flags
 *
 * Sets the keyword flags.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_keywords (LrgCardDef     *self,
                           LrgCardKeyword  keywords);

/**
 * lrg_card_def_has_keyword:
 * @self: an #LrgCardDef
 * @keyword: the keyword to check
 *
 * Checks if the card has a specific keyword.
 *
 * Returns: %TRUE if the keyword is set
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_def_has_keyword (LrgCardDef     *self,
                          LrgCardKeyword  keyword);

/**
 * lrg_card_def_add_keyword:
 * @self: an #LrgCardDef
 * @keyword: the keyword to add
 *
 * Adds a keyword to the card.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_add_keyword (LrgCardDef     *self,
                          LrgCardKeyword  keyword);

/**
 * lrg_card_def_remove_keyword:
 * @self: an #LrgCardDef
 * @keyword: the keyword to remove
 *
 * Removes a keyword from the card.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_remove_keyword (LrgCardDef     *self,
                             LrgCardKeyword  keyword);

/**
 * lrg_card_def_get_upgradeable:
 * @self: an #LrgCardDef
 *
 * Gets whether this card can be upgraded.
 *
 * Returns: %TRUE if upgradeable
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_def_get_upgradeable (LrgCardDef *self);

/**
 * lrg_card_def_set_upgradeable:
 * @self: an #LrgCardDef
 * @upgradeable: whether upgradeable
 *
 * Sets whether this card can be upgraded.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_upgradeable (LrgCardDef *self,
                              gboolean    upgradeable);

/**
 * lrg_card_def_get_upgraded_def_id:
 * @self: an #LrgCardDef
 *
 * Gets the ID of the upgraded version of this card.
 *
 * Returns: (transfer none) (nullable): the upgraded card ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_card_def_get_upgraded_def_id (LrgCardDef *self);

/**
 * lrg_card_def_set_upgraded_def_id:
 * @self: an #LrgCardDef
 * @upgraded_id: (nullable): the upgraded card ID
 *
 * Sets the ID of the upgraded version.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_upgraded_def_id (LrgCardDef  *self,
                                  const gchar *upgraded_id);

/**
 * lrg_card_def_get_icon:
 * @self: an #LrgCardDef
 *
 * Gets the icon path for this card.
 *
 * Returns: (transfer none) (nullable): the icon path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_card_def_get_icon (LrgCardDef *self);

/**
 * lrg_card_def_set_icon:
 * @self: an #LrgCardDef
 * @icon: (nullable): the icon path
 *
 * Sets the icon path.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_icon (LrgCardDef  *self,
                       const gchar *icon);

/* Effects */

/**
 * lrg_card_def_add_effect:
 * @self: an #LrgCardDef
 * @effect: (transfer full): the effect to add
 *
 * Adds an effect to this card definition.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_add_effect (LrgCardDef    *self,
                         LrgCardEffect *effect);

/**
 * lrg_card_def_get_effects:
 * @self: an #LrgCardDef
 *
 * Gets the list of effects for this card.
 *
 * Returns: (transfer none) (element-type LrgCardEffect): the effects list
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_card_def_get_effects (LrgCardDef *self);

/**
 * lrg_card_def_clear_effects:
 * @self: an #LrgCardDef
 *
 * Removes all effects from this card.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_clear_effects (LrgCardDef *self);

/* Tags */

/**
 * lrg_card_def_add_tag:
 * @self: an #LrgCardDef
 * @tag: the tag to add
 *
 * Adds a tag to this card for filtering and synergies.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_add_tag (LrgCardDef  *self,
                      const gchar *tag);

/**
 * lrg_card_def_has_tag:
 * @self: an #LrgCardDef
 * @tag: the tag to check
 *
 * Checks if the card has a specific tag.
 *
 * Returns: %TRUE if the tag is present
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_def_has_tag (LrgCardDef  *self,
                      const gchar *tag);

/**
 * lrg_card_def_get_tags:
 * @self: an #LrgCardDef
 *
 * Gets all tags for this card.
 *
 * Returns: (transfer none) (element-type utf8): the tags list
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_card_def_get_tags (LrgCardDef *self);

/* Scoring (Balatro-style) */

/**
 * lrg_card_def_get_suit:
 * @self: an #LrgCardDef
 *
 * Gets the suit for scoring cards.
 *
 * Returns: the #LrgCardSuit
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardSuit
lrg_card_def_get_suit (LrgCardDef *self);

/**
 * lrg_card_def_set_suit:
 * @self: an #LrgCardDef
 * @suit: the #LrgCardSuit
 *
 * Sets the suit for scoring cards.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_suit (LrgCardDef  *self,
                       LrgCardSuit  suit);

/**
 * lrg_card_def_get_rank:
 * @self: an #LrgCardDef
 *
 * Gets the rank for scoring cards.
 *
 * Returns: the #LrgCardRank
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardRank
lrg_card_def_get_rank (LrgCardDef *self);

/**
 * lrg_card_def_set_rank:
 * @self: an #LrgCardDef
 * @rank: the #LrgCardRank
 *
 * Sets the rank for scoring cards.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_rank (LrgCardDef  *self,
                       LrgCardRank  rank);

/**
 * lrg_card_def_get_chip_value:
 * @self: an #LrgCardDef
 *
 * Gets the chip value for scoring.
 *
 * Returns: the chip value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_card_def_get_chip_value (LrgCardDef *self);

/**
 * lrg_card_def_set_chip_value:
 * @self: an #LrgCardDef
 * @chips: the chip value
 *
 * Sets the chip value for scoring.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_card_def_set_chip_value (LrgCardDef *self,
                             gint        chips);

/* Virtual Method Wrappers */

/**
 * lrg_card_def_on_play:
 * @self: an #LrgCardDef
 * @ctx: the combat context
 * @target: (nullable): the target combatant
 *
 * Calls the on_play virtual function.
 *
 * Returns: %TRUE if successfully played
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_def_on_play (LrgCardDef       *self,
                      LrgCombatContext *ctx,
                      LrgCombatant     *target);

/**
 * lrg_card_def_on_discard:
 * @self: an #LrgCardDef
 * @ctx: the combat context
 *
 * Calls the on_discard virtual function.
 *
 * Returns: %TRUE if discard should proceed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_def_on_discard (LrgCardDef       *self,
                         LrgCombatContext *ctx);

/**
 * lrg_card_def_on_exhaust:
 * @self: an #LrgCardDef
 * @ctx: the combat context
 *
 * Calls the on_exhaust virtual function.
 *
 * Returns: %TRUE if exhaust should proceed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_def_on_exhaust (LrgCardDef       *self,
                         LrgCombatContext *ctx);

/**
 * lrg_card_def_on_draw:
 * @self: an #LrgCardDef
 * @ctx: the combat context
 *
 * Calls the on_draw virtual function.
 *
 * Returns: %TRUE if draw should proceed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_def_on_draw (LrgCardDef       *self,
                      LrgCombatContext *ctx);

/**
 * lrg_card_def_can_play:
 * @self: an #LrgCardDef
 * @ctx: the combat context
 *
 * Checks if the card can be played.
 *
 * Returns: %TRUE if the card can be played
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_card_def_can_play (LrgCardDef       *self,
                       LrgCombatContext *ctx);

/**
 * lrg_card_def_calculate_cost:
 * @self: an #LrgCardDef
 * @ctx: (nullable): the combat context
 *
 * Calculates the energy cost.
 *
 * Returns: the energy cost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_card_def_calculate_cost (LrgCardDef       *self,
                             LrgCombatContext *ctx);

/**
 * lrg_card_def_get_tooltip:
 * @self: an #LrgCardDef
 * @ctx: (nullable): the combat context
 *
 * Gets the tooltip text.
 *
 * Returns: (transfer full) (nullable): tooltip text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_card_def_get_tooltip (LrgCardDef       *self,
                          LrgCombatContext *ctx);

G_END_DECLS
