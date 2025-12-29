/* lrg-prestige.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-prestige.h"

#include <math.h>

typedef struct
{
    gchar        *id;
    gchar        *name;
    LrgBigNumber *threshold;
    LrgBigNumber *points;
    gdouble       scaling_exponent;
    gint64        times_prestiged;
} LrgPrestigePrivate;

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_THRESHOLD,
    PROP_POINTS,
    PROP_SCALING_EXPONENT,
    PROP_TIMES_PRESTIGED,
    N_PROPS
};

enum
{
    SIGNAL_PRESTIGE_PERFORMED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_TYPE_WITH_PRIVATE (LrgPrestige, lrg_prestige, G_TYPE_OBJECT)

/* Default virtual implementations */

static LrgBigNumber *
lrg_prestige_real_calculate_reward (LrgPrestige        *self,
                                    const LrgBigNumber *current_value)
{
    LrgPrestigePrivate *priv = lrg_prestige_get_instance_private (self);
    g_autoptr(LrgBigNumber) ratio = NULL;
    g_autoptr(LrgBigNumber) reward = NULL;

    /* Can't prestige below threshold */
    if (lrg_big_number_compare (current_value, priv->threshold) < 0)
        return lrg_big_number_new_zero ();

    /* reward = (current / threshold)^exponent */
    ratio = lrg_big_number_divide (current_value, priv->threshold);
    reward = lrg_big_number_pow (ratio, priv->scaling_exponent);

    return g_steal_pointer (&reward);
}

static gboolean
lrg_prestige_real_can_prestige (LrgPrestige        *self,
                                const LrgBigNumber *current_value)
{
    LrgPrestigePrivate *priv = lrg_prestige_get_instance_private (self);

    /* Must meet threshold */
    return lrg_big_number_compare (current_value, priv->threshold) >= 0;
}

static void
lrg_prestige_real_on_prestige (LrgPrestige        *self,
                               const LrgBigNumber *reward)
{
    /* Default implementation does nothing */
    (void)self;
    (void)reward;
}

static gdouble
lrg_prestige_real_get_bonus_multiplier (LrgPrestige        *self,
                                        const LrgBigNumber *prestige_points)
{
    gdouble points_val;

    /*
     * Default formula: 1.0 + sqrt(points) * 0.1
     * This gives diminishing returns as points increase.
     */
    points_val = lrg_big_number_to_double (prestige_points);

    if (points_val <= 0.0)
        return 1.0;

    return 1.0 + sqrt (points_val) * 0.1;
}

/* GObject implementation */

static void
lrg_prestige_finalize (GObject *object)
{
    LrgPrestige *self = LRG_PRESTIGE (object);
    LrgPrestigePrivate *priv = lrg_prestige_get_instance_private (self);

    g_free (priv->id);
    g_free (priv->name);
    lrg_big_number_free (priv->threshold);
    lrg_big_number_free (priv->points);

    G_OBJECT_CLASS (lrg_prestige_parent_class)->finalize (object);
}

