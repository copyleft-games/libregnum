/* lrg-tween.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Single property tween that animates a GObject property.
 */

#include "lrg-tween.h"
#include "lrg-easing.h"
#include "../lrg-log.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_TWEEN

/**
 * LrgTween:
 *
 * A tween that animates a single property on a #GObject target.
 *
 * #LrgTween can animate numeric properties (int, uint, float, double)
 * by interpolating between a start and end value using the configured
 * easing function.
 *
 * Example usage:
 * |[<!-- language="C" -->
 * LrgTween *tween = lrg_tween_new (G_OBJECT (sprite), "opacity", 1.0);
 * lrg_tween_set_from_float (tween, 0.0);
 * lrg_tween_set_to_float (tween, 1.0);
 * lrg_tween_base_set_easing (LRG_TWEEN_BASE (tween), LRG_EASING_EASE_OUT_CUBIC);
 * lrg_tween_base_start (LRG_TWEEN_BASE (tween));
 * ]|
 *
 * Since: 1.0
 */

struct _LrgTween
{
    LrgTweenBase  parent_instance;

    /* Target */
    GObject      *target;
    gchar        *property_name;
    GParamSpec   *pspec;

    /* Values */
    GValue        from_value;
    GValue        to_value;
    gboolean      from_value_set;
    gboolean      to_value_set;

    /* Options */
    gboolean      relative;
    gboolean      use_current_as_from;
};

enum
{
    PROP_0,
    PROP_TARGET,
    PROP_PROPERTY_NAME,
    PROP_RELATIVE,
    PROP_USE_CURRENT_AS_FROM,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgTween, lrg_tween, LRG_TYPE_TWEEN_BASE)

/*
 * Helper to interpolate between two GValues.
 * Supports int, uint, float, double.
 */
static void
interpolate_value (const GValue *from,
                   const GValue *to,
                   gfloat        t,
                   GValue       *result)
{
    GType type;

    type = G_VALUE_TYPE (from);
    g_value_init (result, type);

    switch (type)
    {
    case G_TYPE_INT:
        {
            gint from_val = g_value_get_int (from);
            gint to_val = g_value_get_int (to);
            gint interpolated = (gint)(from_val + (to_val - from_val) * t);
            g_value_set_int (result, interpolated);
        }
        break;

    case G_TYPE_UINT:
        {
            guint from_val = g_value_get_uint (from);
            guint to_val = g_value_get_uint (to);
            guint interpolated = (guint)(from_val + (gfloat)(to_val - from_val) * t);
            g_value_set_uint (result, interpolated);
        }
        break;

    case G_TYPE_FLOAT:
        {
            gfloat from_val = g_value_get_float (from);
            gfloat to_val = g_value_get_float (to);
            gfloat interpolated = from_val + (to_val - from_val) * t;
            g_value_set_float (result, interpolated);
        }
        break;

    case G_TYPE_DOUBLE:
        {
            gdouble from_val = g_value_get_double (from);
            gdouble to_val = g_value_get_double (to);
            gdouble interpolated = from_val + (to_val - from_val) * (gdouble)t;
            g_value_set_double (result, interpolated);
        }
        break;

    default:
        lrg_warning (LRG_LOG_DOMAIN, "Cannot interpolate GValue of type %s",
                     g_type_name (type));
        break;
    }
}

/*
 * Adds two GValues (for relative mode).
 */
static void
add_values (const GValue *base,
            const GValue *delta,
            GValue       *result)
{
    GType type;

    type = G_VALUE_TYPE (base);
    g_value_init (result, type);

    switch (type)
    {
    case G_TYPE_INT:
        g_value_set_int (result, g_value_get_int (base) + g_value_get_int (delta));
        break;

    case G_TYPE_UINT:
        g_value_set_uint (result, g_value_get_uint (base) + g_value_get_uint (delta));
        break;

    case G_TYPE_FLOAT:
        g_value_set_float (result, g_value_get_float (base) + g_value_get_float (delta));
        break;

    case G_TYPE_DOUBLE:
        g_value_set_double (result, g_value_get_double (base) + g_value_get_double (delta));
        break;

    default:
        lrg_warning (LRG_LOG_DOMAIN, "Cannot add GValues of type %s",
                     g_type_name (type));
        break;
    }
}

