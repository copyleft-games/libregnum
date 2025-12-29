/* lrg-weather-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base class for weather effects.
 */

#include "lrg-weather-effect.h"
#include "../lrg-log.h"

#include <math.h>

typedef struct
{
    gchar   *id;
    gboolean active;
    gfloat   intensity;
    gfloat   target_intensity;
    gfloat   transition_speed;
    gfloat   wind_x;
    gfloat   wind_y;
    gint     render_layer;
} LrgWeatherEffectPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgWeatherEffect, lrg_weather_effect, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_ACTIVE,
    PROP_INTENSITY,
    PROP_TARGET_INTENSITY,
    PROP_TRANSITION_SPEED,
    PROP_WIND_X,
    PROP_WIND_Y,
    PROP_RENDER_LAYER,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_ACTIVATED,
    SIGNAL_DEACTIVATED,
    SIGNAL_INTENSITY_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Virtual method defaults */

static void
lrg_weather_effect_real_activate (LrgWeatherEffect *self)
{
    LrgWeatherEffectPrivate *priv = lrg_weather_effect_get_instance_private (self);

    if (!priv->active)
    {
        priv->active = TRUE;
        g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
    }
}

static void
lrg_weather_effect_real_deactivate (LrgWeatherEffect *self)
{
    LrgWeatherEffectPrivate *priv = lrg_weather_effect_get_instance_private (self);

    if (priv->active)
    {
        priv->active = FALSE;
        g_signal_emit (self, signals[SIGNAL_DEACTIVATED], 0);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
    }
}

static void
lrg_weather_effect_real_update (LrgWeatherEffect *self,
                                gfloat            delta_time)
{
    LrgWeatherEffectPrivate *priv = lrg_weather_effect_get_instance_private (self);

    if (!priv->active)
        return;

    /* Transition intensity toward target */
    if (priv->intensity != priv->target_intensity)
    {
        gfloat diff = priv->target_intensity - priv->intensity;
        gfloat step = priv->transition_speed * delta_time;

        if (fabsf (diff) <= step)
        {
            priv->intensity = priv->target_intensity;
        }
        else if (diff > 0)
        {
            priv->intensity += step;
        }
        else
        {
            priv->intensity -= step;
        }

        g_signal_emit (self, signals[SIGNAL_INTENSITY_CHANGED], 0);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
    }
}

static void
lrg_weather_effect_real_render (LrgWeatherEffect *self)
{
    /* Default implementation does nothing - subclasses override */
}

static void
lrg_weather_effect_real_set_intensity (LrgWeatherEffect *self,
                                       gfloat            intensity)
{
    LrgWeatherEffectPrivate *priv = lrg_weather_effect_get_instance_private (self);

    intensity = CLAMP (intensity, 0.0f, 1.0f);

    if (priv->intensity != intensity)
    {
        priv->intensity = intensity;
        priv->target_intensity = intensity;
        g_signal_emit (self, signals[SIGNAL_INTENSITY_CHANGED], 0);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_INTENSITY]);
    }
}

static void
lrg_weather_effect_real_set_wind (LrgWeatherEffect *self,
                                  gfloat            wind_x,
                                  gfloat            wind_y)
{
    LrgWeatherEffectPrivate *priv = lrg_weather_effect_get_instance_private (self);

    if (priv->wind_x != wind_x || priv->wind_y != wind_y)
    {
        priv->wind_x = wind_x;
        priv->wind_y = wind_y;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIND_X]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIND_Y]);
    }
}

/* GObject implementation */

static void
lrg_weather_effect_finalize (GObject *object)
{
    LrgWeatherEffect *self = LRG_WEATHER_EFFECT (object);
    LrgWeatherEffectPrivate *priv = lrg_weather_effect_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);

    G_OBJECT_CLASS (lrg_weather_effect_parent_class)->finalize (object);
}