static void
lrg_prestige_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgPrestige *self = LRG_PRESTIGE (object);
    LrgPrestigePrivate *priv = lrg_prestige_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;

    case PROP_NAME:
        g_value_set_string (value, priv->name);
        break;

    case PROP_THRESHOLD:
        g_value_set_boxed (value, priv->threshold);
        break;

    case PROP_POINTS:
        g_value_set_boxed (value, priv->points);
        break;

    case PROP_SCALING_EXPONENT:
        g_value_set_double (value, priv->scaling_exponent);
        break;

    case PROP_TIMES_PRESTIGED:
        g_value_set_int64 (value, priv->times_prestiged);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_prestige_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgPrestige *self = LRG_PRESTIGE (object);

    switch (prop_id)
    {
    case PROP_ID:
        lrg_prestige_set_id (self, g_value_get_string (value));
        break;

    case PROP_NAME:
        lrg_prestige_set_name (self, g_value_get_string (value));
        break;

    case PROP_THRESHOLD:
        lrg_prestige_set_threshold (self, g_value_get_boxed (value));
        break;

    case PROP_POINTS:
        lrg_prestige_set_points (self, g_value_get_boxed (value));
        break;

    case PROP_SCALING_EXPONENT:
        lrg_prestige_set_scaling_exponent (self, g_value_get_double (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_prestige_class_init (LrgPrestigeClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_prestige_finalize;
    object_class->get_property = lrg_prestige_get_property;
    object_class->set_property = lrg_prestige_set_property;

    /* Virtual methods */
    klass->calculate_reward = lrg_prestige_real_calculate_reward;
    klass->can_prestige = lrg_prestige_real_can_prestige;
    klass->on_prestige = lrg_prestige_real_on_prestige;
    klass->get_bonus_multiplier = lrg_prestige_real_get_bonus_multiplier;

    /**
     * LrgPrestige:id:
     *
     * Unique identifier for this prestige layer.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Unique identifier",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPrestige:name:
     *
     * Display name for this prestige layer.
     *
     * Since: 1.0
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPrestige:threshold:
     *
     * Minimum value required to prestige.
     *
     * Since: 1.0
     */
    properties[PROP_THRESHOLD] =
        g_param_spec_boxed ("threshold",
                            "Threshold",
                            "Minimum value to prestige",
                            LRG_TYPE_BIG_NUMBER,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPrestige:points:
     *
     * Current prestige points accumulated.
     *
     * Since: 1.0
     */
    properties[PROP_POINTS] =
        g_param_spec_boxed ("points",
                            "Points",
                            "Current prestige points",
                            LRG_TYPE_BIG_NUMBER,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgPrestige:scaling-exponent:
     *
     * Exponent for reward calculation: (current/threshold)^exponent.
     *
     * Since: 1.0
     */
    properties[PROP_SCALING_EXPONENT] =
        g_param_spec_double ("scaling-exponent",
                             "Scaling Exponent",
                             "Reward scaling exponent",
                             0.0, 10.0, 0.5,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgPrestige:times-prestiged:
     *
     * Number of times prestige has been performed.
     *
     * Since: 1.0
     */
    properties[PROP_TIMES_PRESTIGED] =
        g_param_spec_int64 ("times-prestiged",
                            "Times Prestiged",
                            "Number of prestige resets",
                            0, G_MAXINT64, 0,
                            G_PARAM_READABLE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgPrestige::prestige-performed:
     * @self: The prestige layer
     * @reward: (type LrgBigNumber): Points awarded
     *
     * Emitted when prestige is successfully performed.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PRESTIGE_PERFORMED] =
        g_signal_new ("prestige-performed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_BIG_NUMBER);
}

static void
lrg_prestige_init (LrgPrestige *self)
{
    LrgPrestigePrivate *priv = lrg_prestige_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->threshold = lrg_big_number_new (1000.0);  /* Default threshold */
    priv->points = lrg_big_number_new_zero ();
    priv->scaling_exponent = 0.5;
    priv->times_prestiged = 0;
}

LrgPrestige *
lrg_prestige_new (void)
{
    return g_object_new (LRG_TYPE_PRESTIGE, NULL);
}

const gchar *
lrg_prestige_get_id (LrgPrestige *self)
{
    LrgPrestigePrivate *priv;

    g_return_val_if_fail (LRG_IS_PRESTIGE (self), NULL);

    priv = lrg_prestige_get_instance_private (self);
    return priv->id;
}

void
lrg_prestige_set_id (LrgPrestige *self,
                     const gchar *id)
{
    LrgPrestigePrivate *priv;

    g_return_if_fail (LRG_IS_PRESTIGE (self));

    priv = lrg_prestige_get_instance_private (self);

    if (g_strcmp0 (priv->id, id) != 0)
    {
        g_free (priv->id);
        priv->id = g_strdup (id);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ID]);
    }
}

const gchar *
lrg_prestige_get_name (LrgPrestige *self)
{
    LrgPrestigePrivate *priv;

    g_return_val_if_fail (LRG_IS_PRESTIGE (self), NULL);

    priv = lrg_prestige_get_instance_private (self);
    return priv->name;
}

void
lrg_prestige_set_name (LrgPrestige *self,
                       const gchar *name)
{
    LrgPrestigePrivate *priv;

    g_return_if_fail (LRG_IS_PRESTIGE (self));

    priv = lrg_prestige_get_instance_private (self);

    if (g_strcmp0 (priv->name, name) != 0)
    {
        g_free (priv->name);
        priv->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

const LrgBigNumber *
lrg_prestige_get_threshold (LrgPrestige *self)
{
    LrgPrestigePrivate *priv;

    g_return_val_if_fail (LRG_IS_PRESTIGE (self), NULL);

    priv = lrg_prestige_get_instance_private (self);
    return priv->threshold;
}

void
lrg_prestige_set_threshold (LrgPrestige        *self,
                            const LrgBigNumber *threshold)
{
    LrgPrestigePrivate *priv;

    g_return_if_fail (LRG_IS_PRESTIGE (self));
    g_return_if_fail (threshold != NULL);

    priv = lrg_prestige_get_instance_private (self);

    lrg_big_number_free (priv->threshold);
    priv->threshold = lrg_big_number_copy (threshold);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_THRESHOLD]);
}

void
lrg_prestige_set_threshold_simple (LrgPrestige *self,
                                   gdouble      threshold)
{
    g_autoptr(LrgBigNumber) thresh = NULL;

    thresh = lrg_big_number_new (threshold);
    lrg_prestige_set_threshold (self, thresh);
}

gdouble
lrg_prestige_get_scaling_exponent (LrgPrestige *self)
{
    LrgPrestigePrivate *priv;

    g_return_val_if_fail (LRG_IS_PRESTIGE (self), 0.5);

    priv = lrg_prestige_get_instance_private (self);
    return priv->scaling_exponent;
}

void
lrg_prestige_set_scaling_exponent (LrgPrestige *self,
                                   gdouble      exponent)
{
    LrgPrestigePrivate *priv;

    g_return_if_fail (LRG_IS_PRESTIGE (self));

    priv = lrg_prestige_get_instance_private (self);

    if (priv->scaling_exponent != exponent)
    {
        priv->scaling_exponent = exponent;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCALING_EXPONENT]);
    }
}

const LrgBigNumber *
lrg_prestige_get_points (LrgPrestige *self)
{
    LrgPrestigePrivate *priv;

    g_return_val_if_fail (LRG_IS_PRESTIGE (self), NULL);

    priv = lrg_prestige_get_instance_private (self);
    return priv->points;
}

void
lrg_prestige_set_points (LrgPrestige        *self,
                         const LrgBigNumber *points)
{
    LrgPrestigePrivate *priv;

    g_return_if_fail (LRG_IS_PRESTIGE (self));
    g_return_if_fail (points != NULL);

    priv = lrg_prestige_get_instance_private (self);

    lrg_big_number_free (priv->points);
    priv->points = lrg_big_number_copy (points);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINTS]);
}

void
lrg_prestige_add_points (LrgPrestige        *self,
                         const LrgBigNumber *points)
{
    LrgPrestigePrivate *priv;

    g_return_if_fail (LRG_IS_PRESTIGE (self));
    g_return_if_fail (points != NULL);

    priv = lrg_prestige_get_instance_private (self);

    lrg_big_number_add_in_place (priv->points, points);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINTS]);
}

