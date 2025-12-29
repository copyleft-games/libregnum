/* lrg-automation.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAutomation - Auto-click/auto-buy logic for idle games.
 *
 * Manages automated actions that trigger on intervals or conditions.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-big-number.h"

G_BEGIN_DECLS

#define LRG_TYPE_AUTOMATION (lrg_automation_get_type ())
#define LRG_TYPE_AUTOMATION_RULE (lrg_automation_rule_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAutomation, lrg_automation, LRG, AUTOMATION, GObject)

/**
 * LrgAutomationTrigger:
 * @LRG_AUTOMATION_TRIGGER_INTERVAL: Trigger on time interval
 * @LRG_AUTOMATION_TRIGGER_THRESHOLD: Trigger when value exceeds threshold
 * @LRG_AUTOMATION_TRIGGER_MANUAL: Only trigger manually
 *
 * Types of automation triggers.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_AUTOMATION_TRIGGER_INTERVAL,
    LRG_AUTOMATION_TRIGGER_THRESHOLD,
    LRG_AUTOMATION_TRIGGER_MANUAL
} LrgAutomationTrigger;

/**
 * LrgAutomationRule:
 * @id: Unique identifier
 * @name: Optional display name
 * @trigger: How the rule is triggered
 * @interval: Time interval for interval trigger
 * @threshold: Threshold for threshold trigger
 * @enabled: Whether rule is active
 * @trigger_count: Number of times triggered
 * @max_triggers: Maximum triggers (0 = unlimited)
 * @accumulated_time: Accumulated time since last trigger
 * @callback: Function to call when triggered
 * @user_data: User data for callback
 * @destroy: Destroy notify for user_data
 *
 * A boxed type representing an automation rule.
 */
typedef struct _LrgAutomationRule LrgAutomationRule;

/**
 * LrgAutomationCallback:
 * @rule: The automation rule
 * @user_data: User data
 *
 * Callback invoked when automation triggers.
 *
 * Returns: %TRUE to continue automation, %FALSE to stop
 *
 * Since: 1.0
 */
typedef gboolean (*LrgAutomationCallback) (LrgAutomationRule *rule,
                                           gpointer           user_data);

struct _LrgAutomationRule
{
    gchar                 *id;
    gchar                 *name;
    LrgAutomationTrigger   trigger;
    gdouble                interval;
    LrgBigNumber          *threshold;
    gboolean               enabled;
    gint64                 trigger_count;
    gint64                 max_triggers;
    gdouble                accumulated_time;
    LrgAutomationCallback  callback;
    gpointer               user_data;
    GDestroyNotify         destroy;
};

LRG_AVAILABLE_IN_ALL
GType lrg_automation_rule_get_type (void) G_GNUC_CONST;

/* Rule construction */

/**
 * lrg_automation_rule_new:
 * @id: Unique identifier
 * @trigger: Trigger type
 *
 * Creates a new automation rule.
 *
 * Returns: (transfer full): A new #LrgAutomationRule
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAutomationRule *
lrg_automation_rule_new (const gchar          *id,
                         LrgAutomationTrigger  trigger);

/**
 * lrg_automation_rule_copy:
 * @self: an #LrgAutomationRule
 *
 * Creates a copy of a rule.
 *
 * Returns: (transfer full): A copy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAutomationRule *
lrg_automation_rule_copy (const LrgAutomationRule *self);

/**
 * lrg_automation_rule_free:
 * @self: an #LrgAutomationRule
 *
 * Frees a rule.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_rule_free (LrgAutomationRule *self);

/* Rule accessors */

