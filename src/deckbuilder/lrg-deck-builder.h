/* lrg-deck-builder.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDeckBuilder - Utility for constructing and validating decks.
 *
 * Provides validation, card limits, and construction helpers
 * for deck building during runs.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_DECK_BUILDER (lrg_deck_builder_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDeckBuilder, lrg_deck_builder, LRG, DECK_BUILDER, GObject)

/* Forward declarations */
typedef struct _LrgDeckDef      LrgDeckDef;
typedef struct _LrgDeckInstance LrgDeckInstance;
typedef struct _LrgCardDef      LrgCardDef;
typedef struct _LrgCardInstance LrgCardInstance;

/* Constructors */

LRG_AVAILABLE_IN_ALL
LrgDeckBuilder * lrg_deck_builder_new (void);

LRG_AVAILABLE_IN_ALL
LrgDeckBuilder * lrg_deck_builder_new_with_def (LrgDeckDef *deck_def);

/* Configuration */

LRG_AVAILABLE_IN_ALL
void lrg_deck_builder_set_deck_def (LrgDeckBuilder *self,
                                    LrgDeckDef     *deck_def);

LRG_AVAILABLE_IN_ALL
LrgDeckDef * lrg_deck_builder_get_deck_def (LrgDeckBuilder *self);

LRG_AVAILABLE_IN_ALL
void lrg_deck_builder_set_max_copies (LrgDeckBuilder *self,
                                      guint           max_copies);

LRG_AVAILABLE_IN_ALL
guint lrg_deck_builder_get_max_copies (LrgDeckBuilder *self);

/* Validation */

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_builder_can_add_card (LrgDeckBuilder  *self,
                                        LrgDeckInstance *deck,
                                        LrgCardDef      *card_def,
                                        GError         **error);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_builder_validate_deck (LrgDeckBuilder  *self,
                                         LrgDeckInstance *deck,
                                         GError         **error);

/* Card operations */

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_builder_add_card (LrgDeckBuilder  *self,
                                    LrgDeckInstance *deck,
                                    LrgCardDef      *card_def,
                                    GError         **error);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_builder_remove_card (LrgDeckBuilder  *self,
                                       LrgDeckInstance *deck,
                                       LrgCardInstance *card,
                                       GError         **error);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_builder_upgrade_card (LrgDeckBuilder  *self,
                                        LrgDeckInstance *deck,
                                        LrgCardInstance *card,
                                        GError         **error);

LRG_AVAILABLE_IN_ALL
gboolean lrg_deck_builder_transform_card (LrgDeckBuilder  *self,
                                          LrgDeckInstance *deck,
                                          LrgCardInstance *old_card,
                                          LrgCardDef      *new_card_def,
                                          GError         **error);

/* Deck construction */

LRG_AVAILABLE_IN_ALL
LrgDeckInstance * lrg_deck_builder_build (LrgDeckBuilder *self,
                                          GError        **error);

LRG_AVAILABLE_IN_ALL
LrgDeckInstance * lrg_deck_builder_build_with_seed (LrgDeckBuilder *self,
                                                    guint32         seed,
                                                    GError        **error);

G_END_DECLS
