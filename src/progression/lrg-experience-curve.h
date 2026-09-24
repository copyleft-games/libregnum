/* lrg-experience-curve.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgExperienceCurve - Level/XP curve with rested experience.
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
 * LRG_EXPERIENCE_CURVE_MAX_LEVEL_CAP:
 *
 * Largest supported #LrgExperienceCurve:level-cap.
 */
#define LRG_EXPERIENCE_CURVE_MAX_LEVEL_CAP (1000)

#define LRG_TYPE_EXPERIENCE_CURVE (lrg_experience_curve_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgExperienceCurve, lrg_experience_curve, LRG, EXPERIENCE_CURVE, GObject)

/**
 * lrg_experience_curve_new:
 * @level_cap: highest reachable level, 1 to %LRG_EXPERIENCE_CURVE_MAX_LEVEL_CAP
 *
 * Creates an experience curve. The default formula is
 * `100 + 100*L + 10*L^2` experience to advance from level L to L+1; replace
 * it with lrg_experience_curve_set_formula() or lrg_experience_curve_set_table().
 *
 * Returns: (transfer full): a new #LrgExperienceCurve
 */
LRG_AVAILABLE_IN_ALL
LrgExperienceCurve *lrg_experience_curve_new (guint level_cap);

/**
 * lrg_experience_curve_get_level_cap:
 * @self: an #LrgExperienceCurve
 *
 * Returns: the highest reachable level
 */
LRG_AVAILABLE_IN_ALL
guint lrg_experience_curve_get_level_cap (LrgExperienceCurve *self);

/**
 * lrg_experience_curve_set_level_cap:
 * @self: an #LrgExperienceCurve
 * @level_cap: new cap, 1 to %LRG_EXPERIENCE_CURVE_MAX_LEVEL_CAP
 *
 * Changes the level cap. An explicit table no longer matches the new cap, so
 * changing the cap discards any table and the formula is used again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_experience_curve_set_level_cap (LrgExperienceCurve *self,
                                         guint               level_cap);

/**
 * lrg_experience_curve_get_rested_rate:
 * @self: an #LrgExperienceCurve
 *
 * Returns: fraction of the current level's requirement accrued per hour of
 *   rest inside a rest area
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_experience_curve_get_rested_rate (LrgExperienceCurve *self);

/**
 * lrg_experience_curve_set_rested_rate:
 * @self: an #LrgExperienceCurve
 * @rate: finite fraction per hour, 0 to 10
 *
 * Sets the rested accrual rate.
 */
LRG_AVAILABLE_IN_ALL
void lrg_experience_curve_set_rested_rate (LrgExperienceCurve *self,
                                           gdouble             rate);

/**
 * lrg_experience_curve_get_rested_cap:
 * @self: an #LrgExperienceCurve
 *
 * Returns: rested pool cap as a fraction of the current level's requirement
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_experience_curve_get_rested_cap (LrgExperienceCurve *self);

/**
 * lrg_experience_curve_set_rested_cap:
 * @self: an #LrgExperienceCurve
 * @cap: finite fraction of a level, 0 to 100
 *
 * Sets the rested pool cap.
 */
LRG_AVAILABLE_IN_ALL
void lrg_experience_curve_set_rested_cap (LrgExperienceCurve *self,
                                          gdouble             cap);

/**
 * lrg_experience_curve_get_rested_multiplier:
 * @self: an #LrgExperienceCurve
 *
 * Returns: multiplier applied to awards while rested experience remains
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_experience_curve_get_rested_multiplier (LrgExperienceCurve *self);

/**
 * lrg_experience_curve_set_rested_multiplier:
 * @self: an #LrgExperienceCurve
 * @multiplier: finite multiplier, 1 to 100
 *
 * Sets the rested multiplier. 2.0 doubles awards while the pool lasts.
 */
LRG_AVAILABLE_IN_ALL
void lrg_experience_curve_set_rested_multiplier (LrgExperienceCurve *self,
                                                 gdouble             multiplier);

/**
 * lrg_experience_curve_set_formula:
 * @self: an #LrgExperienceCurve
 * @base: constant term
 * @linear: coefficient of L
 * @quadratic: coefficient of L squared
 * @cubic: coefficient of L cubed
 *
 * Sets the polynomial used when no table is installed. The experience needed
 * to advance from level L to L+1 is
 * `round(base + linear*L + quadratic*L^2 + cubic*L^3)`, never less than 1 and
 * saturating at %G_MAXUINT64. All coefficients must be finite. Installing a
 * formula also discards any explicit table.
 */
LRG_AVAILABLE_IN_ALL
void lrg_experience_curve_set_formula (LrgExperienceCurve *self,
                                       gdouble             base,
                                       gdouble             linear,
                                       gdouble             quadratic,
                                       gdouble             cubic);

/**
 * lrg_experience_curve_get_formula:
 * @self: an #LrgExperienceCurve
 * @base: (out) (optional): constant term
 * @linear: (out) (optional): coefficient of L
 * @quadratic: (out) (optional): coefficient of L squared
 * @cubic: (out) (optional): coefficient of L cubed
 *
 * Reads the formula coefficients.
 */
