/* lrg-fog.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Fog weather effect implementation.
 */

#include "lrg-fog.h"
#include "../lrg-log.h"
#include <math.h>

struct _LrgFog
{
    LrgWeatherEffect parent_instance;

    LrgFogType fog_type;
    gfloat     density;
    gfloat     start_distance;
    gfloat     end_distance;
    gfloat     height_falloff;
    gfloat     base_height;

    guint8  color_r, color_g, color_b, color_a;

    gboolean animated;
    gfloat   scroll_speed_x;
    gfloat   scroll_speed_y;
    gfloat   scroll_offset_x;
    gfloat   scroll_offset_y;
};

G_DEFINE_FINAL_TYPE (LrgFog, lrg_fog, LRG_TYPE_WEATHER_EFFECT)

enum
{
    PROP_0,
    PROP_FOG_TYPE,
    PROP_DENSITY,
    PROP_START_DISTANCE,
    PROP_END_DISTANCE,
    PROP_HEIGHT_FALLOFF,
    PROP_BASE_HEIGHT,
    PROP_ANIMATED,
    PROP_SCROLL_SPEED_X,
    PROP_SCROLL_SPEED_Y,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_fog_update (LrgWeatherEffect *effect,
                gfloat            delta_time)
{
    LrgFog *self = LRG_FOG (effect);

    LRG_WEATHER_EFFECT_CLASS (lrg_fog_parent_class)->update (effect, delta_time);

    if (!lrg_weather_effect_is_active (effect))
        return;

    if (self->animated)
    {
        gfloat wind_x, wind_y;
        lrg_weather_effect_get_wind (effect, &wind_x, &wind_y);

        self->scroll_offset_x += (self->scroll_speed_x + wind_x * 0.1f) * delta_time;
        self->scroll_offset_y += (self->scroll_speed_y + wind_y * 0.1f) * delta_time;
    }
}

static void
lrg_fog_render (LrgWeatherEffect *effect)
{
    LrgFog *self = LRG_FOG (effect);
    gfloat intensity;

    if (!lrg_weather_effect_is_active (effect))
        return;

    intensity = lrg_weather_effect_get_intensity (effect);

    /*
     * Fog rendering typically done via post-process shader:
     *
     * Pass uniforms:
     * - fog_type: self->fog_type
     * - fog_density: self->density * intensity
     * - fog_color: (self->color_r, g, b, a)
     * - fog_start: self->start_distance
     * - fog_end: self->end_distance
     * - height_falloff: self->height_falloff
     * - base_height: self->base_height
     * - scroll_offset: (self->scroll_offset_x, self->scroll_offset_y)
     */

    (void) self;
    (void) intensity;
}

static void
lrg_fog_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    LrgFog *self = LRG_FOG (object);
    switch (prop_id)
    {
    case PROP_FOG_TYPE: g_value_set_enum (value, self->fog_type); break;
    case PROP_DENSITY: g_value_set_float (value, self->density); break;
    case PROP_START_DISTANCE: g_value_set_float (value, self->start_distance); break;
    case PROP_END_DISTANCE: g_value_set_float (value, self->end_distance); break;
    case PROP_HEIGHT_FALLOFF: g_value_set_float (value, self->height_falloff); break;
    case PROP_BASE_HEIGHT: g_value_set_float (value, self->base_height); break;
    case PROP_ANIMATED: g_value_set_boolean (value, self->animated); break;
    case PROP_SCROLL_SPEED_X: g_value_set_float (value, self->scroll_speed_x); break;
    case PROP_SCROLL_SPEED_Y: g_value_set_float (value, self->scroll_speed_y); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_fog_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    LrgFog *self = LRG_FOG (object);
    switch (prop_id)
    {
    case PROP_FOG_TYPE: self->fog_type = g_value_get_enum (value); break;
    case PROP_DENSITY: self->density = g_value_get_float (value); break;
    case PROP_START_DISTANCE: self->start_distance = g_value_get_float (value); break;
    case PROP_END_DISTANCE: self->end_distance = g_value_get_float (value); break;
    case PROP_HEIGHT_FALLOFF: self->height_falloff = g_value_get_float (value); break;
    case PROP_BASE_HEIGHT: self->base_height = g_value_get_float (value); break;
    case PROP_ANIMATED: self->animated = g_value_get_boolean (value); break;
    case PROP_SCROLL_SPEED_X: self->scroll_speed_x = g_value_get_float (value); break;
    case PROP_SCROLL_SPEED_Y: self->scroll_speed_y = g_value_get_float (value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_fog_class_init (LrgFogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgWeatherEffectClass *effect_class = LRG_WEATHER_EFFECT_CLASS (klass);

    object_class->get_property = lrg_fog_get_property;
    object_class->set_property = lrg_fog_set_property;

    effect_class->update = lrg_fog_update;
    effect_class->render = lrg_fog_render;

    properties[PROP_FOG_TYPE] = g_param_spec_enum ("fog-type", "Fog Type", "Type of fog", LRG_TYPE_FOG_TYPE, LRG_FOG_TYPE_UNIFORM, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_DENSITY] = g_param_spec_float ("density", "Density", "Fog density", 0, 1, 0.3f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_START_DISTANCE] = g_param_spec_float ("start-distance", "Start Distance", "Linear fog start", 0, 10000, 100, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_END_DISTANCE] = g_param_spec_float ("end-distance", "End Distance", "Linear fog end", 0, 10000, 1000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_HEIGHT_FALLOFF] = g_param_spec_float ("height-falloff", "Height Falloff", "Height fog falloff", 0, 100, 2, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_BASE_HEIGHT] = g_param_spec_float ("base-height", "Base Height", "Height fog base", -10000, 10000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_ANIMATED] = g_param_spec_boolean ("animated", "Animated", "Enable scrolling", TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SCROLL_SPEED_X] = g_param_spec_float ("scroll-speed-x", "Scroll X", "X scroll speed", -1000, 1000, 10, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SCROLL_SPEED_Y] = g_param_spec_float ("scroll-speed-y", "Scroll Y", "Y scroll speed", -1000, 1000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_fog_init (LrgFog *self)
{
    self->fog_type = LRG_FOG_TYPE_UNIFORM;
    self->density = 0.3f;
    self->start_distance = 100.0f;
    self->end_distance = 1000.0f;
    self->height_falloff = 2.0f;
    self->base_height = 0.0f;
    self->color_r = 200; self->color_g = 200; self->color_b = 210; self->color_a = 180;
    self->animated = TRUE;
    self->scroll_speed_x = 10.0f;
    self->scroll_speed_y = 0.0f;
    self->scroll_offset_x = 0.0f;
    self->scroll_offset_y = 0.0f;
}

/* Public API */

LrgFog * lrg_fog_new (void) { return g_object_new (LRG_TYPE_FOG, NULL); }

LrgFogType lrg_fog_get_fog_type (LrgFog *self) { g_return_val_if_fail (LRG_IS_FOG (self), LRG_FOG_TYPE_UNIFORM); return self->fog_type; }
void lrg_fog_set_fog_type (LrgFog *self, LrgFogType type) { g_return_if_fail (LRG_IS_FOG (self)); self->fog_type = type; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOG_TYPE]); }

gfloat lrg_fog_get_density (LrgFog *self) { g_return_val_if_fail (LRG_IS_FOG (self), 0); return self->density; }
void lrg_fog_set_density (LrgFog *self, gfloat density) { g_return_if_fail (LRG_IS_FOG (self)); self->density = CLAMP (density, 0, 1); g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DENSITY]); }

gfloat lrg_fog_get_start_distance (LrgFog *self) { g_return_val_if_fail (LRG_IS_FOG (self), 0); return self->start_distance; }
void lrg_fog_set_start_distance (LrgFog *self, gfloat distance) { g_return_if_fail (LRG_IS_FOG (self)); self->start_distance = distance; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START_DISTANCE]); }