/*
 * Virtual method implementations
 */

static void
lrg_tween_start (LrgTweenBase *base)
{
    LrgTween *self;
    LrgTweenBaseClass *parent_class;

    self = LRG_TWEEN (base);

    /* Capture current value if requested */
    if (self->use_current_as_from && self->target != NULL && self->pspec != NULL)
    {
        if (G_IS_VALUE (&self->from_value))
        {
            g_value_unset (&self->from_value);
        }

        g_value_init (&self->from_value, G_PARAM_SPEC_VALUE_TYPE (self->pspec));
        g_object_get_property (self->target, self->property_name, &self->from_value);
        self->from_value_set = TRUE;
    }

    /* Calculate absolute target if relative mode */
    if (self->relative && self->from_value_set && self->to_value_set)
    {
        GValue absolute_to = G_VALUE_INIT;
        add_values (&self->from_value, &self->to_value, &absolute_to);

        /* Reinitialize to_value with correct type before copying */
        g_value_unset (&self->to_value);
        g_value_init (&self->to_value, G_VALUE_TYPE (&absolute_to));
        g_value_copy (&absolute_to, &self->to_value);
        g_value_unset (&absolute_to);

        /* Mark as no longer relative so we don't recalculate on restart */
        self->relative = FALSE;
    }

    /* Chain up to parent */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_parent_class);
    if (parent_class->start != NULL)
    {
        parent_class->start (base);
    }
}

static void
lrg_tween_update (LrgTweenBase *base,
                  gfloat        delta_time)
{
    LrgTween *self;
    LrgTweenBaseClass *parent_class;
    gfloat progress;
    gfloat eased;
    LrgEasingType easing;
    GValue current_value = G_VALUE_INIT;

    self = LRG_TWEEN (base);

    /* Skip if not properly configured */
    if (self->target == NULL || self->pspec == NULL)
    {
        return;
    }

    if (!self->from_value_set || !self->to_value_set)
    {
        return;
    }

    /* Chain up first to update base state (elapsed, progress) */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_parent_class);
    if (parent_class->update != NULL)
    {
        parent_class->update (base, delta_time);
    }

    /* Get current progress and apply easing */
    progress = lrg_tween_base_get_progress (base);
    easing = lrg_tween_base_get_easing (base);
    eased = lrg_easing_apply (easing, progress);

    /* Interpolate and set property */
    interpolate_value (&self->from_value, &self->to_value, eased, &current_value);

    if (G_IS_VALUE (&current_value))
    {
        g_object_set_property (self->target, self->property_name, &current_value);
        g_value_unset (&current_value);
    }
}

static void
lrg_tween_reset (LrgTweenBase *base)
{
    LrgTween *self;
    LrgTweenBaseClass *parent_class;

    self = LRG_TWEEN (base);

    /* Reset to from_value if we have a target */
    if (self->target != NULL && self->pspec != NULL && self->from_value_set)
    {
        g_object_set_property (self->target, self->property_name, &self->from_value);
    }

    /* Chain up to parent */
    parent_class = LRG_TWEEN_BASE_CLASS (lrg_tween_parent_class);
    if (parent_class->reset != NULL)
    {
        parent_class->reset (base);
    }
}

/*
 * GObject virtual methods
 */

static void
lrg_tween_dispose (GObject *object)
{
    LrgTween *self;

    self = LRG_TWEEN (object);

    /* Release the weak reference on target */
    if (self->target != NULL)
    {
        g_object_remove_weak_pointer (self->target, (gpointer *)&self->target);
        self->target = NULL;
    }

    G_OBJECT_CLASS (lrg_tween_parent_class)->dispose (object);
}

