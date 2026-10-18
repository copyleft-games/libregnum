/* test-weather-render.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the weather renderers: world-space rain and snow around a
 * camera, the fog model and its hand-off to LrgLightSet, the lightning
 * flash curve and bolt, the manager's 3D passes and fog crossfade, and
 * render-layer ordering. Simulation and fog maths are headless; the
 * drawing is checked by reading pixels back in a hidden window and
 * skipped without a display.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
    const gchar *display = g_getenv ("DISPLAY");
    const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

    if ((display == NULL || display[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0'))
        return FALSE;

    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (8, 8, "lrg-weather-render-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

#define SKIP_WITHOUT_GRAPHICS() \
    do { \
        if (!graphics_available) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

static GrlVector3
vec (gfloat x,
     gfloat y,
     gfloat z)
{
    GrlVector3 v;

    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

/* assert_in_volume:
 * @p lies in the box of half width @radius and height @height centred
 * on @cam (the half-open box lrg_weather_wrap() folds into). */
static void
assert_in_volume (const GrlVector3 *p,
                  const GrlVector3 *cam,
                  gfloat            radius,
                  gfloat            height)
{
    g_assert_cmpfloat (p->x, >=, cam->x - radius - 1e-3f);
    g_assert_cmpfloat (p->x, <=, cam->x + radius + 1e-3f);
    g_assert_cmpfloat (p->z, >=, cam->z - radius - 1e-3f);
    g_assert_cmpfloat (p->z, <=, cam->z + radius + 1e-3f);
    g_assert_cmpfloat (p->y, >=, cam->y - height * 0.5f - 1e-3f);
    g_assert_cmpfloat (p->y, <=, cam->y + height * 0.5f + 1e-3f);
}

/* ------------------------------------------------------------------------ */
/* Base class                                                               */
/* ------------------------------------------------------------------------ */

static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           gpointer    user_data)
{
    (*(guint *)user_data)++;
}

static void
test_effect_space_volume_camera (void)
{
    g_autoptr(LrgRain) rain = lrg_rain_new ();
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (rain);
    GrlVector3 cam;
    GrlVector3 here = vec (1, 2, 3);
    GrlVector3 bad = vec (NAN, 0, 0);
    gfloat radius;
    gfloat height;
    guint notified = 0;

    g_assert_cmpint (lrg_weather_effect_get_space (effect), ==, LRG_WEATHER_SPACE_SCREEN);
    g_signal_connect (rain, "notify::space", G_CALLBACK (on_notify), &notified);
    lrg_weather_effect_set_space (effect, LRG_WEATHER_SPACE_WORLD);
    lrg_weather_effect_set_space (effect, LRG_WEATHER_SPACE_WORLD);
    g_assert_cmpint (lrg_weather_effect_get_space (effect), ==, LRG_WEATHER_SPACE_WORLD);
    g_assert_cmpuint (notified, ==, 1);

    /* the property goes through the same path */
    g_object_set (rain, "space", LRG_WEATHER_SPACE_SCREEN, NULL);
    g_assert_cmpint (lrg_weather_effect_get_space (effect), ==, LRG_WEATHER_SPACE_SCREEN);
    g_assert_cmpuint (notified, ==, 2);

    lrg_weather_effect_get_volume (effect, &radius, &height);
    g_assert_cmpfloat (radius, ==, 15.0f);
    g_assert_cmpfloat (height, ==, 12.0f);
    lrg_weather_effect_set_volume (effect, 4.0f, 6.0f);
    lrg_weather_effect_set_volume (effect, -1.0f, NAN);   /* ignored */
    lrg_weather_effect_get_volume (effect, &radius, &height);
    g_assert_cmpfloat (radius, ==, 4.0f);
    g_assert_cmpfloat (height, ==, 6.0f);

    lrg_weather_effect_get_camera_position (effect, &cam);
    g_assert_cmpfloat (cam.x, ==, 0.0f);
    lrg_weather_effect_set_camera_position (effect, &here);
    lrg_weather_effect_set_camera_position (effect, &bad);   /* ignored */
    lrg_weather_effect_get_camera_position (effect, &cam);
    g_assert_cmpfloat (cam.x, ==, 1.0f);
    g_assert_cmpfloat (cam.y, ==, 2.0f);
    g_assert_cmpfloat (cam.z, ==, 3.0f);

    /* render_world remembers the camera even without a window */
    here = vec (-7, 8, 9);
    lrg_weather_effect_render_world (effect, &here);
    lrg_weather_effect_get_camera_position (effect, &cam);
    g_assert_cmpfloat (cam.x, ==, -7.0f);
    g_assert_cmpfloat (cam.z, ==, 9.0f);

    /* overlays with no size are no-ops */
    lrg_weather_effect_render_overlay (effect, 0, 10);
}

/* ------------------------------------------------------------------------ */
/* Rain                                                                     */
/* ------------------------------------------------------------------------ */

static void
test_rain_space_defaults (void)
{
    g_autoptr(LrgRain) rain = lrg_rain_new ();

    g_assert_cmpfloat (lrg_rain_get_drop_speed (rain), ==, 500.0f);
    g_assert_true (lrg_rain_get_splash_enabled (rain));

    lrg_weather_effect_set_space (LRG_WEATHER_EFFECT (rain), LRG_WEATHER_SPACE_WORLD);
    g_assert_cmpfloat (lrg_rain_get_drop_speed (rain), ==, 9.0f);
    g_assert_cmpfloat (lrg_rain_get_drop_length (rain), ==, 0.5f);
    g_assert_cmpfloat (lrg_rain_get_drop_thickness (rain), <, 0.1f);
    g_assert_false (lrg_rain_get_splash_enabled (rain));
    g_assert_cmpfloat (lrg_rain_get_splash_height (rain), ==, 0.0f);

    /* world-sized values pass property validation */
    g_object_set (rain, "drop-length", 0.25f, "drop-thickness", 0.01f, NULL);
    g_assert_cmpfloat (lrg_rain_get_drop_length (rain), ==, 0.25f);

    lrg_weather_effect_set_space (LRG_WEATHER_EFFECT (rain), LRG_WEATHER_SPACE_SCREEN);
    g_assert_cmpfloat (lrg_rain_get_drop_speed (rain), ==, 500.0f);
    g_assert_cmpfloat (lrg_rain_get_drop_length (rain), ==, 15.0f);
    g_assert_true (lrg_rain_get_splash_enabled (rain));
}

/* A world rain of 400 drops around @cam, active at full intensity. */
static LrgRain *
world_rain (const GrlVector3 *cam)
{
    LrgRain *rain = lrg_rain_new ();
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (rain);

    lrg_weather_effect_set_space (effect, LRG_WEATHER_SPACE_WORLD);
    lrg_rain_set_drop_count (rain, 400);
    lrg_weather_effect_set_camera_position (effect, cam);
    lrg_weather_effect_activate (effect);
    lrg_weather_effect_set_intensity (effect, 1.0f);
    return rain;
}

