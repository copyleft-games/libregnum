/* lrg-rain.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rain weather effect implementation.
 */

#include "lrg-rain.h"
#include "../lrg-log.h"
#include <math.h>
#include <stdlib.h>

typedef struct
{
    gfloat x;
    gfloat y;
    gfloat speed;
    gfloat length;
    gboolean active;
} RainDrop;

struct _LrgRain
{
    LrgWeatherEffect parent_instance;

    /* Drop configuration */
    guint   drop_count;
    gfloat  drop_speed;
    gfloat  drop_speed_variation;
    gfloat  drop_length;
    gfloat  drop_thickness;

    /* Splash configuration */
    gboolean splash_enabled;
    gfloat   splash_height;

    /* Color (RGBA) */
    guint8  color_r;
    guint8  color_g;
    guint8  color_b;
    guint8  color_a;

    /* Area */
    gfloat  area_x;
    gfloat  area_y;
    gfloat  area_width;
    gfloat  area_height;

    /* Internal state */
    RainDrop *drops;
    guint     allocated_drops;
};

G_DEFINE_FINAL_TYPE (LrgRain, lrg_rain, LRG_TYPE_WEATHER_EFFECT)

enum
{
    PROP_0,
    PROP_DROP_COUNT,
    PROP_DROP_SPEED,
    PROP_DROP_SPEED_VARIATION,
    PROP_DROP_LENGTH,
    PROP_DROP_THICKNESS,
    PROP_SPLASH_ENABLED,
    PROP_SPLASH_HEIGHT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* Helper to initialize a drop */
static void
init_drop (LrgRain  *self,
           RainDrop *drop)
{
    drop->x = self->area_x + (gfloat)rand () / RAND_MAX * self->area_width;
    drop->y = self->area_y + (gfloat)rand () / RAND_MAX * self->area_height;
    drop->speed = self->drop_speed + ((gfloat)rand () / RAND_MAX - 0.5f) * 2.0f * self->drop_speed_variation;
    drop->length = self->drop_length * (0.8f + (gfloat)rand () / RAND_MAX * 0.4f);
    drop->active = TRUE;
}

/* Reallocate drops array */
static void
reallocate_drops (LrgRain *self)
{
    guint i;

    if (self->allocated_drops == self->drop_count)
        return;

    g_clear_pointer (&self->drops, g_free);

    if (self->drop_count > 0)
    {
        self->drops = g_new0 (RainDrop, self->drop_count);
        self->allocated_drops = self->drop_count;

        for (i = 0; i < self->drop_count; i++)
        {
            init_drop (self, &self->drops[i]);
            /* Scatter initial drops across the area */
            self->drops[i].y = self->area_y + (gfloat)rand () / RAND_MAX * self->area_height;
        }
    }
    else
    {
        self->allocated_drops = 0;
    }
}

/* Virtual method overrides */

static void
lrg_rain_activate (LrgWeatherEffect *effect)
{
    LrgRain *self = LRG_RAIN (effect);

    reallocate_drops (self);

    /* Chain up */
    LRG_WEATHER_EFFECT_CLASS (lrg_rain_parent_class)->activate (effect);
}

static void
lrg_rain_deactivate (LrgWeatherEffect *effect)
{
    LrgRain *self = LRG_RAIN (effect);

    g_clear_pointer (&self->drops, g_free);
    self->allocated_drops = 0;

    /* Chain up */
    LRG_WEATHER_EFFECT_CLASS (lrg_rain_parent_class)->deactivate (effect);
}

static void
lrg_rain_update (LrgWeatherEffect *effect,
                 gfloat            delta_time)
{
    LrgRain *self = LRG_RAIN (effect);
    gfloat wind_x, wind_y;
    gfloat intensity;
    guint i;

    /* Chain up first for intensity transition */
    LRG_WEATHER_EFFECT_CLASS (lrg_rain_parent_class)->update (effect, delta_time);

    if (!lrg_weather_effect_is_active (effect))
        return;

    lrg_weather_effect_get_wind (effect, &wind_x, &wind_y);
    intensity = lrg_weather_effect_get_intensity (effect);

    /* Update drops */
    for (i = 0; i < self->allocated_drops; i++)
    {
        RainDrop *drop = &self->drops[i];

        if (!drop->active)
        {
            /* Respawn based on intensity */
            if ((gfloat)rand () / RAND_MAX < intensity)
            {
                init_drop (self, drop);
                drop->y = self->area_y; /* Start from top */
            }
            continue;
        }

        /* Update position */
        drop->y += (drop->speed + wind_y * 0.5f) * delta_time;
        drop->x += wind_x * delta_time;

        /* Check if drop is below splash height or out of area */
        if (self->splash_enabled && drop->y >= self->splash_height)
        {
            drop->active = FALSE;
            /* TODO: Spawn splash particle */
        }
        else if (drop->y > self->area_y + self->area_height)
        {
            drop->active = FALSE;
        }

        /* Wrap horizontally */
        if (drop->x < self->area_x)
            drop->x += self->area_width;
        else if (drop->x > self->area_x + self->area_width)
            drop->x -= self->area_width;
    }
}

static void
lrg_rain_render (LrgWeatherEffect *effect)
{
    LrgRain *self = LRG_RAIN (effect);
    gfloat wind_x, wind_y;
    gfloat angle;
    guint i;

    if (!lrg_weather_effect_is_active (effect))
        return;

    lrg_weather_effect_get_wind (effect, &wind_x, &wind_y);

    /* Calculate drop angle based on wind */
    angle = atan2f (wind_x, self->drop_speed);

    /*
     * Rendering would use graylib here:
     *
     * for (i = 0; i < self->allocated_drops; i++)
     * {
     *     RainDrop *drop = &self->drops[i];
     *     if (!drop->active)
     *         continue;
     *
     *     gfloat end_x = drop->x + sinf (angle) * drop->length;
     *     gfloat end_y = drop->y + cosf (angle) * drop->length;
     *
     *     GrlColor color = { self->color_r, self->color_g, self->color_b, self->color_a };
     *     grl_draw_line_ex (drop->x, drop->y, end_x, end_y, self->drop_thickness, &color);
     * }
     */

    (void)angle;
    (void)i;
}

static void
lrg_rain_set_wind (LrgWeatherEffect *effect,
                   gfloat            wind_x,
                   gfloat            wind_y)
{
    /* Chain up */
    LRG_WEATHER_EFFECT_CLASS (lrg_rain_parent_class)->set_wind (effect, wind_x, wind_y);

    /* Rain could adjust drop angles here if needed */
}

/* GObject implementation */

static void
lrg_rain_finalize (GObject *object)
{
    LrgRain *self = LRG_RAIN (object);

    g_clear_pointer (&self->drops, g_free);

    G_OBJECT_CLASS (lrg_rain_parent_class)->finalize (object);
}

static void
lrg_rain_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
    LrgRain *self = LRG_RAIN (object);

