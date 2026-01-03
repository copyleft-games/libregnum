/* lrg-idle-template.c - Game template for idle/clicker games
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-idle-template.h"
#include "lrg-idle-mixin.h"
#include "../idle/lrg-idle-calculator.h"
#include "../idle/lrg-prestige.h"
#include "../idle/lrg-big-number.h"

/**
 * SECTION:lrg-idle-template
 * @title: LrgIdleTemplate
 * @short_description: Game template for idle/clicker games
 * @see_also: #LrgGameTemplate, #LrgIdleMixin, #LrgIdleCalculator
 *
 * #LrgIdleTemplate is a derivable game template specialized for idle/clicker
 * games. It provides automatic integration with the idle game systems.
 *
 * ## Properties
 *
 * - `offline-efficiency` - Production rate when offline (default: 0.5)
 * - `max-offline-hours` - Maximum hours to calculate (default: 24.0)
 * - `prestige-enabled` - Whether prestige system is active
 * - `show-offline-popup` - Whether to show offline earnings popup
 *
 * Since: 1.0
 */

typedef struct
{
    LrgIdleCalculator *calculator;
    LrgPrestige       *prestige;

    gdouble offline_efficiency;
    gdouble max_offline_hours;
    gboolean prestige_enabled;
    gboolean show_offline_popup;

    /* Auto-save timer */
    gdouble auto_save_timer;
    gdouble auto_save_interval;
} LrgIdleTemplatePrivate;

static void lrg_idle_template_idle_mixin_init (LrgIdleMixinInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgIdleTemplate, lrg_idle_template, LRG_TYPE_GAME_TEMPLATE,
                         G_ADD_PRIVATE (LrgIdleTemplate)
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_IDLE_MIXIN,
                                                lrg_idle_template_idle_mixin_init))

enum
{
    PROP_0,
    PROP_OFFLINE_EFFICIENCY,
    PROP_MAX_OFFLINE_HOURS,
    PROP_PRESTIGE_ENABLED,
    PROP_SHOW_OFFLINE_POPUP,
    PROP_AUTO_SAVE_INTERVAL,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * LrgIdleMixin Interface Implementation
 * ========================================================================== */

static LrgIdleCalculator *
lrg_idle_template_mixin_get_idle_calculator (LrgIdleMixin *mixin)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (mixin);
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);
    return priv->calculator;
}

static LrgPrestige *
lrg_idle_template_mixin_get_prestige (LrgIdleMixin *mixin)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (mixin);
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);

    if (!priv->prestige_enabled)
        return NULL;

    return priv->prestige;
}

static LrgBigNumber *
lrg_idle_template_mixin_calculate_offline_progress (LrgIdleMixin *mixin,
                                                     gdouble       efficiency,
                                                     gdouble       max_hours)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (mixin);
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);
    gint64 snapshot_time;

    if (priv->calculator == NULL)
        return lrg_big_number_new (0.0);

    snapshot_time = lrg_idle_calculator_get_snapshot_time (priv->calculator);
    return lrg_idle_calculator_simulate_offline (priv->calculator,
                                                  snapshot_time,
                                                  efficiency,
                                                  max_hours);
}

static void
lrg_idle_template_mixin_apply_offline_progress (LrgIdleMixin       *mixin,
                                                 const LrgBigNumber *progress)
{
    /* Default implementation - subclasses should override */
    (void)mixin;
    (void)progress;
}

static gdouble
lrg_idle_template_mixin_get_auto_save_interval (LrgIdleMixin *mixin)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (mixin);
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);
    return priv->auto_save_interval;
}

static void
lrg_idle_template_mixin_on_prestige_performed (LrgIdleMixin       *mixin,
                                                const LrgBigNumber *reward)
{
    /* Default implementation - subclasses should override */
    (void)mixin;
    (void)reward;
}

