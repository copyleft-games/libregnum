/* lrg-card-keyword-def.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCardKeywordDef - Custom keyword definition.
 *
 * Custom keywords allow mods to add new keyword mechanics beyond
 * the built-in LrgCardKeyword flags. Each custom keyword has a
 * unique ID, display name, description, and optional behavior hooks.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-card-event.h"

G_BEGIN_DECLS

#define LRG_TYPE_CARD_KEYWORD_DEF (lrg_card_keyword_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgCardKeywordDef, lrg_card_keyword_def, LRG, CARD_KEYWORD_DEF, GObject)

/**
 * LrgCardKeywordDefClass:
 * @parent_class: parent class
 * @on_card_played: called when a card with this keyword is played
 * @on_card_drawn: called when a card with this keyword is drawn
 * @on_card_discarded: called when a card with this keyword is discarded
 * @on_turn_start: called at turn start for cards with this keyword
 * @on_turn_end: called at turn end for cards with this keyword
 * @modify_cost: modify the card's cost
 * @can_play: check if the card can be played
 *
 * Class structure for custom keywords.
 *
 * Since: 1.0
 */
struct _LrgCardKeywordDefClass
{
    GObjectClass parent_class;

    /**
     * LrgCardKeywordDefClass::on_card_played:
     * @self: the keyword definition
     * @card: the card being played
     * @context: the combat context
     *
     * Called when a card with this keyword is played.
     *
     * Since: 1.0
     */
    void (*on_card_played) (LrgCardKeywordDef *self,
                            gpointer           card,
                            gpointer           context);

    /**
     * LrgCardKeywordDefClass::on_card_drawn:
     * @self: the keyword definition
     * @card: the card drawn
     * @context: the combat context
     *
     * Called when a card with this keyword is drawn.
     *
     * Since: 1.0
     */
    void (*on_card_drawn) (LrgCardKeywordDef *self,
                           gpointer           card,
                           gpointer           context);

    /**
     * LrgCardKeywordDefClass::on_card_discarded:
     * @self: the keyword definition
     * @card: the card discarded
     * @context: the combat context
     *
     * Called when a card with this keyword is discarded.
     *
     * Since: 1.0
     */
    void (*on_card_discarded) (LrgCardKeywordDef *self,
                               gpointer           card,
                               gpointer           context);

    /**
     * LrgCardKeywordDefClass::on_turn_start:
     * @self: the keyword definition
     * @card: the card
     * @context: the combat context
     *
     * Called at turn start for cards with this keyword in hand.
     *
     * Since: 1.0
     */
    void (*on_turn_start) (LrgCardKeywordDef *self,
                           gpointer           card,
                           gpointer           context);

    /**
     * LrgCardKeywordDefClass::on_turn_end:
     * @self: the keyword definition
     * @card: the card
     * @context: the combat context
     *
     * Called at turn end for cards with this keyword in hand.
     *
     * Since: 1.0
     */
    void (*on_turn_end) (LrgCardKeywordDef *self,
                         gpointer           card,
                         gpointer           context);

    /**
     * LrgCardKeywordDefClass::modify_cost:
     * @self: the keyword definition
     * @card: the card
     * @context: the combat context
     * @base_cost: the base cost
     *
     * Modifies the card's energy cost.
     *
     * Returns: the modified cost
     *
     * Since: 1.0
     */
    gint (*modify_cost) (LrgCardKeywordDef *self,
                         gpointer           card,
                         gpointer           context,
                         gint               base_cost);

    /**
     * LrgCardKeywordDefClass::can_play:
     * @self: the keyword definition
     * @card: the card
     * @context: the combat context
     *
     * Checks if a card with this keyword can be played.
     *
     * Returns: %TRUE if the card can be played
     *
     * Since: 1.0
     */
    gboolean (*can_play) (LrgCardKeywordDef *self,
                          gpointer           card,
                          gpointer           context);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_card_keyword_def_new:
 * @id: unique keyword identifier
 * @name: display name
 * @description: keyword description
 *
 * Creates a new custom keyword definition.
 *
 * Returns: (transfer full): a new #LrgCardKeywordDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgCardKeywordDef * lrg_card_keyword_def_new (const gchar *id,
                                              const gchar *name,
                                              const gchar *description);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_card_keyword_def_get_id:
 * @self: a #LrgCardKeywordDef
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): the keyword ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_keyword_def_get_id (LrgCardKeywordDef *self);

