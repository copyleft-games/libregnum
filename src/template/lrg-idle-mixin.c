/* lrg-idle-mixin.c - Interface for idle game mechanics
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-idle-mixin.h"
#include "../idle/lrg-idle-calculator.h"
#include "../idle/lrg-prestige.h"
#include "../idle/lrg-big-number.h"

/**
 * SECTION:lrg-idle-mixin
 * @title: LrgIdleMixin
 * @short_description: Interface for idle game mechanics
 * @see_also: #LrgIdleCalculator, #LrgPrestige, #LrgBigNumber
 *
 * #LrgIdleMixin is a composable interface that provides idle/clicker
 * game mechanics. It integrates with the existing idle game systems
 * to provide offline progress, auto-save, and prestige functionality.
 *
 * ## Implementing the Interface
 *
 * To add idle mechanics to your game, implement this interface:
 *
 * |[<!-- language="C" -->
 * static LrgIdleCalculator *
 * my_game_get_idle_calculator (LrgIdleMixin *mixin)
 * {
 *     MyGame *self = MY_GAME (mixin);
 *     return self->calculator;
 * }
 *
 * static void
 * my_game_idle_mixin_init (LrgIdleMixinInterface *iface)
 * {
 *     iface->get_idle_calculator = my_game_get_idle_calculator;
 *     // ... implement other methods
 * }
 *
 * G_DEFINE_TYPE_WITH_CODE (MyGame, my_game, LRG_TYPE_GAME_TEMPLATE,
 *     G_IMPLEMENT_INTERFACE (LRG_TYPE_IDLE_MIXIN, my_game_idle_mixin_init))
 * ]|
 *
 * Since: 1.0
 */

G_DEFINE_INTERFACE (LrgIdleMixin, lrg_idle_mixin, G_TYPE_OBJECT)

/* Default implementations */

static LrgIdleCalculator *
lrg_idle_mixin_real_get_idle_calculator (LrgIdleMixin *self)
{
    return NULL;
}

static LrgPrestige *
lrg_idle_mixin_real_get_prestige (LrgIdleMixin *self)
{
    return NULL;
}

static LrgBigNumber *
lrg_idle_mixin_real_calculate_offline_progress (LrgIdleMixin *self,
                                                 gdouble       efficiency,
                                                 gdouble       max_hours)
{
    LrgIdleCalculator *calculator;
    gint64 snapshot_time;

    calculator = lrg_idle_mixin_get_idle_calculator (self);
    if (calculator == NULL)
        return lrg_big_number_new (0.0);

    snapshot_time = lrg_idle_calculator_get_snapshot_time (calculator);
    return lrg_idle_calculator_simulate_offline (calculator, snapshot_time,
                                                  efficiency, max_hours);
}

static void
lrg_idle_mixin_real_apply_offline_progress (LrgIdleMixin       *self,
                                             const LrgBigNumber *progress)
{
    /* Default implementation does nothing - subclasses must implement */
}

static gdouble
lrg_idle_mixin_real_get_auto_save_interval (LrgIdleMixin *self)
{
    /* Idle games save frequently to preserve offline progress */
    return 30.0;
}

static void
lrg_idle_mixin_real_on_prestige_performed (LrgIdleMixin       *self,
                                            const LrgBigNumber *reward)
{
    /* Default implementation does nothing - subclasses must implement */
}

static void
lrg_idle_mixin_default_init (LrgIdleMixinInterface *iface)
{
    iface->get_idle_calculator = lrg_idle_mixin_real_get_idle_calculator;
    iface->get_prestige = lrg_idle_mixin_real_get_prestige;
    iface->calculate_offline_progress = lrg_idle_mixin_real_calculate_offline_progress;
    iface->apply_offline_progress = lrg_idle_mixin_real_apply_offline_progress;
    iface->get_auto_save_interval = lrg_idle_mixin_real_get_auto_save_interval;
    iface->on_prestige_performed = lrg_idle_mixin_real_on_prestige_performed;
}

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
LrgIdleCalculator *
lrg_idle_mixin_get_idle_calculator (LrgIdleMixin *self)
{
    LrgIdleMixinInterface *iface;

    g_return_val_if_fail (LRG_IS_IDLE_MIXIN (self), NULL);

    iface = LRG_IDLE_MIXIN_GET_IFACE (self);
    if (iface->get_idle_calculator != NULL)
        return iface->get_idle_calculator (self);

    return NULL;
}

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
LrgPrestige *
lrg_idle_mixin_get_prestige (LrgIdleMixin *self)
{
    LrgIdleMixinInterface *iface;

    g_return_val_if_fail (LRG_IS_IDLE_MIXIN (self), NULL);

    iface = LRG_IDLE_MIXIN_GET_IFACE (self);
    if (iface->get_prestige != NULL)
        return iface->get_prestige (self);

    return NULL;
}

