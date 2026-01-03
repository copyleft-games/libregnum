/* lrg-idle-mixin.h - Interface for idle game mechanics
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * LrgIdleMixin is a composable interface that provides idle/clicker game
 * mechanics. It integrates with the existing LrgIdleCalculator and LrgPrestige
 * systems to provide offline progress, auto-save, and prestige functionality.
 *
 * ## Features
 *
 * - **Offline Progress**: Calculate and apply production while the game was closed
 * - **Prestige System**: Optional prestige layer with configurable rewards
 * - **Auto-Save**: Configurable save interval with snapshot timestamps
 * - **Big Number Formatting**: Integration with LrgBigNumber for large values
 *
 * ## Usage
 *
 * Implement this interface on your game state or template class:
 *
 * ```c
 * G_DEFINE_TYPE_WITH_CODE (MyIdleGame, my_idle_game, LRG_TYPE_GAME_TEMPLATE,
 *     G_IMPLEMENT_INTERFACE (LRG_TYPE_IDLE_MIXIN, my_idle_game_idle_mixin_init))
 * ```
 *
 * Since: 1.0
 */

#ifndef LRG_IDLE_MIXIN_H
#define LRG_IDLE_MIXIN_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/* Forward declarations */
typedef struct _LrgIdleCalculator LrgIdleCalculator;
typedef struct _LrgPrestige       LrgPrestige;
typedef struct _LrgBigNumber      LrgBigNumber;

#define LRG_TYPE_IDLE_MIXIN (lrg_idle_mixin_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgIdleMixin, lrg_idle_mixin, LRG, IDLE_MIXIN, GObject)

/**
 * LrgIdleMixinInterface:
 * @parent_iface: the parent interface
 * @get_idle_calculator: Returns the idle calculator instance
 * @get_prestige: Returns the prestige layer instance (optional)
 * @calculate_offline_progress: Calculate accumulated offline production
 * @apply_offline_progress: Apply calculated offline progress to game state
 * @get_auto_save_interval: Get auto-save interval in seconds
 * @on_prestige_performed: Hook called when prestige is performed
 *
 * Interface for idle game mechanics. Implement this interface to add
 * idle/clicker functionality to your game template or state.
 *
 * Since: 1.0
 */
struct _LrgIdleMixinInterface
{
    GTypeInterface parent_iface;

    /*< public >*/

    /**
     * LrgIdleMixinInterface::get_idle_calculator:
     * @self: an #LrgIdleMixin
     *
     * Gets the idle calculator that tracks generators and production.
     *
     * Returns: (transfer none): the #LrgIdleCalculator instance
     */
    LrgIdleCalculator * (*get_idle_calculator) (LrgIdleMixin *self);

    /**
     * LrgIdleMixinInterface::get_prestige:
     * @self: an #LrgIdleMixin
     *
     * Gets the optional prestige layer.
     *
     * Returns: (transfer none) (nullable): the #LrgPrestige instance, or %NULL
     */
    LrgPrestige * (*get_prestige) (LrgIdleMixin *self);

    /**
     * LrgIdleMixinInterface::calculate_offline_progress:
     * @self: an #LrgIdleMixin
     * @efficiency: offline efficiency multiplier (0.0 to 1.0)
     * @max_hours: maximum hours to calculate (0 = unlimited)
     *
     * Calculates the production accumulated while the game was closed.
     * Uses the last snapshot time from the idle calculator.
     *
     * Returns: (transfer full): the total offline production
     */
    LrgBigNumber * (*calculate_offline_progress) (LrgIdleMixin *self,
                                                   gdouble       efficiency,
                                                   gdouble       max_hours);

    /**
     * LrgIdleMixinInterface::apply_offline_progress:
     * @self: an #LrgIdleMixin
     * @progress: (transfer none): the progress to apply
     *
     * Applies offline progress to the game state. Implementers should
     * update their resource values and optionally display a notification.
     */
    void (*apply_offline_progress) (LrgIdleMixin       *self,
                                    const LrgBigNumber *progress);

    /**
     * LrgIdleMixinInterface::get_auto_save_interval:
     * @self: an #LrgIdleMixin
     *
     * Gets the auto-save interval in seconds. Idle games typically
     * save frequently to preserve offline progress.
     *
     * Returns: auto-save interval in seconds (default: 30.0)
     */
    gdouble (*get_auto_save_interval) (LrgIdleMixin *self);