static void
test_rain_world_volume (void)
{
    GrlVector3 cam = vec (100, 50, -20);
    g_autoptr(LrgRain) rain = world_rain (&cam);
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (rain);
    GrlVector3 p;
    guint frame;
    guint i;

    g_assert_cmpuint (lrg_rain_get_visible_count (rain), ==, 400);
    for (frame = 0; frame < 120; frame++)
        lrg_weather_effect_update (effect, 1.0f / 30.0f);

    for (i = 0; i < 400; i++)
    {
        g_assert_true (lrg_rain_get_drop (rain, i, &p));
        assert_in_volume (&p, &cam, 15.0f, 12.0f);
    }
    g_assert_false (lrg_rain_get_drop (rain, 400, &p));

    /* a camera jump: every drop is still around the camera, now and
     * after the next update */
    cam = vec (-5000, 3, 7000);
    lrg_weather_effect_set_camera_position (effect, &cam);
    for (i = 0; i < 400; i++)
    {
        g_assert_true (lrg_rain_get_drop (rain, i, &p));
        assert_in_volume (&p, &cam, 15.0f, 12.0f);
    }
    lrg_weather_effect_update (effect, 1.0f / 30.0f);
    for (i = 0; i < 400; i++)
    {
        g_assert_true (lrg_rain_get_drop (rain, i, &p));
        assert_in_volume (&p, &cam, 15.0f, 12.0f);
    }

    /* the intensity thins the rain */
    lrg_weather_effect_set_intensity (effect, 0.25f);
    g_assert_cmpuint (lrg_rain_get_visible_count (rain), ==, 100);
    g_assert_true (lrg_rain_get_drop (rain, 99, &p));
    g_assert_false (lrg_rain_get_drop (rain, 100, &p));
    lrg_weather_effect_set_intensity (effect, 0.0f);
    g_assert_cmpuint (lrg_rain_get_visible_count (rain), ==, 0);

    lrg_weather_effect_deactivate (effect);
    g_assert_cmpuint (lrg_rain_get_visible_count (rain), ==, 0);
    g_assert_false (lrg_rain_get_drop (rain, 0, &p));
}

static void
test_rain_world_fall_and_wind (void)
{
    GrlVector3 cam = vec (0, 0, 0);
    g_autoptr(LrgRain) rain = world_rain (&cam);
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (rain);
    guint checked = 0;
    guint i;

    lrg_weather_effect_set_wind (effect, 3.0f, -2.0f);

    /* Over 0.1 s a drop falls 0.75 .. 1.05 (9 +- 1.5 units/s) and moves
     * (0.3, -0.2) with the wind; drops near a face may wrap, skip them. */
    for (i = 0; i < 400; i++)
    {
        GrlVector3 before;
        GrlVector3 after;

        g_assert_true (lrg_rain_get_drop (rain, i, &before));
        if (fabsf (before.x) > 14.0f || fabsf (before.z) > 14.0f || before.y < -4.5f)
            continue;
        lrg_weather_effect_update (effect, 0.1f);
        g_assert_true (lrg_rain_get_drop (rain, i, &after));
        g_assert_cmpfloat_with_epsilon (after.x - before.x, 0.3f, 1e-3f);
        g_assert_cmpfloat_with_epsilon (after.z - before.z, -0.2f, 1e-3f);
        g_assert_cmpfloat (before.y - after.y, >=, 0.75f - 1e-3f);
        g_assert_cmpfloat (before.y - after.y, <=, 1.05f + 1e-3f);
        if (++checked == 20)
            break;
    }
    g_assert_cmpuint (checked, ==, 20);
}

static void
test_rain_world_splash (void)
{
    GrlVector3 cam = vec (0, 10, 0);
    g_autoptr(LrgRain) rain = world_rain (&cam);
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (rain);
    GrlVector3 p;
    guint frame;
    guint i;

    /* ground two units below the camera, inside the 12-high box */
    lrg_rain_set_splash_enabled (rain, TRUE);
    lrg_rain_set_splash_height (rain, 8.0f);
    g_assert_cmpuint (lrg_rain_get_splash_count (rain), ==, 0);

    for (frame = 0; frame < 10; frame++)
        lrg_weather_effect_update (effect, 1.0f / 60.0f);
    g_assert_cmpuint (lrg_rain_get_splash_count (rain), >, 0);
    g_assert_cmpuint (lrg_rain_get_splash_count (rain), <=, 128);

    /* nothing stays below the ground */
    for (i = 0; i < 400; i++)
    {
        g_assert_true (lrg_rain_get_drop (rain, i, &p));
        g_assert_cmpfloat (p.y, >=, 8.0f);
    }

    /* splashes fade within 0.3 s once the rain stops */
    lrg_rain_set_splash_enabled (rain, FALSE);
    lrg_weather_effect_update (effect, 0.31f);
    g_assert_cmpuint (lrg_rain_get_splash_count (rain), ==, 0);
}

static void
test_rain_screen_splash (void)
{
    g_autoptr(LrgRain) rain = lrg_rain_new ();
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (rain);
    GrlVector3 p;
    guint frame;

    lrg_rain_set_area (rain, 0, 0, 200, 200);
    lrg_rain_set_splash_height (rain, 100.0f);
    lrg_rain_set_drop_count (rain, 200);
    lrg_weather_effect_activate (effect);
    lrg_weather_effect_set_intensity (effect, 1.0f);

    for (frame = 0; frame < 10; frame++)
        lrg_weather_effect_update (effect, 1.0f / 30.0f);
    g_assert_cmpuint (lrg_rain_get_splash_count (rain), >, 0);
    g_assert_cmpuint (lrg_rain_get_visible_count (rain), >, 0);

    /* screen drops report pixels with z = 0 */
    for (frame = 0; frame < 200; frame++)
    {
        if (lrg_rain_get_drop (rain, frame, &p))
        {
            g_assert_cmpfloat (p.z, ==, 0.0f);
            g_assert_cmpfloat (p.x, >=, 0.0f);
            g_assert_cmpfloat (p.x, <=, 200.0f);
            break;
        }
    }

    /* the 2D render is harmless headless */
    lrg_weather_effect_render (effect);
}

/* ------------------------------------------------------------------------ */
/* Snow                                                                     */
/* ------------------------------------------------------------------------ */

static void
test_snow_world (void)
{
    g_autoptr(LrgSnow) snow = lrg_snow_new ();
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (snow);
    GrlVector3 cam = vec (30, 5, 30);
    GrlVector3 p;
    gfloat before_x[300];
    gdouble mean_shift = 0.0;
    guint frame;
    guint i;

    lrg_weather_effect_set_space (effect, LRG_WEATHER_SPACE_WORLD);
    g_assert_cmpfloat (lrg_snow_get_flake_speed (snow), ==, 1.2f);
    g_assert_cmpfloat (lrg_snow_get_flake_size (snow), ==, 0.05f);
    lrg_snow_set_flake_count (snow, 300);
    lrg_weather_effect_set_volume (effect, 50.0f, 1000.0f);
    lrg_weather_effect_set_camera_position (effect, &cam);
    lrg_weather_effect_activate (effect);
    lrg_weather_effect_set_intensity (effect, 1.0f);
    g_assert_cmpuint (lrg_snow_get_visible_count (snow), ==, 300);

    /* wind carries the flakes: the mean X shift is ~ wind x time (sway
     * averages out). The box is too tall for a flake to leave through
     * the bottom in 1 s; side wraps are undone per flake. */
    for (i = 0; i < 300; i++)
    {
        g_assert_true (lrg_snow_get_flake (snow, i, &p));
        before_x[i] = p.x;
    }
    lrg_weather_effect_set_wind (effect, 2.0f, 0.0f);
    for (frame = 0; frame < 30; frame++)
        lrg_weather_effect_update (effect, 1.0f / 30.0f);
    for (i = 0; i < 300; i++)
    {
        gfloat d;

        g_assert_true (lrg_snow_get_flake (snow, i, &p));
        assert_in_volume (&p, &cam, 50.0f, 1000.0f);
        d = p.x - before_x[i];
        if (d > 50.0f)
            d -= 100.0f;
        else if (d < -50.0f)
            d += 100.0f;
        mean_shift += d;
    }
    mean_shift /= 300.0;
    g_assert_cmpfloat (mean_shift, >, 1.5);
    g_assert_cmpfloat (mean_shift, <, 2.5);

    lrg_weather_effect_set_intensity (effect, 0.5f);
    g_assert_cmpuint (lrg_snow_get_visible_count (snow), ==, 150);
    g_assert_false (lrg_snow_get_flake (snow, 150, &p));

    lrg_weather_effect_set_space (effect, LRG_WEATHER_SPACE_SCREEN);
    g_assert_cmpfloat (lrg_snow_get_flake_speed (snow), ==, 100.0f);
    g_assert_true (lrg_snow_get_flake (snow, 0, &p));
    g_assert_cmpfloat (p.z, ==, 0.0f);
}

