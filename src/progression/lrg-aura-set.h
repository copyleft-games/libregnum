/* lrg-aura-set.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAuraSet - The buffs and debuffs on one entity.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-aura.h"

G_BEGIN_DECLS

/**
 * LRG_AURA_SET_MAX_CAPACITY:
 *
 * Largest #LrgAuraSet:capacity.
 */
#define LRG_AURA_SET_MAX_CAPACITY (256)

/**
 * LRG_AURA_SET_DEFAULT_CAPACITY:
 *
 * Default #LrgAuraSet:capacity.
 */
#define LRG_AURA_SET_DEFAULT_CAPACITY (32)

/**
 * LRG_AURA_SET_MAX_TICKS_PER_CALL:
 *
 * Most #LrgAuraSet::aura-ticked emissions for one aura in one
 * lrg_aura_set_tick() call; further elapsed periods are dropped.
 */
#define LRG_AURA_SET_MAX_TICKS_PER_CALL (100)

/**
 * LRG_AURA_SET_VARIANT_TYPE:
 *
 * GVariant type string of lrg_aura_set_to_variant(): (capacity, auras in
 * application order). Each aura is (id, source, kind, dispel or "", stat or
 * "", magnitude, duration, remaining, period, tick_left, stacks, max_stacks).
 */
#define LRG_AURA_SET_VARIANT_TYPE "(ua(suussddddduu))"

#define LRG_TYPE_AURA_SET (lrg_aura_set_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAuraSet, lrg_aura_set, LRG, AURA_SET, GObject)

/**
 * lrg_aura_set_new:
 * @capacity: most simultaneous auras, 1 to %LRG_AURA_SET_MAX_CAPACITY
 *
 * Returns: (transfer full): a new, empty #LrgAuraSet
 */
LRG_AVAILABLE_IN_ALL
LrgAuraSet *lrg_aura_set_new (guint capacity);

/**
 * lrg_aura_set_get_capacity:
 * @self: an #LrgAuraSet
 *
 * Returns: most simultaneous auras
 */
LRG_AVAILABLE_IN_ALL
guint lrg_aura_set_get_capacity (LrgAuraSet *self);

/**
 * lrg_aura_set_set_capacity:
 * @self: an #LrgAuraSet
 * @capacity: 1 to %LRG_AURA_SET_MAX_CAPACITY and not below the current count
 *
 * Changes the capacity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_aura_set_set_capacity (LrgAuraSet *self,
                                guint       capacity);

/**
 * lrg_aura_set_apply:
 * @self: an #LrgAuraSet
 * @aura: aura to copy in; its @remaining and @tick_left are ignored
 *
 * Applies an aura. A new aura is copied in with @remaining = @duration and
 * @tick_left = @period. When the same (id, source) is present, its stacks
 * become MIN (stacks + 1, max_stacks) and it takes the incoming magnitude,
 * duration, period, max_stacks, kind, dispel and stat; @remaining is reset
 * to the duration and @tick_left is kept (clamped to the new period).
 * Invalid auras (see lrg_aura_is_valid()) and new auras on a full set are
 * rejected without changes.
 *
 * Returns: %LRG_AURA_APPLY_RESULT_ADDED, %LRG_AURA_APPLY_RESULT_STACKED when
 *   the stack count grew, %LRG_AURA_APPLY_RESULT_REFRESHED otherwise, or
 *   %LRG_AURA_APPLY_RESULT_REJECTED
 */
LRG_AVAILABLE_IN_ALL
LrgAuraApplyResult lrg_aura_set_apply (LrgAuraSet    *self,
                                       const LrgAura *aura);

/**
 * lrg_aura_set_remove:
 * @self: an #LrgAuraSet
 * @id: aura identifier
 * @source: source entity id
 *
 * Removes one aura, emitting #LrgAuraSet::aura-removed.
 *
 * Returns: %TRUE if the aura was present
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_aura_set_remove (LrgAuraSet  *self,
                              const gchar *id,
                              guint        source);

/**
 * lrg_aura_set_remove_all_by_id:
 * @self: an #LrgAuraSet
 * @id: aura identifier
 *
 * Removes @id from every source, emitting #LrgAuraSet::aura-removed for each.
 *
 * Returns: number of auras removed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_aura_set_remove_all_by_id (LrgAuraSet  *self,
                                     const gchar *id);

/**
 * lrg_aura_set_dispel:
 * @self: an #LrgAuraSet
 * @kind: buff or debuff
 * @dispel: (nullable): dispel type to match, or %NULL for any dispellable
 *   aura (auras whose dispel is %NULL are never dispelled)
 * @max_count: most auras to remove (0 removes none)
 *
 * Removes matching auras oldest first, emitting #LrgAuraSet::aura-removed.
 *
 * Returns: number of auras removed
 */