    /**
     * LrgIdleMixinInterface::on_prestige_performed:
     * @self: an #LrgIdleMixin
     * @reward: (transfer none): the prestige points awarded
     *
     * Hook called when prestige is performed. Implementers should
     * reset appropriate game state and apply prestige bonuses.
     */
    void (*on_prestige_performed) (LrgIdleMixin       *self,
                                   const LrgBigNumber *reward);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Interface Methods
 * ========================================================================== */

/**
 * lrg_idle_mixin_get_idle_calculator:
 * @self: an #LrgIdleMixin
 *
 * Gets the idle calculator that tracks generators and production rates.
 *
 * Returns: (transfer none): the #LrgIdleCalculator instance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgIdleCalculator *
lrg_idle_mixin_get_idle_calculator (LrgIdleMixin *self);

/**
 * lrg_idle_mixin_get_prestige:
 * @self: an #LrgIdleMixin
 *
 * Gets the optional prestige layer for reset-based progression.
 *
 * Returns: (transfer none) (nullable): the #LrgPrestige instance, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPrestige *
lrg_idle_mixin_get_prestige (LrgIdleMixin *self);

/**
 * lrg_idle_mixin_calculate_offline_progress:
 * @self: an #LrgIdleMixin
 * @efficiency: offline efficiency (0.0 to 1.0, where 1.0 is full production)
 * @max_hours: maximum hours to calculate (0 = unlimited)
 *
 * Calculates production accumulated while the game was closed.
 * Uses the snapshot time from the idle calculator to determine
 * how long the player was away.
 *
 * Returns: (transfer full): the total offline production as #LrgBigNumber
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_mixin_calculate_offline_progress (LrgIdleMixin *self,
                                           gdouble       efficiency,
                                           gdouble       max_hours);

/**
 * lrg_idle_mixin_apply_offline_progress:
 * @self: an #LrgIdleMixin
 * @progress: (transfer none): the progress amount to apply
 *
 * Applies calculated offline progress to the game state.
 * Implementers should update their resource values and
 * optionally show a "Welcome back!" notification.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_mixin_apply_offline_progress (LrgIdleMixin       *self,
                                       const LrgBigNumber *progress);

/**
 * lrg_idle_mixin_get_auto_save_interval:
 * @self: an #LrgIdleMixin
 *
 * Gets the auto-save interval in seconds. Idle games typically
 * save more frequently than other games to preserve offline progress.
 *
 * Returns: auto-save interval in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_idle_mixin_get_auto_save_interval (LrgIdleMixin *self);

/**
 * lrg_idle_mixin_on_prestige_performed:
 * @self: an #LrgIdleMixin
 * @reward: (transfer none): the prestige points that were awarded
 *
 * Called when prestige is performed. Implementers should reset
 * generators, resources, and other resettable state while
 * preserving permanent upgrades from prestige points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_mixin_on_prestige_performed (LrgIdleMixin       *self,
                                      const LrgBigNumber *reward);

/* ==========================================================================
 * Helper Methods
 * ========================================================================== */

/**
 * lrg_idle_mixin_perform_prestige:
 * @self: an #LrgIdleMixin
 * @current_value: (transfer none): current accumulated value
 *
 * Performs prestige if the prestige layer is available and requirements
 * are met. Calls the prestige perform method and then the
 * on_prestige_performed hook.
 *
 * Returns: (transfer full) (nullable): points awarded, or %NULL if not performed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_mixin_perform_prestige (LrgIdleMixin       *self,
                                 const LrgBigNumber *current_value);

/**
 * lrg_idle_mixin_can_prestige:
 * @self: an #LrgIdleMixin
 * @current_value: (transfer none): current accumulated value
 *
 * Checks if prestige requirements are met.
 *
 * Returns: %TRUE if prestige is available
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_idle_mixin_can_prestige (LrgIdleMixin       *self,
                             const LrgBigNumber *current_value);

/**
 * lrg_idle_mixin_get_prestige_reward:
 * @self: an #LrgIdleMixin
 * @current_value: (transfer none): current accumulated value
 *
 * Calculates the prestige reward that would be gained.
 *
 * Returns: (transfer full) (nullable): pending prestige points, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_mixin_get_prestige_reward (LrgIdleMixin       *self,
                                    const LrgBigNumber *current_value);

/**
 * lrg_idle_mixin_get_prestige_multiplier:
 * @self: an #LrgIdleMixin
 *
 * Gets the current production multiplier from prestige points.
 *
 * Returns: multiplier value (1.0 = no bonus)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_idle_mixin_get_prestige_multiplier (LrgIdleMixin *self);

/**
 * lrg_idle_mixin_take_snapshot:
 * @self: an #LrgIdleMixin
 *
 * Takes a snapshot of the current time for offline progress calculation.
 * Call this before saving the game.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_mixin_take_snapshot (LrgIdleMixin *self);

/**
 * lrg_idle_mixin_simulate:
 * @self: an #LrgIdleMixin
 * @seconds: number of seconds to simulate
 *
 * Simulates production for a given time period. Useful for
 * time-skip features or bonus production events.
 *
 * Returns: (transfer full): production over the time period
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_mixin_simulate (LrgIdleMixin *self,
                         gdouble       seconds);

/**
 * lrg_idle_mixin_get_total_rate:
 * @self: an #LrgIdleMixin
 *
 * Gets the total production rate per second from all generators.
 *
 * Returns: (transfer full): total rate per second
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_mixin_get_total_rate (LrgIdleMixin *self);

G_END_DECLS

#endif /* LRG_IDLE_MIXIN_H */
