/* lrg-automation.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-automation.h"

/* ========================================================================= */
/* LrgAutomationRule - Boxed type for automation rules                      */
/* ========================================================================= */

/* Forward declarations for G_DEFINE_BOXED_TYPE */
LrgAutomationRule *lrg_automation_rule_copy (const LrgAutomationRule *self);
void lrg_automation_rule_free (LrgAutomationRule *self);

G_DEFINE_BOXED_TYPE (LrgAutomationRule, lrg_automation_rule,
                     lrg_automation_rule_copy,
                     lrg_automation_rule_free)

LrgAutomationRule *
lrg_automation_rule_new (const gchar          *id,
                         LrgAutomationTrigger  trigger)
{
    LrgAutomationRule *self;

    g_return_val_if_fail (id != NULL, NULL);

    self = g_slice_new0 (LrgAutomationRule);
    self->id = g_strdup (id);
    self->name = NULL;
    self->trigger = trigger;
    self->interval = 1.0;
    self->threshold = lrg_big_number_new_zero ();
    self->enabled = TRUE;
    self->trigger_count = 0;
    self->max_triggers = 0;
    self->accumulated_time = 0.0;
    self->callback = NULL;
    self->user_data = NULL;
    self->destroy = NULL;

    return self;
}

LrgAutomationRule *
lrg_automation_rule_copy (const LrgAutomationRule *self)
{
    LrgAutomationRule *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_slice_new0 (LrgAutomationRule);
    copy->id = g_strdup (self->id);
    copy->name = g_strdup (self->name);
    copy->trigger = self->trigger;
    copy->interval = self->interval;
    copy->threshold = lrg_big_number_copy (self->threshold);
    copy->enabled = self->enabled;
    copy->trigger_count = self->trigger_count;
    copy->max_triggers = self->max_triggers;
    copy->accumulated_time = self->accumulated_time;

    /*
     * Note: We don't copy the callback/user_data.
     * The copy will have no callback set.
     */
    copy->callback = NULL;
    copy->user_data = NULL;
    copy->destroy = NULL;

    return copy;
}

void
lrg_automation_rule_free (LrgAutomationRule *self)
{
    if (self == NULL)
        return;

    g_free (self->id);
    g_free (self->name);
    lrg_big_number_free (self->threshold);

    if (self->destroy && self->user_data)
        self->destroy (self->user_data);

    g_slice_free (LrgAutomationRule, self);
}

const gchar *
lrg_automation_rule_get_id (const LrgAutomationRule *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

const gchar *
lrg_automation_rule_get_name (const LrgAutomationRule *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->name;
}

void
lrg_automation_rule_set_name (LrgAutomationRule *self,
                              const gchar       *name)
{
    g_return_if_fail (self != NULL);

    g_free (self->name);
    self->name = g_strdup (name);
}

LrgAutomationTrigger
lrg_automation_rule_get_trigger (const LrgAutomationRule *self)
{
    g_return_val_if_fail (self != NULL, LRG_AUTOMATION_TRIGGER_MANUAL);
    return self->trigger;
}

gdouble
lrg_automation_rule_get_interval (const LrgAutomationRule *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return self->interval;
}

void
lrg_automation_rule_set_interval (LrgAutomationRule *self,
                                  gdouble            interval)
{
    g_return_if_fail (self != NULL);
    self->interval = interval;
}

const LrgBigNumber *
lrg_automation_rule_get_threshold (const LrgAutomationRule *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->threshold;
}

void
lrg_automation_rule_set_threshold (LrgAutomationRule  *self,
                                   const LrgBigNumber *threshold)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (threshold != NULL);

    lrg_big_number_free (self->threshold);
    self->threshold = lrg_big_number_copy (threshold);
}

void
lrg_automation_rule_set_threshold_simple (LrgAutomationRule *self,
                                          gdouble            threshold)
{
    g_autoptr(LrgBigNumber) bn = NULL;

    bn = lrg_big_number_new (threshold);
    lrg_automation_rule_set_threshold (self, bn);
}

gboolean
lrg_automation_rule_is_enabled (const LrgAutomationRule *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->enabled;
}

void
lrg_automation_rule_set_enabled (LrgAutomationRule *self,
                                 gboolean           enabled)
{
    g_return_if_fail (self != NULL);
    self->enabled = enabled;
}

gint64
lrg_automation_rule_get_trigger_count (const LrgAutomationRule *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->trigger_count;
}

gint64
lrg_automation_rule_get_max_triggers (const LrgAutomationRule *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->max_triggers;
}