static void
lrg_idle_template_idle_mixin_init (LrgIdleMixinInterface *iface)
{
    iface->get_idle_calculator = lrg_idle_template_mixin_get_idle_calculator;
    iface->get_prestige = lrg_idle_template_mixin_get_prestige;
    iface->calculate_offline_progress = lrg_idle_template_mixin_calculate_offline_progress;
    iface->apply_offline_progress = lrg_idle_template_mixin_apply_offline_progress;
    iface->get_auto_save_interval = lrg_idle_template_mixin_get_auto_save_interval;
    iface->on_prestige_performed = lrg_idle_template_mixin_on_prestige_performed;
}

/* ==========================================================================
 * Virtual Method Implementations
 * ========================================================================== */

static LrgIdleCalculator *
lrg_idle_template_real_create_idle_calculator (LrgIdleTemplate *self)
{
    (void)self;
    return lrg_idle_calculator_new ();
}

static LrgPrestige *
lrg_idle_template_real_create_prestige (LrgIdleTemplate *self)
{
    (void)self;
    return lrg_prestige_new ();
}

static void
lrg_idle_template_real_on_offline_progress_calculated (LrgIdleTemplate    *self,
                                                        const LrgBigNumber *progress,
                                                        gdouble             seconds_offline)
{
    /* Default: just log it */
    (void)self;
    (void)progress;
    (void)seconds_offline;
}

static gchar *
lrg_idle_template_real_format_big_number (LrgIdleTemplate    *self,
                                           const LrgBigNumber *number)
{
    (void)self;
    return lrg_big_number_format_short (number);
}

static gdouble
lrg_idle_template_real_get_offline_efficiency (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);
    return priv->offline_efficiency;
}

static gdouble
lrg_idle_template_real_get_max_offline_hours (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);
    return priv->max_offline_hours;
}

/* Override LrgGameTemplate's pre_startup to initialize idle systems */
static void
lrg_idle_template_pre_startup (LrgGameTemplate *base)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (base);
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);
    LrgIdleTemplateClass *klass = LRG_IDLE_TEMPLATE_GET_CLASS (self);

    /* Chain up first */
    LRG_GAME_TEMPLATE_CLASS (lrg_idle_template_parent_class)->pre_startup (base);

    /* Create idle calculator */
    if (klass->create_idle_calculator != NULL)
        priv->calculator = klass->create_idle_calculator (self);
    else
        priv->calculator = lrg_idle_calculator_new ();

    /* Create prestige layer if enabled */
    if (priv->prestige_enabled && klass->create_prestige != NULL)
        priv->prestige = klass->create_prestige (self);
    else if (priv->prestige_enabled)
        priv->prestige = lrg_prestige_new ();
}

/* Override post_startup to process offline progress */
static void
lrg_idle_template_post_startup (LrgGameTemplate *base)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (base);

    /* Chain up first */
    if (LRG_GAME_TEMPLATE_CLASS (lrg_idle_template_parent_class)->post_startup != NULL)
        LRG_GAME_TEMPLATE_CLASS (lrg_idle_template_parent_class)->post_startup (base);

    /* Process offline progress */
    lrg_idle_template_process_offline_progress (self);
}

/* Override post_update for auto-save timing */
static void
lrg_idle_template_post_update (LrgGameTemplate *base,
                                gdouble          delta)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (base);
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);

    /* Chain up */
    if (LRG_GAME_TEMPLATE_CLASS (lrg_idle_template_parent_class)->post_update != NULL)
        LRG_GAME_TEMPLATE_CLASS (lrg_idle_template_parent_class)->post_update (base, delta);

    /* Auto-save timer */
    priv->auto_save_timer += delta;
    if (priv->auto_save_timer >= priv->auto_save_interval)
    {
        priv->auto_save_timer = 0.0;

        /* Take snapshot for offline calculation */
        if (priv->calculator != NULL)
            lrg_idle_calculator_take_snapshot (priv->calculator);

        /* Trigger auto-save via parent */
        /* Note: The parent LrgGameTemplate handles the actual save */
    }
}

