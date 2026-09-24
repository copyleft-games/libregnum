/* lrg-vital.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVital - A regenerating or decaying resource pool (boxed).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_VITAL_MAX_VALUE:
 *
 * Largest accepted maximum, and largest absolute regeneration rate.
 */
#define LRG_VITAL_MAX_VALUE (1000000000.0)

/**
 * LRG_VITAL_VARIANT_TYPE:
 *
 * GVariant type string of lrg_vital_to_variant():
 * (kind, current, maximum, regen, idle_regen).
 */
#define LRG_VITAL_VARIANT_TYPE "(sdddd)"

#define LRG_TYPE_VITAL (lrg_vital_get_type ())

/**
 * LrgVital:
 * @kind: resource kind such as "mana", "rage" or "energy"
 * @current: current amount, 0 to @maximum
 * @maximum: capacity, greater than 0
 * @regen: change per second while in combat
 * @idle_regen: change per second out of combat; negative values decay
 *   (rage)
 *
 * A resource pool. Fields may be read directly; use the functions to change
 * them so the clamping rules hold.
 */
struct _LrgVital
{
    gchar   *kind;
    gdouble  current;
    gdouble  maximum;
    gdouble  regen;
    gdouble  idle_regen;
};

LRG_AVAILABLE_IN_ALL
GType lrg_vital_get_type (void) G_GNUC_CONST;

/**
 * lrg_vital_new:
 * @kind: resource kind (1-128 bytes of UTF-8)
 * @maximum: finite capacity in (0, %LRG_VITAL_MAX_VALUE]
 * @regen: finite in-combat rate per second
 * @idle_regen: finite out-of-combat rate per second
 *
 * Creates a pool that starts full when @idle_regen is not negative and
 * empty otherwise (rage-like resources).
 *
 * Returns: (transfer full): a new #LrgVital
 */
LRG_AVAILABLE_IN_ALL
LrgVital *lrg_vital_new (const gchar *kind,
                         gdouble      maximum,
                         gdouble      regen,
                         gdouble      idle_regen);

/**
 * lrg_vital_copy:
 * @self: an #LrgVital
 *
 * Returns: (transfer full): a deep copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgVital *lrg_vital_copy (const LrgVital *self);

/**
 * lrg_vital_free:
 * @self: (nullable): an #LrgVital
 *
 * Frees a pool.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vital_free (LrgVital *self);

/**
 * lrg_vital_tick:
 * @self: an #LrgVital
 * @delta: elapsed seconds; NaN, infinite and negative values are ignored
 * @in_combat: selects @regen (%TRUE) or @idle_regen (%FALSE)
 *
 * Regenerates or decays the pool, clamped to 0..@maximum.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vital_tick (LrgVital *self,
                     gdouble   delta,
                     gboolean  in_combat);

/**
 * lrg_vital_spend:
 * @self: an #LrgVital
 * @amount: finite, non-negative amount
 *
 * Spends atomically: either the whole amount is removed or nothing changes.
 *
 * Returns: %TRUE if spent; %FALSE (unchanged) when insufficient or @amount
 *   is negative or not finite
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vital_spend (LrgVital *self,
                          gdouble   amount);

/**
 * lrg_vital_gain:
 * @self: an #LrgVital
 * @amount: amount to add; negative drains; non-finite is ignored
 *
 * Adds to the pool, clamped to 0..@maximum.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vital_gain (LrgVital *self,
                     gdouble   amount);

/**
 * lrg_vital_set_maximum:
 * @self: an #LrgVital
 * @maximum: finite capacity in (0, %LRG_VITAL_MAX_VALUE]
 * @keep_ratio: %TRUE to keep the current fraction; %FALSE keeps the current
 *   amount, clamped to the new maximum
 *
 * Changes the capacity.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vital_set_maximum (LrgVital *self,
                            gdouble   maximum,
                            gboolean  keep_ratio);

/**
 * lrg_vital_get_fraction:
 * @self: an #LrgVital
 *
 * Returns: @current divided by @maximum, 0 to 1
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_vital_get_fraction (const LrgVital *self);

/**
 * lrg_vital_to_variant:
 * @self: an #LrgVital
 *
 * Serialises the pool as %LRG_VITAL_VARIANT_TYPE.
 *
 * Returns: (transfer full): a non-floating #GVariant
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_vital_to_variant (const LrgVital *self);

/**
 * lrg_vital_new_from_variant:
 * @variant: a %LRG_VITAL_VARIANT_TYPE variant
 * @error: (nullable): return location for a #GError
 *
 * Restores a pool. Requires normal form, a valid kind, finite numbers,
 * 0 < maximum <= %LRG_VITAL_MAX_VALUE, 0 <= current <= maximum and rates
 * within +/-%LRG_VITAL_MAX_VALUE; otherwise %LRG_PROGRESSION_ERROR_INVALID.
 *
 * Returns: (transfer full) (nullable): the pool, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgVital *lrg_vital_new_from_variant (GVariant  *variant,
                                      GError   **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgVital, lrg_vital_free)

G_END_DECLS
