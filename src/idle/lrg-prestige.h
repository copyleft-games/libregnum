/* lrg-prestige.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPrestige - Prestige layer reset mechanics for idle games.
 *
 * Prestige systems allow players to reset progress in exchange for
 * permanent bonuses, creating a compelling progression loop.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-big-number.h"

G_BEGIN_DECLS

#define LRG_TYPE_PRESTIGE (lrg_prestige_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgPrestige, lrg_prestige, LRG, PRESTIGE, GObject)

/**
 * LrgPrestigeClass:
 * @parent_class: Parent class
 * @calculate_reward: Calculate prestige points to be gained
 * @can_prestige: Check if prestige is available
 * @on_prestige: Called when prestige is performed (for subclass cleanup)
 * @get_bonus_multiplier: Calculate bonus multiplier from prestige points
 *
 * Virtual table for #LrgPrestige.
 *
 * Since: 1.0
 */
struct _LrgPrestigeClass
{
    GObjectClass parent_class;

    /*< public >*/

    /**
     * LrgPrestigeClass::calculate_reward:
     * @self: an #LrgPrestige
     * @current_value: Current accumulated value
     *
     * Calculates how many prestige points would be gained.
     *
     * Returns: (transfer full): Prestige points to gain
     */
    LrgBigNumber * (*calculate_reward)     (LrgPrestige        *self,
                                            const LrgBigNumber *current_value);

    /**
     * LrgPrestigeClass::can_prestige:
     * @self: an #LrgPrestige
     * @current_value: Current accumulated value
     *
     * Checks if prestige requirements are met.
     *
     * Returns: %TRUE if prestige is available
     */
    gboolean       (*can_prestige)         (LrgPrestige        *self,
                                            const LrgBigNumber *current_value);

    /**
     * LrgPrestigeClass::on_prestige:
     * @self: an #LrgPrestige
     * @reward: Points being awarded
     *
     * Called when prestige is performed. Subclasses can override
     * to perform additional cleanup or state changes.
     */
    void           (*on_prestige)          (LrgPrestige        *self,
                                            const LrgBigNumber *reward);

    /**
     * LrgPrestigeClass::get_bonus_multiplier:
     * @self: an #LrgPrestige
     * @prestige_points: Current prestige points
     *
     * Calculates the bonus multiplier from prestige points.
     *
     * Returns: Multiplier value (1.0 = no bonus)
     */
    gdouble        (*get_bonus_multiplier) (LrgPrestige        *self,
                                            const LrgBigNumber *prestige_points);

    /*< private >*/
    gpointer _reserved[8];
};

/* Construction */

/**
 * lrg_prestige_new:
 *
 * Creates a new prestige layer with default settings.
 *
 * Returns: (transfer full): A new #LrgPrestige
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPrestige *
lrg_prestige_new (void);

/* Configuration */

