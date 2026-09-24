/* lrg-cooldown-set.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCooldownSet - Per-ability, per-category and global cooldowns.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-ability-def.h"

G_BEGIN_DECLS

/**
 * LRG_COOLDOWN_SET_MAX_ENTRIES:
 *
 * Most running key cooldowns, and separately most running category
 * cooldowns, a set holds.
 */
#define LRG_COOLDOWN_SET_MAX_ENTRIES (1024)

/**
 * LRG_COOLDOWN_SET_MAX_SECONDS:
 *
 * Longest cooldown in seconds (one day); longer requests are clamped.
 */
#define LRG_COOLDOWN_SET_MAX_SECONDS (86400.0)

/**
 * LRG_COOLDOWN_SET_VARIANT_TYPE:
 *
 * GVariant type string of lrg_cooldown_set_to_variant(): remaining global
 * cooldown, (key, remaining) pairs sorted by key, (category, remaining)
 * pairs sorted by category.
 */
#define LRG_COOLDOWN_SET_VARIANT_TYPE "(da(sd)a(sd))"

#define LRG_TYPE_COOLDOWN_SET (lrg_cooldown_set_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCooldownSet, lrg_cooldown_set, LRG, COOLDOWN_SET, GObject)

/**
 * lrg_cooldown_set_new:
 *
 * Creates an empty set with a one-second #LrgCooldownSet:global-duration.
 *
 * Returns: (transfer full): a new #LrgCooldownSet
 */
LRG_AVAILABLE_IN_ALL
LrgCooldownSet *lrg_cooldown_set_new (void);

/**
 * lrg_cooldown_set_get_global_duration:
 * @self: an #LrgCooldownSet
 *
 * Returns: seconds lrg_cooldown_set_start_ability() puts on the global cooldown
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_cooldown_set_get_global_duration (LrgCooldownSet *self);

/**
 * lrg_cooldown_set_set_global_duration:
 * @self: an #LrgCooldownSet
 * @seconds: finite seconds, 0 to %LRG_COOLDOWN_SET_MAX_SECONDS
 *
 * Sets the global cooldown length used by lrg_cooldown_set_start_ability().
 */
LRG_AVAILABLE_IN_ALL
void lrg_cooldown_set_set_global_duration (LrgCooldownSet *self,
                                           gdouble         seconds);