    switch (prop_id)
    {
    case PROP_DROP_COUNT:
        g_value_set_uint (value, self->drop_count);
        break;
    case PROP_DROP_SPEED:
        g_value_set_float (value, self->drop_speed);
        break;
    case PROP_DROP_SPEED_VARIATION:
        g_value_set_float (value, self->drop_speed_variation);
        break;
    case PROP_DROP_LENGTH:
        g_value_set_float (value, self->drop_length);
        break;
    case PROP_DROP_THICKNESS:
        g_value_set_float (value, self->drop_thickness);
        break;
    case PROP_SPLASH_ENABLED:
        g_value_set_boolean (value, self->splash_enabled);
        break;
    case PROP_SPLASH_HEIGHT:
        g_value_set_float (value, self->splash_height);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_rain_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
    LrgRain *self = LRG_RAIN (object);

    switch (prop_id)
    {
    case PROP_DROP_COUNT:
        lrg_rain_set_drop_count (self, g_value_get_uint (value));
        break;
    case PROP_DROP_SPEED:
        self->drop_speed = g_value_get_float (value);
        break;
    case PROP_DROP_SPEED_VARIATION:
        self->drop_speed_variation = g_value_get_float (value);
        break;
    case PROP_DROP_LENGTH:
        self->drop_length = g_value_get_float (value);
        break;
    case PROP_DROP_THICKNESS:
        self->drop_thickness = g_value_get_float (value);
        break;
    case PROP_SPLASH_ENABLED:
        self->splash_enabled = g_value_get_boolean (value);
        break;
    case PROP_SPLASH_HEIGHT:
        self->splash_height = g_value_get_float (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_rain_class_init (LrgRainClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgWeatherEffectClass *effect_class = LRG_WEATHER_EFFECT_CLASS (klass);

    object_class->finalize = lrg_rain_finalize;
    object_class->get_property = lrg_rain_get_property;
    object_class->set_property = lrg_rain_set_property;

    effect_class->activate = lrg_rain_activate;
    effect_class->deactivate = lrg_rain_deactivate;
    effect_class->update = lrg_rain_update;
    effect_class->render = lrg_rain_render;
    effect_class->set_wind = lrg_rain_set_wind;

    properties[PROP_DROP_COUNT] =
        g_param_spec_uint ("drop-count",
                           "Drop Count",
                           "Maximum number of rain drops",
                           0, 100000, 1000,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DROP_SPEED] =
        g_param_spec_float ("drop-speed",
                            "Drop Speed",
                            "Base drop fall speed in pixels per second",
                            0.0f, 10000.0f, 500.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DROP_SPEED_VARIATION] =
        g_param_spec_float ("drop-speed-variation",
                            "Drop Speed Variation",
                            "Random speed variation",
                            0.0f, 1000.0f, 100.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DROP_LENGTH] =
        g_param_spec_float ("drop-length",
                            "Drop Length",
                            "Rain drop length in pixels",
                            1.0f, 100.0f, 15.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DROP_THICKNESS] =
        g_param_spec_float ("drop-thickness",
                            "Drop Thickness",
                            "Rain drop thickness in pixels",
                            0.5f, 10.0f, 1.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SPLASH_ENABLED] =
        g_param_spec_boolean ("splash-enabled",
                              "Splash Enabled",
                              "Whether splash effects are enabled",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_SPLASH_HEIGHT] =
        g_param_spec_float ("splash-height",
                            "Splash Height",
                            "Y position where splashes occur",
                            0.0f, 10000.0f, 600.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_rain_init (LrgRain *self)
{
    self->drop_count = 1000;
    self->drop_speed = 500.0f;
    self->drop_speed_variation = 100.0f;
    self->drop_length = 15.0f;
    self->drop_thickness = 1.0f;
    self->splash_enabled = TRUE;
    self->splash_height = 600.0f;

    /* Default rain color: light blue with transparency */
    self->color_r = 200;
    self->color_g = 200;
    self->color_b = 255;
    self->color_a = 128;

    /* Default area covers typical screen */
    self->area_x = 0.0f;
    self->area_y = 0.0f;
    self->area_width = 1280.0f;
    self->area_height = 720.0f;

    self->drops = NULL;
    self->allocated_drops = 0;
}

/* Public API */

LrgRain *
lrg_rain_new (void)
{
    return g_object_new (LRG_TYPE_RAIN, NULL);
}

guint
lrg_rain_get_drop_count (LrgRain *self)
{
    g_return_val_if_fail (LRG_IS_RAIN (self), 0);
    return self->drop_count;
}

void
lrg_rain_set_drop_count (LrgRain *self,
                         guint    count)
{
    g_return_if_fail (LRG_IS_RAIN (self));

    if (self->drop_count != count)
    {
        self->drop_count = count;
        if (lrg_weather_effect_is_active (LRG_WEATHER_EFFECT (self)))
            reallocate_drops (self);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DROP_COUNT]);
    }
}

gfloat
lrg_rain_get_drop_speed (LrgRain *self)
{
    g_return_val_if_fail (LRG_IS_RAIN (self), 0.0f);
    return self->drop_speed;
}

void
lrg_rain_set_drop_speed (LrgRain *self,
                         gfloat   speed)
{
    g_return_if_fail (LRG_IS_RAIN (self));
    self->drop_speed = speed;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DROP_SPEED]);
}

gfloat
lrg_rain_get_drop_speed_variation (LrgRain *self)
{
    g_return_val_if_fail (LRG_IS_RAIN (self), 0.0f);
    return self->drop_speed_variation;
}

void
lrg_rain_set_drop_speed_variation (LrgRain *self,
                                   gfloat   variation)
{
    g_return_if_fail (LRG_IS_RAIN (self));
    self->drop_speed_variation = variation;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DROP_SPEED_VARIATION]);
}