gfloat lrg_fog_get_end_distance (LrgFog *self) { g_return_val_if_fail (LRG_IS_FOG (self), 0); return self->end_distance; }
void lrg_fog_set_end_distance (LrgFog *self, gfloat distance) { g_return_if_fail (LRG_IS_FOG (self)); self->end_distance = distance; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_END_DISTANCE]); }

gfloat lrg_fog_get_height_falloff (LrgFog *self) { g_return_val_if_fail (LRG_IS_FOG (self), 0); return self->height_falloff; }
void lrg_fog_set_height_falloff (LrgFog *self, gfloat falloff) { g_return_if_fail (LRG_IS_FOG (self)); self->height_falloff = falloff; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT_FALLOFF]); }

gfloat lrg_fog_get_base_height (LrgFog *self) { g_return_val_if_fail (LRG_IS_FOG (self), 0); return self->base_height; }
void lrg_fog_set_base_height (LrgFog *self, gfloat height) { g_return_if_fail (LRG_IS_FOG (self)); self->base_height = height; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BASE_HEIGHT]); }

void lrg_fog_get_color (LrgFog *self, guint8 *r, guint8 *g, guint8 *b, guint8 *a) { g_return_if_fail (LRG_IS_FOG (self)); if (r) *r = self->color_r; if (g) *g = self->color_g; if (b) *b = self->color_b; if (a) *a = self->color_a; }
void lrg_fog_set_color (LrgFog *self, guint8 r, guint8 g, guint8 b, guint8 a) { g_return_if_fail (LRG_IS_FOG (self)); self->color_r = r; self->color_g = g; self->color_b = b; self->color_a = a; }

gboolean lrg_fog_get_animated (LrgFog *self) { g_return_val_if_fail (LRG_IS_FOG (self), FALSE); return self->animated; }
void lrg_fog_set_animated (LrgFog *self, gboolean animated) { g_return_if_fail (LRG_IS_FOG (self)); self->animated = animated; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ANIMATED]); }

gfloat lrg_fog_get_scroll_speed_x (LrgFog *self) { g_return_val_if_fail (LRG_IS_FOG (self), 0); return self->scroll_speed_x; }
void lrg_fog_set_scroll_speed_x (LrgFog *self, gfloat speed) { g_return_if_fail (LRG_IS_FOG (self)); self->scroll_speed_x = speed; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLL_SPEED_X]); }

gfloat lrg_fog_get_scroll_speed_y (LrgFog *self) { g_return_val_if_fail (LRG_IS_FOG (self), 0); return self->scroll_speed_y; }
void lrg_fog_set_scroll_speed_y (LrgFog *self, gfloat speed) { g_return_if_fail (LRG_IS_FOG (self)); self->scroll_speed_y = speed; g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SCROLL_SPEED_Y]); }