/* Override shutdown to take final snapshot */
static void
lrg_idle_template_shutdown (LrgGameTemplate *base)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (base);
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);

    /* Take final snapshot before saving */
    if (priv->calculator != NULL)
        lrg_idle_calculator_take_snapshot (priv->calculator);

    /* Chain up */
    if (LRG_GAME_TEMPLATE_CLASS (lrg_idle_template_parent_class)->shutdown != NULL)
        LRG_GAME_TEMPLATE_CLASS (lrg_idle_template_parent_class)->shutdown (base);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_idle_template_dispose (GObject *object)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (object);
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);

    g_clear_object (&priv->calculator);
    g_clear_object (&priv->prestige);

    G_OBJECT_CLASS (lrg_idle_template_parent_class)->dispose (object);
}

static void
lrg_idle_template_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (object);
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_OFFLINE_EFFICIENCY:
        g_value_set_double (value, priv->offline_efficiency);
        break;
    case PROP_MAX_OFFLINE_HOURS:
        g_value_set_double (value, priv->max_offline_hours);
        break;
    case PROP_PRESTIGE_ENABLED:
        g_value_set_boolean (value, priv->prestige_enabled);
        break;
    case PROP_SHOW_OFFLINE_POPUP:
        g_value_set_boolean (value, priv->show_offline_popup);
        break;
    case PROP_AUTO_SAVE_INTERVAL:
        g_value_set_double (value, priv->auto_save_interval);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_idle_template_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgIdleTemplate *self = LRG_IDLE_TEMPLATE (object);
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_OFFLINE_EFFICIENCY:
        priv->offline_efficiency = g_value_get_double (value);
        break;
    case PROP_MAX_OFFLINE_HOURS:
        priv->max_offline_hours = g_value_get_double (value);
        break;
    case PROP_PRESTIGE_ENABLED:
        priv->prestige_enabled = g_value_get_boolean (value);
        break;
    case PROP_SHOW_OFFLINE_POPUP:
        priv->show_offline_popup = g_value_get_boolean (value);
        break;
    case PROP_AUTO_SAVE_INTERVAL:
        priv->auto_save_interval = g_value_get_double (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_idle_template_class_init (LrgIdleTemplateClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgGameTemplateClass *template_class = LRG_GAME_TEMPLATE_CLASS (klass);

    object_class->dispose = lrg_idle_template_dispose;
    object_class->get_property = lrg_idle_template_get_property;
    object_class->set_property = lrg_idle_template_set_property;

    /* Override template vfuncs */
    template_class->pre_startup = lrg_idle_template_pre_startup;
    template_class->post_startup = lrg_idle_template_post_startup;
    template_class->post_update = lrg_idle_template_post_update;
    template_class->shutdown = lrg_idle_template_shutdown;

    /* Idle template vfuncs */
    klass->create_idle_calculator = lrg_idle_template_real_create_idle_calculator;
    klass->create_prestige = lrg_idle_template_real_create_prestige;
    klass->on_offline_progress_calculated = lrg_idle_template_real_on_offline_progress_calculated;
    klass->format_big_number = lrg_idle_template_real_format_big_number;
    klass->get_offline_efficiency = lrg_idle_template_real_get_offline_efficiency;
    klass->get_max_offline_hours = lrg_idle_template_real_get_max_offline_hours;

    /**
     * LrgIdleTemplate:offline-efficiency:
     *
     * The production efficiency when offline (0.0 to 1.0).
     * Default is 0.5 (50% of online production).
     *
     * Since: 1.0
     */
    properties[PROP_OFFLINE_EFFICIENCY] =
        g_param_spec_double ("offline-efficiency",
                             "Offline Efficiency",
                             "Production efficiency when offline",
                             0.0, 1.0, 0.5,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgIdleTemplate:max-offline-hours:
     *
     * Maximum hours of offline progress to calculate.
     * Default is 24.0 (1 day). Set to 0 for unlimited.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_OFFLINE_HOURS] =
        g_param_spec_double ("max-offline-hours",
                             "Max Offline Hours",
                             "Maximum hours of offline progress",
                             0.0, G_MAXDOUBLE, 24.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgIdleTemplate:prestige-enabled:
     *
     * Whether the prestige system is enabled.
     * Default is %TRUE.
     *
     * Since: 1.0
     */
    properties[PROP_PRESTIGE_ENABLED] =
        g_param_spec_boolean ("prestige-enabled",
                              "Prestige Enabled",
                              "Whether prestige is available",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgIdleTemplate:show-offline-popup:
     *
     * Whether to show the offline earnings popup on load.
     * Default is %TRUE.
     *
     * Since: 1.0
     */
    properties[PROP_SHOW_OFFLINE_POPUP] =
        g_param_spec_boolean ("show-offline-popup",
                              "Show Offline Popup",
                              "Show offline earnings notification",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgIdleTemplate:auto-save-interval:
     *
     * Auto-save interval in seconds. Idle games save frequently
     * to preserve offline progress.
     * Default is 30.0 seconds.
     *
     * Since: 1.0
     */
    properties[PROP_AUTO_SAVE_INTERVAL] =
        g_param_spec_double ("auto-save-interval",
                             "Auto-Save Interval",
                             "Seconds between auto-saves",
                             1.0, 3600.0, 30.0,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_idle_template_init (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv = lrg_idle_template_get_instance_private (self);

    priv->calculator = NULL;
    priv->prestige = NULL;
    priv->offline_efficiency = 0.5;
    priv->max_offline_hours = 24.0;
    priv->prestige_enabled = TRUE;
    priv->show_offline_popup = TRUE;
    priv->auto_save_timer = 0.0;
    priv->auto_save_interval = 30.0;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_idle_template_new:
 *
 * Creates a new idle template.
 *
 * Returns: (transfer full): a new #LrgIdleTemplate
 *
 * Since: 1.0
 */
LrgIdleTemplate *
lrg_idle_template_new (void)
{
    return g_object_new (LRG_TYPE_IDLE_TEMPLATE, NULL);
}

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
LrgIdleCalculator *
lrg_idle_template_get_idle_calculator (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), NULL);

    priv = lrg_idle_template_get_instance_private (self);
    return priv->calculator;
}

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
LrgPrestige *
lrg_idle_template_get_prestige (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), NULL);

    priv = lrg_idle_template_get_instance_private (self);
    return priv->prestige;
}

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
gdouble
lrg_idle_template_get_offline_efficiency (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), 0.5);

    priv = lrg_idle_template_get_instance_private (self);
    return priv->offline_efficiency;
}

/**
 * lrg_idle_template_set_offline_efficiency:
 * @self: an #LrgIdleTemplate
 * @efficiency: efficiency multiplier (0.0 to 1.0)
 *
 * Sets the offline production efficiency.
 *
 * Since: 1.0
 */
void
lrg_idle_template_set_offline_efficiency (LrgIdleTemplate *self,
                                          gdouble          efficiency)
{
    LrgIdleTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_IDLE_TEMPLATE (self));

    priv = lrg_idle_template_get_instance_private (self);
    priv->offline_efficiency = CLAMP (efficiency, 0.0, 1.0);
}

