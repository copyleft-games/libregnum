/* lrg-snow.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Snow weather effect implementation.
 *
 * Like LrgRain: screen-space flakes drift through a pixel area and
 * respawn at its top by intensity; world-space flakes keep a world
 * position inside a box centred on the camera, fall slowly, sway on two
 * horizontal axes with their own phase, ride the wind and wrap around
 * the box. World flakes are drawn as camera-facing squares, the first
 * flake_count x intensity of them.
 */

#include "config.h"

#include <math.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-snow.h"
#include "lrg-weather-effect-private.h"
#include "../lrg-log.h"

/* Screen-space defaults (pixels), the original 2D values. */
#define SNOW_SCREEN_SPEED       (100.0f)
#define SNOW_SCREEN_SIZE        (4.0f)
#define SNOW_SCREEN_SIZE_VAR    (2.0f)
#define SNOW_SCREEN_SWAY        (30.0f)
#define SNOW_SCREEN_SWAY_SPEED  (2.0f)

/* World-space defaults (world units; metres: a flake falls about a
 * metre per second and is a few centimetres across). */
#define SNOW_WORLD_SPEED        (1.2f)
#define SNOW_WORLD_SIZE         (0.05f)
#define SNOW_WORLD_SIZE_VAR     (0.02f)
#define SNOW_WORLD_SWAY         (0.35f)
#define SNOW_WORLD_SWAY_SPEED   (1.5f)