LRG_AVAILABLE_IN_ALL
void lrg_experience_curve_get_formula (LrgExperienceCurve *self,
                                       gdouble            *base,
                                       gdouble            *linear,
                                       gdouble            *quadratic,
                                       gdouble            *cubic);

/**
 * lrg_experience_curve_set_table:
 * @self: an #LrgExperienceCurve
 * @table: (array length=n_levels) (nullable): experience per level; entry i
 *   is the experience needed to go from level i+1 to level i+2
 * @n_levels: number of entries, which must equal level-cap minus one
 * @error: (nullable): return location for a #GError
 *
 * Installs an explicit table that overrides the formula. Every entry must be
 * at least 1. On error (%LRG_PROGRESSION_ERROR_INVALID) the curve is unchanged.
 *
 * Returns: %TRUE if the table was installed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_experience_curve_set_table (LrgExperienceCurve *self,
                                         const guint64      *table,
                                         gsize               n_levels,
                                         GError            **error);

/**
 * lrg_experience_curve_clear_table:
 * @self: an #LrgExperienceCurve
 *
 * Removes an explicit table so the formula is used again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_experience_curve_clear_table (LrgExperienceCurve *self);

/**
 * lrg_experience_curve_has_table:
 * @self: an #LrgExperienceCurve
 *
 * Returns: %TRUE when an explicit table overrides the formula
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_experience_curve_has_table (LrgExperienceCurve *self);

/**
 * lrg_experience_curve_get_xp_for_level:
 * @self: an #LrgExperienceCurve
 * @level: current level
 *
 * Returns: experience needed to advance from @level to @level + 1; 0 for
 *   level 0 or at/after the level cap
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_experience_curve_get_xp_for_level (LrgExperienceCurve *self,
                                               guint               level);

/**
 * lrg_experience_curve_get_total_xp_for_level:
 * @self: an #LrgExperienceCurve
 * @level: target level; values above the cap are treated as the cap
 *
 * Returns: cumulative experience needed to reach @level from level 1
 *   (0 for levels 0 and 1), saturating at %G_MAXUINT64
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_experience_curve_get_total_xp_for_level (LrgExperienceCurve *self,
                                                     guint               level);

/**
 * lrg_experience_curve_award:
 * @self: an #LrgExperienceCurve
 * @level: (inout): character level; values outside 1..cap are clamped first
 * @xp: (inout): experience progress within @level; 0 at the cap
 * @amount: experience to award before the rested bonus
 * @rested_pool: (inout) (optional) (nullable): rested pool to consume
 *
 * Awards experience, levelling as many times as the total allows. When
 * @rested_pool is given, a bonus of
 * `MIN (*rested_pool, amount * (rested-multiplier - 1))` is added and removed
 * from the pool. At the level cap nothing is awarded, the pool is untouched
 * and @xp is set to 0. All arithmetic saturates.
 *
 * Returns: number of levels gained
 */
LRG_AVAILABLE_IN_ALL
guint lrg_experience_curve_award (LrgExperienceCurve *self,
                                  guint              *level,
                                  guint64            *xp,
                                  guint64             amount,
                                  guint64            *rested_pool);

/**
 * lrg_experience_curve_get_rested_limit:
 * @self: an #LrgExperienceCurve
 * @level: character level
 *
 * Returns: largest pool accrual can reach at @level
 *   (rested-cap times the level requirement); 0 at the cap
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_experience_curve_get_rested_limit (LrgExperienceCurve *self,
                                               guint               level);

/**
 * lrg_experience_curve_accrue_rested:
 * @self: an #LrgExperienceCurve
 * @level: character level
 * @pool: current rested pool
 * @seconds: seconds rested; zero or negative accrues nothing
 * @in_rest_area: %TRUE inside an inn or city; outside, a quarter of the rate
 *   applies
 *
 * Computes the pool after resting. Accrual stops at
 * lrg_experience_curve_get_rested_limit() but never reduces an existing pool.
 *
 * Returns: the new pool
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_experience_curve_accrue_rested (LrgExperienceCurve *self,
                                            guint               level,
                                            guint64             pool,
                                            gint64              seconds,
                                            gboolean            in_rest_area);

/**
 * lrg_experience_curve_validate:
 * @self: an #LrgExperienceCurve
 * @level: level to check
 * @xp: progress to check
 * @error: (nullable): return location for a #GError
 *
 * Checks a persisted (level, xp) pair: level in 1..cap and xp below the level
 * requirement, or exactly 0 at the cap. Fails with
 * %LRG_PROGRESSION_ERROR_INVALID.
 *
 * Returns: %TRUE when the pair is consistent with this curve
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_experience_curve_validate (LrgExperienceCurve *self,
                                        guint               level,
                                        guint64             xp,
                                        GError            **error);

G_END_DECLS