/* ------------------------------------------------------------------------ */
/* Fog                                                                      */
/* ------------------------------------------------------------------------ */

static LrgFog *
active_fog (LrgFogType type)
{
    LrgFog *fog = lrg_fog_new ();

    lrg_fog_set_fog_type (fog, type);
    lrg_fog_set_color (fog, 100, 110, 120, 255);
    lrg_weather_effect_activate (LRG_WEATHER_EFFECT (fog));
    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (fog), 1.0f);
    return fog;
}

static void
test_fog_amount (void)
{
    g_autoptr(LrgFog) linear = active_fog (LRG_FOG_TYPE_LINEAR);
    g_autoptr(LrgFog) expo = active_fog (LRG_FOG_TYPE_EXPONENTIAL);
    g_autoptr(LrgFog) uniform = active_fog (LRG_FOG_TYPE_UNIFORM);
    g_autoptr(LrgFog) height = active_fog (LRG_FOG_TYPE_HEIGHT);

    lrg_fog_set_start_distance (linear, 10.0f);
    lrg_fog_set_end_distance (linear, 20.0f);
    g_assert_cmpfloat_with_epsilon (lrg_fog_get_amount (linear, 5.0f, 0.0f), 0.0f, 1e-5f);
    g_assert_cmpfloat_with_epsilon (lrg_fog_get_amount (linear, 15.0f, 0.0f), 0.5f, 1e-5f);
    g_assert_cmpfloat_with_epsilon (lrg_fog_get_amount (linear, 25.0f, 0.0f), 1.0f, 1e-5f);

    /* end <= start: a hard edge at the end */
    lrg_fog_set_start_distance (linear, 20.0f);
    g_assert_cmpfloat (lrg_fog_get_amount (linear, 19.0f, 0.0f), ==, 0.0f);
    g_assert_cmpfloat (lrg_fog_get_amount (linear, 20.0f, 0.0f), ==, 1.0f);

    lrg_fog_set_density (expo, 0.1f);
    g_assert_cmpfloat_with_epsilon (lrg_fog_get_amount (expo, 10.0f, 0.0f),
                                    1.0f - expf (-1.0f), 1e-5f);
    g_assert_cmpfloat (lrg_fog_get_amount (expo, 0.0f, 0.0f), ==, 0.0f);

    lrg_fog_set_density (uniform, 0.3f);
    g_assert_cmpfloat_with_epsilon (lrg_fog_get_amount (uniform, 0.0f, 0.0f), 0.3f, 1e-5f);
    g_assert_cmpfloat_with_epsilon (lrg_fog_get_amount (uniform, 1000.0f, 0.0f), 0.3f, 1e-5f);

    /* height fog: full below the base, thinned by e^-1 one unit above */
    lrg_fog_set_density (height, 0.1f);
    lrg_fog_set_base_height (height, 2.0f);
    lrg_fog_set_height_falloff (height, 1.0f);
    g_assert_cmpfloat_with_epsilon (lrg_fog_get_amount (height, 10.0f, 0.0f),
                                    1.0f - expf (-1.0f), 1e-5f);
    g_assert_cmpfloat_with_epsilon (lrg_fog_get_amount (height, 10.0f, 3.0f),
                                    (1.0f - expf (-1.0f)) * expf (-1.0f), 1e-5f);

    /* the colour alpha and the intensity cap it */
    lrg_fog_set_color (uniform, 0, 0, 0, 51);
    g_assert_cmpfloat_with_epsilon (lrg_fog_get_amount (uniform, 0.0f, 0.0f), 0.06f, 1e-5f);
    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (uniform), 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_fog_get_amount (uniform, 0.0f, 0.0f), 0.03f, 1e-5f);

    lrg_weather_effect_deactivate (LRG_WEATHER_EFFECT (uniform));
    g_assert_cmpfloat (lrg_fog_get_amount (uniform, 0.0f, 0.0f), ==, 0.0f);
}

static void
test_fog_veil_and_scroll (void)
{
    g_autoptr(LrgFog) fog = active_fog (LRG_FOG_TYPE_UNIFORM);
    gfloat x;
    gfloat y;

    g_assert_cmpfloat (lrg_fog_get_veil_opacity (fog), ==, 0.5f);
    lrg_fog_set_veil_opacity (fog, 2.0f);
    g_assert_cmpfloat (lrg_fog_get_veil_opacity (fog), ==, 1.0f);
    g_object_set (fog, "veil-opacity", 0.25f, NULL);
    g_assert_cmpfloat (lrg_fog_get_veil_opacity (fog), ==, 0.25f);

    lrg_fog_set_scroll_speed_x (fog, 4.0f);
    lrg_fog_set_scroll_speed_y (fog, -2.0f);
    lrg_weather_effect_update (LRG_WEATHER_EFFECT (fog), 0.5f);
    lrg_fog_get_scroll_offset (fog, &x, &y);
    g_assert_cmpfloat_with_epsilon (x, 2.0f, 1e-5f);
    g_assert_cmpfloat_with_epsilon (y, -1.0f, 1e-5f);

    /* headless passes are no-ops */
    lrg_weather_effect_render (LRG_WEATHER_EFFECT (fog));
    lrg_weather_effect_render_overlay (LRG_WEATHER_EFFECT (fog), 64, 64);
}

/* ------------------------------------------------------------------------ */
/* Light set fog                                                            */
/* ------------------------------------------------------------------------ */

static void
test_light_set_fog (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlColor   grey = { 128, 128, 128, 255 };
    GrlColor   out;
    GrlVector3 p;
    LrgFogType type;
    gfloat     density;
    gfloat     start;
    gfloat     end;

    g_assert_false (lrg_light_set_get_fog (lights, NULL, NULL, NULL, NULL, NULL));
    p = vec (10, 0, 0);
    g_assert_cmpfloat (lrg_light_set_sample_fog (lights, &p), ==, 0.0f);

    lrg_light_set_set_fog (lights, LRG_FOG_TYPE_LINEAR, &grey, 0.0f, 0.0f, 20.0f);
    g_assert_true (lrg_light_set_get_fog (lights, &type, &out, &density, &start, &end));
    g_assert_cmpint (type, ==, LRG_FOG_TYPE_LINEAR);
    g_assert_cmpuint (out.r, ==, 128);
    g_assert_cmpuint (out.a, ==, 255);
    g_assert_cmpfloat (end, ==, 20.0f);

    /* distances from the selected camera, in draw space (world minus
     * the origin): camera (1000, 5, 0) with origin (1000, 0, 0) is at
     * (0, 5, 0) */
    lrg_light_set_set_origin (lights, 1000.0, 0.0, 0.0);
    lrg_light_set_select (lights, 1000.0, 5.0, 0.0);
    p = vec (10, 5, 0);
    g_assert_cmpfloat_with_epsilon (lrg_light_set_sample_fog (lights, &p), 0.5f, 1e-5f);

    /* height fog: the base is a world height, moved by the origin */
    lrg_light_set_set_origin (lights, 0.0, 100.0, 0.0);
    lrg_light_set_select (lights, 0.0, 100.0, 0.0);
    lrg_light_set_set_fog (lights, LRG_FOG_TYPE_HEIGHT, &grey, 0.1f, 0.0f, 0.0f);
    lrg_light_set_set_fog_height (lights, 100.0f, 1.0f);
    p = vec (10, 0, 0);   /* world height 100: at the base */
    g_assert_cmpfloat_with_epsilon (lrg_light_set_sample_fog (lights, &p),
                                    1.0f - expf (-1.0f), 1e-4f);
    p = vec (0, 1, 0);    /* one above the base, one away */
    g_assert_cmpfloat_with_epsilon (lrg_light_set_sample_fog (lights, &p),
                                    (1.0f - expf (-0.1f)) * expf (-1.0f), 1e-4f);

    /* non-finite input is neutralised */
    lrg_light_set_set_fog (lights, LRG_FOG_TYPE_EXPONENTIAL, &grey, NAN, INFINITY, NAN);
    g_assert_true (lrg_light_set_get_fog (lights, NULL, NULL, &density, &start, NULL));
    g_assert_cmpfloat (density, ==, 0.0f);
    g_assert_cmpfloat (start, ==, 0.0f);

    lrg_light_set_clear_fog (lights);
    g_assert_false (lrg_light_set_get_fog (lights, NULL, NULL, NULL, NULL, NULL));
}