static void
lrg_tween_finalize (GObject *object)
{
    LrgTween *self;

    self = LRG_TWEEN (object);

    g_clear_pointer (&self->property_name, g_free);

    if (G_IS_VALUE (&self->from_value))
    {
        g_value_unset (&self->from_value);
    }

    if (G_IS_VALUE (&self->to_value))
    {
        g_value_unset (&self->to_value);
    }

    G_OBJECT_CLASS (lrg_tween_parent_class)->finalize (object);
}

static void
lrg_tween_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
    LrgTween *self;

    self = LRG_TWEEN (object);

    switch (prop_id)
    {
    case PROP_TARGET:
        g_value_set_object (value, self->target);
        break;

    case PROP_PROPERTY_NAME:
        g_value_set_string (value, self->property_name);
        break;

    case PROP_RELATIVE:
        g_value_set_boolean (value, self->relative);
        break;

    case PROP_USE_CURRENT_AS_FROM:
        g_value_set_boolean (value, self->use_current_as_from);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_tween_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    LrgTween *self;

    self = LRG_TWEEN (object);

    switch (prop_id)
    {
    case PROP_TARGET:
        {
            GObject *new_target;

            /* Remove old weak reference */
            if (self->target != NULL)
            {
                g_object_remove_weak_pointer (self->target, (gpointer *)&self->target);
            }

            new_target = g_value_get_object (value);

            if (new_target != NULL)
            {
                self->target = new_target;
                g_object_add_weak_pointer (self->target, (gpointer *)&self->target);

                /* Look up property spec if we have both target and property name */
                if (self->property_name != NULL)
                {
                    self->pspec = g_object_class_find_property (
                        G_OBJECT_GET_CLASS (self->target),
                        self->property_name);

                    if (self->pspec == NULL)
                    {
                        lrg_warning (LRG_LOG_DOMAIN,
                                     "Property '%s' not found on target object",
                                     self->property_name);
                    }
                }
            }
            else
            {
                self->target = NULL;
                self->pspec = NULL;
            }
        }
        break;

    case PROP_PROPERTY_NAME:
        g_clear_pointer (&self->property_name, g_free);
        self->property_name = g_value_dup_string (value);

        /* Look up property spec if we have both target and property name */
        if (self->target != NULL && self->property_name != NULL)
        {
            self->pspec = g_object_class_find_property (
                G_OBJECT_GET_CLASS (self->target),
                self->property_name);

            if (self->pspec == NULL)
            {
                lrg_warning (LRG_LOG_DOMAIN,
                             "Property '%s' not found on target object",
                             self->property_name);
            }
        }
        break;

    case PROP_RELATIVE:
        self->relative = g_value_get_boolean (value);
        break;

    case PROP_USE_CURRENT_AS_FROM:
        self->use_current_as_from = g_value_get_boolean (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_tween_class_init (LrgTweenClass *klass)
{
    GObjectClass *object_class;
    LrgTweenBaseClass *tween_base_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->dispose = lrg_tween_dispose;
    object_class->finalize = lrg_tween_finalize;
    object_class->get_property = lrg_tween_get_property;
    object_class->set_property = lrg_tween_set_property;

    tween_base_class = LRG_TWEEN_BASE_CLASS (klass);
    tween_base_class->start = lrg_tween_start;
    tween_base_class->update = lrg_tween_update;
    tween_base_class->reset = lrg_tween_reset;

    /**
     * LrgTween:target:
     *
     * The target #GObject to animate.
     *
     * Since: 1.0
     */
    properties[PROP_TARGET] =
        g_param_spec_object ("target",
                             "Target",
                             "The target GObject to animate",
                             G_TYPE_OBJECT,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTween:property-name:
     *
     * The name of the property to animate on the target.
     *
     * Since: 1.0
     */
    properties[PROP_PROPERTY_NAME] =
        g_param_spec_string ("property-name",
                             "Property Name",
                             "The name of the property to animate",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgTween:relative:
     *
     * Whether the end value is relative to the start value.
     * When %TRUE, the "to" value is added to the "from" value
     * to calculate the actual target.
     *
     * Since: 1.0
     */
    properties[PROP_RELATIVE] =
        g_param_spec_boolean ("relative",
                              "Relative",
                              "Whether the end value is relative to start",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgTween:use-current-as-from:
     *
     * Whether to capture the current property value as the start
     * value when the tween starts.
     *
     * Since: 1.0
     */
    properties[PROP_USE_CURRENT_AS_FROM] =
        g_param_spec_boolean ("use-current-as-from",
                              "Use Current As From",
                              "Whether to use current value as start",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_tween_init (LrgTween *self)
{
    self->target = NULL;
    self->property_name = NULL;
    self->pspec = NULL;
    self->from_value_set = FALSE;
    self->to_value_set = FALSE;
    self->relative = FALSE;
    self->use_current_as_from = TRUE;  /* Default: capture current on start */
}

/*
 * Public API
 */

/**
 * lrg_tween_new:
 * @target: (transfer none): The target #GObject to animate
 * @property_name: The name of the property to animate
 * @duration: Duration in seconds
 *
 * Creates a new tween that animates a property on the target object.
 * The property must be readable, writable, and of a numeric type
 * (int, uint, float, double) or a type that can be interpolated.
 *
 * Returns: (transfer full): A new #LrgTween
 *
 * Since: 1.0
 */
LrgTween *
lrg_tween_new (GObject     *target,
               const gchar *property_name,
               gfloat       duration)
{
    g_return_val_if_fail (G_IS_OBJECT (target), NULL);
    g_return_val_if_fail (property_name != NULL, NULL);
    g_return_val_if_fail (duration > 0.0f, NULL);

    return g_object_new (LRG_TYPE_TWEEN,
                         "target", target,
                         "property-name", property_name,
                         "duration", duration,
                         NULL);
}

/**
 * lrg_tween_new_full:
 * @target: (transfer none): The target #GObject to animate
 * @property_name: The name of the property to animate
 * @duration: Duration in seconds
 * @from: (nullable): Start value (or %NULL to use current value)
 * @to: End value
 *
 * Creates a new tween with explicit start and end values.
 *
 * Returns: (transfer full): A new #LrgTween
 *
 * Since: 1.0
 */
LrgTween *
lrg_tween_new_full (GObject      *target,
                    const gchar  *property_name,
                    gfloat        duration,
                    const GValue *from,
                    const GValue *to)
{
    LrgTween *tween;

    g_return_val_if_fail (G_IS_OBJECT (target), NULL);
    g_return_val_if_fail (property_name != NULL, NULL);
    g_return_val_if_fail (duration > 0.0f, NULL);
    g_return_val_if_fail (to != NULL, NULL);

    tween = lrg_tween_new (target, property_name, duration);

    if (from != NULL)
    {
        lrg_tween_set_from_value (tween, from);
        tween->use_current_as_from = FALSE;
    }

    lrg_tween_set_to_value (tween, to);

    return tween;
}

/**
 * lrg_tween_get_target:
 * @self: A #LrgTween
 *
 * Gets the target object being animated.
 *
 * Returns: (transfer none) (nullable): The target #GObject
 *
 * Since: 1.0
 */
GObject *
lrg_tween_get_target (LrgTween *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN (self), NULL);

    return self->target;
}

/**
 * lrg_tween_get_property_name:
 * @self: A #LrgTween
 *
 * Gets the name of the property being animated.
 *
 * Returns: (transfer none): The property name
 *
 * Since: 1.0
 */
const gchar *
lrg_tween_get_property_name (LrgTween *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN (self), NULL);

    return self->property_name;
}

/**
 * lrg_tween_set_from_float:
 * @self: A #LrgTween
 * @value: The start value
 *
 * Sets the start value for a float property.
 *
 * Since: 1.0
 */
void
lrg_tween_set_from_float (LrgTween *self,
                          gfloat    value)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    if (G_IS_VALUE (&self->from_value))
    {
        g_value_unset (&self->from_value);
    }

    g_value_init (&self->from_value, G_TYPE_FLOAT);
    g_value_set_float (&self->from_value, value);
    self->from_value_set = TRUE;
    self->use_current_as_from = FALSE;
}

/**
 * lrg_tween_set_to_float:
 * @self: A #LrgTween
 * @value: The end value
 *
 * Sets the end value for a float property.
 *
 * Since: 1.0
 */
void
lrg_tween_set_to_float (LrgTween *self,
                        gfloat    value)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    if (G_IS_VALUE (&self->to_value))
    {
        g_value_unset (&self->to_value);
    }

    g_value_init (&self->to_value, G_TYPE_FLOAT);
    g_value_set_float (&self->to_value, value);
    self->to_value_set = TRUE;
}

/**
 * lrg_tween_set_from_double:
 * @self: A #LrgTween
 * @value: The start value
 *
 * Sets the start value for a double property.
 *
 * Since: 1.0
 */
void
lrg_tween_set_from_double (LrgTween *self,
                           gdouble   value)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    if (G_IS_VALUE (&self->from_value))
    {
        g_value_unset (&self->from_value);
    }

    g_value_init (&self->from_value, G_TYPE_DOUBLE);
    g_value_set_double (&self->from_value, value);
    self->from_value_set = TRUE;
    self->use_current_as_from = FALSE;
}

/**
 * lrg_tween_set_to_double:
 * @self: A #LrgTween
 * @value: The end value
 *
 * Sets the end value for a double property.
 *
 * Since: 1.0
 */
void
lrg_tween_set_to_double (LrgTween *self,
                         gdouble   value)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    if (G_IS_VALUE (&self->to_value))
    {
        g_value_unset (&self->to_value);
    }

    g_value_init (&self->to_value, G_TYPE_DOUBLE);
    g_value_set_double (&self->to_value, value);
    self->to_value_set = TRUE;
}

/**
 * lrg_tween_set_from_int:
 * @self: A #LrgTween
 * @value: The start value
 *
 * Sets the start value for an integer property.
 *
 * Since: 1.0
 */
void
lrg_tween_set_from_int (LrgTween *self,
                        gint      value)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    if (G_IS_VALUE (&self->from_value))
    {
        g_value_unset (&self->from_value);
    }

    g_value_init (&self->from_value, G_TYPE_INT);
    g_value_set_int (&self->from_value, value);
    self->from_value_set = TRUE;
    self->use_current_as_from = FALSE;
}

/**
 * lrg_tween_set_to_int:
 * @self: A #LrgTween
 * @value: The end value
 *
 * Sets the end value for an integer property.
 *
 * Since: 1.0
 */
void
lrg_tween_set_to_int (LrgTween *self,
                      gint      value)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    if (G_IS_VALUE (&self->to_value))
    {
        g_value_unset (&self->to_value);
    }

    g_value_init (&self->to_value, G_TYPE_INT);
    g_value_set_int (&self->to_value, value);
    self->to_value_set = TRUE;
}

