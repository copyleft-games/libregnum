/* lrg-rain.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rain weather effect implementation.
 *
 * Two simulations share one drop array. In screen space drops fall
 * through a pixel area and respawn at its top with a chance given by
 * the intensity (the original 2D behaviour). In world space every drop
 * lives at a world position inside a box centred on the camera: it
 * falls, drifts with the wind and is folded back into the box
 * (lrg_weather_wrap()) whenever it leaves, so moving the camera never
 * empties the volume and nearby drops show real parallax. The first
 * drop_count x intensity drops are drawn, as camera-facing streaks
 * along their velocity that fade toward the tail. Splashes are a small
 * pool of expanding rings (world) or spreading ticks (screen).
 */

#include "config.h"

#include <math.h>
#include <raylib.h>
#include <rlgl.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-rain.h"
#include "lrg-weather-effect-private.h"
#include "../lrg-log.h"

/* Live splashes at once and how long each lasts, seconds. */
#define RAIN_MAX_SPLASHES   (128)
#define RAIN_SPLASH_LIFE    (0.3f)

/* Screen-space defaults (pixels), the original 2D values. */
#define RAIN_SCREEN_SPEED      (500.0f)
#define RAIN_SCREEN_VARIATION  (100.0f)
#define RAIN_SCREEN_LENGTH     (15.0f)
#define RAIN_SCREEN_THICKNESS  (1.0f)
#define RAIN_SCREEN_SPLASH_Y   (600.0f)

/* World-space defaults (world units; metres: terminal velocity of a
 * rain drop, about one 60 Hz frame of motion blur, a hair-thin streak). */
#define RAIN_WORLD_SPEED       (9.0f)
#define RAIN_WORLD_VARIATION   (1.5f)
#define RAIN_WORLD_LENGTH      (0.5f)
#define RAIN_WORLD_THICKNESS   (0.012f)
#define RAIN_WORLD_SPLASH_Y    (0.0f)

typedef struct
{
    gfloat x;
    gfloat y;
    gfloat z;          /* world space only */
    gfloat speed;
    gfloat length;
    gboolean active;   /* screen space only; world drops always exist */
} RainDrop;

typedef struct
{
    gfloat x;
    gfloat y;
    gfloat z;
    gfloat age;
} RainSplash;

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
    RainDrop   *drops;
    guint       allocated_drops;
    RainSplash  splashes[RAIN_MAX_SPLASHES];
    guint       n_splashes;
    GRand      *rng;
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

static gboolean
is_world (LrgRain *self)
{
    return lrg_weather_effect_get_space (LRG_WEATHER_EFFECT (self)) == LRG_WEATHER_SPACE_WORLD;
}

static gfloat
rand_unit (LrgRain *self)
{
    return (gfloat)g_rand_double (self->rng);
}

/* world_visible:
 * World drops drawn now: the intensity's share of the array, so a fade
 * thins the rain instead of stopping it. */
static guint
world_visible (LrgRain *self)
{
    gfloat intensity = lrg_weather_effect_get_intensity (LRG_WEATHER_EFFECT (self));

    return (guint)((gfloat)self->allocated_drops * CLAMP (intensity, 0.0f, 1.0f) + 0.5f);
}

/* Helper to initialize a drop: screen drops anywhere in the area, world
 * drops anywhere in the camera's box. */
static void
init_drop (LrgRain  *self,
           RainDrop *drop)
{
    drop->speed = self->drop_speed + (rand_unit (self) - 0.5f) * 2.0f * self->drop_speed_variation;
    drop->speed = MAX (drop->speed, 0.0f);
    drop->length = self->drop_length * (0.8f + rand_unit (self) * 0.4f);
    drop->active = TRUE;

    if (is_world (self))
    {
        GrlVector3 cam;
        gfloat     radius;
        gfloat     height;

        lrg_weather_effect_get_camera_position (LRG_WEATHER_EFFECT (self), &cam);
        lrg_weather_effect_get_volume (LRG_WEATHER_EFFECT (self), &radius, &height);
        drop->x = cam.x + (rand_unit (self) * 2.0f - 1.0f) * radius;
        drop->y = cam.y + (rand_unit (self) - 0.5f) * height;
        drop->z = cam.z + (rand_unit (self) * 2.0f - 1.0f) * radius;
    }
    else
    {
        drop->x = self->area_x + rand_unit (self) * self->area_width;
        drop->y = self->area_y + rand_unit (self) * self->area_height;
        drop->z = 0.0f;
    }
}