static void
test_fog_apply (void)
{
    g_autoptr(LrgFog)      fog = active_fog (LRG_FOG_TYPE_EXPONENTIAL);
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlColor   color;
    GrlVector3 p = vec (0, 0, 20);
    gfloat     density;

    lrg_fog_set_density (fog, 0.05f);
    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (fog), 0.5f);
    g_assert_true (lrg_fog_apply (fog, lights));
    g_assert_true (lrg_light_set_get_fog (lights, NULL, &color, &density, NULL, NULL));
    g_assert_cmpuint (color.r, ==, 100);
    g_assert_cmpuint (color.a, ==, 128);    /* 255 x 0.5 */
    g_assert_cmpfloat_with_epsilon (density, 0.05f, 1e-6f);

    /* the light set and the effect agree on the amount */
    lrg_light_set_select (lights, 0, 0, 0);
    g_assert_cmpfloat_with_epsilon (lrg_light_set_sample_fog (lights, &p),
                                    lrg_fog_get_amount (fog, 20.0f, 0.0f), 3e-3f);

    lrg_weather_effect_deactivate (LRG_WEATHER_EFFECT (fog));
    g_assert_false (lrg_fog_apply (fog, lights));
    g_assert_false (lrg_light_set_get_fog (lights, NULL, NULL, NULL, NULL, NULL));
}

/* ------------------------------------------------------------------------ */
/* Lightning                                                                */
/* ------------------------------------------------------------------------ */

static void
test_lightning_flash_level (void)
{
    g_autoptr(LrgLightning) lightning = lrg_lightning_new ();
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (lightning);
    gfloat start;
    gfloat later;
    guint frame;

    lrg_lightning_set_thunder_enabled (lightning, FALSE);
    lrg_lightning_set_min_interval (lightning, 300.0f);
    lrg_lightning_set_max_interval (lightning, 300.0f);
    lrg_weather_effect_activate (effect);
    lrg_weather_effect_set_intensity (effect, 1.0f);
    g_assert_cmpfloat (lrg_lightning_get_flash_level (lightning), ==, 0.0f);

    /* flash_intensity 0.8 x the attack's 60 % at the very start */
    lrg_lightning_trigger_flash (lightning);
    start = lrg_lightning_get_flash_level (lightning);
    g_assert_cmpfloat_with_epsilon (start, 0.8f * 0.6f, 1e-5f);

    /* peak after the attack (10 % of 0.1 s), then the decay */
    lrg_weather_effect_update (effect, 0.01f);
    g_assert_cmpfloat_with_epsilon (lrg_lightning_get_flash_level (lightning), 0.8f, 1e-4f);
    lrg_weather_effect_update (effect, 0.05f);
    later = lrg_lightning_get_flash_level (lightning);
    g_assert_cmpfloat (later, <, 0.8f);
    g_assert_cmpfloat (later, >, 0.0f);

    /* the second sub-flash is weaker */
    lrg_weather_effect_update (effect, 0.045f);
    g_assert_true (lrg_lightning_is_flashing (lightning));
    lrg_weather_effect_update (effect, 0.01f);
    g_assert_cmpfloat_with_epsilon (lrg_lightning_get_flash_level (lightning),
                                    0.8f * 0.6f, 1e-3f);

    /* the effect intensity scales the level */
    lrg_weather_effect_set_intensity (effect, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_lightning_get_flash_level (lightning),
                                    0.4f * 0.6f, 1e-3f);

    for (frame = 0; frame < 20; frame++)
        lrg_weather_effect_update (effect, 0.02f);
    g_assert_false (lrg_lightning_is_flashing (lightning));
    g_assert_cmpfloat (lrg_lightning_get_flash_level (lightning), ==, 0.0f);
}

static void
test_lightning_bolt (void)
{
    g_autoptr(LrgLightning) lightning = lrg_lightning_new ();
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (lightning);
    GrlVector3 cam = vec (10, 2, -30);
    GrlVector3 top;
    GrlVector3 bottom;
    GrlVector3 p;
    gfloat distance;
    gfloat height;
    gfloat width;
    guint n;
    guint i;

    lrg_lightning_get_bolt_shape (lightning, &distance, &height, &width);
    g_assert_cmpfloat (distance, ==, 120.0f);
    lrg_lightning_set_bolt_shape (lightning, 200.0f, 100.0f, 2.0f);
    lrg_lightning_set_bolt_shape (lightning, -1.0f, 0.0f, NAN);   /* ignored */
    lrg_lightning_get_bolt_shape (lightning, &distance, &height, &width);
    g_assert_cmpfloat (distance, ==, 200.0f);
    g_assert_cmpfloat (height, ==, 100.0f);
    g_assert_cmpfloat (width, ==, 2.0f);

    lrg_lightning_set_bolts_enabled (lightning, TRUE);
    lrg_lightning_set_thunder_enabled (lightning, FALSE);
    lrg_weather_effect_set_camera_position (effect, &cam);
    lrg_weather_effect_activate (effect);
    g_assert_cmpuint (lrg_lightning_get_bolt_point_count (lightning), ==, 0);
    g_assert_false (lrg_lightning_get_bolt_point (lightning, 0, &p));

    lrg_lightning_trigger_flash (lightning);
    n = lrg_lightning_get_bolt_point_count (lightning);
    g_assert_cmpuint (n, ==, 33);
    g_assert_true (lrg_lightning_get_bolt_point (lightning, 0, &top));
    g_assert_true (lrg_lightning_get_bolt_point (lightning, n - 1, &bottom));
    g_assert_false (lrg_lightning_get_bolt_point (lightning, n, &p));

    /* the top is 200 away horizontally and 100 above the camera; the
     * bolt spans 125 down, ending 25 below the camera */
    g_assert_cmpfloat_with_epsilon (hypotf (top.x - cam.x, top.z - cam.z), 200.0f, 1e-2f);
    g_assert_cmpfloat_with_epsilon (top.y, cam.y + 100.0f, 1e-3f);
    g_assert_cmpfloat_with_epsilon (bottom.y, cam.y - 25.0f, 1e-3f);
    for (i = 1; i < n; i++)
    {
        GrlVector3 prev;

        g_assert_true (lrg_lightning_get_bolt_point (lightning, i - 1, &prev));
        g_assert_true (lrg_lightning_get_bolt_point (lightning, i, &p));
        g_assert_cmpfloat (p.y, <, prev.y);
    }
}

/* ------------------------------------------------------------------------ */
/* Weather and manager                                                      */
/* ------------------------------------------------------------------------ */

/* A minimal custom effect that records its passes. */
#define TEST_TYPE_RECORDER (test_recorder_get_type ())
G_DECLARE_FINAL_TYPE (TestRecorder, test_recorder, TEST, RECORDER, LrgWeatherEffect)

struct _TestRecorder
{
    LrgWeatherEffect parent_instance;
    GString         *log;      /* borrowed */
};

G_DEFINE_FINAL_TYPE (TestRecorder, test_recorder, LRG_TYPE_WEATHER_EFFECT)

