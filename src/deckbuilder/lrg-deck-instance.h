/* lrg-deck-instance.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDeckInstance - Runtime deck state.
 *
 * This is a final type that tracks the current state of a deck
 * during a run. It manages the draw pile, discard pile, exhaust pile,
 * and hand. Implements LrgSaveable for persistence.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_DECK_INSTANCE (lrg_deck_instance_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDeckInstance, lrg_deck_instance, LRG, DECK_INSTANCE, GObject)

/* Forward declarations */
typedef struct _LrgDeckDef      LrgDeckDef;
typedef struct _LrgCardDef      LrgCardDef;
typedef struct _LrgCardInstance LrgCardInstance;
typedef struct _LrgCardPile     LrgCardPile;
typedef struct _LrgHand         LrgHand;

/* Constructors */

LRG_AVAILABLE_IN_ALL
LrgDeckInstance * lrg_deck_instance_new (LrgDeckDef *deck_def);

LRG_AVAILABLE_IN_ALL
LrgDeckInstance * lrg_deck_instance_new_with_seed (LrgDeckDef *deck_def,
                                                   guint32     seed);

/* Properties */

LRG_AVAILABLE_IN_ALL
LrgDeckDef * lrg_deck_instance_get_def (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
guint32 lrg_deck_instance_get_seed (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
GRand * lrg_deck_instance_get_rng (LrgDeckInstance *self);

/* Piles and hand */

LRG_AVAILABLE_IN_ALL
LrgCardPile * lrg_deck_instance_get_draw_pile (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
LrgCardPile * lrg_deck_instance_get_discard_pile (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
LrgCardPile * lrg_deck_instance_get_exhaust_pile (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
LrgHand * lrg_deck_instance_get_hand (LrgDeckInstance *self);

/* Deck operations */

LRG_AVAILABLE_IN_ALL
void lrg_deck_instance_setup (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
void lrg_deck_instance_shuffle_draw_pile (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
void lrg_deck_instance_shuffle_discard_into_draw (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
LrgCardInstance * lrg_deck_instance_draw_card (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
guint lrg_deck_instance_draw_cards (LrgDeckInstance *self,
                                    guint            count);

LRG_AVAILABLE_IN_ALL
void lrg_deck_instance_discard_hand (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
void lrg_deck_instance_end_combat (LrgDeckInstance *self);

/* Card manipulation */

LRG_AVAILABLE_IN_ALL
void lrg_deck_instance_add_card (LrgDeckInstance *self,
                                 LrgCardDef      *card_def);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_instance_remove_card (LrgDeckInstance *self,
                                        LrgCardInstance *card);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_instance_upgrade_card (LrgDeckInstance *self,
                                         LrgCardInstance *card);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_instance_transform_card (LrgDeckInstance *self,
                                           LrgCardInstance *old_card,
                                           LrgCardDef      *new_card_def);

/* Statistics */

LRG_AVAILABLE_IN_ALL
guint lrg_deck_instance_get_total_cards (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
guint lrg_deck_instance_count_card_def (LrgDeckInstance *self,
                                        LrgCardDef      *card_def);

LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_deck_instance_get_all_cards (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_deck_instance_find_cards_by_def (LrgDeckInstance *self,
                                                 LrgCardDef      *card_def);

/* Master deck (all cards in run) */

LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_deck_instance_get_master_deck (LrgDeckInstance *self);

LRG_AVAILABLE_IN_ALL
guint lrg_deck_instance_get_master_deck_size (LrgDeckInstance *self);

G_END_DECLS