/**
 * lrg_idle_template_get_max_offline_hours:
 * @self: an #LrgIdleTemplate
 *
 * Gets the maximum hours of offline progress.
 *
 * Returns: max hours (0 = unlimited)
 *
 * Since: 1.0
 */
gdouble
lrg_idle_template_get_max_offline_hours (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), 24.0);

    priv = lrg_idle_template_get_instance_private (self);
    return priv->max_offline_hours;
}

/**
 * lrg_idle_template_set_max_offline_hours:
 * @self: an #LrgIdleTemplate
 * @hours: max hours (0 = unlimited)
 *
 * Sets the maximum hours of offline progress.
 *
 * Since: 1.0
 */
void
lrg_idle_template_set_max_offline_hours (LrgIdleTemplate *self,
                                         gdouble          hours)
{
    LrgIdleTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_IDLE_TEMPLATE (self));

    priv = lrg_idle_template_get_instance_private (self);
    priv->max_offline_hours = hours;
}

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
gboolean
lrg_idle_template_get_prestige_enabled (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), FALSE);

    priv = lrg_idle_template_get_instance_private (self);
    return priv->prestige_enabled;
}

/**
 * lrg_idle_template_set_prestige_enabled:
 * @self: an #LrgIdleTemplate
 * @enabled: whether prestige should be enabled
 *
 * Enables or disables the prestige system.
 *
 * Since: 1.0
 */