/**
 * lrg_cooldown_set_start:
 * @self: an #LrgCooldownSet
 * @key: ability or item id (1-128 bytes of UTF-8)
 * @seconds: finite duration; clamped to %LRG_COOLDOWN_SET_MAX_SECONDS,
 *   zero or negative clears the cooldown
 *
 * Starts (or restarts) a cooldown for @key. When
 * %LRG_COOLDOWN_SET_MAX_ENTRIES keys already run, the entry with the least
 * time left (ties: smallest key) is dropped to make room.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cooldown_set_start (LrgCooldownSet *self,
                             const gchar    *key,
                             gdouble         seconds);

/**
 * lrg_cooldown_set_start_category:
 * @self: an #LrgCooldownSet
 * @category: shared category (1-128 bytes of UTF-8)
 * @seconds: finite duration with the same rules as lrg_cooldown_set_start()
 *
 * Starts (or restarts) a category cooldown.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cooldown_set_start_category (LrgCooldownSet *self,
                                      const gchar    *category,
                                      gdouble         seconds);

/**
 * lrg_cooldown_set_start_global:
 * @self: an #LrgCooldownSet
 * @seconds: finite duration, clamped to 0..%LRG_COOLDOWN_SET_MAX_SECONDS
 *
 * Starts (or restarts) the global cooldown.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cooldown_set_start_global (LrgCooldownSet *self,
                                    gdouble         seconds);

/**
 * lrg_cooldown_set_get_remaining:
 * @self: an #LrgCooldownSet
 * @key: (nullable): ability or item id
 *
 * Returns: seconds left on @key, 0 when ready
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_cooldown_set_get_remaining (LrgCooldownSet *self,
                                        const gchar    *key);

/**
 * lrg_cooldown_set_get_category_remaining:
 * @self: an #LrgCooldownSet
 * @category: (nullable): shared category
 *
 * Returns: seconds left on @category, 0 when ready
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_cooldown_set_get_category_remaining (LrgCooldownSet *self,
                                                 const gchar    *category);

/**
 * lrg_cooldown_set_get_global_remaining:
 * @self: an #LrgCooldownSet
 *
 * Returns: seconds left on the global cooldown
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_cooldown_set_get_global_remaining (LrgCooldownSet *self);

/**
 * lrg_cooldown_set_is_ready:
 * @self: an #LrgCooldownSet
 * @key: (nullable): ability or item id, %NULL to skip
 * @category: (nullable): shared category, %NULL to skip
 * @uses_global: whether the global cooldown also applies
 *
 * Returns: %TRUE when @key, @category and (if @uses_global) the global
 *   cooldown all have no time left
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cooldown_set_is_ready (LrgCooldownSet *self,
                                    const gchar    *key,
                                    const gchar    *category,
                                    gboolean        uses_global);

/**
 * lrg_cooldown_set_reset:
 * @self: an #LrgCooldownSet
 * @key: ability or item id
 *
 * Finishes the cooldown for @key immediately.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cooldown_set_reset (LrgCooldownSet *self,
                             const gchar    *key);

/**
 * lrg_cooldown_set_clear:
 * @self: an #LrgCooldownSet
 *
 * Finishes every key, category and global cooldown.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cooldown_set_clear (LrgCooldownSet *self);

/**
 * lrg_cooldown_set_tick:
 * @self: an #LrgCooldownSet
 * @delta: elapsed seconds; NaN, infinite, zero and negative values are ignored
 *
 * Advances every cooldown and removes finished entries.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cooldown_set_tick (LrgCooldownSet *self,
                            gdouble         delta);

/**
 * lrg_cooldown_set_get_count:
 * @self: an #LrgCooldownSet
 *
 * Returns: number of running key cooldowns (categories and global excluded)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_cooldown_set_get_count (LrgCooldownSet *self);

/**
 * lrg_cooldown_set_ability_ready:
 * @self: an #LrgCooldownSet
 * @def: ability definition
 *
 * lrg_cooldown_set_is_ready() with the definition's id, category and
 * triggers-gcd.
 *
 * Returns: %TRUE when @def is off cooldown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_cooldown_set_ability_ready (LrgCooldownSet *self,
                                         LrgAbilityDef  *def);

/**
 * lrg_cooldown_set_start_ability:
 * @self: an #LrgCooldownSet
 * @def: ability definition
 *
 * Starts the definition's cooldown (if positive), its category cooldown (if
 * it has a category and a positive category-cooldown) and, when it triggers
 * the GCD, the global cooldown for #LrgCooldownSet:global-duration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cooldown_set_start_ability (LrgCooldownSet *self,
                                     LrgAbilityDef  *def);

/**
 * lrg_cooldown_set_to_variant:
 * @self: an #LrgCooldownSet
 *
 * Serialises remaining times as %LRG_COOLDOWN_SET_VARIANT_TYPE. The
 * global-duration property is configuration and is not included.
 *
 * Returns: (transfer full): a non-floating #GVariant
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_cooldown_set_to_variant (LrgCooldownSet *self);

/**
 * lrg_cooldown_set_new_from_variant:
 * @variant: a %LRG_COOLDOWN_SET_VARIANT_TYPE variant
 * @error: (nullable): return location for a #GError
 *
 * Restores a set. Requires normal form, at most
 * %LRG_COOLDOWN_SET_MAX_ENTRIES entries per array, valid unique names and
 * finite times in 0..%LRG_COOLDOWN_SET_MAX_SECONDS (zero entries are
 * accepted and dropped as already finished); otherwise
 * %LRG_PROGRESSION_ERROR_INVALID.
 *
 * Returns: (transfer full) (nullable): the set, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgCooldownSet *lrg_cooldown_set_new_from_variant (GVariant  *variant,
                                                   GError   **error);

G_END_DECLS