static void
test_recorder_render (LrgWeatherEffect *effect)
{
    TestRecorder *self = TEST_RECORDER (effect);

    g_string_append_printf (self->log, "%s;", lrg_weather_effect_get_id (effect));
}

static void
test_recorder_render_world (LrgWeatherEffect *effect,
                            const GrlVector3 *camera_position)
{
    TestRecorder *self = TEST_RECORDER (effect);

    g_string_append_printf (self->log, "w%s;", lrg_weather_effect_get_id (effect));
}

static void
test_recorder_render_overlay (LrgWeatherEffect *effect,
                              gint              width,
                              gint              height)
{
    TestRecorder *self = TEST_RECORDER (effect);

    g_string_append_printf (self->log, "o%s;", lrg_weather_effect_get_id (effect));
}

static void
test_recorder_class_init (TestRecorderClass *klass)
{
    LrgWeatherEffectClass *effect_class = LRG_WEATHER_EFFECT_CLASS (klass);

    effect_class->render = test_recorder_render;
    effect_class->render_world = test_recorder_render_world;
    effect_class->render_overlay = test_recorder_render_overlay;
}

static void
test_recorder_init (TestRecorder *self)
{
}

static TestRecorder *
recorder_new (const gchar *id,
              gint         layer,
              GString     *log)
{
    TestRecorder *rec = g_object_new (TEST_TYPE_RECORDER, "id", id, NULL);

    lrg_weather_effect_set_render_layer (LRG_WEATHER_EFFECT (rec), layer);
    rec->log = log;
    return rec;
}

static void
test_weather_render_layers (void)
{
    g_autoptr(LrgWeather) weather = lrg_weather_new ("w", "W");
    g_autoptr(GString) log = g_string_new (NULL);
    g_autoptr(TestRecorder) a = recorder_new ("a", 2, log);
    g_autoptr(TestRecorder) b = recorder_new ("b", -1, log);
    g_autoptr(TestRecorder) c = recorder_new ("c", 2, log);
    g_autoptr(TestRecorder) d = recorder_new ("d", 0, log);
    GrlVector3 cam = vec (0, 0, 0);

    lrg_weather_add_effect (weather, LRG_WEATHER_EFFECT (a));
    lrg_weather_add_effect (weather, LRG_WEATHER_EFFECT (b));
    lrg_weather_add_effect (weather, LRG_WEATHER_EFFECT (c));
    lrg_weather_add_effect (weather, LRG_WEATHER_EFFECT (d));

    /* nothing while inactive */
    lrg_weather_render (weather);
    g_assert_cmpstr (log->str, ==, "");

    /* custom effects still render headless, by layer, stable within one */
    lrg_weather_activate (weather);
    lrg_weather_render (weather);
    g_assert_cmpstr (log->str, ==, "b;d;a;c;");
    g_string_truncate (log, 0);
    lrg_weather_render_world (weather, &cam);
    g_assert_cmpstr (log->str, ==, "wb;wd;wa;wc;");
    g_string_truncate (log, 0);
    lrg_weather_render_overlay (weather, 32, 32);
    g_assert_cmpstr (log->str, ==, "ob;od;oa;oc;");
}

/* register_fog_weather:
 * Registers weather @id holding one LrgFog of @density and @r, @g, @b. */
static LrgFog *
register_fog_weather (LrgWeatherManager *manager,
                      const gchar       *id,
                      gfloat             density,
                      guint8             r,
                      guint8             g,
                      guint8             b)
{
    g_autoptr(LrgWeather) weather = lrg_weather_new (id, id);
    g_autoptr(LrgFog) fog = lrg_fog_new ();

    lrg_fog_set_fog_type (fog, LRG_FOG_TYPE_EXPONENTIAL);
    lrg_fog_set_density (fog, density);
    lrg_fog_set_color (fog, r, g, b, 255);
    lrg_weather_add_effect (weather, LRG_WEATHER_EFFECT (fog));
    lrg_weather_manager_register_weather (manager, weather);
    return fog;
}

static void
test_manager_apply_fog (void)
{
    g_autoptr(LrgWeatherManager) manager = lrg_weather_manager_new ();
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlColor color;
    gfloat   density;

    register_fog_weather (manager, "red", 0.1f, 255, 0, 0);
    register_fog_weather (manager, "blue", 0.3f, 0, 0, 255);
    lrg_weather_manager_set_day_night_enabled (manager, FALSE);

    g_assert_false (lrg_weather_manager_apply_fog (manager, lights));

    lrg_weather_manager_set_weather (manager, "red", 0.0f);
    lrg_weather_manager_update (manager, 0.0f);
    g_assert_true (lrg_weather_manager_apply_fog (manager, lights));
    g_assert_true (lrg_light_set_get_fog (lights, NULL, &color, &density, NULL, NULL));
    g_assert_cmpuint (color.r, ==, 255);
    g_assert_cmpfloat_with_epsilon (density, 0.1f, 1e-5f);

    /* half way through a crossfade the two fogs blend evenly */
    lrg_weather_manager_set_weather (manager, "blue", 2.0f);
    lrg_weather_manager_update (manager, 1.0f);
    g_assert_true (lrg_weather_manager_is_transitioning (manager));
    g_assert_true (lrg_weather_manager_apply_fog (manager, lights));
    g_assert_true (lrg_light_set_get_fog (lights, NULL, &color, &density, NULL, NULL));
    g_assert_cmpfloat_with_epsilon (density, 0.2f, 1e-4f);
    g_assert_cmpuint (color.r, >=, 126);
    g_assert_cmpuint (color.r, <=, 129);
    g_assert_cmpuint (color.b, >=, 126);
    g_assert_cmpuint (color.b, <=, 129);
    g_assert_cmpuint (color.a, ==, 255);

    lrg_weather_manager_update (manager, 1.5f);
    g_assert_false (lrg_weather_manager_is_transitioning (manager));
    lrg_weather_manager_apply_fog (manager, lights);
    g_assert_true (lrg_light_set_get_fog (lights, NULL, &color, &density, NULL, NULL));
    g_assert_cmpuint (color.b, ==, 255);
    g_assert_cmpfloat_with_epsilon (density, 0.3f, 1e-5f);

    /* clearing the weather clears the fog */
    lrg_weather_manager_clear_weather (manager, 0.0f);
    g_assert_false (lrg_weather_manager_apply_fog (manager, lights));
    g_assert_false (lrg_light_set_get_fog (lights, NULL, NULL, NULL, NULL, NULL));
}

static void
test_manager_world_and_flash (void)
{
    g_autoptr(LrgWeatherManager) manager = lrg_weather_manager_new ();
    g_autoptr(LrgWeather) storm = lrg_weather_new ("storm", "Storm");
    g_autoptr(LrgRain) rain = lrg_rain_new ();
    g_autoptr(LrgLightning) lightning = lrg_lightning_new ();
    GrlVector3 cam = vec (500, 20, 500);
    GrlVector3 got;
    GrlVector3 p;
    guint i;

    lrg_weather_effect_set_space (LRG_WEATHER_EFFECT (rain), LRG_WEATHER_SPACE_WORLD);
    lrg_rain_set_drop_count (rain, 100);
    lrg_lightning_set_thunder_enabled (lightning, FALSE);
    lrg_weather_add_effect (storm, LRG_WEATHER_EFFECT (rain));
    lrg_weather_add_effect (storm, LRG_WEATHER_EFFECT (lightning));
    lrg_weather_manager_register_weather (manager, storm);
    lrg_weather_manager_set_day_night_enabled (manager, FALSE);
    lrg_weather_manager_set_weather (manager, "storm", 0.0f);
    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (rain), 1.0f);
    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (lightning), 1.0f);

    /* the 3D pass centres every effect on the camera, headless too */
    lrg_weather_manager_render_world (manager, &cam);
    lrg_weather_effect_get_camera_position (LRG_WEATHER_EFFECT (lightning), &got);
    g_assert_cmpfloat (got.x, ==, 500.0f);
    lrg_weather_manager_update (manager, 1.0f / 60.0f);
    for (i = 0; i < 100; i++)
    {
        g_assert_true (lrg_rain_get_drop (rain, i, &p));
        assert_in_volume (&p, &cam, 15.0f, 12.0f);
    }

    g_assert_cmpfloat (lrg_weather_manager_get_flash_level (manager), ==, 0.0f);
    lrg_lightning_trigger_flash (lightning);
    g_assert_cmpfloat (lrg_weather_manager_get_flash_level (manager), >, 0.0f);
    g_assert_cmpfloat (lrg_weather_manager_get_flash_level (manager), ==,
                       lrg_lightning_get_flash_level (lightning));

    lrg_weather_manager_render_overlay (manager, 64, 64);
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */
/* ------------------------------------------------------------------------ */