typedef struct
{
    gfloat x;
    gfloat y;
    gfloat z;            /* world space only */
    gfloat speed;
    gfloat size;
    gfloat sway_offset;
    gboolean active;     /* screen space only */
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
    GRand     *rng;
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

static gboolean
is_world (LrgSnow *self)
{
    return lrg_weather_effect_get_space (LRG_WEATHER_EFFECT (self)) == LRG_WEATHER_SPACE_WORLD;
}

static gfloat
rand_unit (LrgSnow *self)
{
    return (gfloat)g_rand_double (self->rng);
}

/* world_visible: the intensity's share of the flakes, drawn first. */
static guint
world_visible (LrgSnow *self)
{
    gfloat intensity = lrg_weather_effect_get_intensity (LRG_WEATHER_EFFECT (self));

    return (guint)((gfloat)self->allocated_flakes * CLAMP (intensity, 0.0f, 1.0f) + 0.5f);
}

static void
init_flake (LrgSnow   *self,
            SnowFlake *flake)
{
    flake->speed = self->flake_speed * (0.5f + rand_unit (self));
    flake->size = self->flake_size + (rand_unit (self) - 0.5f) * 2.0f * self->flake_size_variation;
    flake->size = MAX (flake->size, 0.0f);
    flake->sway_offset = rand_unit (self) * (gfloat)G_PI * 2.0f;
    flake->active = TRUE;

    if (is_world (self))
    {
        GrlVector3 cam;
        gfloat     radius;
        gfloat     height;

        lrg_weather_effect_get_camera_position (LRG_WEATHER_EFFECT (self), &cam);
        lrg_weather_effect_get_volume (LRG_WEATHER_EFFECT (self), &radius, &height);
        flake->x = cam.x + (rand_unit (self) * 2.0f - 1.0f) * radius;
        flake->y = cam.y + (rand_unit (self) - 0.5f) * height;
        flake->z = cam.z + (rand_unit (self) * 2.0f - 1.0f) * radius;
    }
    else
    {
        flake->x = self->area_x + rand_unit (self) * self->area_width;
        flake->y = self->area_y + rand_unit (self) * self->area_height;
        flake->z = 0.0f;
    }
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

/* Unit-dependent settings for the current space. */
static void
apply_space_defaults (LrgSnow *self)
{
    gboolean world = is_world (self);

    self->flake_speed = world ? SNOW_WORLD_SPEED : SNOW_SCREEN_SPEED;
    self->flake_size = world ? SNOW_WORLD_SIZE : SNOW_SCREEN_SIZE;
    self->flake_size_variation = world ? SNOW_WORLD_SIZE_VAR : SNOW_SCREEN_SIZE_VAR;
    self->sway_amount = world ? SNOW_WORLD_SWAY : SNOW_SCREEN_SWAY;
    self->sway_speed = world ? SNOW_WORLD_SWAY_SPEED : SNOW_SCREEN_SWAY_SPEED;
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
lrg_snow_space_changed (LrgWeatherEffect *effect)
{
    LrgSnow *self = LRG_SNOW (effect);

    apply_space_defaults (self);
    if (lrg_weather_effect_is_active (effect))
        reallocate_flakes (self);
}

/* The original 2D drift through the area. */
static void
update_screen (LrgSnow *self,
               gfloat   delta_time,
               gfloat   wind_x,
               gfloat   wind_y,
               gfloat   intensity)
{
    guint i;

    for (i = 0; i < self->allocated_flakes; i++)
    {
        SnowFlake *flake = &self->flakes[i];
        gfloat sway;

        if (!flake->active)
        {
            if (rand_unit (self) < intensity)
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

/* World drift: fall, sway on X and Z out of phase (a lazy spiral),
 * ride the wind, wrap around the camera's box; a flake leaving through
 * the bottom restarts at a fresh X/Z on top. */
static void
update_world (LrgSnow *self,
              gfloat   delta_time,
              gfloat   wind_x,
              gfloat   wind_z)
{
    GrlVector3 cam;
    gfloat     radius;
    gfloat     height;
    guint      visible;
    guint      i;

    lrg_weather_effect_get_camera_position (LRG_WEATHER_EFFECT (self), &cam);
    lrg_weather_effect_get_volume (LRG_WEATHER_EFFECT (self), &radius, &height);
    visible = world_visible (self);

    for (i = 0; i < self->allocated_flakes; i++)
    {
        SnowFlake *flake = &self->flakes[i];
        gfloat     phase = self->time * self->sway_speed + flake->sway_offset;
        gfloat     old_y;

        flake->x += (wind_x + sinf (phase) * self->sway_amount) * delta_time;
        flake->z += (wind_z + cosf (phase * 0.7f + flake->sway_offset) * self->sway_amount) * delta_time;
        flake->y -= flake->speed * delta_time;

        old_y = flake->y;
        flake->y = lrg_weather_wrap (flake->y, cam.y, height);
        if (flake->y > old_y + height * 0.5f)
        {
            if (self->accumulation_enabled && i < visible)
                self->accumulation += flake->size * 0.001f;
            flake->x = cam.x + (rand_unit (self) * 2.0f - 1.0f) * radius;
            flake->z = cam.z + (rand_unit (self) * 2.0f - 1.0f) * radius;
        }
        flake->x = lrg_weather_wrap (flake->x, cam.x, radius * 2.0f);
        flake->z = lrg_weather_wrap (flake->z, cam.z, radius * 2.0f);
    }
}

static void
lrg_snow_update (LrgWeatherEffect *effect,
                 gfloat            delta_time)
{
    LrgSnow *self = LRG_SNOW (effect);
    gfloat wind_x, wind_y;
    gfloat intensity;

    LRG_WEATHER_EFFECT_CLASS (lrg_snow_parent_class)->update (effect, delta_time);

    if (!lrg_weather_effect_is_active (effect))
        return;
    if (!isfinite (delta_time) || delta_time < 0.0f)
        delta_time = 0.0f;

    lrg_weather_effect_get_wind (effect, &wind_x, &wind_y);
    intensity = lrg_weather_effect_get_intensity (effect);
    self->time += delta_time;

    if (is_world (self))
        update_world (self, delta_time, wind_x, wind_y);
    else
        update_screen (self, delta_time, wind_x, wind_y, intensity);
}

/* The 2D pass: a dot per live flake (flake size is its diameter) and
 * the accumulated snow as a band along the bottom of the area. */
static void
lrg_snow_render (LrgWeatherEffect *effect)
{
    LrgSnow *self = LRG_SNOW (effect);
    Color    color;
    guint    i;

    if (!lrg_weather_effect_is_active (effect) || is_world (self) || !lrg_weather_can_draw ())
        return;

    color = (Color){ self->color_r, self->color_g, self->color_b, self->color_a };
    for (i = 0; i < self->allocated_flakes; i++)
    {
        SnowFlake *flake = &self->flakes[i];

        if (flake->active)
            DrawCircleV ((Vector2){ flake->x, flake->y }, MAX (flake->size * 0.5f, 0.5f), color);
    }

    if (self->accumulation_enabled && self->accumulation > 0.0f)
    {
        gfloat band = MIN (self->accumulation, self->area_height);

        DrawRectangleV ((Vector2){ self->area_x, self->area_y + self->area_height - band },
                        (Vector2){ self->area_width, band }, color);
    }
}

/* The 3D pass: camera-facing squares. */
static void
lrg_snow_render_world (LrgWeatherEffect *effect,
                       const GrlVector3 *camera_position)
{
    LrgSnow *self = LRG_SNOW (effect);
    gfloat   right[3];
    gfloat   up[3];
    gfloat   radius;
    gfloat   height;
    guint8   color[4];
    guint    visible;
    guint    i;

    if (!lrg_weather_effect_is_active (effect) || !is_world (self) || !lrg_weather_can_draw ())
        return;
    visible = world_visible (self);
    if (visible == 0)
        return;

    lrg_weather_effect_get_volume (effect, &radius, &height);
    lrg_weather_camera_basis (right, up);
    color[0] = self->color_r;
    color[1] = self->color_g;
    color[2] = self->color_b;
    color[3] = self->color_a;

    lrg_weather_begin_particles ();
    for (i = 0; i < visible; i++)
    {
        SnowFlake *flake = &self->flakes[i];
        gfloat     centre[3];

        centre[0] = lrg_weather_wrap (flake->x, camera_position->x, radius * 2.0f);
        centre[1] = lrg_weather_wrap (flake->y, camera_position->y, height);
        centre[2] = lrg_weather_wrap (flake->z, camera_position->z, radius * 2.0f);
        lrg_weather_emit_sprite (centre, flake->size, right, up, color);
    }
    lrg_weather_end_particles ();
}

static void
lrg_snow_finalize (GObject *object)
{
    LrgSnow *self = LRG_SNOW (object);
    g_clear_pointer (&self->flakes, g_free);
    g_clear_pointer (&self->rng, g_rand_free);
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
    effect_class->render_world = lrg_snow_render_world;
    effect_class->space_changed = lrg_snow_space_changed;

    properties[PROP_FLAKE_COUNT] = g_param_spec_uint ("flake-count", "Flake Count", "Number of snowflakes", 0, 100000, 500, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_FLAKE_SPEED] = g_param_spec_float ("flake-speed", "Flake Speed", "Fall speed (pixels or world units per second)", 0, 1000, SNOW_SCREEN_SPEED, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_FLAKE_SIZE] = g_param_spec_float ("flake-size", "Flake Size", "Size (pixels or world units)", 0, 50, SNOW_SCREEN_SIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_FLAKE_SIZE_VARIATION] = g_param_spec_float ("flake-size-variation", "Size Variation", "Size variation", 0, 20, SNOW_SCREEN_SIZE_VAR, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SWAY_AMOUNT] = g_param_spec_float ("sway-amount", "Sway Amount", "Horizontal sway speed", 0, 200, SNOW_SCREEN_SWAY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SWAY_SPEED] = g_param_spec_float ("sway-speed", "Sway Speed", "Sway frequency", 0, 20, SNOW_SCREEN_SWAY_SPEED, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_ACCUMULATION_ENABLED] = g_param_spec_boolean ("accumulation-enabled", "Accumulation", "Enable accumulation", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_snow_init (LrgSnow *self)
{
    self->flake_count = 500;
    self->rng = g_rand_new_with_seed (0x5a0u);
    apply_space_defaults (self);
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

guint
lrg_snow_get_visible_count (LrgSnow *self)
{
    guint count = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_SNOW (self), 0);

    if (!lrg_weather_effect_is_active (LRG_WEATHER_EFFECT (self)))
        return 0;
    if (is_world (self))
        return world_visible (self);
    for (i = 0; i < self->allocated_flakes; i++)
        if (self->flakes[i].active)
            count++;
    return count;
}

gboolean
lrg_snow_get_flake (LrgSnow    *self,
                    guint       index,
                    GrlVector3 *out_position)
{
    SnowFlake *flake;

    g_return_val_if_fail (LRG_IS_SNOW (self), FALSE);
    g_return_val_if_fail (out_position != NULL, FALSE);

    if (index >= self->allocated_flakes)
        return FALSE;
    flake = &self->flakes[index];

    if (is_world (self))
    {
        GrlVector3 cam;
        gfloat     radius;
        gfloat     height;

        if (index >= world_visible (self))
            return FALSE;
        lrg_weather_effect_get_camera_position (LRG_WEATHER_EFFECT (self), &cam);
        lrg_weather_effect_get_volume (LRG_WEATHER_EFFECT (self), &radius, &height);
        out_position->x = lrg_weather_wrap (flake->x, cam.x, radius * 2.0f);
        out_position->y = lrg_weather_wrap (flake->y, cam.y, height);
        out_position->z = lrg_weather_wrap (flake->z, cam.z, radius * 2.0f);
        return TRUE;
    }

    if (!flake->active)
        return FALSE;
    out_position->x = flake->x;
    out_position->y = flake->y;
    out_position->z = 0.0f;
    return TRUE;
}