/* (Re)builds the drop array, scattered over the area or volume. */
static void
init_all_drops (LrgRain *self)
{
    guint i;

    g_clear_pointer (&self->drops, g_free);
    self->allocated_drops = 0;
    self->n_splashes = 0;

    if (self->drop_count == 0)
        return;

    self->drops = g_new0 (RainDrop, self->drop_count);
    self->allocated_drops = self->drop_count;
    for (i = 0; i < self->drop_count; i++)
        init_drop (self, &self->drops[i]);
}

/* Reallocate drops array when the count changed */
static void
reallocate_drops (LrgRain *self)
{
    if (self->allocated_drops == self->drop_count && self->drops != NULL)
        return;
    init_all_drops (self);
}

static void
spawn_splash (LrgRain *self,
              gfloat   x,
              gfloat   y,
              gfloat   z)
{
    RainSplash *splash;

    /* A full pool drops the newest: splashes are decoration. */
    if (self->n_splashes >= RAIN_MAX_SPLASHES)
        return;
    splash = &self->splashes[self->n_splashes++];
    splash->x = x;
    splash->y = y;
    splash->z = z;
    splash->age = 0.0f;
}

static void
age_splashes (LrgRain *self,
              gfloat   delta_time)
{
    guint i = 0;

    while (i < self->n_splashes)
    {
        self->splashes[i].age += delta_time;
        if (self->splashes[i].age >= RAIN_SPLASH_LIFE)
            self->splashes[i] = self->splashes[--self->n_splashes];   /* swap-remove */
        else
            i++;
    }
}

/* apply_space_defaults:
 * Unit-dependent settings for the current space (see
 * lrg_weather_effect_set_space()). */
static void
apply_space_defaults (LrgRain *self)
{
    if (is_world (self))
    {
        self->drop_speed = RAIN_WORLD_SPEED;
        self->drop_speed_variation = RAIN_WORLD_VARIATION;
        self->drop_length = RAIN_WORLD_LENGTH;
        self->drop_thickness = RAIN_WORLD_THICKNESS;
        self->splash_enabled = FALSE;
        self->splash_height = RAIN_WORLD_SPLASH_Y;
    }
    else
    {
        self->drop_speed = RAIN_SCREEN_SPEED;
        self->drop_speed_variation = RAIN_SCREEN_VARIATION;
        self->drop_length = RAIN_SCREEN_LENGTH;
        self->drop_thickness = RAIN_SCREEN_THICKNESS;
        self->splash_enabled = TRUE;
        self->splash_height = RAIN_SCREEN_SPLASH_Y;
    }
}

/* Virtual method overrides */

static void
lrg_rain_activate (LrgWeatherEffect *effect)
{
    LrgRain *self = LRG_RAIN (effect);

    init_all_drops (self);

    /* Chain up */
    LRG_WEATHER_EFFECT_CLASS (lrg_rain_parent_class)->activate (effect);
}

static void
lrg_rain_deactivate (LrgWeatherEffect *effect)
{
    LrgRain *self = LRG_RAIN (effect);

    g_clear_pointer (&self->drops, g_free);
    self->allocated_drops = 0;
    self->n_splashes = 0;

    /* Chain up */
    LRG_WEATHER_EFFECT_CLASS (lrg_rain_parent_class)->deactivate (effect);
}

static void
lrg_rain_space_changed (LrgWeatherEffect *effect)
{
    LrgRain *self = LRG_RAIN (effect);

    apply_space_defaults (self);
    if (lrg_weather_effect_is_active (effect))
        init_all_drops (self);
}

/* update_screen:
 * The original 2D simulation: fall through the area, splash at the
 * splash height, respawn at the top with the intensity as the chance. */