LRG_AVAILABLE_IN_ALL
guint lrg_aura_set_dispel (LrgAuraSet  *self,
                           LrgAuraKind  kind,
                           const gchar *dispel,
                           guint        max_count);

/**
 * lrg_aura_set_get:
 * @self: an #LrgAuraSet
 * @id: aura identifier
 * @source: source entity id
 *
 * Returns: (transfer none) (nullable): the aura, valid until it is removed
 */
LRG_AVAILABLE_IN_ALL
LrgAura *lrg_aura_set_get (LrgAuraSet  *self,
                           const gchar *id,
                           guint        source);

/**
 * lrg_aura_set_has:
 * @self: an #LrgAuraSet
 * @id: aura identifier
 *
 * Returns: %TRUE if @id is present from any source
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_aura_set_has (LrgAuraSet  *self,
                           const gchar *id);

/**
 * lrg_aura_set_get_all:
 * @self: an #LrgAuraSet
 *
 * Returns: (transfer none) (element-type LrgAura): auras in application order;
 *   do not modify the array
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_aura_set_get_all (LrgAuraSet *self);

/**
 * lrg_aura_set_get_count:
 * @self: an #LrgAuraSet
 *
 * Returns: number of auras
 */
LRG_AVAILABLE_IN_ALL
guint lrg_aura_set_get_count (LrgAuraSet *self);

/**
 * lrg_aura_set_sum_stat:
 * @self: an #LrgAuraSet
 * @stat: stat or effect key
 *
 * Returns: sum of magnitude times stacks over auras whose stat is @stat
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_aura_set_sum_stat (LrgAuraSet  *self,
                               const gchar *stat);

/**
 * lrg_aura_set_tick:
 * @self: an #LrgAuraSet
 * @delta: elapsed seconds; NaN, infinite, zero and negative are ignored
 *
 * Advances auras in application order. For each aura, periodic ticks that
 * fall within its remaining lifetime emit #LrgAuraSet::aura-ticked once per
 * elapsed period (at most %LRG_AURA_SET_MAX_TICKS_PER_CALL), then a timed
 * aura that ran out emits #LrgAuraSet::aura-expired and is removed.
 * Handlers may add or remove auras; removed auras stay valid until the
 * outermost emission returns.
 */
LRG_AVAILABLE_IN_ALL
void lrg_aura_set_tick (LrgAuraSet *self,
                        gdouble     delta);

/**
 * lrg_aura_set_clear:
 * @self: an #LrgAuraSet
 *
 * Removes every aura, emitting #LrgAuraSet::aura-removed for each in order.
 */
LRG_AVAILABLE_IN_ALL
void lrg_aura_set_clear (LrgAuraSet *self);

/**
 * lrg_aura_set_to_variant:
 * @self: an #LrgAuraSet
 *
 * Serialises the set as %LRG_AURA_SET_VARIANT_TYPE.
 *
 * Returns: (transfer full): a non-floating #GVariant
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_aura_set_to_variant (LrgAuraSet *self);

/**
 * lrg_aura_set_new_from_variant:
 * @variant: a %LRG_AURA_SET_VARIANT_TYPE variant
 * @error: (nullable): return location for a #GError
 *
 * Restores a set. Requires normal form, capacity 1..%LRG_AURA_SET_MAX_CAPACITY,
 * no more auras than the capacity, every aura valid per lrg_aura_is_valid()
 * ("" dispel/stat mean none) and unique (id, source) pairs; otherwise
 * %LRG_PROGRESSION_ERROR_INVALID.
 *
 * Returns: (transfer full) (nullable): the set, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgAuraSet *lrg_aura_set_new_from_variant (GVariant  *variant,
                                           GError   **error);

G_END_DECLS
