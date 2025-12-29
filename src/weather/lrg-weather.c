/* lrg-weather.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Weather state definition implementation.
 */

#include "lrg-weather.h"
#include "../lrg-log.h"

typedef struct
{
    gchar     *id;
    gchar     *name;
    gboolean   active;
    GPtrArray *effects;
    guint8     ambient_r, ambient_g, ambient_b;
    gfloat     ambient_brightness;
    gfloat     wind_x, wind_y;
} LrgWeatherPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgWeather, lrg_weather, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_ACTIVE,
    PROP_AMBIENT_BRIGHTNESS,
    PROP_WIND_X,
    PROP_WIND_Y,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_ACTIVATED,
    SIGNAL_DEACTIVATED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_weather_real_activate (LrgWeather *self)
{
    LrgWeatherPrivate *priv = lrg_weather_get_instance_private (self);
    guint i;

    if (priv->active)
        return;

    priv->active = TRUE;

    for (i = 0; i < priv->effects->len; i++)
    {
        LrgWeatherEffect *effect = g_ptr_array_index (priv->effects, i);
        lrg_weather_effect_set_wind (effect, priv->wind_x, priv->wind_y);
        lrg_weather_effect_activate (effect);
    }

    g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
}

static void
lrg_weather_real_deactivate (LrgWeather *self)
{
    LrgWeatherPrivate *priv = lrg_weather_get_instance_private (self);
    guint i;

    if (!priv->active)
        return;

    priv->active = FALSE;

    for (i = 0; i < priv->effects->len; i++)
    {
        LrgWeatherEffect *effect = g_ptr_array_index (priv->effects, i);
        lrg_weather_effect_deactivate (effect);
    }

    g_signal_emit (self, signals[SIGNAL_DEACTIVATED], 0);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
}

static void
lrg_weather_real_update (LrgWeather *self,
                         gfloat      delta_time)
{
    LrgWeatherPrivate *priv = lrg_weather_get_instance_private (self);
    guint i;

    if (!priv->active)
        return;

    for (i = 0; i < priv->effects->len; i++)
    {
        LrgWeatherEffect *effect = g_ptr_array_index (priv->effects, i);
        lrg_weather_effect_update (effect, delta_time);
    }
}

static void
lrg_weather_finalize (GObject *object)
{
    LrgWeather *self = LRG_WEATHER (object);
    LrgWeatherPrivate *priv = lrg_weather_get_instance_private (self);

    g_clear_pointer (&priv->id, g_free);
    g_clear_pointer (&priv->name, g_free);
    g_clear_pointer (&priv->effects, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_weather_parent_class)->finalize (object);
}

