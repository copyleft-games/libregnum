/* lrg-idle-template.h - Game template for idle/clicker games
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * LrgIdleTemplate is a derivable game template specialized for idle/clicker
 * games. It provides automatic integration with the idle game systems,
 * including offline progress calculation, prestige mechanics, and auto-save.
 *
 * ## Features
 *
 * - **Offline Progress**: Automatically calculate and display earnings on game load
 * - **Prestige System**: Built-in prestige layer with configurable rewards
 * - **Auto-Save**: Frequent saves with snapshot timestamps for offline calculation
 * - **Big Number Display**: Integration with LrgBigNumber for large values
 * - **Generator Management**: Easy integration with LrgIdleCalculator
 *
 * ## Usage
 *
 * Subclass LrgIdleTemplate for your idle game:
 *
 * ```c
 * G_DECLARE_FINAL_TYPE (MyIdleGame, my_idle_game, MY, IDLE_GAME, LrgIdleTemplate)
 *
 * static void
 * my_idle_game_apply_offline_progress (LrgIdleMixin       *mixin,
 *                                       const LrgBigNumber *progress)
 * {
 *     MyIdleGame *self = MY_IDLE_GAME (mixin);
 *     // Add progress to player's resources
 *     // Show welcome back notification
 * }
 *
 * static void
 * my_idle_game_idle_mixin_init (LrgIdleMixinInterface *iface)
 * {
 *     iface->apply_offline_progress = my_idle_game_apply_offline_progress;
 * }
 *
 * G_DEFINE_TYPE_WITH_CODE (MyIdleGame, my_idle_game, LRG_TYPE_IDLE_TEMPLATE,
 *     G_IMPLEMENT_INTERFACE (LRG_TYPE_IDLE_MIXIN, my_idle_game_idle_mixin_init))
 * ```
 *
 * Since: 1.0
 */

#ifndef LRG_IDLE_TEMPLATE_H
#define LRG_IDLE_TEMPLATE_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-game-template.h"
#include "lrg-idle-mixin.h"

G_BEGIN_DECLS

/* Forward declarations */
typedef struct _LrgIdleCalculator LrgIdleCalculator;
typedef struct _LrgPrestige       LrgPrestige;
typedef struct _LrgBigNumber      LrgBigNumber;

#define LRG_TYPE_IDLE_TEMPLATE (lrg_idle_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgIdleTemplate, lrg_idle_template, LRG, IDLE_TEMPLATE, LrgGameTemplate)

/**
 * LrgIdleTemplateClass:
 * @parent_class: the parent class
 * @create_idle_calculator: Creates the idle calculator instance
 * @create_prestige: Creates the prestige layer instance (optional)
 * @on_offline_progress_calculated: Called after offline progress is calculated
 * @format_big_number: Format a big number for display
 * @get_offline_efficiency: Get the offline production efficiency
 * @get_max_offline_hours: Get the maximum offline hours to calculate
 *
 * The virtual function table for #LrgIdleTemplate.
 * Override these methods to customize idle game behavior.
 *
 * Since: 1.0
 */
struct _LrgIdleTemplateClass
{
    LrgGameTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgIdleTemplateClass::create_idle_calculator:
     * @self: an #LrgIdleTemplate
     *
     * Creates the idle calculator instance. Override to configure
     * generators with initial values.
     *
     * Returns: (transfer full): a new #LrgIdleCalculator
     */
    LrgIdleCalculator * (*create_idle_calculator) (LrgIdleTemplate *self);

    /**
     * LrgIdleTemplateClass::create_prestige:
     * @self: an #LrgIdleTemplate
     *
     * Creates the optional prestige layer. Return %NULL to disable prestige.
     *
     * Returns: (transfer full) (nullable): a new #LrgPrestige, or %NULL
     */
    LrgPrestige * (*create_prestige) (LrgIdleTemplate *self);

    /**
     * LrgIdleTemplateClass::on_offline_progress_calculated:
     * @self: an #LrgIdleTemplate
     * @progress: (transfer none): the calculated offline progress
     * @seconds_offline: how long the player was offline
     *
     * Called after offline progress is calculated but before it's applied.
     * Use this to show a welcome back notification.
     */
    void (*on_offline_progress_calculated) (LrgIdleTemplate    *self,
                                             const LrgBigNumber *progress,
                                             gdouble             seconds_offline);

    /**
     * LrgIdleTemplateClass::format_big_number:
     * @self: an #LrgIdleTemplate
     * @number: (transfer none): the number to format
     *
     * Formats a big number for display. Override for custom notation.
     *
     * Returns: (transfer full): formatted string
     */
    gchar * (*format_big_number) (LrgIdleTemplate    *self,
                                  const LrgBigNumber *number);

    /**
     * LrgIdleTemplateClass::get_offline_efficiency:
     * @self: an #LrgIdleTemplate
     *
     * Gets the offline production efficiency (0.0 to 1.0).
     * Default is 0.5 (50% of online production).
     *
     * Returns: efficiency multiplier
     */
    gdouble (*get_offline_efficiency) (LrgIdleTemplate *self);

