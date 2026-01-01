/* lrg-synergy.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSynergy - Card synergy definition.
 *
 * Synergies define relationships between cards that provide bonuses
 * when certain conditions are met. Examples include:
 * - Having multiple cards of the same type
 * - Cards that share keywords
 * - Cards that work well together mechanically
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_SYNERGY (lrg_synergy_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgSynergy, lrg_synergy, LRG, SYNERGY, GObject)

/**
 * LrgSynergyType:
 * @LRG_SYNERGY_TYPE_KEYWORD: Synergy based on shared keywords
 * @LRG_SYNERGY_TYPE_CARD_TYPE: Synergy based on card types
 * @LRG_SYNERGY_TYPE_TAG: Synergy based on card tags
 * @LRG_SYNERGY_TYPE_CUSTOM: Custom synergy logic
 *
 * Types of card synergies.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_SYNERGY_TYPE_KEYWORD,
    LRG_SYNERGY_TYPE_CARD_TYPE,
    LRG_SYNERGY_TYPE_TAG,
    LRG_SYNERGY_TYPE_CUSTOM
} LrgSynergyType;

/**
 * LrgSynergyClass:
 * @parent_class: parent class
 * @check_cards: check if cards have synergy
 * @calculate_bonus: calculate the synergy bonus value
 * @get_synergy_cards: get cards that contribute to synergy
 *
 * Class structure for synergies.
 *
 * Since: 1.0
 */
struct _LrgSynergyClass
{
    GObjectClass parent_class;

    /**
     * LrgSynergyClass::check_cards:
     * @self: the synergy
     * @cards: (element-type gpointer): array of cards to check
     *
     * Checks if the given cards have this synergy active.
     *
     * Returns: %TRUE if synergy is active
     *
     * Since: 1.0
     */
    gboolean (*check_cards) (LrgSynergy *self,
                             GPtrArray  *cards);

    /**
     * LrgSynergyClass::calculate_bonus:
     * @self: the synergy
     * @cards: (element-type gpointer): array of cards
     *
     * Calculates the bonus value from this synergy.
     *
     * Returns: the bonus value
     *
     * Since: 1.0
     */
    gint (*calculate_bonus) (LrgSynergy *self,
                             GPtrArray  *cards);

    /**
     * LrgSynergyClass::get_synergy_cards:
     * @self: the synergy
     * @cards: (element-type gpointer): array of all cards
     *
     * Gets the subset of cards that contribute to this synergy.
     *
     * Returns: (transfer container) (element-type gpointer): contributing cards
     *
     * Since: 1.0
     */
    GPtrArray * (*get_synergy_cards) (LrgSynergy *self,
                                      GPtrArray  *cards);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_synergy_new:
 * @id: unique synergy identifier
 * @name: display name
 * @synergy_type: the type of synergy
 *
 * Creates a new synergy definition.
 *
 * Returns: (transfer full): a new #LrgSynergy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSynergy * lrg_synergy_new (const gchar    *id,
                              const gchar    *name,
                              LrgSynergyType  synergy_type);

/**
 * lrg_synergy_new_keyword:
 * @id: unique synergy identifier
 * @name: display name
 * @keyword: the keyword for synergy
 * @min_count: minimum cards with keyword for synergy
 *
 * Creates a keyword-based synergy.
 *
 * Returns: (transfer full): a new #LrgSynergy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSynergy * lrg_synergy_new_keyword (const gchar    *id,
                                      const gchar    *name,
                                      LrgCardKeyword  keyword,
                                      guint           min_count);

/**
 * lrg_synergy_new_card_type:
 * @id: unique synergy identifier
 * @name: display name
 * @card_type: the card type for synergy
 * @min_count: minimum cards of type for synergy
 *
 * Creates a card-type-based synergy.
 *
 * Returns: (transfer full): a new #LrgSynergy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSynergy * lrg_synergy_new_card_type (const gchar *id,
                                        const gchar *name,
                                        LrgCardType  card_type,
                                        guint        min_count);

/**
 * lrg_synergy_new_tag:
 * @id: unique synergy identifier
 * @name: display name
 * @tag: the tag for synergy
 * @min_count: minimum cards with tag for synergy
 *
 * Creates a tag-based synergy.
 *
 * Returns: (transfer full): a new #LrgSynergy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSynergy * lrg_synergy_new_tag (const gchar *id,
                                  const gchar *name,
                                  const gchar *tag,
                                  guint        min_count);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_synergy_get_id:
 * @self: a #LrgSynergy
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): the synergy ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_synergy_get_id (LrgSynergy *self);

/**
 * lrg_synergy_get_name:
 * @self: a #LrgSynergy
 *
 * Gets the display name.
 *
 * Returns: (transfer none): the synergy name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_synergy_get_name (LrgSynergy *self);

/**
 * lrg_synergy_get_description:
 * @self: a #LrgSynergy
 *
 * Gets the description.
 *
 * Returns: (transfer none) (nullable): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_synergy_get_description (LrgSynergy *self);

/**
 * lrg_synergy_set_description:
 * @self: a #LrgSynergy
 * @description: (nullable): the description
 *
 * Sets the description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_synergy_set_description (LrgSynergy  *self,
                                  const gchar *description);

/**
 * lrg_synergy_get_synergy_type:
 * @self: a #LrgSynergy
 *
 * Gets the synergy type.
 *
 * Returns: the synergy type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSynergyType lrg_synergy_get_synergy_type (LrgSynergy *self);

/**
 * lrg_synergy_get_min_count:
 * @self: a #LrgSynergy
 *
 * Gets the minimum card count for synergy activation.
 *
 * Returns: the minimum count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_synergy_get_min_count (LrgSynergy *self);

/**
 * lrg_synergy_set_min_count:
 * @self: a #LrgSynergy
 * @min_count: the minimum count
 *
 * Sets the minimum card count for synergy activation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_synergy_set_min_count (LrgSynergy *self,
                                guint       min_count);

/**
 * lrg_synergy_get_bonus_per_card:
 * @self: a #LrgSynergy
 *
 * Gets the bonus value per additional card.
 *
 * Returns: the bonus per card
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_synergy_get_bonus_per_card (LrgSynergy *self);

/**
 * lrg_synergy_set_bonus_per_card:
 * @self: a #LrgSynergy
 * @bonus: the bonus per card
 *
 * Sets the bonus value per additional card.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_synergy_set_bonus_per_card (LrgSynergy *self,
                                     gint        bonus);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_synergy_check_cards:
 * @self: a #LrgSynergy
 * @cards: (element-type gpointer): array of cards to check
 *
 * Checks if the given cards have this synergy active.
 *
 * Returns: %TRUE if synergy is active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_synergy_check_cards (LrgSynergy *self,
                                  GPtrArray  *cards);

/**
 * lrg_synergy_calculate_bonus:
 * @self: a #LrgSynergy
 * @cards: (element-type gpointer): array of cards
 *
 * Calculates the bonus value from this synergy.
 *
 * Returns: the bonus value, or 0 if synergy not active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_synergy_calculate_bonus (LrgSynergy *self,
                                  GPtrArray  *cards);

/**
 * lrg_synergy_get_synergy_cards:
 * @self: a #LrgSynergy
 * @cards: (element-type gpointer): array of all cards
 *
 * Gets the subset of cards that contribute to this synergy.
 *
 * Returns: (transfer container) (element-type gpointer): contributing cards
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_synergy_get_synergy_cards (LrgSynergy *self,
                                           GPtrArray  *cards);

G_END_DECLS
