/* lrg-snow.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Snow weather effect implementation.
 */

#include "lrg-snow.h"
#include "../lrg-log.h"
#include <math.h>
#include <stdlib.h>

typedef struct
{
    gfloat x;
    gfloat y;
    gfloat speed;
    gfloat size;
    gfloat sway_offset;
    gboolean active;
} SnowFlake;

struct _LrgSnow
{
    LrgWeatherEffect parent_instance;

    guint   flake_count;
    gfloat  flake_speed;
    gfloat  flake_size;
    gfloat  flake_size_variation;
    gfloat  sway_amount;
    gfloat  sway_speed;
    gboolean accumulation_enabled;
    gfloat   accumulation;

    guint8  color_r, color_g, color_b, color_a;
    gfloat  area_x, area_y, area_width, area_height;

    SnowFlake *flakes;
    guint      allocated_flakes;
    gfloat     time;
};

G_DEFINE_FINAL_TYPE (LrgSnow, lrg_snow, LRG_TYPE_WEATHER_EFFECT)

enum
{
    PROP_0,
    PROP_FLAKE_COUNT,
    PROP_FLAKE_SPEED,
    PROP_FLAKE_SIZE,
    PROP_FLAKE_SIZE_VARIATION,
    PROP_SWAY_AMOUNT,
    PROP_SWAY_SPEED,
    PROP_ACCUMULATION_ENABLED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
init_flake (LrgSnow   *self,
            SnowFlake *flake)
{
    flake->x = self->area_x + (gfloat)rand () / RAND_MAX * self->area_width;
    flake->y = self->area_y + (gfloat)rand () / RAND_MAX * self->area_height;
    flake->speed = self->flake_speed * (0.5f + (gfloat)rand () / RAND_MAX);
    flake->size = self->flake_size + ((gfloat)rand () / RAND_MAX - 0.5f) * 2.0f * self->flake_size_variation;
    flake->sway_offset = (gfloat)rand () / RAND_MAX * G_PI * 2.0f;
    flake->active = TRUE;
}

static void
reallocate_flakes (LrgSnow *self)
{
    guint i;

    g_clear_pointer (&self->flakes, g_free);

    if (self->flake_count > 0)
    {
        self->flakes = g_new0 (SnowFlake, self->flake_count);
        self->allocated_flakes = self->flake_count;

        for (i = 0; i < self->flake_count; i++)
            init_flake (self, &self->flakes[i]);
    }
    else
    {
        self->allocated_flakes = 0;
    }
}

static void
lrg_snow_activate (LrgWeatherEffect *effect)
{
    LrgSnow *self = LRG_SNOW (effect);
    reallocate_flakes (self);
    LRG_WEATHER_EFFECT_CLASS (lrg_snow_parent_class)->activate (effect);
}

static void
lrg_snow_deactivate (LrgWeatherEffect *effect)
{
    LrgSnow *self = LRG_SNOW (effect);
    g_clear_pointer (&self->flakes, g_free);
    self->allocated_flakes = 0;
    LRG_WEATHER_EFFECT_CLASS (lrg_snow_parent_class)->deactivate (effect);
}

static void
lrg_snow_update (LrgWeatherEffect *effect,
                 gfloat            delta_time)
{
    LrgSnow *self = LRG_SNOW (effect);
    gfloat wind_x, wind_y;
    gfloat intensity;
    guint i;

    LRG_WEATHER_EFFECT_CLASS (lrg_snow_parent_class)->update (effect, delta_time);

    if (!lrg_weather_effect_is_active (effect))
        return;

    lrg_weather_effect_get_wind (effect, &wind_x, &wind_y);
    intensity = lrg_weather_effect_get_intensity (effect);
    self->time += delta_time;

    for (i = 0; i < self->allocated_flakes; i++)
    {
        SnowFlake *flake = &self->flakes[i];
        gfloat sway;

        if (!flake->active)
        {
            if ((gfloat)rand () / RAND_MAX < intensity)
            {
                init_flake (self, flake);
                flake->y = self->area_y;
            }
            continue;
        }

        sway = sinf (self->time * self->sway_speed + flake->sway_offset) * self->sway_amount;
        flake->x += (sway + wind_x * 0.3f) * delta_time;
        flake->y += (flake->speed + wind_y * 0.2f) * delta_time;

        if (flake->y > self->area_y + self->area_height)
        {
            flake->active = FALSE;
            if (self->accumulation_enabled)
                self->accumulation += flake->size * 0.001f;
        }

        if (flake->x < self->area_x)
            flake->x += self->area_width;
        else if (flake->x > self->area_x + self->area_width)
            flake->x -= self->area_width;
    }
}

static void
lrg_snow_render (LrgWeatherEffect *effect)
{
    /* Rendering would use graylib circle/texture draws */
    (void)effect;
}

static void
lrg_snow_finalize (GObject *object)
{
    LrgSnow *self = LRG_SNOW (object);
    g_clear_pointer (&self->flakes, g_free);
    G_OBJECT_CLASS (lrg_snow_parent_class)->finalize (object);
}

static void
lrg_snow_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    LrgSnow *self = LRG_SNOW (object);
    switch (prop_id)
    {
    case PROP_FLAKE_COUNT: g_value_set_uint (value, self->flake_count); break;
    case PROP_FLAKE_SPEED: g_value_set_float (value, self->flake_speed); break;
    case PROP_FLAKE_SIZE: g_value_set_float (value, self->flake_size); break;
    case PROP_FLAKE_SIZE_VARIATION: g_value_set_float (value, self->flake_size_variation); break;
    case PROP_SWAY_AMOUNT: g_value_set_float (value, self->sway_amount); break;
    case PROP_SWAY_SPEED: g_value_set_float (value, self->sway_speed); break;
    case PROP_ACCUMULATION_ENABLED: g_value_set_boolean (value, self->accumulation_enabled); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_snow_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    LrgSnow *self = LRG_SNOW (object);
    switch (prop_id)
    {
    case PROP_FLAKE_COUNT: lrg_snow_set_flake_count (self, g_value_get_uint (value)); break;
    case PROP_FLAKE_SPEED: self->flake_speed = g_value_get_float (value); break;
    case PROP_FLAKE_SIZE: self->flake_size = g_value_get_float (value); break;
    case PROP_FLAKE_SIZE_VARIATION: self->flake_size_variation = g_value_get_float (value); break;
    case PROP_SWAY_AMOUNT: self->sway_amount = g_value_get_float (value); break;
    case PROP_SWAY_SPEED: self->sway_speed = g_value_get_float (value); break;
    case PROP_ACCUMULATION_ENABLED: self->accumulation_enabled = g_value_get_boolean (value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_snow_class_init (LrgSnowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgWeatherEffectClass *effect_class = LRG_WEATHER_EFFECT_CLASS (klass);

    object_class->finalize = lrg_snow_finalize;
    object_class->get_property = lrg_snow_get_property;
    object_class->set_property = lrg_snow_set_property;

    effect_class->activate = lrg_snow_activate;
    effect_class->deactivate = lrg_snow_deactivate;
    effect_class->update = lrg_snow_update;
    effect_class->render = lrg_snow_render;

    properties[PROP_FLAKE_COUNT] = g_param_spec_uint ("flake-count", "Flake Count", "Number of snowflakes", 0, 100000, 500, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_FLAKE_SPEED] = g_param_spec_float ("flake-speed", "Flake Speed", "Fall speed", 0, 1000, 100, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_FLAKE_SIZE] = g_param_spec_float ("flake-size", "Flake Size", "Size in pixels", 1, 50, 4, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_FLAKE_SIZE_VARIATION] = g_param_spec_float ("flake-size-variation", "Size Variation", "Size variation", 0, 20, 2, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SWAY_AMOUNT] = g_param_spec_float ("sway-amount", "Sway Amount", "Horizontal sway", 0, 200, 30, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SWAY_SPEED] = g_param_spec_float ("sway-speed", "Sway Speed", "Sway frequency", 0, 20, 2, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_ACCUMULATION_ENABLED] = g_param_spec_boolean ("accumulation-enabled", "Accumulation", "Enable accumulation", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_snow_init (LrgSnow *self)
{
    self->flake_count = 500;
    self->flake_speed = 100.0f;
    self->flake_size = 4.0f;
    self->flake_size_variation = 2.0f;
    self->sway_amount = 30.0f;
    self->sway_speed = 2.0f;
    self->accumulation_enabled = FALSE;
    self->accumulation = 0.0f;
    self->color_r = 255; self->color_g = 255; self->color_b = 255; self->color_a = 230;
    self->area_x = 0; self->area_y = 0; self->area_width = 1280; self->area_height = 720;
    self->flakes = NULL; self->allocated_flakes = 0; self->time = 0;
}

/* Public API */

LrgSnow * lrg_snow_new (void) { return g_object_new (LRG_TYPE_SNOW, NULL); }

guint lrg_snow_get_flake_count (LrgSnow *self) { g_return_val_if_fail (LRG_IS_SNOW (self), 0); return self->flake_count; }
void lrg_snow_set_flake_count (LrgSnow *self, guint count) { g_return_if_fail (LRG_IS_SNOW (self)); if (self->flake_count != count) { self->flake_count = count; if (lrg_weather_effect_is_active (LRG_WEATHER_EFFECT (self))) reallocate_flakes (self); g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLAKE_COUNT]); } }

gfloat lrg_snow_get_flake_speed (LrgSnow *self) { g_return_val_if_fail (LRG_IS_SNOW (self), 0); return self->flake_speed; }
void lrg_snow_set_flake_speed (LrgSnow *self, gfloat speed) { g_return_if_fail (LRG_IS_SNOW (self)); self->flake_speed = speed; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLAKE_SPEED]); }

gfloat lrg_snow_get_flake_size (LrgSnow *self) { g_return_val_if_fail (LRG_IS_SNOW (self), 0); return self->flake_size; }
void lrg_snow_set_flake_size (LrgSnow *self, gfloat size) { g_return_if_fail (LRG_IS_SNOW (self)); self->flake_size = size; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLAKE_SIZE]); }