/**
 * lrg_card_keyword_def_get_name:
 * @self: a #LrgCardKeywordDef
 *
 * Gets the display name.
 *
 * Returns: (transfer none): the keyword name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_keyword_def_get_name (LrgCardKeywordDef *self);

/**
 * lrg_card_keyword_def_get_description:
 * @self: a #LrgCardKeywordDef
 *
 * Gets the description.
 *
 * Returns: (transfer none): the description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_keyword_def_get_description (LrgCardKeywordDef *self);

/**
 * lrg_card_keyword_def_get_icon:
 * @self: a #LrgCardKeywordDef
 *
 * Gets the icon identifier.
 *
 * Returns: (transfer none) (nullable): the icon ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_card_keyword_def_get_icon (LrgCardKeywordDef *self);

/**
 * lrg_card_keyword_def_set_icon:
 * @self: a #LrgCardKeywordDef
 * @icon: (nullable): the icon identifier
 *
 * Sets the icon identifier.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_def_set_icon (LrgCardKeywordDef *self,
                                    const gchar       *icon);

/**
 * lrg_card_keyword_def_is_positive:
 * @self: a #LrgCardKeywordDef
 *
 * Checks if the keyword is beneficial.
 *
 * Returns: %TRUE if positive
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_keyword_def_is_positive (LrgCardKeywordDef *self);

/**
 * lrg_card_keyword_def_set_positive:
 * @self: a #LrgCardKeywordDef
 * @positive: whether the keyword is positive
 *
 * Sets whether the keyword is beneficial.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_def_set_positive (LrgCardKeywordDef *self,
                                        gboolean           positive);

/**
 * lrg_card_keyword_def_is_negative:
 * @self: a #LrgCardKeywordDef
 *
 * Checks if the keyword is detrimental.
 *
 * Returns: %TRUE if negative
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_keyword_def_is_negative (LrgCardKeywordDef *self);

/**
 * lrg_card_keyword_def_set_negative:
 * @self: a #LrgCardKeywordDef
 * @negative: whether the keyword is negative
 *
 * Sets whether the keyword is detrimental.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_def_set_negative (LrgCardKeywordDef *self,
                                        gboolean           negative);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_card_keyword_def_on_card_played:
 * @self: a #LrgCardKeywordDef
 * @card: the card being played
 * @context: the combat context
 *
 * Called when a card with this keyword is played.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_def_on_card_played (LrgCardKeywordDef *self,
                                          gpointer           card,
                                          gpointer           context);

/**
 * lrg_card_keyword_def_on_card_drawn:
 * @self: a #LrgCardKeywordDef
 * @card: the card drawn
 * @context: the combat context
 *
 * Called when a card with this keyword is drawn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_def_on_card_drawn (LrgCardKeywordDef *self,
                                         gpointer           card,
                                         gpointer           context);

/**
 * lrg_card_keyword_def_on_card_discarded:
 * @self: a #LrgCardKeywordDef
 * @card: the card discarded
 * @context: the combat context
 *
 * Called when a card with this keyword is discarded.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_def_on_card_discarded (LrgCardKeywordDef *self,
                                             gpointer           card,
                                             gpointer           context);

/**
 * lrg_card_keyword_def_on_turn_start:
 * @self: a #LrgCardKeywordDef
 * @card: the card
 * @context: the combat context
 *
 * Called at turn start for cards with this keyword in hand.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_def_on_turn_start (LrgCardKeywordDef *self,
                                         gpointer           card,
                                         gpointer           context);

/**
 * lrg_card_keyword_def_on_turn_end:
 * @self: a #LrgCardKeywordDef
 * @card: the card
 * @context: the combat context
 *
 * Called at turn end for cards with this keyword in hand.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_card_keyword_def_on_turn_end (LrgCardKeywordDef *self,
                                       gpointer           card,
                                       gpointer           context);

/**
 * lrg_card_keyword_def_modify_cost:
 * @self: a #LrgCardKeywordDef
 * @card: the card
 * @context: the combat context
 * @base_cost: the base cost
 *
 * Modifies the card's energy cost.
 *
 * Returns: the modified cost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_card_keyword_def_modify_cost (LrgCardKeywordDef *self,
                                       gpointer           card,
                                       gpointer           context,
                                       gint               base_cost);

/**
 * lrg_card_keyword_def_can_play:
 * @self: a #LrgCardKeywordDef
 * @card: the card
 * @context: the combat context
 *
 * Checks if a card with this keyword can be played.
 *
 * Returns: %TRUE if the card can be played
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_card_keyword_def_can_play (LrgCardKeywordDef *self,
                                        gpointer           card,
                                        gpointer           context);

G_END_DECLS