gint64
lrg_prestige_get_times_prestiged (LrgPrestige *self)
{
    LrgPrestigePrivate *priv;

    g_return_val_if_fail (LRG_IS_PRESTIGE (self), 0);

    priv = lrg_prestige_get_instance_private (self);
    return priv->times_prestiged;
}

LrgBigNumber *
lrg_prestige_calculate_reward (LrgPrestige        *self,
                               const LrgBigNumber *current_value)
{
    LrgPrestigeClass *klass;

    g_return_val_if_fail (LRG_IS_PRESTIGE (self), lrg_big_number_new_zero ());
    g_return_val_if_fail (current_value != NULL, lrg_big_number_new_zero ());

    klass = LRG_PRESTIGE_GET_CLASS (self);
    return klass->calculate_reward (self, current_value);
}

gboolean
lrg_prestige_can_prestige (LrgPrestige        *self,
                           const LrgBigNumber *current_value)
{
    LrgPrestigeClass *klass;

    g_return_val_if_fail (LRG_IS_PRESTIGE (self), FALSE);
    g_return_val_if_fail (current_value != NULL, FALSE);

    klass = LRG_PRESTIGE_GET_CLASS (self);
    return klass->can_prestige (self, current_value);
}

LrgBigNumber *
lrg_prestige_perform (LrgPrestige        *self,
                      const LrgBigNumber *current_value)
{
    LrgPrestigePrivate *priv;
    LrgPrestigeClass *klass;
    g_autoptr(LrgBigNumber) reward = NULL;

    g_return_val_if_fail (LRG_IS_PRESTIGE (self), lrg_big_number_new_zero ());
    g_return_val_if_fail (current_value != NULL, lrg_big_number_new_zero ());

    priv = lrg_prestige_get_instance_private (self);
    klass = LRG_PRESTIGE_GET_CLASS (self);

    /* Check if we can prestige */
    if (!klass->can_prestige (self, current_value))
        return lrg_big_number_new_zero ();

    /* Calculate reward */
    reward = klass->calculate_reward (self, current_value);

    /* Add to accumulated points */
    lrg_big_number_add_in_place (priv->points, reward);
    priv->times_prestiged++;

    /* Notify property changes */
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINTS]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMES_PRESTIGED]);

    /* Call virtual on_prestige for subclass cleanup */
    klass->on_prestige (self, reward);

    /* Emit signal */
    g_signal_emit (self, signals[SIGNAL_PRESTIGE_PERFORMED], 0, reward);

    return lrg_big_number_copy (reward);
}

gdouble
lrg_prestige_get_bonus_multiplier (LrgPrestige *self)
{
    LrgPrestigePrivate *priv;
    LrgPrestigeClass *klass;

    g_return_val_if_fail (LRG_IS_PRESTIGE (self), 1.0);

    priv = lrg_prestige_get_instance_private (self);
    klass = LRG_PRESTIGE_GET_CLASS (self);

    return klass->get_bonus_multiplier (self, priv->points);
}

void
lrg_prestige_reset (LrgPrestige *self)
{
    LrgPrestigePrivate *priv;

    g_return_if_fail (LRG_IS_PRESTIGE (self));

    priv = lrg_prestige_get_instance_private (self);

    lrg_big_number_free (priv->points);
    priv->points = lrg_big_number_new_zero ();
    priv->times_prestiged = 0;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POINTS]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMES_PRESTIGED]);
}