void
lrg_automation_rule_set_max_triggers (LrgAutomationRule *self,
                                      gint64             max)
{
    g_return_if_fail (self != NULL);
    self->max_triggers = max;
}

void
lrg_automation_rule_set_callback (LrgAutomationRule     *self,
                                  LrgAutomationCallback  callback,
                                  gpointer               user_data,
                                  GDestroyNotify         destroy)
{
    g_return_if_fail (self != NULL);

    /* Clean up old callback data */
    if (self->destroy && self->user_data)
        self->destroy (self->user_data);

    self->callback = callback;
    self->user_data = user_data;
    self->destroy = destroy;
}

void
lrg_automation_rule_reset (LrgAutomationRule *self)
{
    g_return_if_fail (self != NULL);

    self->trigger_count = 0;
    self->accumulated_time = 0.0;
}

/*
 * Internal function to trigger a rule.
 * Returns TRUE if the rule should continue, FALSE to disable.
 */
static gboolean
automation_rule_do_trigger (LrgAutomationRule *self)
{
    gboolean result = TRUE;

    self->trigger_count++;

    /* Call the callback */
    if (self->callback)
        result = self->callback (self, self->user_data);

    /* Check if we've hit max triggers */
    if (self->max_triggers > 0 && self->trigger_count >= self->max_triggers)
    {
        self->enabled = FALSE;
        return FALSE;
    }

    return result;
}

/* ========================================================================= */
/* LrgAutomation - GObject managing automation rules                        */
/* ========================================================================= */

struct _LrgAutomation
{
    GObject    parent_instance;

    GPtrArray *rules;
    gboolean   enabled;
};

enum
{
    PROP_0,
    PROP_ENABLED,
    N_PROPS
};

enum
{
    SIGNAL_RULE_TRIGGERED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_TYPE (LrgAutomation, lrg_automation, G_TYPE_OBJECT)

static void
lrg_automation_finalize (GObject *object)
{
    LrgAutomation *self = LRG_AUTOMATION (object);

    g_ptr_array_unref (self->rules);

    G_OBJECT_CLASS (lrg_automation_parent_class)->finalize (object);
}

static void
lrg_automation_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
    LrgAutomation *self = LRG_AUTOMATION (object);