static void
lrg_weather_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    LrgWeather *self = LRG_WEATHER (object);
    LrgWeatherPrivate *priv = lrg_weather_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID: g_value_set_string (value, priv->id); break;
    case PROP_NAME: g_value_set_string (value, priv->name); break;
    case PROP_ACTIVE: g_value_set_boolean (value, priv->active); break;
    case PROP_AMBIENT_BRIGHTNESS: g_value_set_float (value, priv->ambient_brightness); break;
    case PROP_WIND_X: g_value_set_float (value, priv->wind_x); break;
    case PROP_WIND_Y: g_value_set_float (value, priv->wind_y); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_weather_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    LrgWeather *self = LRG_WEATHER (object);
    LrgWeatherPrivate *priv = lrg_weather_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_ID: g_clear_pointer (&priv->id, g_free); priv->id = g_value_dup_string (value); break;
    case PROP_NAME: g_clear_pointer (&priv->name, g_free); priv->name = g_value_dup_string (value); break;
    case PROP_AMBIENT_BRIGHTNESS: priv->ambient_brightness = g_value_get_float (value); break;
    case PROP_WIND_X: lrg_weather_set_wind (self, g_value_get_float (value), priv->wind_y); break;
    case PROP_WIND_Y: lrg_weather_set_wind (self, priv->wind_x, g_value_get_float (value)); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_weather_class_init (LrgWeatherClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_weather_finalize;
    object_class->get_property = lrg_weather_get_property;
    object_class->set_property = lrg_weather_set_property;

    klass->activate = lrg_weather_real_activate;
    klass->deactivate = lrg_weather_real_deactivate;
    klass->update = lrg_weather_real_update;

    properties[PROP_ID] = g_param_spec_string ("id", "ID", "Weather ID", NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
    properties[PROP_NAME] = g_param_spec_string ("name", "Name", "Weather name", NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
    properties[PROP_ACTIVE] = g_param_spec_boolean ("active", "Active", "Is active", FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    properties[PROP_AMBIENT_BRIGHTNESS] = g_param_spec_float ("ambient-brightness", "Ambient", "Brightness", 0, 2, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_WIND_X] = g_param_spec_float ("wind-x", "Wind X", "Wind X", -1000, 1000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_WIND_Y] = g_param_spec_float ("wind-y", "Wind Y", "Wind Y", -1000, 1000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[SIGNAL_ACTIVATED] = g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
    signals[SIGNAL_DEACTIVATED] = g_signal_new ("deactivated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_weather_init (LrgWeather *self)
{
    LrgWeatherPrivate *priv = lrg_weather_get_instance_private (self);

    priv->id = NULL;
    priv->name = NULL;
    priv->active = FALSE;
    priv->effects = g_ptr_array_new_with_free_func (g_object_unref);
    priv->ambient_r = 255; priv->ambient_g = 255; priv->ambient_b = 255;
    priv->ambient_brightness = 1.0f;
    priv->wind_x = 0.0f;
    priv->wind_y = 0.0f;
}

/* Public API */

LrgWeather *
lrg_weather_new (const gchar *id, const gchar *name)
{
    return g_object_new (LRG_TYPE_WEATHER, "id", id, "name", name, NULL);
}

const gchar * lrg_weather_get_id (LrgWeather *self) { LrgWeatherPrivate *priv; g_return_val_if_fail (LRG_IS_WEATHER (self), NULL); priv = lrg_weather_get_instance_private (self); return priv->id; }
const gchar * lrg_weather_get_name (LrgWeather *self) { LrgWeatherPrivate *priv; g_return_val_if_fail (LRG_IS_WEATHER (self), NULL); priv = lrg_weather_get_instance_private (self); return priv->name; }
gboolean lrg_weather_is_active (LrgWeather *self) { LrgWeatherPrivate *priv; g_return_val_if_fail (LRG_IS_WEATHER (self), FALSE); priv = lrg_weather_get_instance_private (self); return priv->active; }

void
lrg_weather_add_effect (LrgWeather *self, LrgWeatherEffect *effect)
{
    LrgWeatherPrivate *priv;
    g_return_if_fail (LRG_IS_WEATHER (self));
    g_return_if_fail (LRG_IS_WEATHER_EFFECT (effect));
    priv = lrg_weather_get_instance_private (self);
    g_ptr_array_add (priv->effects, g_object_ref (effect));
    if (priv->active)
    {
        lrg_weather_effect_set_wind (effect, priv->wind_x, priv->wind_y);
        lrg_weather_effect_activate (effect);
    }
}

gboolean
lrg_weather_remove_effect (LrgWeather *self, LrgWeatherEffect *effect)
{
    LrgWeatherPrivate *priv;
    g_return_val_if_fail (LRG_IS_WEATHER (self), FALSE);
    priv = lrg_weather_get_instance_private (self);
    if (priv->active)
        lrg_weather_effect_deactivate (effect);
    return g_ptr_array_remove (priv->effects, effect);
}

LrgWeatherEffect *
lrg_weather_get_effect (LrgWeather *self, const gchar *effect_id)
{
    LrgWeatherPrivate *priv;
    guint i;
    g_return_val_if_fail (LRG_IS_WEATHER (self), NULL);
    priv = lrg_weather_get_instance_private (self);
    for (i = 0; i < priv->effects->len; i++)
    {
        LrgWeatherEffect *effect = g_ptr_array_index (priv->effects, i);
        if (g_strcmp0 (lrg_weather_effect_get_id (effect), effect_id) == 0)
            return effect;
    }
    return NULL;
}

GList *
lrg_weather_get_effects (LrgWeather *self)
{
    LrgWeatherPrivate *priv;
    GList *list = NULL;
    guint i;
    g_return_val_if_fail (LRG_IS_WEATHER (self), NULL);
    priv = lrg_weather_get_instance_private (self);
    for (i = 0; i < priv->effects->len; i++)
        list = g_list_append (list, g_ptr_array_index (priv->effects, i));
    return list;
}

guint lrg_weather_get_effect_count (LrgWeather *self) { LrgWeatherPrivate *priv; g_return_val_if_fail (LRG_IS_WEATHER (self), 0); priv = lrg_weather_get_instance_private (self); return priv->effects->len; }

void lrg_weather_get_ambient_color (LrgWeather *self, guint8 *r, guint8 *g, guint8 *b) { LrgWeatherPrivate *priv; g_return_if_fail (LRG_IS_WEATHER (self)); priv = lrg_weather_get_instance_private (self); if (r) *r = priv->ambient_r; if (g) *g = priv->ambient_g; if (b) *b = priv->ambient_b; }
void lrg_weather_set_ambient_color (LrgWeather *self, guint8 r, guint8 g, guint8 b) { LrgWeatherPrivate *priv; g_return_if_fail (LRG_IS_WEATHER (self)); priv = lrg_weather_get_instance_private (self); priv->ambient_r = r; priv->ambient_g = g; priv->ambient_b = b; }

gfloat lrg_weather_get_ambient_brightness (LrgWeather *self) { LrgWeatherPrivate *priv; g_return_val_if_fail (LRG_IS_WEATHER (self), 1.0f); priv = lrg_weather_get_instance_private (self); return priv->ambient_brightness; }
void lrg_weather_set_ambient_brightness (LrgWeather *self, gfloat brightness) { LrgWeatherPrivate *priv; g_return_if_fail (LRG_IS_WEATHER (self)); priv = lrg_weather_get_instance_private (self); priv->ambient_brightness = CLAMP (brightness, 0.0f, 2.0f); g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AMBIENT_BRIGHTNESS]); }

void lrg_weather_get_wind (LrgWeather *self, gfloat *wind_x, gfloat *wind_y) { LrgWeatherPrivate *priv; g_return_if_fail (LRG_IS_WEATHER (self)); priv = lrg_weather_get_instance_private (self); if (wind_x) *wind_x = priv->wind_x; if (wind_y) *wind_y = priv->wind_y; }

void
lrg_weather_set_wind (LrgWeather *self, gfloat wind_x, gfloat wind_y)
{
    LrgWeatherPrivate *priv;
    guint i;
    g_return_if_fail (LRG_IS_WEATHER (self));
    priv = lrg_weather_get_instance_private (self);
    priv->wind_x = wind_x;
    priv->wind_y = wind_y;
    for (i = 0; i < priv->effects->len; i++)
    {
        LrgWeatherEffect *effect = g_ptr_array_index (priv->effects, i);
        lrg_weather_effect_set_wind (effect, wind_x, wind_y);
    }
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIND_X]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIND_Y]);
}

void lrg_weather_activate (LrgWeather *self) { g_return_if_fail (LRG_IS_WEATHER (self)); LRG_WEATHER_GET_CLASS (self)->activate (self); }
void lrg_weather_deactivate (LrgWeather *self) { g_return_if_fail (LRG_IS_WEATHER (self)); LRG_WEATHER_GET_CLASS (self)->deactivate (self); }
void lrg_weather_update (LrgWeather *self, gfloat delta_time) { g_return_if_fail (LRG_IS_WEATHER (self)); LRG_WEATHER_GET_CLASS (self)->update (self, delta_time); }

void
lrg_weather_render (LrgWeather *self)
{
    LrgWeatherPrivate *priv;
    guint i;
    g_return_if_fail (LRG_IS_WEATHER (self));
    priv = lrg_weather_get_instance_private (self);
    if (!priv->active)
        return;
    /* Sort by render layer, then render each effect */
    for (i = 0; i < priv->effects->len; i++)
    {
        LrgWeatherEffect *effect = g_ptr_array_index (priv->effects, i);
        lrg_weather_effect_render (effect);
    }
}