/**
 * lrg_tween_set_from_uint:
 * @self: A #LrgTween
 * @value: The start value
 *
 * Sets the start value for an unsigned integer property.
 *
 * Since: 1.0
 */
void
lrg_tween_set_from_uint (LrgTween *self,
                         guint     value)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    if (G_IS_VALUE (&self->from_value))
    {
        g_value_unset (&self->from_value);
    }

    g_value_init (&self->from_value, G_TYPE_UINT);
    g_value_set_uint (&self->from_value, value);
    self->from_value_set = TRUE;
    self->use_current_as_from = FALSE;
}

/**
 * lrg_tween_set_to_uint:
 * @self: A #LrgTween
 * @value: The end value
 *
 * Sets the end value for an unsigned integer property.
 *
 * Since: 1.0
 */
void
lrg_tween_set_to_uint (LrgTween *self,
                       guint     value)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    if (G_IS_VALUE (&self->to_value))
    {
        g_value_unset (&self->to_value);
    }

    g_value_init (&self->to_value, G_TYPE_UINT);
    g_value_set_uint (&self->to_value, value);
    self->to_value_set = TRUE;
}

/**
 * lrg_tween_set_from_value:
 * @self: A #LrgTween
 * @value: The start value
 *
 * Sets the start value using a #GValue.
 * The value type must be compatible with the property type.
 *
 * Since: 1.0
 */