/* count_pixels:
 * Pixels of @image whose red is above @min_r and blue below @max_b. */
static guint
count_red (Image  image,
           guint8 min_r)
{
    guint n = 0;
    gint  x;
    gint  y;

    for (y = 0; y < image.height; y++)
        for (x = 0; x < image.width; x++)
        {
            Color c = GetImageColor (image, x, y);

            if (c.r > min_r && c.b < 128)
                n++;
        }
    return n;
}

/* render_world_effect:
 * Clears a 64 x 64 target to blue and draws @effect's 3D pass from a
 * camera at (0, 0, 0) looking down -Z; returns the image (unload it). */
static Image
render_world_effect (LrgWeatherEffect *effect)
{
    RenderTexture2D rt = LoadRenderTexture (64, 64);
    Camera3D        camera;
    GrlVector3      eye = vec (0, 0, 0);
    Image           shot;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 0, 0, 0 };
    camera.target = (Vector3){ 0, 0, -1 };
    camera.up = (Vector3){ 0, 1, 0 };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode (rt);
    ClearBackground ((Color){ 0, 0, 255, 255 });
    BeginMode3D (camera);
    lrg_weather_effect_render_world (effect, &eye);
    EndMode3D ();
    EndTextureMode ();

    shot = LoadImageFromTexture (rt.texture);
    UnloadRenderTexture (rt);
    return shot;
}

static void
test_draw_rain_world (void)
{
    GrlVector3 cam = vec (0, 0, 0);
    g_autoptr(LrgRain) rain = NULL;
    Image shot;
    guint lit;

    SKIP_WITHOUT_GRAPHICS ();

    rain = world_rain (&cam);
    lrg_rain_set_drop_count (rain, 3000);
    lrg_rain_set_drop_thickness (rain, 0.05f);
    lrg_rain_set_drop_length (rain, 1.0f);
    lrg_rain_set_color (rain, 255, 0, 0, 255);
    lrg_weather_effect_set_volume (LRG_WEATHER_EFFECT (rain), 6.0f, 6.0f);

    shot = render_world_effect (LRG_WEATHER_EFFECT (rain));
    lit = count_red (shot, 100);
    UnloadImage (shot);
    g_assert_cmpuint (lit, >, 20);

    /* no intensity, no rain */
    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (rain), 0.0f);
    shot = render_world_effect (LRG_WEATHER_EFFECT (rain));
    lit = count_red (shot, 100);
    UnloadImage (shot);
    g_assert_cmpuint (lit, ==, 0);

    /* a screen-space rain draws nothing in the 3D pass */
    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (rain), 1.0f);
    lrg_weather_effect_set_space (LRG_WEATHER_EFFECT (rain), LRG_WEATHER_SPACE_SCREEN);
    shot = render_world_effect (LRG_WEATHER_EFFECT (rain));
    lit = count_red (shot, 100);
    UnloadImage (shot);
    g_assert_cmpuint (lit, ==, 0);
}

static void
test_draw_snow_world (void)
{
    g_autoptr(LrgSnow) snow = lrg_snow_new ();
    LrgWeatherEffect *effect = LRG_WEATHER_EFFECT (snow);
    Image shot;
    guint lit;

    SKIP_WITHOUT_GRAPHICS ();

    lrg_weather_effect_set_space (effect, LRG_WEATHER_SPACE_WORLD);
    lrg_snow_set_flake_count (snow, 3000);
    lrg_snow_set_flake_size (snow, 0.15f);
    lrg_snow_set_color (snow, 255, 0, 0, 255);
    lrg_weather_effect_set_volume (effect, 6.0f, 6.0f);
    lrg_weather_effect_activate (effect);
    lrg_weather_effect_set_intensity (effect, 1.0f);

    shot = render_world_effect (effect);
    lit = count_red (shot, 100);
    UnloadImage (shot);
    g_assert_cmpuint (lit, >, 20);
}

static void
test_draw_screen_passes (void)
{
    g_autoptr(LrgFog) fog = active_fog (LRG_FOG_TYPE_UNIFORM);
    g_autoptr(LrgLightning) lightning = lrg_lightning_new ();
    g_autoptr(LrgRain) rain = lrg_rain_new ();
    RenderTexture2D rt;
    Image shot;
    Color px;
    guint lit;
    guint frame;

    SKIP_WITHOUT_GRAPHICS ();

    rt = LoadRenderTexture (32, 32);

    /* fog veil: full opacity, green */
    lrg_fog_set_color (fog, 0, 255, 0, 255);
    lrg_fog_set_veil_opacity (fog, 1.0f);
    BeginTextureMode (rt);
    ClearBackground ((Color){ 0, 0, 255, 255 });
    lrg_weather_effect_render_overlay (LRG_WEATHER_EFFECT (fog), 32, 32);
    EndTextureMode ();
    shot = LoadImageFromTexture (rt.texture);
    px = GetImageColor (shot, 16, 16);
    UnloadImage (shot);
    g_assert_cmpuint (px.g, >, 245);
    g_assert_cmpuint (px.b, <, 10);

    /* lightning flash over black: as bright as the flash level */
    lrg_lightning_set_thunder_enabled (lightning, FALSE);
    lrg_weather_effect_activate (LRG_WEATHER_EFFECT (lightning));
    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (lightning), 1.0f);
    lrg_lightning_set_flash_intensity (lightning, 1.0f);
    lrg_lightning_trigger_flash (lightning);
    lrg_weather_effect_update (LRG_WEATHER_EFFECT (lightning), 0.01f);
    BeginTextureMode (rt);
    ClearBackground ((Color){ 0, 0, 0, 255 });
    lrg_weather_effect_render_overlay (LRG_WEATHER_EFFECT (lightning), 32, 32);
    EndTextureMode ();
    shot = LoadImageFromTexture (rt.texture);
    px = GetImageColor (shot, 16, 16);
    UnloadImage (shot);
    g_assert_cmpuint (px.r, >, 240);

    /* legacy 2D rain draws red lines into its area */
    lrg_rain_set_area (rain, 0, 0, 32, 32);
    lrg_rain_set_splash_enabled (rain, FALSE);
    lrg_rain_set_drop_count (rain, 200);
    lrg_rain_set_drop_thickness (rain, 2.0f);
    lrg_rain_set_color (rain, 255, 0, 0, 255);
    lrg_weather_effect_activate (LRG_WEATHER_EFFECT (rain));
    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (rain), 1.0f);
    for (frame = 0; frame < 3; frame++)
        lrg_weather_effect_update (LRG_WEATHER_EFFECT (rain), 0.005f);
    BeginTextureMode (rt);
    ClearBackground ((Color){ 0, 0, 255, 255 });
    lrg_weather_effect_render (LRG_WEATHER_EFFECT (rain));
    EndTextureMode ();
    shot = LoadImageFromTexture (rt.texture);
    lit = count_red (shot, 100);
    UnloadImage (shot);
    g_assert_cmpuint (lit, >, 20);

    UnloadRenderTexture (rt);
}