gfloat
lrg_rain_get_drop_length (LrgRain *self)
{
    g_return_val_if_fail (LRG_IS_RAIN (self), 0.0f);
    return self->drop_length;
}

void
lrg_rain_set_drop_length (LrgRain *self,
                          gfloat   length)
{
    g_return_if_fail (LRG_IS_RAIN (self));
    self->drop_length = length;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DROP_LENGTH]);
}

gfloat
lrg_rain_get_drop_thickness (LrgRain *self)
{
    g_return_val_if_fail (LRG_IS_RAIN (self), 0.0f);
    return self->drop_thickness;
}

void
lrg_rain_set_drop_thickness (LrgRain *self,
                             gfloat   thickness)
{
    g_return_if_fail (LRG_IS_RAIN (self));
    self->drop_thickness = thickness;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DROP_THICKNESS]);
}

gboolean
lrg_rain_get_splash_enabled (LrgRain *self)
{
    g_return_val_if_fail (LRG_IS_RAIN (self), FALSE);
    return self->splash_enabled;
}

void
lrg_rain_set_splash_enabled (LrgRain  *self,
                             gboolean  enabled)
{
    g_return_if_fail (LRG_IS_RAIN (self));
    self->splash_enabled = enabled;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPLASH_ENABLED]);
}

