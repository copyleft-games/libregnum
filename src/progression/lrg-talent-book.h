/* lrg-talent-book.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTalentBook - dual/multi specialisation: a set of talent loadouts
 * of which one is active.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-talent-loadout.h"

G_BEGIN_DECLS

/**
 * LRG_TALENT_BOOK_DEFAULT_CAPACITY:
 *
 * Default number of loadouts a book can hold (dual spec).
 */
#define LRG_TALENT_BOOK_DEFAULT_CAPACITY (2)

/**
 * LRG_TALENT_BOOK_MAX_CAPACITY:
 *
 * Largest permitted book capacity.
 */
#define LRG_TALENT_BOOK_MAX_CAPACITY (8)

/**
 * LRG_TALENT_BOOK_VARIANT_TYPE:
 *
 * GVariant type string of a persisted book: (class_id, unlocked count,
 * active index, respec count, one "(smsa(ssu))" loadout variant per
 * unlocked loadout).
 */
#define LRG_TALENT_BOOK_VARIANT_TYPE "(suuuav)"

#define LRG_TYPE_TALENT_BOOK (lrg_talent_book_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTalentBook, lrg_talent_book, LRG, TALENT_BOOK, GObject)

/**
 * lrg_talent_book_new:
 * @class_id: class id (construct-only)
 * @capacity: maximum number of loadouts, 1..%LRG_TALENT_BOOK_MAX_CAPACITY
 *
 * Creates a book with one unlocked, empty, active loadout.
 *
 * Returns: (transfer full): a new #LrgTalentBook
 */
LRG_AVAILABLE_IN_ALL
LrgTalentBook *lrg_talent_book_new (const gchar *class_id,
                                    guint        capacity);

/**
 * lrg_talent_book_get_class_id:
 * @self: a #LrgTalentBook
 *
 * Returns: (transfer none): the class id
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_talent_book_get_class_id (LrgTalentBook *self);

/**
 * lrg_talent_book_get_capacity:
 * @self: a #LrgTalentBook
 *
 * Returns: the maximum number of loadouts
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_book_get_capacity (LrgTalentBook *self);

/**
 * lrg_talent_book_get_unlocked:
 * @self: a #LrgTalentBook
 *
 * Returns: number of unlocked loadouts (1..capacity)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_book_get_unlocked (LrgTalentBook *self);

/**
 * lrg_talent_book_unlock:
 * @self: a #LrgTalentBook
 * @error: (nullable): return location for an error
 *
 * Unlocks the next loadout slot with an empty loadout. Fails with
 * %LRG_PROGRESSION_ERROR_LIMIT when every slot is already unlocked.
 *
 * Returns: %TRUE if a slot was unlocked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_talent_book_unlock (LrgTalentBook  *self,
                                 GError        **error);

/**
 * lrg_talent_book_get_active_index:
 * @self: a #LrgTalentBook
 *
 * Returns: index of the active loadout
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_book_get_active_index (LrgTalentBook *self);

/**
 * lrg_talent_book_set_active:
 * @self: a #LrgTalentBook
 * @index: loadout index
 * @error: (nullable): return location for an error
 *
 * Switches the active loadout. Fails with %LRG_PROGRESSION_ERROR_INVALID
 * when @index >= capacity and %LRG_PROGRESSION_ERROR_REQUIREMENT when the
 * slot is still locked. Setting the already active index succeeds.
 *
 * Returns: %TRUE if @index is now active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_talent_book_set_active (LrgTalentBook  *self,
                                     guint           index,
                                     GError        **error);

/**
 * lrg_talent_book_get_loadout:
 * @self: a #LrgTalentBook
 * @index: loadout index
 *
 * Returns: (transfer none) (nullable): the loadout, %NULL if locked or out
 *   of range
 */
LRG_AVAILABLE_IN_ALL
LrgTalentLoadout *lrg_talent_book_get_loadout (LrgTalentBook *self,
                                               guint          index);