/* floor_batch:
 * A 20 x 20 white quad at y = 0 facing up, centred on the origin. */
static LrgStaticBatch *
floor_batch (void)
{
    LrgStaticBatch *batch = lrg_static_batch_new ();
    gfloat positions[12] = { -10, 0, -10, 10, 0, -10, -10, 0, 10, 10, 0, 10 };
    gfloat normals[12] = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
    gfloat texcoords[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    guint32 indices[6] = { 0, 2, 1, 1, 2, 3 };

    g_assert_true (lrg_static_batch_add_mesh (batch, positions, normals, texcoords, NULL, 4,
                                              indices, 6, NULL, 0, 0, NULL));
    g_assert_true (lrg_static_batch_upload (batch, NULL));
    return batch;
}

/* Centre pixel of the floor seen from 10 above. */
static Color
render_floor (LrgStaticBatch *batch)
{
    RenderTexture2D rt = LoadRenderTexture (16, 16);
    Camera3D        camera;
    Image           shot;
    Color           centre;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 0, 10, 0.01f };
    camera.target = (Vector3){ 0, 0, 0 };
    camera.up = (Vector3){ 0, 0, -1 };
    camera.fovy = 30.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode (rt);
    ClearBackground ((Color){ 0, 0, 255, 255 });
    BeginMode3D (camera);
    lrg_static_batch_draw (batch, LRG_STATIC_BATCH_ALL_LAYERS);
    EndMode3D ();
    EndTextureMode ();

    shot = LoadImageFromTexture (rt.texture);
    centre = GetImageColor (shot, 8, 8);
    UnloadImage (shot);
    UnloadRenderTexture (rt);
    return centre;
}

static void
test_draw_light_set_fog (void)
{
    g_autoptr(LrgLightSet)    lights = NULL;
    g_autoptr(LrgStaticBatch) batch = NULL;
    GrlShader *shader;
    GrlColor   white = { 255, 255, 255, 255 };
    GrlColor   red = { 255, 0, 0, 255 };
    GrlColor   half_red = { 255, 0, 0, 128 };
    Color      px;

    SKIP_WITHOUT_GRAPHICS ();

    lights = lrg_light_set_new ();
    shader = lrg_light_set_get_shader (lights);
    if (shader == NULL)
    {
        g_test_skip ("GLSL 330 unavailable");
        return;
    }
    batch = floor_batch ();
    lrg_static_batch_set_shader (batch, shader);
    lrg_light_set_set_ambient (lights, &white);

    /* no fog: the white floor */
    lrg_light_set_apply (lights, 0, 10, 0);
    px = render_floor (batch);
    g_assert_cmpuint (px.g, >, 245);

    /* the floor is 10 away: linear fog full at 5 turns it red */
    lrg_light_set_set_fog (lights, LRG_FOG_TYPE_LINEAR, &red, 0.0f, 0.0f, 5.0f);
    lrg_light_set_apply (lights, 0, 10, 0);
    px = render_floor (batch);
    g_assert_cmpuint (px.r, >, 245);
    g_assert_cmpuint (px.g, <, 10);

    /* fog beginning beyond the floor leaves it white */
    lrg_light_set_set_fog (lights, LRG_FOG_TYPE_LINEAR, &red, 0.0f, 50.0f, 100.0f);
    lrg_light_set_apply (lights, 0, 10, 0);
    px = render_floor (batch);
    g_assert_cmpuint (px.g, >, 245);

    /* alpha 128 caps the blend at half */
    lrg_light_set_set_fog (lights, LRG_FOG_TYPE_UNIFORM, &half_red, 1.0f, 0.0f, 0.0f);
    lrg_light_set_apply (lights, 0, 10, 0);
    px = render_floor (batch);
    g_assert_cmpuint (px.r, >, 245);
    g_assert_cmpuint (px.g, >, 110);
    g_assert_cmpuint (px.g, <, 145);

    lrg_light_set_clear_fog (lights);
    lrg_light_set_apply (lights, 0, 10, 0);
    px = render_floor (batch);
    g_assert_cmpuint (px.g, >, 245);

    lrg_static_batch_set_shader (batch, NULL);
}

static void
test_draw_billboard_fog (void)
{
    g_autoptr(LrgLightSet)       lights = NULL;
    g_autoptr(LrgBillboardBatch) batch = NULL;
    g_autoptr(GrlImage)          image = NULL;
    g_autoptr(GrlTexture)        texture = NULL;
    GrlVector3 pos = vec (0, 0, 0);
    GrlVector3 eye = vec (0, 1, 5);
    GrlColor   white = { 255, 255, 255, 255 };
    GrlColor   green = { 0, 255, 0, 255 };
    RenderTexture2D rt;
    Camera3D   camera;
    Image      shot;
    Color      centre;
    gint       pass;

    SKIP_WITHOUT_GRAPHICS ();

    image = grl_image_new_color (4, 4, &white);
    texture = grl_texture_new_from_image (image);
    batch = lrg_billboard_batch_new ();
    lrg_billboard_batch_set_material_texture (batch, 0, texture);
    lrg_billboard_batch_add (batch, 0, &pos, 2, 2);
    lrg_billboard_batch_set_alpha_cutoff (batch, 0.0f);   /* fog alone needs the shader */

    lights = lrg_light_set_new ();
    lrg_light_set_set_ambient (lights, &white);
    lrg_light_set_select (lights, 0.0, 1.0, 5.0);
    lrg_billboard_batch_set_light_set (batch, lights);

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 0, 1, 5 };
    camera.target = (Vector3){ 0, 1, 0 };
    camera.up = (Vector3){ 0, 1, 0 };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    rt = LoadRenderTexture (16, 16);
    for (pass = 0; pass < 2; pass++)
    {
        /* pass 0: no fog, white; pass 1: fog full at 2 units, green */
        if (pass == 1)
            lrg_light_set_set_fog (lights, LRG_FOG_TYPE_LINEAR, &green, 0.0f, 0.0f, 2.0f);
        BeginTextureMode (rt);
        ClearBackground ((Color){ 0, 0, 255, 255 });
        BeginMode3D (camera);
        lrg_billboard_batch_draw (batch, &eye);
        EndMode3D ();
        EndTextureMode ();

        shot = LoadImageFromTexture (rt.texture);
        centre = GetImageColor (shot, 8, 8);
        UnloadImage (shot);
        if (pass == 0)
        {
            g_assert_cmpuint (centre.r, >, 245);
            g_assert_cmpuint (centre.b, >, 245);
        }
        else
        {
            g_assert_cmpuint (centre.r, <, 10);
            g_assert_cmpuint (centre.g, >, 245);
            g_assert_cmpuint (centre.b, <, 10);
        }
    }
    UnloadRenderTexture (rt);
}


/* ------------------------------------------------------------------------ */
/* Enhanced sky: clouds, moons, the ground overlay                          */
/* ------------------------------------------------------------------------ */

/* render_sky_view:
 * Clears a 64 x 64 target to dark blue and runs @draw with a camera at
 * the origin looking along @target. */
static Image
render_sky_view (Vector3 target,
                 void   (*draw) (gpointer),
                 gpointer data)
{
    RenderTexture2D rt = LoadRenderTexture (64, 64);
    Camera3D        camera;
    Image           shot;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 0, 0, 0 };
    camera.target = target;
    camera.up = (Vector3){ 0, 0, -1 };
    if (fabsf (target.y) < 0.9f)
        camera.up = (Vector3){ 0, 1, 0 };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode (rt);
    ClearBackground ((Color){ 10, 10, 60, 255 });
    BeginMode3D (camera);
    draw (data);
    EndMode3D ();
    EndTextureMode ();

    shot = LoadImageFromTexture (rt.texture);
    UnloadRenderTexture (rt);
    return shot;
}

/* count_bright:
 * Pixels brighter than @min in every channel. */