gfloat lrg_snow_get_flake_size_variation (LrgSnow *self) { g_return_val_if_fail (LRG_IS_SNOW (self), 0); return self->flake_size_variation; }
void lrg_snow_set_flake_size_variation (LrgSnow *self, gfloat variation) { g_return_if_fail (LRG_IS_SNOW (self)); self->flake_size_variation = variation; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLAKE_SIZE_VARIATION]); }

gfloat lrg_snow_get_sway_amount (LrgSnow *self) { g_return_val_if_fail (LRG_IS_SNOW (self), 0); return self->sway_amount; }
void lrg_snow_set_sway_amount (LrgSnow *self, gfloat amount) { g_return_if_fail (LRG_IS_SNOW (self)); self->sway_amount = amount; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SWAY_AMOUNT]); }

gfloat lrg_snow_get_sway_speed (LrgSnow *self) { g_return_val_if_fail (LRG_IS_SNOW (self), 0); return self->sway_speed; }
void lrg_snow_set_sway_speed (LrgSnow *self, gfloat speed) { g_return_if_fail (LRG_IS_SNOW (self)); self->sway_speed = speed; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SWAY_SPEED]); }

gboolean lrg_snow_get_accumulation_enabled (LrgSnow *self) { g_return_val_if_fail (LRG_IS_SNOW (self), FALSE); return self->accumulation_enabled; }
void lrg_snow_set_accumulation_enabled (LrgSnow *self, gboolean enabled) { g_return_if_fail (LRG_IS_SNOW (self)); self->accumulation_enabled = enabled; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACCUMULATION_ENABLED]); }

gfloat lrg_snow_get_accumulation_height (LrgSnow *self) { g_return_val_if_fail (LRG_IS_SNOW (self), 0); return self->accumulation; }

void lrg_snow_get_color (LrgSnow *self, guint8 *r, guint8 *g, guint8 *b, guint8 *a) { g_return_if_fail (LRG_IS_SNOW (self)); if (r) *r = self->color_r; if (g) *g = self->color_g; if (b) *b = self->color_b; if (a) *a = self->color_a; }
void lrg_snow_set_color (LrgSnow *self, guint8 r, guint8 g, guint8 b, guint8 a) { g_return_if_fail (LRG_IS_SNOW (self)); self->color_r = r; self->color_g = g; self->color_b = b; self->color_a = a; }

void lrg_snow_set_area (LrgSnow *self, gfloat x, gfloat y, gfloat width, gfloat height) { g_return_if_fail (LRG_IS_SNOW (self)); self->area_x = x; self->area_y = y; self->area_width = width; self->area_height = height; }