/**
 * lrg_talent_book_get_active:
 * @self: a #LrgTalentBook
 *
 * Returns: (transfer none): the active loadout
 */
LRG_AVAILABLE_IN_ALL
LrgTalentLoadout *lrg_talent_book_get_active (LrgTalentBook *self);

/**
 * lrg_talent_book_get_respec_count:
 * @self: a #LrgTalentBook
 *
 * Returns: number of recorded respecs (for escalating respec prices)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_talent_book_get_respec_count (LrgTalentBook *self);

/**
 * lrg_talent_book_record_respec:
 * @self: a #LrgTalentBook
 *
 * Increments the respec counter (saturating). The caller resets the
 * loadout and charges the price itself.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_book_record_respec (LrgTalentBook *self);

/**
 * lrg_talent_book_set_respec_count:
 * @self: a #LrgTalentBook
 * @count: new respec count (e.g. decayed over time by the game)
 *
 * Sets the respec counter.
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_book_set_respec_count (LrgTalentBook *self,
                                       guint          count);

/**
 * lrg_talent_book_set_point_rules:
 * @self: a #LrgTalentBook
 * @first_level: level granting the first point, at least 1
 * @points_per_level: points per level, 0..100
 *
 * Applies first-level and points-per-level to every loadout, including
 * loadouts unlocked later. These rules are not persisted; apply them after
 * lrg_talent_book_new_from_variant().
 */
LRG_AVAILABLE_IN_ALL
void lrg_talent_book_set_point_rules (LrgTalentBook *self,
                                      guint          first_level,
                                      guint          points_per_level);

/**
 * lrg_talent_book_validate:
 * @self: a #LrgTalentBook
 * @trees: (element-type utf8 LrgTalentTree): trees by id
 * @level: character level
 * @error: (nullable): return location for an error
 *
 * Runs lrg_talent_loadout_validate() on every unlocked loadout.
 *
 * Returns: %TRUE if every loadout is legal
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_talent_book_validate (LrgTalentBook  *self,
                                   GHashTable     *trees,
                                   guint           level,
                                   GError        **error);

/**
 * lrg_talent_book_to_variant:
 * @self: a #LrgTalentBook
 *
 * Serialises to %LRG_TALENT_BOOK_VARIANT_TYPE "(suuuav)". The capacity
 * is a game rule and is not stored.
 *
 * Returns: (transfer full): a non-floating variant
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_talent_book_to_variant (LrgTalentBook *self);

/**
 * lrg_talent_book_new_from_variant:
 * @variant: a "(suuuav)" variant
 * @error: (nullable): return location for an error
 *
 * Restores a book with capacity MAX(%LRG_TALENT_BOOK_DEFAULT_CAPACITY,
 * unlocked). See lrg_talent_book_new_from_variant_full().
 *
 * Returns: (transfer full) (nullable): the book or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgTalentBook *lrg_talent_book_new_from_variant (GVariant  *variant,
                                                 GError   **error);

/**
 * lrg_talent_book_new_from_variant_full:
 * @variant: a "(suuuav)" variant
 * @capacity: capacity of the restored book, 1..%LRG_TALENT_BOOK_MAX_CAPACITY
 * @error: (nullable): return location for an error
 *
 * Restores a book. Rejects with %LRG_PROGRESSION_ERROR_INVALID: a wrong
 * type string, non-normal form, an invalid class id, unlocked outside
 * 1..@capacity, active >= unlocked, a loadout count different from
 * unlocked, any loadout that lrg_talent_loadout_new_from_variant() rejects
 * and any loadout of another class. Nothing is leaked on failure.
 *
 * Returns: (transfer full) (nullable): the book or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgTalentBook *lrg_talent_book_new_from_variant_full (GVariant  *variant,
                                                      guint      capacity,
                                                      GError   **error);

G_END_DECLS