/**
 * lrg_idle_mixin_calculate_offline_progress:
 * @self: an #LrgIdleMixin
 * @efficiency: offline efficiency (0.0 to 1.0)
 * @max_hours: maximum hours to calculate (0 = unlimited)
 *
 * Calculates production accumulated while the game was closed.
 *
 * Returns: (transfer full): the total offline production as #LrgBigNumber
 *
 * Since: 1.0
 */
LrgBigNumber *
lrg_idle_mixin_calculate_offline_progress (LrgIdleMixin *self,
                                           gdouble       efficiency,
                                           gdouble       max_hours)
{
    LrgIdleMixinInterface *iface;

    g_return_val_if_fail (LRG_IS_IDLE_MIXIN (self), NULL);

    iface = LRG_IDLE_MIXIN_GET_IFACE (self);
    if (iface->calculate_offline_progress != NULL)
        return iface->calculate_offline_progress (self, efficiency, max_hours);

    return lrg_big_number_new (0.0);
}

/**
 * lrg_idle_mixin_apply_offline_progress:
 * @self: an #LrgIdleMixin
 * @progress: (transfer none): the progress amount to apply
 *
 * Applies calculated offline progress to the game state.
 *
 * Since: 1.0
 */
void
lrg_idle_mixin_apply_offline_progress (LrgIdleMixin       *self,
                                       const LrgBigNumber *progress)
{
    LrgIdleMixinInterface *iface;

    g_return_if_fail (LRG_IS_IDLE_MIXIN (self));
    g_return_if_fail (progress != NULL);

    iface = LRG_IDLE_MIXIN_GET_IFACE (self);
    if (iface->apply_offline_progress != NULL)
        iface->apply_offline_progress (self, progress);
}

/**
 * lrg_idle_mixin_get_auto_save_interval:
 * @self: an #LrgIdleMixin
 *
 * Gets the auto-save interval in seconds.
 *
 * Returns: auto-save interval in seconds
 *
 * Since: 1.0
 */
gdouble
lrg_idle_mixin_get_auto_save_interval (LrgIdleMixin *self)
{
    LrgIdleMixinInterface *iface;

    g_return_val_if_fail (LRG_IS_IDLE_MIXIN (self), 30.0);

    iface = LRG_IDLE_MIXIN_GET_IFACE (self);
    if (iface->get_auto_save_interval != NULL)
        return iface->get_auto_save_interval (self);

    return 30.0;
}

/**
 * lrg_idle_mixin_on_prestige_performed:
 * @self: an #LrgIdleMixin
 * @reward: (transfer none): the prestige points that were awarded
 *
 * Called when prestige is performed.
 *
 * Since: 1.0
 */
void
lrg_idle_mixin_on_prestige_performed (LrgIdleMixin       *self,
                                      const LrgBigNumber *reward)
{
    LrgIdleMixinInterface *iface;

    g_return_if_fail (LRG_IS_IDLE_MIXIN (self));
    g_return_if_fail (reward != NULL);

    iface = LRG_IDLE_MIXIN_GET_IFACE (self);
    if (iface->on_prestige_performed != NULL)
        iface->on_prestige_performed (self, reward);
}

/* ==========================================================================
 * Helper Methods
 * ========================================================================== */

/**
 * lrg_idle_mixin_perform_prestige:
 * @self: an #LrgIdleMixin
 * @current_value: (transfer none): current accumulated value
 *
 * Performs prestige if available and requirements are met.
 *
 * Returns: (transfer full) (nullable): points awarded, or %NULL if not performed
 *
 * Since: 1.0
 */