void
lrg_tween_set_from_value (LrgTween     *self,
                          const GValue *value)
{
    g_return_if_fail (LRG_IS_TWEEN (self));
    g_return_if_fail (G_IS_VALUE (value));

    if (G_IS_VALUE (&self->from_value))
    {
        g_value_unset (&self->from_value);
    }

    g_value_init (&self->from_value, G_VALUE_TYPE (value));
    g_value_copy (value, &self->from_value);
    self->from_value_set = TRUE;
    self->use_current_as_from = FALSE;
}

/**
 * lrg_tween_set_to_value:
 * @self: A #LrgTween
 * @value: The end value
 *
 * Sets the end value using a #GValue.
 * The value type must be compatible with the property type.
 *
 * Since: 1.0
 */
void
lrg_tween_set_to_value (LrgTween     *self,
                        const GValue *value)
{
    g_return_if_fail (LRG_IS_TWEEN (self));
    g_return_if_fail (G_IS_VALUE (value));

    if (G_IS_VALUE (&self->to_value))
    {
        g_value_unset (&self->to_value);
    }

    g_value_init (&self->to_value, G_VALUE_TYPE (value));
    g_value_copy (value, &self->to_value);
    self->to_value_set = TRUE;
}

/**
 * lrg_tween_get_relative:
 * @self: A #LrgTween
 *
 * Gets whether the end value is relative to the start value.
 *
 * Returns: %TRUE if relative mode is enabled
 *
 * Since: 1.0
 */
