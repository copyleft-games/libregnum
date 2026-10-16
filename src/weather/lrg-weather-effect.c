/* lrg-weather-effect.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base class for weather effects, plus the rlgl helpers the effects
 * share to draw camera-facing particles (lrg-weather-effect-private.h).
 */

#include "config.h"

#include <math.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-weather-effect.h"
#include "lrg-weather-effect-private.h"
#include "../lrg-log.h"

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
    LrgWeatherSpace space;
    gfloat   volume_radius;
    gfloat   volume_height;
    gfloat   camera[3];
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
    PROP_SPACE,
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
    case PROP_SPACE:
        g_value_set_enum (value, priv->space);
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
    case PROP_SPACE:
        lrg_weather_effect_set_space (self, g_value_get_enum (value));
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

    /**
     * LrgWeatherEffect:space:
     *
     * Where the effect simulates and draws its particles: a 2D screen
     * area (pixels) or a 3D volume around the camera (world units).
     */
    properties[PROP_SPACE] =
        g_param_spec_enum ("space",
                           "Space",
                           "Screen (2D) or world (3D) particles",
                           LRG_TYPE_WEATHER_SPACE,
                           LRG_WEATHER_SPACE_SCREEN,
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
    priv->space = LRG_WEATHER_SPACE_SCREEN;
    priv->volume_radius = LRG_WEATHER_DEFAULT_VOLUME_RADIUS;
    priv->volume_height = LRG_WEATHER_DEFAULT_VOLUME_HEIGHT;
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

void
lrg_weather_effect_render_world (LrgWeatherEffect *self,
                                 const GrlVector3 *camera_position)
{
    LrgWeatherEffectClass *klass;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));
    g_return_if_fail (camera_position != NULL);

    /* The camera is kept even headless: the next update wraps and
     * spawns around it. */
    lrg_weather_effect_set_camera_position (self, camera_position);

    klass = LRG_WEATHER_EFFECT_GET_CLASS (self);
    if (klass->render_world != NULL)
        klass->render_world (self, camera_position);
}

void
lrg_weather_effect_render_overlay (LrgWeatherEffect *self,
                                   gint              width,
                                   gint              height)
{
    LrgWeatherEffectClass *klass;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    if (width <= 0 || height <= 0)
        return;

    klass = LRG_WEATHER_EFFECT_GET_CLASS (self);
    if (klass->render_overlay != NULL)
        klass->render_overlay (self, width, height);
}

LrgWeatherSpace
lrg_weather_effect_get_space (LrgWeatherEffect *self)
{
    LrgWeatherEffectPrivate *priv;

    g_return_val_if_fail (LRG_IS_WEATHER_EFFECT (self), LRG_WEATHER_SPACE_SCREEN);

    priv = lrg_weather_effect_get_instance_private (self);
    return priv->space;
}

void
lrg_weather_effect_set_space (LrgWeatherEffect *self,
                              LrgWeatherSpace   space)
{
    LrgWeatherEffectPrivate *priv;
    LrgWeatherEffectClass   *klass;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));
    g_return_if_fail (space == LRG_WEATHER_SPACE_SCREEN || space == LRG_WEATHER_SPACE_WORLD);

    priv = lrg_weather_effect_get_instance_private (self);
    if (priv->space == space)
        return;

    /* Store first: the subclass resets its units for the new space. */
    priv->space = space;
    klass = LRG_WEATHER_EFFECT_GET_CLASS (self);
    if (klass->space_changed != NULL)
        klass->space_changed (self);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPACE]);
}

void
lrg_weather_effect_get_volume (LrgWeatherEffect *self,
                               gfloat           *radius,
                               gfloat           *height)
{
    LrgWeatherEffectPrivate *priv;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    priv = lrg_weather_effect_get_instance_private (self);
    if (radius != NULL)
        *radius = priv->volume_radius;
    if (height != NULL)
        *height = priv->volume_height;
}

void
lrg_weather_effect_set_volume (LrgWeatherEffect *self,
                               gfloat            radius,
                               gfloat            height)
{
    LrgWeatherEffectPrivate *priv;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));

    priv = lrg_weather_effect_get_instance_private (self);
    if (isfinite (radius) && radius > 0.0f)
        priv->volume_radius = radius;
    if (isfinite (height) && height > 0.0f)
        priv->volume_height = height;
}

void
lrg_weather_effect_get_camera_position (LrgWeatherEffect *self,
                                        GrlVector3       *out_position)
{
    LrgWeatherEffectPrivate *priv;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));
    g_return_if_fail (out_position != NULL);

    priv = lrg_weather_effect_get_instance_private (self);
    out_position->x = priv->camera[0];
    out_position->y = priv->camera[1];
    out_position->z = priv->camera[2];
}

void
lrg_weather_effect_set_camera_position (LrgWeatherEffect *self,
                                        const GrlVector3 *position)
{
    LrgWeatherEffectPrivate *priv;

    g_return_if_fail (LRG_IS_WEATHER_EFFECT (self));
    g_return_if_fail (position != NULL);

    if (!isfinite (position->x) || !isfinite (position->y) || !isfinite (position->z))
        return;

    priv = lrg_weather_effect_get_instance_private (self);
    priv->camera[0] = position->x;
    priv->camera[1] = position->y;
    priv->camera[2] = position->z;
}

