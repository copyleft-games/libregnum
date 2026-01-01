/* lrg-deck-def.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDeckDef - Template definition for a deck.
 *
 * This is a derivable class that defines the starting cards and
 * constraints for a deck. Actual deck state during a run is
 * represented by LrgDeckInstance.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_DECK_DEF (lrg_deck_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgDeckDef, lrg_deck_def, LRG, DECK_DEF, GObject)

/* Forward declarations */
typedef struct _LrgCardDef LrgCardDef;

/**
 * LrgDeckDefClass:
 * @parent_class: the parent class
 * @validate: virtual function to validate deck configuration
 * @get_starting_cards: virtual function to get starting card definitions
 *
 * The class structure for #LrgDeckDef.
 *
 * Since: 1.0
 */
struct _LrgDeckDefClass
{
    GObjectClass parent_class;

    /**
     * LrgDeckDefClass::validate:
     * @self: the deck definition
     * @error: (nullable): return location for error
     *
     * Validates the deck configuration.
     *
     * Returns: %TRUE if valid
     */
    gboolean (* validate) (LrgDeckDef  *self,
                           GError     **error);

    /**
     * LrgDeckDefClass::get_starting_cards:
     * @self: the deck definition
     *
     * Gets the starting card definitions and counts.
     * Override to provide dynamic starting cards.
     *
     * Returns: (element-type LrgCardDef) (transfer none): starting cards
     */
    GPtrArray * (* get_starting_cards) (LrgDeckDef *self);

    gpointer _reserved[8];
};

/**
 * LrgDeckCardEntry:
 * @card_def: the card definition
 * @count: number of copies
 *
 * Entry specifying a card and how many copies in the deck.
 *
 * Since: 1.0
 */
typedef struct _LrgDeckCardEntry LrgDeckCardEntry;
struct _LrgDeckCardEntry
{
    LrgCardDef *card_def;
    guint       count;
};

LRG_AVAILABLE_IN_ALL
GType lrg_deck_card_entry_get_type (void) G_GNUC_CONST;

LRG_AVAILABLE_IN_ALL
LrgDeckCardEntry * lrg_deck_card_entry_new (LrgCardDef *card_def,
                                            guint       count);

LRG_AVAILABLE_IN_ALL
LrgDeckCardEntry * lrg_deck_card_entry_copy (LrgDeckCardEntry *entry);

LRG_AVAILABLE_IN_ALL
void lrg_deck_card_entry_free (LrgDeckCardEntry *entry);

LRG_AVAILABLE_IN_ALL
LrgCardDef * lrg_deck_card_entry_get_card_def (LrgDeckCardEntry *entry);

LRG_AVAILABLE_IN_ALL
guint lrg_deck_card_entry_get_count (LrgDeckCardEntry *entry);

LRG_AVAILABLE_IN_ALL
void lrg_deck_card_entry_set_count (LrgDeckCardEntry *entry,
                                    guint             count);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgDeckCardEntry, lrg_deck_card_entry_free)

/* Constructors */

LRG_AVAILABLE_IN_ALL
LrgDeckDef * lrg_deck_def_new (const gchar *id);

/* Properties */

LRG_AVAILABLE_IN_ALL
const gchar * lrg_deck_def_get_id (LrgDeckDef *self);

LRG_AVAILABLE_IN_ALL
const gchar * lrg_deck_def_get_name (LrgDeckDef *self);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_set_name (LrgDeckDef  *self,
                            const gchar *name);

LRG_AVAILABLE_IN_ALL
const gchar * lrg_deck_def_get_description (LrgDeckDef *self);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_set_description (LrgDeckDef  *self,
                                   const gchar *description);

LRG_AVAILABLE_IN_ALL
const gchar * lrg_deck_def_get_character_id (LrgDeckDef *self);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_set_character_id (LrgDeckDef  *self,
                                    const gchar *character_id);

LRG_AVAILABLE_IN_ALL
guint lrg_deck_def_get_min_size (LrgDeckDef *self);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_set_min_size (LrgDeckDef *self,
                                guint       min_size);

LRG_AVAILABLE_IN_ALL
guint lrg_deck_def_get_max_size (LrgDeckDef *self);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_set_max_size (LrgDeckDef *self,
                                guint       max_size);

/* Starting cards */

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_add_starting_card (LrgDeckDef *self,
                                     LrgCardDef *card_def,
                                     guint       count);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_def_remove_starting_card (LrgDeckDef *self,
                                            LrgCardDef *card_def);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_clear_starting_cards (LrgDeckDef *self);

LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_deck_def_get_starting_cards (LrgDeckDef *self);

LRG_AVAILABLE_IN_ALL
guint lrg_deck_def_get_starting_card_count (LrgDeckDef *self);

LRG_AVAILABLE_IN_ALL
guint lrg_deck_def_get_total_starting_cards (LrgDeckDef *self);

/* Card restrictions */

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_add_allowed_card_type (LrgDeckDef  *self,
                                         LrgCardType  card_type);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_remove_allowed_card_type (LrgDeckDef  *self,
                                            LrgCardType  card_type);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_def_is_card_type_allowed (LrgDeckDef  *self,
                                            LrgCardType  card_type);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_set_allowed_types (LrgDeckDef  *self,
                                     LrgCardType  card_type);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_add_allowed_type (LrgDeckDef  *self,
                                    LrgCardType  card_type);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_add_banned_card (LrgDeckDef *self,
                                   LrgCardDef *card_def);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_remove_banned_card (LrgDeckDef *self,
                                      LrgCardDef *card_def);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_def_is_card_banned (LrgDeckDef *self,
                                      LrgCardDef *card_def);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_ban_card (LrgDeckDef *self,
                            LrgCardDef *card_def);

LRG_AVAILABLE_IN_ALL
void lrg_deck_def_unban_card (LrgDeckDef *self,
                              LrgCardDef *card_def);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_def_can_add_card (LrgDeckDef *self,
                                    LrgCardDef *card_def);

/* Validation */

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_def_validate (LrgDeckDef  *self,
                                GError     **error);

G_END_DECLS