void
lrg_idle_template_set_prestige_enabled (LrgIdleTemplate *self,
                                        gboolean         enabled)
{
    LrgIdleTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_IDLE_TEMPLATE (self));

    priv = lrg_idle_template_get_instance_private (self);
    priv->prestige_enabled = enabled;
}

/**
 * lrg_idle_template_get_show_offline_popup:
 * @self: an #LrgIdleTemplate
 *
 * Gets whether to show offline progress popup.
 *
 * Returns: %TRUE if popup should be shown
 *
 * Since: 1.0
 */
gboolean
lrg_idle_template_get_show_offline_popup (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), TRUE);

    priv = lrg_idle_template_get_instance_private (self);
    return priv->show_offline_popup;
}

/**
 * lrg_idle_template_set_show_offline_popup:
 * @self: an #LrgIdleTemplate
 * @show: whether to show the popup
 *
 * Sets whether to show offline progress popup.
 *
 * Since: 1.0
 */
void
lrg_idle_template_set_show_offline_popup (LrgIdleTemplate *self,
                                          gboolean         show)
{
    LrgIdleTemplatePrivate *priv;

    g_return_if_fail (LRG_IS_IDLE_TEMPLATE (self));

    priv = lrg_idle_template_get_instance_private (self);
    priv->show_offline_popup = show;
}

/**
 * lrg_idle_template_process_offline_progress:
 * @self: an #LrgIdleTemplate
 *
 * Calculates and applies offline progress.
 *
 * Returns: (transfer full) (nullable): the offline progress, or %NULL
 *
 * Since: 1.0
 */
LrgBigNumber *
lrg_idle_template_process_offline_progress (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv;
    LrgIdleTemplateClass *klass;
    g_autoptr(LrgBigNumber) progress = NULL;
    gint64 snapshot_time;
    gint64 now;
    gdouble seconds_offline;
    gdouble efficiency;
    gdouble max_hours;

    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), NULL);

    priv = lrg_idle_template_get_instance_private (self);
    klass = LRG_IDLE_TEMPLATE_GET_CLASS (self);

    if (priv->calculator == NULL)
        return NULL;

    /* Get snapshot time */
    snapshot_time = lrg_idle_calculator_get_snapshot_time (priv->calculator);
    if (snapshot_time <= 0)
        return NULL;

    /* Calculate time offline */
    now = g_get_real_time () / G_USEC_PER_SEC;
    seconds_offline = (gdouble)(now - snapshot_time);

    if (seconds_offline <= 0.0)
        return NULL;

    /* Get efficiency and max hours */
    if (klass->get_offline_efficiency != NULL)
        efficiency = klass->get_offline_efficiency (self);
    else
        efficiency = priv->offline_efficiency;

    if (klass->get_max_offline_hours != NULL)
        max_hours = klass->get_max_offline_hours (self);
    else
        max_hours = priv->max_offline_hours;

    /* Calculate progress */
    progress = lrg_idle_calculator_simulate_offline (priv->calculator,
                                                      snapshot_time,
                                                      efficiency,
                                                      max_hours);

    /* Notify about offline progress */
    if (klass->on_offline_progress_calculated != NULL)
        klass->on_offline_progress_calculated (self, progress, seconds_offline);

    /* Apply progress via mixin */
    lrg_idle_mixin_apply_offline_progress (LRG_IDLE_MIXIN (self), progress);

    /* Update snapshot time to now */
    lrg_idle_calculator_take_snapshot (priv->calculator);

    return g_steal_pointer (&progress);
}

/**
 * lrg_idle_template_format_big_number:
 * @self: an #LrgIdleTemplate
 * @number: (transfer none): the number to format
 *
 * Formats a big number for display.
 *
 * Returns: (transfer full): formatted string
 *
 * Since: 1.0
 */