/* ------------------------------------------------------------------------ */
/* Shared drawing helpers (lrg-weather-effect-private.h)                    */
/* ------------------------------------------------------------------------ */

gfloat
lrg_weather_wrap (gfloat value,
                  gfloat centre,
                  gfloat extent)
{
    gfloat rel;

    if (!isfinite (value) || !isfinite (centre) || !isfinite (extent) || extent <= 0.0f)
        return centre;

    /* Offset from the box's low edge, folded into [0, extent). */
    rel = value - (centre - extent * 0.5f);
    rel -= floorf (rel / extent) * extent;
    if (rel >= extent || rel < 0.0f)   /* float rounding at the edge */
        rel = 0.0f;
    return centre - extent * 0.5f + rel;
}

gboolean
lrg_weather_can_draw (void)
{
    return IsWindowReady ();
}

void
lrg_weather_begin_particles (void)
{
    /* GL state changes are immediate but rlgl batches vertices: flush
     * what was queued under the old state before changing it. */
    rlDrawRenderBatchActive ();
    rlDisableBackfaceCulling ();
    rlEnableDepthTest ();
    rlDisableDepthMask ();
    rlSetTexture (0);
    rlBegin (RL_TRIANGLES);
}

void
lrg_weather_end_particles (void)
{
    rlEnd ();
    rlDrawRenderBatchActive ();
    rlEnableDepthMask ();
    rlEnableBackfaceCulling ();
}

void
lrg_weather_camera_basis (gfloat *right,
                          gfloat *up)
{
    Matrix view = rlGetMatrixModelview ();

    /* The view matrix rows are the camera axes in world space. */
    right[0] = view.m0;
    right[1] = view.m4;
    right[2] = view.m8;
    up[0] = view.m1;
    up[1] = view.m5;
    up[2] = view.m9;
}

void
lrg_weather_emit_ribbon (const gfloat *a,
                         const gfloat *b,
                         gfloat        width,
                         const gfloat *eye,
                         const guint8 *color_a,
                         const guint8 *color_b)
{
    gfloat dir[3];
    gfloat view[3];
    gfloat side[3];
    gfloat len;
    guint  k;

    for (k = 0; k < 3; k++)
    {
        dir[k] = b[k] - a[k];
        view[k] = eye[k] - (a[k] + b[k]) * 0.5f;
    }

    /* side = dir x view: across the segment, facing the eye. */
    side[0] = dir[1] * view[2] - dir[2] * view[1];
    side[1] = dir[2] * view[0] - dir[0] * view[2];
    side[2] = dir[0] * view[1] - dir[1] * view[0];
    len = sqrtf (side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
    if (len < 1e-8f || !isfinite (len))
    {
        /* Seen end-on: any horizontal side will do. */
        side[0] = 1.0f;
        side[1] = 0.0f;
        side[2] = 0.0f;
        len = 1.0f;
    }
    for (k = 0; k < 3; k++)
        side[k] *= width * 0.5f / len;

    rlColor4ub (color_a[0], color_a[1], color_a[2], color_a[3]);
    rlVertex3f (a[0] - side[0], a[1] - side[1], a[2] - side[2]);
    rlVertex3f (a[0] + side[0], a[1] + side[1], a[2] + side[2]);
    rlColor4ub (color_b[0], color_b[1], color_b[2], color_b[3]);
    rlVertex3f (b[0] + side[0], b[1] + side[1], b[2] + side[2]);

    rlColor4ub (color_a[0], color_a[1], color_a[2], color_a[3]);
    rlVertex3f (a[0] - side[0], a[1] - side[1], a[2] - side[2]);
    rlColor4ub (color_b[0], color_b[1], color_b[2], color_b[3]);
    rlVertex3f (b[0] + side[0], b[1] + side[1], b[2] + side[2]);
    rlVertex3f (b[0] - side[0], b[1] - side[1], b[2] - side[2]);
}

void
lrg_weather_emit_sprite (const gfloat *centre,
                         gfloat        size,
                         const gfloat *right,
                         const gfloat *up,
                         const guint8 *color)
{
    gfloat h = size * 0.5f;
    gfloat q[4][3];
    guint  k;

    for (k = 0; k < 3; k++)
    {
        q[0][k] = centre[k] - right[k] * h - up[k] * h;
        q[1][k] = centre[k] + right[k] * h - up[k] * h;
        q[2][k] = centre[k] + right[k] * h + up[k] * h;
        q[3][k] = centre[k] - right[k] * h + up[k] * h;
    }

    rlColor4ub (color[0], color[1], color[2], color[3]);
    rlVertex3f (q[0][0], q[0][1], q[0][2]);
    rlVertex3f (q[1][0], q[1][1], q[1][2]);
    rlVertex3f (q[2][0], q[2][1], q[2][2]);
    rlVertex3f (q[0][0], q[0][1], q[0][2]);
    rlVertex3f (q[2][0], q[2][1], q[2][2]);
    rlVertex3f (q[3][0], q[3][1], q[3][2]);
}
