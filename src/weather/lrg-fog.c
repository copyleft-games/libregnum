/* lrg-fog.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Fog weather effect implementation.
 *
 * Fog is two things here. For 3D scenes it is a set of distance-fog
 * parameters scaled by the effect's intensity: lrg_fog_apply() hands
 * them to an LrgLightSet whose shader (and lit billboards) blend toward
 * the fog colour per fragment, and lrg_fog_get_amount() evaluates the
 * same model on the CPU. As a fallback, and for 2D games, it is a
 * screen veil: a full-screen rectangle of the fog colour.
 */

#include "config.h"

#include <math.h>
#include <raylib.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-fog.h"
#include "lrg-weather-effect-private.h"
#include "../graphics/lrg-light-set-private.h"
#include "../lrg-log.h"

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

    gfloat   veil_opacity;
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
    PROP_VEIL_OPACITY,
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

/* draw_veil:
 * The screen veil: the fog colour over the whole @width x @height,
 * alpha = colour alpha x intensity x veil opacity. */
static void
draw_veil (LrgFog *self,
           gint    width,
           gint    height)
{
    gfloat intensity;
    gfloat alpha;

    if (!lrg_weather_effect_is_active (LRG_WEATHER_EFFECT (self)) || !lrg_weather_can_draw ())
        return;

    intensity = CLAMP (lrg_weather_effect_get_intensity (LRG_WEATHER_EFFECT (self)), 0.0f, 1.0f);
    alpha = (gfloat)self->color_a * intensity * self->veil_opacity;
    if (alpha < 0.5f || width <= 0 || height <= 0)
        return;

    DrawRectangle (0, 0, width, height,
                   (Color){ self->color_r, self->color_g, self->color_b,
                            (guint8)MIN (alpha + 0.5f, 255.0f) });
}

/* The 2D pass: the veil over the whole screen. */
static void
lrg_fog_render (LrgWeatherEffect *effect)
{
    if (!lrg_weather_can_draw ())
        return;
    draw_veil (LRG_FOG (effect), GetScreenWidth (), GetScreenHeight ());
}

/* The overlay pass of a 3D scene: the veil (0 opacity when the scene
 * fogs per fragment through lrg_fog_apply()). */
static void
lrg_fog_render_overlay (LrgWeatherEffect *effect,
                        gint              width,
                        gint              height)
{
    draw_veil (LRG_FOG (effect), width, height);
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
    case PROP_VEIL_OPACITY: g_value_set_float (value, self->veil_opacity); break;
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
    case PROP_VEIL_OPACITY: self->veil_opacity = g_value_get_float (value); break;
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
    effect_class->render_overlay = lrg_fog_render_overlay;

    properties[PROP_FOG_TYPE] = g_param_spec_enum ("fog-type", "Fog Type", "Type of fog", LRG_TYPE_FOG_TYPE, LRG_FOG_TYPE_UNIFORM, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_DENSITY] = g_param_spec_float ("density", "Density", "Fog density", 0, 1, 0.3f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_START_DISTANCE] = g_param_spec_float ("start-distance", "Start Distance", "Linear fog start", 0, 10000, 100, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_END_DISTANCE] = g_param_spec_float ("end-distance", "End Distance", "Linear fog end", 0, 10000, 1000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_HEIGHT_FALLOFF] = g_param_spec_float ("height-falloff", "Height Falloff", "Height fog falloff", 0, 100, 2, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_BASE_HEIGHT] = g_param_spec_float ("base-height", "Base Height", "Height fog base", -10000, 10000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_ANIMATED] = g_param_spec_boolean ("animated", "Animated", "Enable scrolling", TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SCROLL_SPEED_X] = g_param_spec_float ("scroll-speed-x", "Scroll X", "X scroll speed", -1000, 1000, 10, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SCROLL_SPEED_Y] = g_param_spec_float ("scroll-speed-y", "Scroll Y", "Y scroll speed", -1000, 1000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgFog:veil-opacity:
     *
     * Strength of the screen veil drawn by render() and
     * render_overlay(): its alpha is the colour alpha x intensity x
     * this. Set 0 when the scene fogs through lrg_fog_apply().
     */
    properties[PROP_VEIL_OPACITY] = g_param_spec_float ("veil-opacity", "Veil Opacity", "Screen veil strength", 0, 1, 0.5f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

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
    self->veil_opacity = 0.5f;
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

gfloat
lrg_fog_get_veil_opacity (LrgFog *self)
{
    g_return_val_if_fail (LRG_IS_FOG (self), 0.0f);
    return self->veil_opacity;
}

void
lrg_fog_set_veil_opacity (LrgFog *self,
                          gfloat  opacity)
{
    g_return_if_fail (LRG_IS_FOG (self));

    if (!isfinite (opacity))
        return;
    self->veil_opacity = CLAMP (opacity, 0.0f, 1.0f);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VEIL_OPACITY]);
}

void
lrg_fog_get_scroll_offset (LrgFog *self,
                           gfloat *x,
                           gfloat *y)
{
    g_return_if_fail (LRG_IS_FOG (self));

    if (x != NULL)
        *x = self->scroll_offset_x;
    if (y != NULL)
        *y = self->scroll_offset_y;
}

/* effective_alpha:
 * The cap on the fog amount right now: colour alpha x intensity, 0 when
 * inactive. */
static gfloat
effective_alpha (LrgFog *self)
{
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (self);

    if (!lrg_weather_effect_is_active (effect))
        return 0.0f;
    return (gfloat)self->color_a / 255.0f
           * CLAMP (lrg_weather_effect_get_intensity (effect), 0.0f, 1.0f);
}

gfloat
lrg_fog_get_amount (LrgFog *self,
                    gfloat  distance,
                    gfloat  height)
{
    gfloat cap;

    g_return_val_if_fail (LRG_IS_FOG (self), 0.0f);

    cap = effective_alpha (self);
    if (cap <= 0.0f)
        return 0.0f;
    return lrg_fog_amount (self->fog_type, self->density, self->start_distance,
                           self->end_distance, self->base_height, self->height_falloff,
                           distance, height) * cap;
}

gboolean
lrg_fog_apply (LrgFog      *self,
               LrgLightSet *lights)
{
    GrlColor color;
    gfloat   cap;

    g_return_val_if_fail (LRG_IS_FOG (self), FALSE);
    g_return_val_if_fail (LRG_IS_LIGHT_SET (lights), FALSE);

    /* The intensity fades the fog through the colour's alpha, the cap,
     * so every fog type fades the same way. */
    cap = effective_alpha (self);
    if (cap <= 0.0f)
    {
        lrg_light_set_clear_fog (lights);
        return FALSE;
    }

    color.r = self->color_r;
    color.g = self->color_g;
    color.b = self->color_b;
    color.a = (guint8)MIN (cap * 255.0f + 0.5f, 255.0f);
    lrg_light_set_set_fog (lights, self->fog_type, &color, self->density,
                           self->start_distance, self->end_distance);
    lrg_light_set_set_fog_height (lights, self->base_height, self->height_falloff);
    return TRUE;
}