gboolean
lrg_tween_get_relative (LrgTween *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN (self), FALSE);

    return self->relative;
}

/**
 * lrg_tween_set_relative:
 * @self: A #LrgTween
 * @relative: Whether to use relative mode
 *
 * Sets whether the end value is relative to the start value.
 * When enabled, the end value is added to the start value.
 *
 * Since: 1.0
 */
void
lrg_tween_set_relative (LrgTween *self,
                        gboolean  relative)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    if (self->relative != relative)
    {
        self->relative = relative;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RELATIVE]);
    }
}

/**
 * lrg_tween_by_float:
 * @self: A #LrgTween
 * @delta: The amount to change
 *
 * Convenience function to animate by a relative amount.
 * Sets the end value as current + delta.
 *
 * Since: 1.0
 */
void
lrg_tween_by_float (LrgTween *self,
                    gfloat    delta)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    lrg_tween_set_to_float (self, delta);
    self->relative = TRUE;
    self->use_current_as_from = TRUE;
}

/**
 * lrg_tween_by_int:
 * @self: A #LrgTween
 * @delta: The amount to change
 *
 * Convenience function to animate by a relative amount.
 * Sets the end value as current + delta.
 *
 * Since: 1.0
 */
void
lrg_tween_by_int (LrgTween *self,
                  gint      delta)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    lrg_tween_set_to_int (self, delta);
    self->relative = TRUE;
    self->use_current_as_from = TRUE;
}

/**
 * lrg_tween_get_use_current_as_from:
 * @self: A #LrgTween
 *
 * Gets whether to use the current property value as the start value.
 *
 * Returns: %TRUE if using current value as start
 *
 * Since: 1.0
 */
gboolean
lrg_tween_get_use_current_as_from (LrgTween *self)
{
    g_return_val_if_fail (LRG_IS_TWEEN (self), FALSE);

    return self->use_current_as_from;
}

/**
 * lrg_tween_set_use_current_as_from:
 * @self: A #LrgTween
 * @use_current: Whether to use current value
 *
 * Sets whether to capture the current property value as the start
 * value when the tween starts.
 *
 * Since: 1.0
 */
void
lrg_tween_set_use_current_as_from (LrgTween *self,
                                   gboolean  use_current)
{
    g_return_if_fail (LRG_IS_TWEEN (self));

    if (self->use_current_as_from != use_current)
    {
        self->use_current_as_from = use_current;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_USE_CURRENT_AS_FROM]);
    }
}