static void
update_screen (LrgRain *self,
               gfloat   delta_time,
               gfloat   wind_x,
               gfloat   wind_y,
               gfloat   intensity)
{
    guint i;

    for (i = 0; i < self->allocated_drops; i++)
    {
        RainDrop *drop = &self->drops[i];

        if (!drop->active)
        {
            /* Respawn based on intensity */
            if (rand_unit (self) < intensity)
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
            spawn_splash (self, drop->x, self->splash_height, 0.0f);
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

/* update_world:
 * Fall and drift in world units; keep every drop inside the camera's
 * box. A drop that reaches the splash plane (when splashes are on and
 * the box reaches above it) splashes there and restarts at the top of
 * the box; one that leaves the box restarts on the opposite side, at a
 * fresh X/Z when it left through the bottom so columns do not repeat. */
static void
update_world (LrgRain *self,
              gfloat   delta_time,
              gfloat   wind_x,
              gfloat   wind_z)
{
    GrlVector3 cam;
    gfloat     radius;
    gfloat     height;
    gfloat     top;
    gboolean   ground;
    guint      visible;
    guint      i;

    lrg_weather_effect_get_camera_position (LRG_WEATHER_EFFECT (self), &cam);
    lrg_weather_effect_get_volume (LRG_WEATHER_EFFECT (self), &radius, &height);
    top = cam.y + height * 0.5f;
    ground = self->splash_enabled && top > self->splash_height;
    visible = world_visible (self);

    for (i = 0; i < self->allocated_drops; i++)
    {
        RainDrop *drop = &self->drops[i];
        gfloat    old_y;

        drop->y -= drop->speed * delta_time;
        drop->x += wind_x * delta_time;
        drop->z += wind_z * delta_time;

        if (ground && drop->y < self->splash_height)
        {
            gfloat x = lrg_weather_wrap (drop->x, cam.x, radius * 2.0f);
            gfloat z = lrg_weather_wrap (drop->z, cam.z, radius * 2.0f);

            /* Only drawn drops leave a visible splash. */
            if (i < visible)
                spawn_splash (self, x, self->splash_height, z);
            drop->x = cam.x + (rand_unit (self) * 2.0f - 1.0f) * radius;
            drop->z = cam.z + (rand_unit (self) * 2.0f - 1.0f) * radius;
            /* Strictly below the top: the box is half-open and a drop at
             * the very top would fold to the bottom. */
            drop->y = top - (rand_unit (self) + 0.01f) * MAX (drop->speed * delta_time, 1e-3f);
            continue;
        }

        old_y = drop->y;
        drop->y = lrg_weather_wrap (drop->y, cam.y, height);
        if (drop->y > old_y + height * 0.5f)
        {
            drop->x = cam.x + (rand_unit (self) * 2.0f - 1.0f) * radius;
            drop->z = cam.z + (rand_unit (self) * 2.0f - 1.0f) * radius;
        }
        drop->x = lrg_weather_wrap (drop->x, cam.x, radius * 2.0f);
        drop->z = lrg_weather_wrap (drop->z, cam.z, radius * 2.0f);
    }
}

static void
lrg_rain_update (LrgWeatherEffect *effect,
                 gfloat            delta_time)
{
    LrgRain *self = LRG_RAIN (effect);
    gfloat wind_x, wind_y;
    gfloat intensity;

    /* Chain up first for intensity transition */
    LRG_WEATHER_EFFECT_CLASS (lrg_rain_parent_class)->update (effect, delta_time);

    if (!lrg_weather_effect_is_active (effect))
        return;
    if (!isfinite (delta_time) || delta_time < 0.0f)
        delta_time = 0.0f;

    lrg_weather_effect_get_wind (effect, &wind_x, &wind_y);
    intensity = lrg_weather_effect_get_intensity (effect);

    age_splashes (self, delta_time);
    if (is_world (self))
        update_world (self, delta_time, wind_x, wind_y);
    else
        update_screen (self, delta_time, wind_x, wind_y, intensity);
}

/* The 2D pass: a line per live drop, slanted by the wind, ending at the
 * drop; splashes as two ticks spreading from the impact. */
static void
lrg_rain_render (LrgWeatherEffect *effect)
{
    LrgRain *self = LRG_RAIN (effect);
    gfloat wind_x, wind_y;
    gfloat angle;
    Color  color;
    guint  i;

    if (!lrg_weather_effect_is_active (effect) || is_world (self) || !lrg_weather_can_draw ())
        return;

    lrg_weather_effect_get_wind (effect, &wind_x, &wind_y);

    /* Calculate drop angle based on wind */
    angle = atan2f (wind_x, MAX (self->drop_speed, 1.0f));
    color = (Color){ self->color_r, self->color_g, self->color_b, self->color_a };

    for (i = 0; i < self->allocated_drops; i++)
    {
        RainDrop *drop = &self->drops[i];
        Vector2   head;
        Vector2   tail;

        if (!drop->active)
            continue;
        head = (Vector2){ drop->x, drop->y };
        tail = (Vector2){ drop->x - sinf (angle) * drop->length,
                          drop->y - cosf (angle) * drop->length };
        DrawLineEx (tail, head, self->drop_thickness, color);
    }

    for (i = 0; i < self->n_splashes; i++)
    {
        RainSplash *splash = &self->splashes[i];
        gfloat      t = splash->age / RAIN_SPLASH_LIFE;
        gfloat      r = 2.0f + 6.0f * t;
        Color       c = color;

        c.a = (guint8)((gfloat)self->color_a * (1.0f - t));
        DrawLineEx ((Vector2){ splash->x, splash->y },
                    (Vector2){ splash->x - r, splash->y - r * 0.5f },
                    self->drop_thickness, c);
        DrawLineEx ((Vector2){ splash->x, splash->y },
                    (Vector2){ splash->x + r, splash->y - r * 0.5f },
                    self->drop_thickness, c);
    }
}

/* The 3D pass: streaks along each drop's velocity, solid at the head
 * and transparent at the tail; splashes as fading rings. */
static void
lrg_rain_render_world (LrgWeatherEffect *effect,
                       const GrlVector3 *camera_position)
{
    LrgRain *self = LRG_RAIN (effect);
    gfloat   eye[3];
    gfloat   wind_x, wind_y;
    gfloat   radius;
    gfloat   height;
    guint8   head_color[4];
    guint8   tail_color[4];
    guint    visible;
    guint    i;

    if (!lrg_weather_effect_is_active (effect) || !is_world (self) || !lrg_weather_can_draw ())
        return;
    visible = world_visible (self);
    if (visible == 0 && self->n_splashes == 0)
        return;

    eye[0] = camera_position->x;
    eye[1] = camera_position->y;
    eye[2] = camera_position->z;
    lrg_weather_effect_get_wind (effect, &wind_x, &wind_y);
    lrg_weather_effect_get_volume (effect, &radius, &height);
    head_color[0] = tail_color[0] = self->color_r;
    head_color[1] = tail_color[1] = self->color_g;
    head_color[2] = tail_color[2] = self->color_b;
    head_color[3] = self->color_a;
    tail_color[3] = 0;

    lrg_weather_begin_particles ();
    for (i = 0; i < visible; i++)
    {
        RainDrop *drop = &self->drops[i];
        gfloat    head[3];
        gfloat    tail[3];
        gfloat    vel[3];
        gfloat    len;
        guint     k;

        /* Wrapped again: the camera may have moved since the update. */
        head[0] = lrg_weather_wrap (drop->x, eye[0], radius * 2.0f);
        head[1] = lrg_weather_wrap (drop->y, eye[1], height);
        head[2] = lrg_weather_wrap (drop->z, eye[2], radius * 2.0f);
        if (self->splash_enabled && head[1] < self->splash_height)
            continue;

        vel[0] = wind_x;
        vel[1] = -drop->speed;
        vel[2] = wind_y;
        len = sqrtf (vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
        if (len < 1e-6f)
        {
            vel[0] = vel[2] = 0.0f;
            vel[1] = -1.0f;
            len = 1.0f;
        }
        for (k = 0; k < 3; k++)
            tail[k] = head[k] - vel[k] / len * drop->length;
        lrg_weather_emit_ribbon (tail, head, self->drop_thickness, eye,
                                 tail_color, head_color);
    }

    for (i = 0; i < self->n_splashes; i++)
    {
        RainSplash *splash = &self->splashes[i];
        gfloat      t = splash->age / RAIN_SPLASH_LIFE;
        gfloat      r = 0.03f + 0.12f * t;
        guint8      c[4];
        guint       seg;

        c[0] = self->color_r;
        c[1] = self->color_g;
        c[2] = self->color_b;
        c[3] = (guint8)((gfloat)self->color_a * (1.0f - t));
        for (seg = 0; seg < 6; seg++)
        {
            gfloat a0 = (gfloat)seg * (gfloat)G_PI / 3.0f;
            gfloat a1 = (gfloat)(seg + 1) * (gfloat)G_PI / 3.0f;
            gfloat p0[3] = { splash->x + cosf (a0) * r, splash->y + 0.01f, splash->z + sinf (a0) * r };
            gfloat p1[3] = { splash->x + cosf (a1) * r, splash->y + 0.01f, splash->z + sinf (a1) * r };

            lrg_weather_emit_ribbon (p0, p1, self->drop_thickness * 1.5f, eye, c, c);
        }
    }
    lrg_weather_end_particles ();
}

static void
lrg_rain_set_wind (LrgWeatherEffect *effect,
                   gfloat            wind_x,
                   gfloat            wind_y)
{
    /* Chain up; the streaks read the wind when drawn. */
    LRG_WEATHER_EFFECT_CLASS (lrg_rain_parent_class)->set_wind (effect, wind_x, wind_y);
}

/* GObject implementation */

static void
lrg_rain_finalize (GObject *object)
{
    LrgRain *self = LRG_RAIN (object);

    g_clear_pointer (&self->drops, g_free);
    g_clear_pointer (&self->rng, g_rand_free);

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
    effect_class->render_world = lrg_rain_render_world;
    effect_class->space_changed = lrg_rain_space_changed;
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
                            "Base drop fall speed (pixels or world units per second)",
                            0.0f, 10000.0f, RAIN_SCREEN_SPEED,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DROP_SPEED_VARIATION] =
        g_param_spec_float ("drop-speed-variation",
                            "Drop Speed Variation",
                            "Random speed variation",
                            0.0f, 1000.0f, RAIN_SCREEN_VARIATION,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DROP_LENGTH] =
        g_param_spec_float ("drop-length",
                            "Drop Length",
                            "Rain streak length (pixels or world units)",
                            0.0f, 100.0f, RAIN_SCREEN_LENGTH,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    properties[PROP_DROP_THICKNESS] =
        g_param_spec_float ("drop-thickness",
                            "Drop Thickness",
                            "Rain streak thickness (pixels or world units)",
                            0.0f, 10.0f, RAIN_SCREEN_THICKNESS,
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
                            "Screen Y or world height where drops splash",
                            -100000.0f, 100000.0f, RAIN_SCREEN_SPLASH_Y,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_rain_init (LrgRain *self)
{
    self->drop_count = 1000;
    self->splash_enabled = TRUE;
    self->rng = g_rand_new_with_seed (0x7a1u);
    apply_space_defaults (self);

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
    self->n_splashes = 0;
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

guint
lrg_rain_get_visible_count (LrgRain *self)
{
    guint count = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_RAIN (self), 0);

    if (!lrg_weather_effect_is_active (LRG_WEATHER_EFFECT (self)))
        return 0;
    if (is_world (self))
        return world_visible (self);
    for (i = 0; i < self->allocated_drops; i++)
        if (self->drops[i].active)
            count++;
    return count;
}

gboolean
lrg_rain_get_drop (LrgRain    *self,
                   guint       index,
                   GrlVector3 *out_position)
{
    RainDrop *drop;

    g_return_val_if_fail (LRG_IS_RAIN (self), FALSE);
    g_return_val_if_fail (out_position != NULL, FALSE);

    if (index >= self->allocated_drops)
        return FALSE;
    drop = &self->drops[index];

    if (is_world (self))
    {
        GrlVector3 cam;
        gfloat     radius;
        gfloat     height;

        /* Exactly what render_world() draws for this drop. */
        if (index >= world_visible (self))
            return FALSE;
        lrg_weather_effect_get_camera_position (LRG_WEATHER_EFFECT (self), &cam);
        lrg_weather_effect_get_volume (LRG_WEATHER_EFFECT (self), &radius, &height);
        out_position->x = lrg_weather_wrap (drop->x, cam.x, radius * 2.0f);
        out_position->y = lrg_weather_wrap (drop->y, cam.y, height);
        out_position->z = lrg_weather_wrap (drop->z, cam.z, radius * 2.0f);
        return TRUE;
    }

    if (!drop->active)
        return FALSE;
    out_position->x = drop->x;
    out_position->y = drop->y;
    out_position->z = 0.0f;
    return TRUE;
}

guint
lrg_rain_get_splash_count (LrgRain *self)
{
    g_return_val_if_fail (LRG_IS_RAIN (self), 0);
    return self->n_splashes;
}