static void
lrg_weather_effect_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgWeatherEffect *self = LRG_WEATHER_EFFECT (object);
    LrgWeatherEffectPrivate *priv = lrg_weather_effect_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, priv->id);
        break;
    case PROP_ACTIVE:
        g_value_set_boolean (value, priv->active);
        break;
    case PROP_INTENSITY:
        g_value_set_float (value, priv->intensity);
        break;
    case PROP_TARGET_INTENSITY:
        g_value_set_float (value, priv->target_intensity);
        break;
    case PROP_TRANSITION_SPEED:
        g_value_set_float (value, priv->transition_speed);
        break;
    case PROP_WIND_X:
        g_value_set_float (value, priv->wind_x);
        break;
    case PROP_WIND_Y:
        g_value_set_float (value, priv->wind_y);
        break;
    case PROP_RENDER_LAYER:
        g_value_set_int (value, priv->render_layer);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_weather_effect_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgWeatherEffect *self = LRG_WEATHER_EFFECT (object);
    LrgWeatherEffectPrivate *priv = lrg_weather_effect_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID:
        g_clear_pointer (&priv->id, g_free);
        priv->id = g_value_dup_string (value);
        break;
    case PROP_INTENSITY:
        lrg_weather_effect_set_intensity (self, g_value_get_float (value));
        break;
    case PROP_TARGET_INTENSITY:
        lrg_weather_effect_set_target_intensity (self, g_value_get_float (value));
        break;
    case PROP_TRANSITION_SPEED:
        priv->transition_speed = g_value_get_float (value);
        break;
    case PROP_WIND_X:
        priv->wind_x = g_value_get_float (value);
        break;
    case PROP_WIND_Y:
        priv->wind_y = g_value_get_float (value);
        break;
    case PROP_RENDER_LAYER:
        priv->render_layer = g_value_get_int (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_weather_effect_class_init (LrgWeatherEffectClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_weather_effect_finalize;
    object_class->get_property = lrg_weather_effect_get_property;
    object_class->set_property = lrg_weather_effect_set_property;

    /* Virtual methods */
    klass->activate = lrg_weather_effect_real_activate;
    klass->deactivate = lrg_weather_effect_real_deactivate;
    klass->update = lrg_weather_effect_real_update;
    klass->render = lrg_weather_effect_real_render;
    klass->set_intensity = lrg_weather_effect_real_set_intensity;
    klass->set_wind = lrg_weather_effect_real_set_wind;

    /**
     * LrgWeatherEffect:id:
     *
     * The effect identifier.
     *
     * Since: 1.0
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Effect identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWeatherEffect:active:
     *
     * Whether the effect is currently active.
     *
     * Since: 1.0
     */
    properties[PROP_ACTIVE] =
        g_param_spec_boolean ("active",
                              "Active",
                              "Whether the effect is active",
                              FALSE,
                              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWeatherEffect:intensity:
     *
     * Current effect intensity from 0.0 to 1.0.
     *
     * Since: 1.0
     */
    properties[PROP_INTENSITY] =
        g_param_spec_float ("intensity",
                            "Intensity",
                            "Effect intensity",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWeatherEffect:target-intensity:
     *
     * Target intensity for smooth transitions.
     *
     * Since: 1.0
     */
    properties[PROP_TARGET_INTENSITY] =
        g_param_spec_float ("target-intensity",
                            "Target Intensity",
                            "Target intensity for transitions",
                            0.0f, 1.0f, 0.5f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWeatherEffect:transition-speed:
     *
     * Speed of intensity transitions in units per second.
     *
     * Since: 1.0
     */
    properties[PROP_TRANSITION_SPEED] =
        g_param_spec_float ("transition-speed",
                            "Transition Speed",
                            "Speed of intensity transitions",
                            0.0f, 10.0f, 0.5f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWeatherEffect:wind-x:
     *
     * X component of wind vector.
     *
     * Since: 1.0
     */
    properties[PROP_WIND_X] =
        g_param_spec_float ("wind-x",
                            "Wind X",
                            "X component of wind",
                            -1000.0f, 1000.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWeatherEffect:wind-y:
     *
     * Y component of wind vector.
     *
     * Since: 1.0
     */
    properties[PROP_WIND_Y] =
        g_param_spec_float ("wind-y",
                            "Wind Y",
                            "Y component of wind",
                            -1000.0f, 1000.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWeatherEffect:render-layer:
     *
     * Render layer for ordering effects.
     *
     * Since: 1.0
     */
    properties[PROP_RENDER_LAYER] =
        g_param_spec_int ("render-layer",
                          "Render Layer",
                          "Render layer for effect ordering",
                          G_MININT, G_MAXINT, 0,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgWeatherEffect::activated:
     * @self: The effect
     *
     * Emitted when the effect is activated.
     *
     * Since: 1.0
     */
    signals[SIGNAL_ACTIVATED] =
        g_signal_new ("activated",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgWeatherEffect::deactivated:
     * @self: The effect
     *
     * Emitted when the effect is deactivated.
     *
     * Since: 1.0
     */
    signals[SIGNAL_DEACTIVATED] =
        g_signal_new ("deactivated",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgWeatherEffect::intensity-changed:
     * @self: The effect
     *
     * Emitted when the intensity changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_INTENSITY_CHANGED] =
        g_signal_new ("intensity-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_weather_effect_init (LrgWeatherEffect *self)
{
    LrgWeatherEffectPrivate *priv = lrg_weather_effect_get_instance_private (self);

    priv->id = NULL;
    priv->active = FALSE;
    priv->intensity = 0.5f;
    priv->target_intensity = 0.5f;
    priv->transition_speed = 0.5f;
    priv->wind_x = 0.0f;
    priv->wind_y = 0.0f;
    priv->render_layer = 0;
}

/* Public API */

const gchar *
lrg_weather_effect_get_id (LrgWeatherEffect *self)
{
    LrgWeatherEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_WEATHER_EFFECT (self), NULL);

    priv = lrg_weather_effect_get_instance_private (self);
    return priv->id;
}

void
lrg_weather_effect_set_id (LrgWeatherEffect *self,
                           const gchar      *id)
{
    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    g_object_set (self, "id", id, NULL);
}

gboolean
lrg_weather_effect_is_active (LrgWeatherEffect *self)
{
    LrgWeatherEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_WEATHER_EFFECT (self), FALSE);

    priv = lrg_weather_effect_get_instance_private (self);
    return priv->active;
}

gfloat
lrg_weather_effect_get_intensity (LrgWeatherEffect *self)
{
    LrgWeatherEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_WEATHER_EFFECT (self), 0.0f);

    priv = lrg_weather_effect_get_instance_private (self);
    return priv->intensity;
}

void
lrg_weather_effect_set_intensity (LrgWeatherEffect *self,
                                  gfloat            intensity)
{
    LrgWeatherEffectClass *klass;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    klass = LRG_WEATHER_EFFECT_GET_CLASS (self);
    if (klass->set_intensity)
        klass->set_intensity (self, intensity);
}

gfloat
lrg_weather_effect_get_target_intensity (LrgWeatherEffect *self)
{
    LrgWeatherEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_WEATHER_EFFECT (self), 0.0f);

    priv = lrg_weather_effect_get_instance_private (self);
    return priv->target_intensity;
}

void
lrg_weather_effect_set_target_intensity (LrgWeatherEffect *self,
                                         gfloat            intensity)
{
    LrgWeatherEffectPrivate *priv;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    priv = lrg_weather_effect_get_instance_private (self);
    intensity = CLAMP (intensity, 0.0f, 1.0f);

    if (priv->target_intensity != intensity)
    {
        priv->target_intensity = intensity;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TARGET_INTENSITY]);
    }
}

gfloat
lrg_weather_effect_get_transition_speed (LrgWeatherEffect *self)
{
    LrgWeatherEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_WEATHER_EFFECT (self), 0.5f);

    priv = lrg_weather_effect_get_instance_private (self);
    return priv->transition_speed;
}

void
lrg_weather_effect_set_transition_speed (LrgWeatherEffect *self,
                                         gfloat            speed)
{
    LrgWeatherEffectPrivate *priv;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    priv = lrg_weather_effect_get_instance_private (self);
    speed = MAX (speed, 0.0f);

    if (priv->transition_speed != speed)
    {
        priv->transition_speed = speed;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRANSITION_SPEED]);
    }
}

void
lrg_weather_effect_get_wind (LrgWeatherEffect *self,
                             gfloat           *wind_x,
                             gfloat           *wind_y)
{
    LrgWeatherEffectPrivate *priv;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    priv = lrg_weather_effect_get_instance_private (self);

    if (wind_x)
        *wind_x = priv->wind_x;
    if (wind_y)
        *wind_y = priv->wind_y;
}

void
lrg_weather_effect_set_wind (LrgWeatherEffect *self,
                             gfloat            wind_x,
                             gfloat            wind_y)
{
    LrgWeatherEffectClass *klass;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    klass = LRG_WEATHER_EFFECT_GET_CLASS (self);
    if (klass->set_wind)
        klass->set_wind (self, wind_x, wind_y);
}

void
lrg_weather_effect_activate (LrgWeatherEffect *self)
{
    LrgWeatherEffectClass *klass;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    klass = LRG_WEATHER_EFFECT_GET_CLASS (self);
    if (klass->activate)
        klass->activate (self);
}

void
lrg_weather_effect_deactivate (LrgWeatherEffect *self)
{
    LrgWeatherEffectClass *klass;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    klass = LRG_WEATHER_EFFECT_GET_CLASS (self);
    if (klass->deactivate)
        klass->deactivate (self);
}

void
lrg_weather_effect_update (LrgWeatherEffect *self,
                           gfloat            delta_time)
{
    LrgWeatherEffectClass *klass;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    klass = LRG_WEATHER_EFFECT_GET_CLASS (self);
    if (klass->update)
        klass->update (self, delta_time);
}

void
lrg_weather_effect_render (LrgWeatherEffect *self)
{
    LrgWeatherEffectClass *klass;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    klass = LRG_WEATHER_EFFECT_GET_CLASS (self);
    if (klass->render)
        klass->render (self);
}

gint
lrg_weather_effect_get_render_layer (LrgWeatherEffect *self)
{
    LrgWeatherEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_WEATHER_EFFECT (self), 0);

    priv = lrg_weather_effect_get_instance_private (self);
    return priv->render_layer;
}

void
lrg_weather_effect_set_render_layer (LrgWeatherEffect *self,
                                     gint              layer)
{
    LrgWeatherEffectPrivate *priv;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    priv = lrg_weather_effect_get_instance_private (self);

    if (priv->render_layer != layer)
    {
        priv->render_layer = layer;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RENDER_LAYER]);
    }
}