/**
 * lrg_prestige_get_id:
 * @self: an #LrgPrestige
 *
 * Gets the prestige layer ID.
 *
 * Returns: (transfer none) (nullable): The ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_prestige_get_id (LrgPrestige *self);

/**
 * lrg_prestige_set_id:
 * @self: an #LrgPrestige
 * @id: (nullable): New ID
 *
 * Sets the prestige layer ID.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_prestige_set_id (LrgPrestige *self,
                     const gchar *id);

/**
 * lrg_prestige_get_name:
 * @self: an #LrgPrestige
 *
 * Gets the display name.
 *
 * Returns: (transfer none) (nullable): The name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_prestige_get_name (LrgPrestige *self);

/**
 * lrg_prestige_set_name:
 * @self: an #LrgPrestige
 * @name: (nullable): New name
 *
 * Sets the display name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_prestige_set_name (LrgPrestige *self,
                       const gchar *name);

/**
 * lrg_prestige_get_threshold:
 * @self: an #LrgPrestige
 *
 * Gets the minimum value required to prestige.
 *
 * Returns: (transfer none): The threshold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgBigNumber *
lrg_prestige_get_threshold (LrgPrestige *self);

/**
 * lrg_prestige_set_threshold:
 * @self: an #LrgPrestige
 * @threshold: Minimum value to prestige
 *
 * Sets the minimum value required to prestige.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_prestige_set_threshold (LrgPrestige        *self,
                            const LrgBigNumber *threshold);

/**
 * lrg_prestige_set_threshold_simple:
 * @self: an #LrgPrestige
 * @threshold: Minimum value as double
 *
 * Sets the threshold with a simple value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_prestige_set_threshold_simple (LrgPrestige *self,
                                   gdouble      threshold);

/**
 * lrg_prestige_get_scaling_exponent:
 * @self: an #LrgPrestige
 *
 * Gets the scaling exponent for reward calculation.
 * reward = (current / threshold)^exponent
 *
 * Returns: The scaling exponent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_prestige_get_scaling_exponent (LrgPrestige *self);

/**
 * lrg_prestige_set_scaling_exponent:
 * @self: an #LrgPrestige
 * @exponent: Scaling exponent
 *
 * Sets the scaling exponent for reward calculation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_prestige_set_scaling_exponent (LrgPrestige *self,
                                   gdouble      exponent);

/* State */

/**
 * lrg_prestige_get_points:
 * @self: an #LrgPrestige
 *
 * Gets current prestige points.
 *
 * Returns: (transfer none): Current points
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgBigNumber *
lrg_prestige_get_points (LrgPrestige *self);

/**
 * lrg_prestige_set_points:
 * @self: an #LrgPrestige
 * @points: New point total
 *
 * Sets prestige points (for save/load).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_prestige_set_points (LrgPrestige        *self,
                         const LrgBigNumber *points);

/**
 * lrg_prestige_add_points:
 * @self: an #LrgPrestige
 * @points: Points to add
 *
 * Adds prestige points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_prestige_add_points (LrgPrestige        *self,
                         const LrgBigNumber *points);

/**
 * lrg_prestige_get_times_prestiged:
 * @self: an #LrgPrestige
 *
 * Gets how many times prestige has been performed.
 *
 * Returns: Prestige count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_prestige_get_times_prestiged (LrgPrestige *self);

/* Operations */

/**
 * lrg_prestige_calculate_reward:
 * @self: an #LrgPrestige
 * @current_value: Current accumulated value
 *
 * Calculates how many prestige points would be gained.
 *
 * Returns: (transfer full): Pending prestige points
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_prestige_calculate_reward (LrgPrestige        *self,
                               const LrgBigNumber *current_value);

/**
 * lrg_prestige_can_prestige:
 * @self: an #LrgPrestige
 * @current_value: Current accumulated value
 *
 * Checks if prestige is available.
 *
 * Returns: %TRUE if prestige requirements are met
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_prestige_can_prestige (LrgPrestige        *self,
                           const LrgBigNumber *current_value);

/**
 * lrg_prestige_perform:
 * @self: an #LrgPrestige
 * @current_value: Current accumulated value
 *
 * Performs prestige, adding reward to points and resetting progress.
 * Emits the ::prestige-performed signal.
 *
 * Returns: (transfer full): Points awarded (for display)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_prestige_perform (LrgPrestige        *self,
                      const LrgBigNumber *current_value);

/**
 * lrg_prestige_get_bonus_multiplier:
 * @self: an #LrgPrestige
 *
 * Gets the current bonus multiplier from prestige points.
 *
 * Returns: Bonus multiplier (1.0 = no bonus)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_prestige_get_bonus_multiplier (LrgPrestige *self);

/**
 * lrg_prestige_reset:
 * @self: an #LrgPrestige
 *
 * Resets all prestige progress (points and count).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_prestige_reset (LrgPrestige *self);

G_END_DECLS