static guint
count_bright (Image  image,
              guint8 min)
{
    guint n = 0;
    gint  x;
    gint  y;

    for (y = 0; y < image.height; y++)
        for (x = 0; x < image.width; x++)
        {
            Color c = GetImageColor (image, x, y);

            if (c.r > min && c.g > min && c.b > min)
                n++;
        }
    return n;
}

static void
draw_clouds (gpointer data)
{
    GrlVector3 eye = vec (0, 0, 0);
    gfloat     sun[3] = { 0.0f, 1.0f, 0.0f };

    lrg_cloud_layer_draw (LRG_CLOUD_LAYER (data), &eye, 500.0f, sun);
}

static void
test_draw_clouds (void)
{
    g_autoptr(LrgCloudLayer) clouds = lrg_cloud_layer_new (4);
    Image clear;
    Image overcast;
    guint before;
    guint after;

    SKIP_WITHOUT_GRAPHICS ();

    /* looking straight up: nothing at coverage 0, a grey deck (its
     * shaded underside) over the dark blue at 1 */
    clear = render_sky_view ((Vector3){ 0, 1, 0 }, draw_clouds, clouds);
    before = count_bright (clear, 80);
    lrg_cloud_layer_set_coverage (clouds, 1.0f);
    overcast = render_sky_view ((Vector3){ 0, 1, 0 }, draw_clouds, clouds);
    after = count_bright (overcast, 80);
    UnloadImage (clear);
    UnloadImage (overcast);
    g_assert_cmpuint (before, ==, 0);
    g_assert_cmpuint (after, >, 64 * 64 / 2);
}

static void
draw_moon (gpointer data)
{
    GrlVector3 eye = vec (0, 0, 0);

    lrg_sky_bodies_draw (LRG_SKY_BODIES (data), &eye);
}

static void
test_draw_moon_phase (void)
{
    g_autoptr(LrgSkyBodies) bodies = lrg_sky_bodies_new ();
    GrlVector3 ahead = vec (0, 0, -1);
    GrlVector3 sun = vec (1, 0, 0);
    GrlColor   white = { 255, 255, 255, 255 };
    Image      full;
    Image      half;
    guint      n_full;
    guint      n_half;

    SKIP_WITHOUT_GRAPHICS ();

    lrg_sky_bodies_set_sun (bodies, &sun, 0.0f, NULL);
    lrg_sky_bodies_set_moon (bodies, 0, &ahead, 10.0f, &white, 0.5);
    full = render_sky_view ((Vector3){ 0, 0, -1 }, draw_moon, bodies);
    lrg_sky_bodies_set_moon (bodies, 0, &ahead, 10.0f, &white, 0.25);
    half = render_sky_view ((Vector3){ 0, 0, -1 }, draw_moon, bodies);
    n_full = count_bright (full, 200);
    n_half = count_bright (half, 200);
    UnloadImage (full);
    UnloadImage (half);

    /* a quarter moon lights about half the full disc */
    g_assert_cmpuint (n_full, >, 50);
    g_assert_cmpuint (n_half, >, n_full * 35 / 100);
    g_assert_cmpuint (n_half, <, n_full * 65 / 100);
}

typedef struct
{
    LrgTerrain       *terrain;
    LrgGroundOverlay *overlay;
} OverlayScene;

static void
draw_ground (gpointer data)
{
    OverlayScene *scene = data;

    lrg_terrain_draw (scene->terrain);
    if (lrg_ground_overlay_begin (scene->overlay, scene->terrain))
    {
        lrg_terrain_draw (scene->terrain);
        lrg_ground_overlay_end (scene->overlay, scene->terrain);
    }
}

static void
test_draw_ground_overlay (void)
{
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (4, 40.0f);
    g_autoptr(LrgGroundOverlay) overlay = lrg_ground_overlay_new ();
    g_autoptr(LrgSnowCover) snow = lrg_snow_cover_new ();
    g_autoptr(GError) error = NULL;
    GrlColor     grey = { 90, 90, 90, 255 };
    OverlayScene scene;
    gfloat       heights[25];
    Image        bare;
    Image        white;
    guint        i;
    guint        n_bare;
    guint        n_white;

    SKIP_WITHOUT_GRAPHICS ();

    /* a flat grey floor 2 below the eye, seen from above */
    for (i = 0; i < 25; i++)
        heights[i] = -2.0f;
    g_assert_true (lrg_terrain_set_chunk (terrain, -1, -1, heights, 25, NULL, NULL, &error));
    g_assert_no_error (error);
    lrg_terrain_set_origin (terrain, -20.0, 0.0, -20.0);
    lrg_terrain_set_tint (terrain, &grey);
    g_assert_true (lrg_terrain_upload (terrain, &error));
    lrg_ground_overlay_set_snow_cover (overlay, snow);
    scene.terrain = terrain;
    scene.overlay = overlay;

    bare = render_sky_view ((Vector3){ 0, -1, 0 }, draw_ground, &scene);
    lrg_snow_cover_set_level (snow, 1.0);
    white = render_sky_view ((Vector3){ 0, -1, 0 }, draw_ground, &scene);
    n_bare = count_bright (bare, 180);
    n_white = count_bright (white, 180);
    UnloadImage (bare);
    UnloadImage (white);
    lrg_ground_overlay_release_gpu (overlay);
    lrg_terrain_unload (terrain);

    g_assert_cmpuint (n_bare, ==, 0);
    g_assert_cmpuint (n_white, >, 64 * 64 / 2);
}

int
main (int   argc,
      char *argv[])
{
    gint result;

    g_test_init (&argc, &argv, NULL);

    graphics_available = init_graphics_context ();

    g_test_add_func ("/weather-render/effect/space-volume-camera", test_effect_space_volume_camera);
    g_test_add_func ("/weather-render/rain/space-defaults", test_rain_space_defaults);
    g_test_add_func ("/weather-render/rain/world-volume", test_rain_world_volume);
    g_test_add_func ("/weather-render/rain/world-fall-wind", test_rain_world_fall_and_wind);
    g_test_add_func ("/weather-render/rain/world-splash", test_rain_world_splash);
    g_test_add_func ("/weather-render/rain/screen-splash", test_rain_screen_splash);
    g_test_add_func ("/weather-render/snow/world", test_snow_world);
    g_test_add_func ("/weather-render/fog/amount", test_fog_amount);
    g_test_add_func ("/weather-render/fog/veil-scroll", test_fog_veil_and_scroll);
    g_test_add_func ("/weather-render/fog/apply", test_fog_apply);
    g_test_add_func ("/weather-render/light-set/fog", test_light_set_fog);
    g_test_add_func ("/weather-render/lightning/flash-level", test_lightning_flash_level);
    g_test_add_func ("/weather-render/lightning/bolt", test_lightning_bolt);
    g_test_add_func ("/weather-render/weather/render-layers", test_weather_render_layers);
    g_test_add_func ("/weather-render/manager/apply-fog", test_manager_apply_fog);
    g_test_add_func ("/weather-render/manager/world-and-flash", test_manager_world_and_flash);
    g_test_add_func ("/weather-render/draw/rain-world", test_draw_rain_world);
    g_test_add_func ("/weather-render/draw/snow-world", test_draw_snow_world);
    g_test_add_func ("/weather-render/draw/screen-passes", test_draw_screen_passes);
    g_test_add_func ("/weather-render/draw/light-set-fog", test_draw_light_set_fog);
    g_test_add_func ("/weather-render/draw/billboard-fog", test_draw_billboard_fog);
    g_test_add_func ("/weather-render/draw/clouds", test_draw_clouds);
    g_test_add_func ("/weather-render/draw/moon-phase", test_draw_moon_phase);
    g_test_add_func ("/weather-render/draw/ground-overlay", test_draw_ground_overlay);

    result = g_test_run ();

    g_clear_object (&test_window);
    return result;
}