LrgBigNumber *
lrg_idle_mixin_perform_prestige (LrgIdleMixin       *self,
                                 const LrgBigNumber *current_value)
{
    LrgPrestige *prestige;
    LrgBigNumber *reward;

    g_return_val_if_fail (LRG_IS_IDLE_MIXIN (self), NULL);
    g_return_val_if_fail (current_value != NULL, NULL);

    prestige = lrg_idle_mixin_get_prestige (self);
    if (prestige == NULL)
        return NULL;

    if (!lrg_prestige_can_prestige (prestige, current_value))
        return NULL;

    reward = lrg_prestige_perform (prestige, current_value);

    /* Call the hook */
    lrg_idle_mixin_on_prestige_performed (self, reward);

    return reward;
}

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
gboolean
lrg_idle_mixin_can_prestige (LrgIdleMixin       *self,
                             const LrgBigNumber *current_value)
{
    LrgPrestige *prestige;

    g_return_val_if_fail (LRG_IS_IDLE_MIXIN (self), FALSE);
    g_return_val_if_fail (current_value != NULL, FALSE);

    prestige = lrg_idle_mixin_get_prestige (self);
    if (prestige == NULL)
        return FALSE;

    return lrg_prestige_can_prestige (prestige, current_value);
}

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
LrgBigNumber *
lrg_idle_mixin_get_prestige_reward (LrgIdleMixin       *self,
                                    const LrgBigNumber *current_value)
{
    LrgPrestige *prestige;

    g_return_val_if_fail (LRG_IS_IDLE_MIXIN (self), NULL);
    g_return_val_if_fail (current_value != NULL, NULL);

    prestige = lrg_idle_mixin_get_prestige (self);
    if (prestige == NULL)
        return NULL;

    return lrg_prestige_calculate_reward (prestige, current_value);
}

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
gdouble
lrg_idle_mixin_get_prestige_multiplier (LrgIdleMixin *self)
{
    LrgPrestige *prestige;

    g_return_val_if_fail (LRG_IS_IDLE_MIXIN (self), 1.0);

    prestige = lrg_idle_mixin_get_prestige (self);
    if (prestige == NULL)
        return 1.0;

    return lrg_prestige_get_bonus_multiplier (prestige);
}

/**
 * lrg_idle_mixin_take_snapshot:
 * @self: an #LrgIdleMixin
 *
 * Takes a snapshot of the current time for offline progress calculation.
 *
 * Since: 1.0
 */
void
lrg_idle_mixin_take_snapshot (LrgIdleMixin *self)
{
    LrgIdleCalculator *calculator;

    g_return_if_fail (LRG_IS_IDLE_MIXIN (self));

    calculator = lrg_idle_mixin_get_idle_calculator (self);
    if (calculator != NULL)
        lrg_idle_calculator_take_snapshot (calculator);
}

/**
 * lrg_idle_mixin_simulate:
 * @self: an #LrgIdleMixin
 * @seconds: number of seconds to simulate
 *
 * Simulates production for a given time period.
 *
 * Returns: (transfer full): production over the time period
 *
 * Since: 1.0
 */
LrgBigNumber *
lrg_idle_mixin_simulate (LrgIdleMixin *self,
                         gdouble       seconds)
{
    LrgIdleCalculator *calculator;

    g_return_val_if_fail (LRG_IS_IDLE_MIXIN (self), NULL);

    calculator = lrg_idle_mixin_get_idle_calculator (self);
    if (calculator == NULL)
        return lrg_big_number_new (0.0);

    return lrg_idle_calculator_simulate (calculator, seconds);
}

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
LrgBigNumber *
lrg_idle_mixin_get_total_rate (LrgIdleMixin *self)
{
    LrgIdleCalculator *calculator;

    g_return_val_if_fail (LRG_IS_IDLE_MIXIN (self), NULL);

    calculator = lrg_idle_mixin_get_idle_calculator (self);
    if (calculator == NULL)
        return lrg_big_number_new (0.0);

    return lrg_idle_calculator_get_total_rate (calculator);
}