gchar *
lrg_idle_template_format_big_number (LrgIdleTemplate    *self,
                                     const LrgBigNumber *number)
{
    LrgIdleTemplateClass *klass;

    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), NULL);
    g_return_val_if_fail (number != NULL, NULL);

    klass = LRG_IDLE_TEMPLATE_GET_CLASS (self);

    if (klass->format_big_number != NULL)
        return klass->format_big_number (self, number);

    return lrg_big_number_format_short (number);
}

/**
 * lrg_idle_template_add_generator:
 * @self: an #LrgIdleTemplate
 * @id: unique generator ID
 * @base_rate: base production rate per second
 *
 * Adds a generator to the idle calculator.
 *
 * Since: 1.0
 */
void
lrg_idle_template_add_generator (LrgIdleTemplate *self,
                                 const gchar     *id,
                                 gdouble          base_rate)
{
    LrgIdleTemplatePrivate *priv;
    g_autoptr(LrgIdleGenerator) generator = NULL;

    g_return_if_fail (LRG_IS_IDLE_TEMPLATE (self));
    g_return_if_fail (id != NULL);

    priv = lrg_idle_template_get_instance_private (self);

    if (priv->calculator == NULL)
        return;

    generator = lrg_idle_generator_new_simple (id, base_rate);
    lrg_idle_calculator_add_generator (priv->calculator, generator);
}

/**
 * lrg_idle_template_set_generator_count:
 * @self: an #LrgIdleTemplate
 * @id: generator ID
 * @count: new count
 *
 * Sets the count for a generator.
 *
 * Since: 1.0
 */
void
lrg_idle_template_set_generator_count (LrgIdleTemplate *self,
                                       const gchar     *id,
                                       gint64           count)
{
    LrgIdleTemplatePrivate *priv;
    LrgIdleGenerator *generator;

    g_return_if_fail (LRG_IS_IDLE_TEMPLATE (self));
    g_return_if_fail (id != NULL);

    priv = lrg_idle_template_get_instance_private (self);

    if (priv->calculator == NULL)
        return;

    generator = lrg_idle_calculator_get_generator (priv->calculator, id);
    if (generator != NULL)
        lrg_idle_generator_set_count (generator, count);
}

/**
 * lrg_idle_template_get_generator_count:
 * @self: an #LrgIdleTemplate
 * @id: generator ID
 *
 * Gets the count for a generator.
 *
 * Returns: generator count, or 0 if not found
 *
 * Since: 1.0
 */
gint64
lrg_idle_template_get_generator_count (LrgIdleTemplate *self,
                                       const gchar     *id)
{
    LrgIdleTemplatePrivate *priv;
    LrgIdleGenerator *generator;

    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), 0);
    g_return_val_if_fail (id != NULL, 0);

    priv = lrg_idle_template_get_instance_private (self);

    if (priv->calculator == NULL)
        return 0;

    generator = lrg_idle_calculator_get_generator (priv->calculator, id);
    if (generator == NULL)
        return 0;

    return lrg_idle_generator_get_count (generator);
}

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
LrgBigNumber *
lrg_idle_template_get_total_production_rate (LrgIdleTemplate *self)
{
    LrgIdleTemplatePrivate *priv;

    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), NULL);

    priv = lrg_idle_template_get_instance_private (self);

    if (priv->calculator == NULL)
        return lrg_big_number_new (0.0);

    return lrg_idle_calculator_get_total_rate (priv->calculator);
}

/**
 * lrg_idle_template_try_prestige:
 * @self: an #LrgIdleTemplate
 * @current_value: (transfer none): current accumulated value
 *
 * Attempts to perform prestige if requirements are met.
 *
 * Returns: (transfer full) (nullable): prestige reward, or %NULL
 *
 * Since: 1.0
 */
LrgBigNumber *
lrg_idle_template_try_prestige (LrgIdleTemplate    *self,
                                const LrgBigNumber *current_value)
{
    g_return_val_if_fail (LRG_IS_IDLE_TEMPLATE (self), NULL);
    g_return_val_if_fail (current_value != NULL, NULL);

    return lrg_idle_mixin_perform_prestige (LRG_IDLE_MIXIN (self), current_value);
}