/**
 * lrg_automation_rule_get_id:
 * @self: an #LrgAutomationRule
 *
 * Gets the rule ID.
 *
 * Returns: (transfer none): The ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_automation_rule_get_id (const LrgAutomationRule *self);

/**
 * lrg_automation_rule_get_name:
 * @self: an #LrgAutomationRule
 *
 * Gets the display name.
 *
 * Returns: (transfer none) (nullable): The name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_automation_rule_get_name (const LrgAutomationRule *self);

/**
 * lrg_automation_rule_set_name:
 * @self: an #LrgAutomationRule
 * @name: (nullable): Display name
 *
 * Sets the display name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_rule_set_name (LrgAutomationRule *self,
                              const gchar       *name);

/**
 * lrg_automation_rule_get_trigger:
 * @self: an #LrgAutomationRule
 *
 * Gets the trigger type.
 *
 * Returns: Trigger type
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAutomationTrigger
lrg_automation_rule_get_trigger (const LrgAutomationRule *self);

/**
 * lrg_automation_rule_get_interval:
 * @self: an #LrgAutomationRule
 *
 * Gets the trigger interval in seconds.
 *
 * Returns: Interval in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_automation_rule_get_interval (const LrgAutomationRule *self);

/**
 * lrg_automation_rule_set_interval:
 * @self: an #LrgAutomationRule
 * @interval: Interval in seconds
 *
 * Sets the trigger interval.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_rule_set_interval (LrgAutomationRule *self,
                                  gdouble            interval);

/**
 * lrg_automation_rule_get_threshold:
 * @self: an #LrgAutomationRule
 *
 * Gets the threshold value.
 *
 * Returns: (transfer none): The threshold
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgBigNumber *
lrg_automation_rule_get_threshold (const LrgAutomationRule *self);

/**
 * lrg_automation_rule_set_threshold:
 * @self: an #LrgAutomationRule
 * @threshold: Threshold value
 *
 * Sets the threshold for THRESHOLD trigger type.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_rule_set_threshold (LrgAutomationRule  *self,
                                   const LrgBigNumber *threshold);

/**
 * lrg_automation_rule_set_threshold_simple:
 * @self: an #LrgAutomationRule
 * @threshold: Threshold as double
 *
 * Sets the threshold with a simple value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_rule_set_threshold_simple (LrgAutomationRule *self,
                                          gdouble            threshold);

/**
 * lrg_automation_rule_is_enabled:
 * @self: an #LrgAutomationRule
 *
 * Checks if the rule is enabled.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_automation_rule_is_enabled (const LrgAutomationRule *self);

/**
 * lrg_automation_rule_set_enabled:
 * @self: an #LrgAutomationRule
 * @enabled: Whether to enable
 *
 * Enables or disables the rule.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_rule_set_enabled (LrgAutomationRule *self,
                                 gboolean           enabled);

/**
 * lrg_automation_rule_get_trigger_count:
 * @self: an #LrgAutomationRule
 *
 * Gets how many times this rule has triggered.
 *
 * Returns: Trigger count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_automation_rule_get_trigger_count (const LrgAutomationRule *self);

/**
 * lrg_automation_rule_get_max_triggers:
 * @self: an #LrgAutomationRule
 *
 * Gets the maximum number of triggers (0 = unlimited).
 *
 * Returns: Max triggers
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint64
lrg_automation_rule_get_max_triggers (const LrgAutomationRule *self);

/**
 * lrg_automation_rule_set_max_triggers:
 * @self: an #LrgAutomationRule
 * @max: Maximum triggers (0 = unlimited)
 *
 * Sets the maximum number of triggers.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_rule_set_max_triggers (LrgAutomationRule *self,
                                      gint64             max);

/**
 * lrg_automation_rule_set_callback:
 * @self: an #LrgAutomationRule
 * @callback: (scope notified) (nullable): Callback function
 * @user_data: (nullable): User data for callback
 * @destroy: (nullable): Destroy function for user_data
 *
 * Sets the callback invoked when the rule triggers.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_rule_set_callback (LrgAutomationRule     *self,
                                  LrgAutomationCallback  callback,
                                  gpointer               user_data,
                                  GDestroyNotify         destroy);

/**
 * lrg_automation_rule_reset:
 * @self: an #LrgAutomationRule
 *
 * Resets the rule state (timer and trigger count).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_rule_reset (LrgAutomationRule *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgAutomationRule, lrg_automation_rule_free)

/* Automation manager construction */

/**
 * lrg_automation_new:
 *
 * Creates a new automation manager.
 *
 * Returns: (transfer full): A new #LrgAutomation
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAutomation *
lrg_automation_new (void);

/* Rule management */

/**
 * lrg_automation_add_rule:
 * @self: an #LrgAutomation
 * @rule: (transfer none): Rule to add
 *
 * Adds a rule to the manager. Takes a copy.
 *
 * Returns: %TRUE if added
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_automation_add_rule (LrgAutomation           *self,
                         const LrgAutomationRule *rule);

/**
 * lrg_automation_remove_rule:
 * @self: an #LrgAutomation
 * @id: Rule ID to remove
 *
 * Removes a rule.
 *
 * Returns: %TRUE if removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_automation_remove_rule (LrgAutomation *self,
                            const gchar   *id);

/**
 * lrg_automation_get_rule:
 * @self: an #LrgAutomation
 * @id: Rule ID
 *
 * Gets a rule by ID.
 *
 * Returns: (transfer none) (nullable): The rule
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAutomationRule *
lrg_automation_get_rule (LrgAutomation *self,
                         const gchar   *id);

/**
 * lrg_automation_get_rules:
 * @self: an #LrgAutomation
 *
 * Gets all rules.
 *
 * Returns: (transfer none) (element-type LrgAutomationRule): Array of rules
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_automation_get_rules (LrgAutomation *self);

/* Global state */

/**
 * lrg_automation_is_enabled:
 * @self: an #LrgAutomation
 *
 * Checks if automation is globally enabled.
 *
 * Returns: %TRUE if enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_automation_is_enabled (LrgAutomation *self);

/**
 * lrg_automation_set_enabled:
 * @self: an #LrgAutomation
 * @enabled: Whether to enable
 *
 * Enables or disables all automation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_set_enabled (LrgAutomation *self,
                            gboolean       enabled);

/* Update and trigger */

/**
 * lrg_automation_update:
 * @self: an #LrgAutomation
 * @delta_time: Time since last update in seconds
 * @current_value: (nullable): Current value for threshold checks
 *
 * Updates all automation rules and triggers any due.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_update (LrgAutomation      *self,
                       gdouble             delta_time,
                       const LrgBigNumber *current_value);

/**
 * lrg_automation_trigger:
 * @self: an #LrgAutomation
 * @rule_id: Rule to trigger
 *
 * Manually triggers a rule.
 *
 * Returns: %TRUE if triggered successfully
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_automation_trigger (LrgAutomation *self,
                        const gchar   *rule_id);

/**
 * lrg_automation_trigger_all:
 * @self: an #LrgAutomation
 *
 * Triggers all enabled rules.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_trigger_all (LrgAutomation *self);

/**
 * lrg_automation_reset:
 * @self: an #LrgAutomation
 *
 * Resets all rule states.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_automation_reset (LrgAutomation *self);

G_END_DECLS