    /**
     * LrgIdleTemplateClass::get_max_offline_hours:
     * @self: an #LrgIdleTemplate
     *
     * Gets the maximum hours of offline progress to calculate.
     * Default is 24.0 (1 day).
     *
     * Returns: max hours (0 = unlimited)
     */
    gdouble (*get_max_offline_hours) (LrgIdleTemplate *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_idle_template_new:
 *
 * Creates a new idle template with default settings.
 *
 * Returns: (transfer full): a new #LrgIdleTemplate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgIdleTemplate *
lrg_idle_template_new (void);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_idle_template_get_idle_calculator:
 * @self: an #LrgIdleTemplate
 *
 * Gets the idle calculator instance.
 *
 * Returns: (transfer none): the #LrgIdleCalculator
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgIdleCalculator *
lrg_idle_template_get_idle_calculator (LrgIdleTemplate *self);

/**
 * lrg_idle_template_get_prestige:
 * @self: an #LrgIdleTemplate
 *
 * Gets the prestige layer instance.
 *
 * Returns: (transfer none) (nullable): the #LrgPrestige, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPrestige *
lrg_idle_template_get_prestige (LrgIdleTemplate *self);

/**
 * lrg_idle_template_get_offline_efficiency:
 * @self: an #LrgIdleTemplate
 *
 * Gets the offline production efficiency.
 *
 * Returns: efficiency multiplier (0.0 to 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_idle_template_get_offline_efficiency (LrgIdleTemplate *self);

/**
 * lrg_idle_template_set_offline_efficiency:
 * @self: an #LrgIdleTemplate
 * @efficiency: efficiency multiplier (0.0 to 1.0)
 *
 * Sets the offline production efficiency.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_template_set_offline_efficiency (LrgIdleTemplate *self,
                                          gdouble          efficiency);

/**
 * lrg_idle_template_get_max_offline_hours:
 * @self: an #LrgIdleTemplate
 *
 * Gets the maximum hours of offline progress to calculate.
 *
 * Returns: max hours (0 = unlimited)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_idle_template_get_max_offline_hours (LrgIdleTemplate *self);

/**
 * lrg_idle_template_set_max_offline_hours:
 * @self: an #LrgIdleTemplate
 * @hours: max hours (0 = unlimited)
 *
 * Sets the maximum hours of offline progress to calculate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_template_set_max_offline_hours (LrgIdleTemplate *self,
                                         gdouble          hours);

/**
 * lrg_idle_template_get_prestige_enabled:
 * @self: an #LrgIdleTemplate
 *
 * Checks if prestige is enabled.
 *
 * Returns: %TRUE if prestige is available
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_idle_template_get_prestige_enabled (LrgIdleTemplate *self);

/**
 * lrg_idle_template_set_prestige_enabled:
 * @self: an #LrgIdleTemplate
 * @enabled: whether prestige should be enabled
 *
 * Enables or disables the prestige system.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_template_set_prestige_enabled (LrgIdleTemplate *self,
                                        gboolean         enabled);

/**
 * lrg_idle_template_get_show_offline_popup:
 * @self: an #LrgIdleTemplate
 *
 * Gets whether to show offline progress popup on load.
 *
 * Returns: %TRUE if popup should be shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_idle_template_get_show_offline_popup (LrgIdleTemplate *self);

/**
 * lrg_idle_template_set_show_offline_popup:
 * @self: an #LrgIdleTemplate
 * @show: whether to show the popup
 *
 * Sets whether to show offline progress popup on load.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_template_set_show_offline_popup (LrgIdleTemplate *self,
                                          gboolean         show);

/* ==========================================================================
 * Operations
 * ========================================================================== */

/**
 * lrg_idle_template_process_offline_progress:
 * @self: an #LrgIdleTemplate
 *
 * Calculates and applies offline progress. Call this on game load.
 *
 * Returns: (transfer full) (nullable): the offline progress, or %NULL if none
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_template_process_offline_progress (LrgIdleTemplate *self);

/**
 * lrg_idle_template_format_big_number:
 * @self: an #LrgIdleTemplate
 * @number: (transfer none): the number to format
 *
 * Formats a big number for display using the template's format settings.
 *
 * Returns: (transfer full): formatted string
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_idle_template_format_big_number (LrgIdleTemplate    *self,
                                     const LrgBigNumber *number);

/**
 * lrg_idle_template_add_generator:
 * @self: an #LrgIdleTemplate
 * @id: unique generator ID
 * @base_rate: base production rate per second
 *
 * Adds a generator to the idle calculator. Convenience method.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_template_add_generator (LrgIdleTemplate *self,
                                 const gchar     *id,
                                 gdouble          base_rate);

/**
 * lrg_idle_template_set_generator_count:
 * @self: an #LrgIdleTemplate
 * @id: generator ID
 * @count: new count
 *
 * Sets the count for a generator. Convenience method.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_idle_template_set_generator_count (LrgIdleTemplate *self,
                                       const gchar     *id,
                                       gint64           count);

/**
 * lrg_idle_template_get_generator_count:
 * @self: an #LrgIdleTemplate
 * @id: generator ID
 *
 * Gets the count for a generator. Convenience method.
 *
 * Returns: generator count, or 0 if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_idle_template_get_generator_count (LrgIdleTemplate *self,
                                       const gchar     *id);

/**
 * lrg_idle_template_get_total_production_rate:
 * @self: an #LrgIdleTemplate
 *
 * Gets the total production rate per second.
 *
 * Returns: (transfer full): total rate
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_template_get_total_production_rate (LrgIdleTemplate *self);

/**
 * lrg_idle_template_try_prestige:
 * @self: an #LrgIdleTemplate
 * @current_value: (transfer none): current accumulated value
 *
 * Attempts to perform prestige if requirements are met.
 *
 * Returns: (transfer full) (nullable): prestige reward, or %NULL if not performed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBigNumber *
lrg_idle_template_try_prestige (LrgIdleTemplate    *self,
                                const LrgBigNumber *current_value);

G_END_DECLS

#endif /* LRG_IDLE_TEMPLATE_H */