    switch (prop_id)
    {
    case PROP_ENABLED:
        g_value_set_boolean (value, self->enabled);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_automation_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
    LrgAutomation *self = LRG_AUTOMATION (object);

    switch (prop_id)
    {
    case PROP_ENABLED:
        lrg_automation_set_enabled (self, g_value_get_boolean (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_automation_class_init (LrgAutomationClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_automation_finalize;
    object_class->get_property = lrg_automation_get_property;
    object_class->set_property = lrg_automation_set_property;

    /**
     * LrgAutomation:enabled:
     *
     * Whether automation is globally enabled.
     *
     * Since: 1.0
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Global automation enabled state",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgAutomation::rule-triggered:
     * @self: The automation manager
     * @rule_id: ID of triggered rule
     *
     * Emitted when any rule is triggered.
     *
     * Since: 1.0
     */
    signals[SIGNAL_RULE_TRIGGERED] =
        g_signal_new ("rule-triggered",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);
}

static void
lrg_automation_init (LrgAutomation *self)
{
    self->rules = g_ptr_array_new_with_free_func (
        (GDestroyNotify)lrg_automation_rule_free);
    self->enabled = TRUE;
}

LrgAutomation *
lrg_automation_new (void)
{
    return g_object_new (LRG_TYPE_AUTOMATION, NULL);
}

gboolean
lrg_automation_add_rule (LrgAutomation           *self,
                         const LrgAutomationRule *rule)
{
    guint i;

    g_return_val_if_fail (LRG_IS_AUTOMATION (self), FALSE);
    g_return_val_if_fail (rule != NULL, FALSE);

    /* Check for duplicate ID */
    for (i = 0; i < self->rules->len; i++)
    {
        LrgAutomationRule *existing = g_ptr_array_index (self->rules, i);
        if (g_strcmp0 (existing->id, rule->id) == 0)
            return FALSE;
    }

    g_ptr_array_add (self->rules, lrg_automation_rule_copy (rule));
    return TRUE;
}

gboolean
lrg_automation_remove_rule (LrgAutomation *self,
                            const gchar   *id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_AUTOMATION (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    for (i = 0; i < self->rules->len; i++)
    {
        LrgAutomationRule *rule = g_ptr_array_index (self->rules, i);
        if (g_strcmp0 (rule->id, id) == 0)
        {
            g_ptr_array_remove_index (self->rules, i);
            return TRUE;
        }
    }

    return FALSE;
}

LrgAutomationRule *
lrg_automation_get_rule (LrgAutomation *self,
                         const gchar   *id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_AUTOMATION (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    for (i = 0; i < self->rules->len; i++)
    {
        LrgAutomationRule *rule = g_ptr_array_index (self->rules, i);
        if (g_strcmp0 (rule->id, id) == 0)
            return rule;
    }

    return NULL;
}

GPtrArray *
lrg_automation_get_rules (LrgAutomation *self)
{
    g_return_val_if_fail (LRG_IS_AUTOMATION (self), NULL);
    return self->rules;
}

gboolean
lrg_automation_is_enabled (LrgAutomation *self)
{
    g_return_val_if_fail (LRG_IS_AUTOMATION (self), FALSE);
    return self->enabled;
}

void
lrg_automation_set_enabled (LrgAutomation *self,
                            gboolean       enabled)
{
    g_return_if_fail (LRG_IS_AUTOMATION (self));

    if (self->enabled != enabled)
    {
        self->enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
    }
}

void
lrg_automation_update (LrgAutomation      *self,
                       gdouble             delta_time,
                       const LrgBigNumber *current_value)
{
    guint i;

    g_return_if_fail (LRG_IS_AUTOMATION (self));

    if (!self->enabled)
        return;

    for (i = 0; i < self->rules->len; i++)
    {
        LrgAutomationRule *rule = g_ptr_array_index (self->rules, i);
        gboolean should_trigger = FALSE;

        if (!rule->enabled)
            continue;

        switch (rule->trigger)
        {
        case LRG_AUTOMATION_TRIGGER_INTERVAL:
            rule->accumulated_time += delta_time;
            /* Loop to handle multiple triggers in a single update */
            while (rule->accumulated_time >= rule->interval && rule->enabled)
            {
                if (!automation_rule_do_trigger (rule))
                {
                    /* Rule hit max triggers or was disabled */
                    g_signal_emit (self, signals[SIGNAL_RULE_TRIGGERED], 0, rule->id);
                    break;
                }
                g_signal_emit (self, signals[SIGNAL_RULE_TRIGGERED], 0, rule->id);
                rule->accumulated_time -= rule->interval;
            }
            break;

        case LRG_AUTOMATION_TRIGGER_THRESHOLD:
            if (current_value != NULL &&
                lrg_big_number_compare (current_value, rule->threshold) >= 0)
            {
                should_trigger = TRUE;
            }
            break;

        case LRG_AUTOMATION_TRIGGER_MANUAL:
            /* Manual triggers only via lrg_automation_trigger() */
            break;
        }

        if (should_trigger)
        {
            automation_rule_do_trigger (rule);
            g_signal_emit (self, signals[SIGNAL_RULE_TRIGGERED], 0, rule->id);
        }
    }
}

gboolean
lrg_automation_trigger (LrgAutomation *self,
                        const gchar   *rule_id)
{
    LrgAutomationRule *rule;

    g_return_val_if_fail (LRG_IS_AUTOMATION (self), FALSE);
    g_return_val_if_fail (rule_id != NULL, FALSE);

    rule = lrg_automation_get_rule (self, rule_id);
    if (rule == NULL)
        return FALSE;

    if (!rule->enabled)
        return FALSE;

    automation_rule_do_trigger (rule);
    g_signal_emit (self, signals[SIGNAL_RULE_TRIGGERED], 0, rule_id);

    return TRUE;
}

void
lrg_automation_trigger_all (LrgAutomation *self)
{
    guint i;

    g_return_if_fail (LRG_IS_AUTOMATION (self));

    for (i = 0; i < self->rules->len; i++)
    {
        LrgAutomationRule *rule = g_ptr_array_index (self->rules, i);

        if (!rule->enabled)
            continue;

        automation_rule_do_trigger (rule);
        g_signal_emit (self, signals[SIGNAL_RULE_TRIGGERED], 0, rule->id);
    }
}

void
lrg_automation_reset (LrgAutomation *self)
{
    guint i;

    g_return_if_fail (LRG_IS_AUTOMATION (self));

    for (i = 0; i < self->rules->len; i++)
    {
        LrgAutomationRule *rule = g_ptr_array_index (self->rules, i);
        lrg_automation_rule_reset (rule);
    }
}