gfloat
lrg_rain_get_splash_height (LrgRain *self)
{
    g_return_val_if_fail (LRG_IS_RAIN (self), 0.0f);
    return self->splash_height;
}

void
lrg_rain_set_splash_height (LrgRain *self,
                            gfloat   height)
{
    g_return_if_fail (LRG_IS_RAIN (self));
    self->splash_height = height;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPLASH_HEIGHT]);
}

void
lrg_rain_get_color (LrgRain *self,
                    guint8  *r,
                    guint8  *g,
                    guint8  *b,
                    guint8  *a)
{
    g_return_if_fail (LRG_IS_RAIN (self));

    if (r) *r = self->color_r;
    if (g) *g = self->color_g;
    if (b) *b = self->color_b;
    if (a) *a = self->color_a;
}

void
lrg_rain_set_color (LrgRain *self,
                    guint8   r,
                    guint8   g,
                    guint8   b,
                    guint8   a)
{
    g_return_if_fail (LRG_IS_RAIN (self));

    self->color_r = r;
    self->color_g = g;
    self->color_b = b;
    self->color_a = a;
}

void
lrg_rain_set_area (LrgRain *self,
                   gfloat   x,
                   gfloat   y,
                   gfloat   width,
                   gfloat   height)
{
    g_return_if_fail (LRG_IS_RAIN (self));

    self->area_x = x;
    self->area_y = y;
    self->area_width = width;
    self->area_height = height;
}

void
lrg_rain_get_area (LrgRain *self,
                   gfloat  *x,
                   gfloat  *y,
                   gfloat  *width,
                   gfloat  *height)
{
    g_return_if_fail (LRG_IS_RAIN (self));

    if (x) *x = self->area_x;
    if (y) *y = self->area_y;
    if (width) *width = self->area_width;
    if (height) *height = self->area_height;
}
