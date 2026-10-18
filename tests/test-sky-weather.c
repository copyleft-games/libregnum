/* test-sky-weather.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The enhanced sky and weather: moon phases and the calendar math,
 * star fields, distance-delayed thunder, the weather field's advection
 * and determinism, the snow cover's accumulation and tracks, cloud
 * density and shadows, billboard sway and the sky bodies.  Headless.
 */

#include "../src/weather/lrg-celestial.h"
#include "../src/weather/lrg-weather-effect.h"
#include "../src/weather/lrg-lightning.h"
#include "../src/weather/lrg-weather-field.h"
#include "../src/weather/lrg-snow-cover.h"
#include "../src/weather/lrg-cloud-layer.h"
#include "../src/weather/lrg-ground-overlay.h"
#include "../src/graphics/lrg-sky-bodies.h"
#include "../src/graphics/lrg-billboard-batch.h"

#include <glib.h>
#include <gio/gio.h>
#include <math.h>

/* ---------------------------------------------------------------------- */
/* Celestial                                                              */
/* ---------------------------------------------------------------------- */

static void
test_moon_phase (void)
{
    /* a 32-day cycle: day 0 new, 8 first quarter, 16 full, 24 last */
    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_phase (0.0, 32.0, 0.0), 0.0, 1e-12);
    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_phase (8.0, 32.0, 0.0), 0.25, 1e-12);
    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_phase (16.0, 32.0, 0.0), 0.5, 1e-12);
    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_phase (40.0, 32.0, 0.0), 0.25, 1e-12);
    /* the offset shifts the cycle; negative days wrap upwards */
    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_phase (0.0, 32.0, 16.0), 0.5, 1e-12);
    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_phase (-8.0, 32.0, 0.0), 0.75, 1e-12);
    /* invalid periods */
    g_assert_cmpfloat (lrg_celestial_moon_phase (5.0, 0.0, 0.0), ==, 0.0);
    g_assert_cmpfloat (lrg_celestial_moon_phase (5.0, -3.0, 0.0), ==, 0.0);
    g_assert_cmpfloat (lrg_celestial_moon_phase (NAN, 32.0, 0.0), ==, 0.0);

    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_illumination (0.0), 0.0, 1e-12);
    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_illumination (0.25), 0.5, 1e-12);
    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_illumination (0.5), 1.0, 1e-12);
    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_illumination (0.75), 0.5, 1e-12);

    g_assert_true (lrg_celestial_moon_is_waxing (0.2));
    g_assert_false (lrg_celestial_moon_is_waxing (0.7));
    g_assert_false (lrg_celestial_moon_is_waxing (0.0));
}

static void
test_moon_disc (void)
{
    gdouble l;
    gdouble r;

    /* new: nothing lit */
    g_assert_false (lrg_celestial_moon_disc_span (0.0, 0.0, &l, &r));
    /* full: the whole row */
    g_assert_true (lrg_celestial_moon_disc_span (0.5, 0.0, &l, &r));
    g_assert_cmpfloat_with_epsilon (l, -1.0, 1e-9);
    g_assert_cmpfloat_with_epsilon (r, 1.0, 1e-9);
    /* first quarter: the right half */
    g_assert_true (lrg_celestial_moon_disc_span (0.25, 0.0, &l, &r));
    g_assert_cmpfloat_with_epsilon (l, 0.0, 1e-9);
    g_assert_cmpfloat_with_epsilon (r, 1.0, 1e-9);
    /* last quarter: the left half */
    g_assert_true (lrg_celestial_moon_disc_span (0.75, 0.0, &l, &r));
    g_assert_cmpfloat_with_epsilon (l, -1.0, 1e-9);
    g_assert_cmpfloat_with_epsilon (r, 0.0, 1e-9);
    /* a waxing crescent hugs the right limb, narrower rows near the top */
    g_assert_true (lrg_celestial_moon_disc_span (0.1, 0.8, &l, &r));
    g_assert_cmpfloat_with_epsilon (r, 0.6, 1e-9);
    g_assert_cmpfloat (l, >, 0.0);
    /* outside the disc */
    g_assert_false (lrg_celestial_moon_disc_span (0.5, 1.5, &l, &r));
}

static void
test_orbit (void)
{
    gfloat d[3];
    gdouble full;

    /* sunrise east, noon up (leaning towards +Z), sunset west */
    lrg_celestial_orbit_direction (0.0, 30.0, d);
    g_assert_cmpfloat_with_epsilon (d[0], 1.0, 1e-6);
    lrg_celestial_orbit_direction (G_PI / 2.0, 30.0, d);
    g_assert_cmpfloat_with_epsilon (d[1], cos (30.0 * G_PI / 180.0), 1e-6);
    g_assert_cmpfloat (d[2], >, 0.0);
    lrg_celestial_orbit_direction (G_PI, 0.0, d);
    g_assert_cmpfloat_with_epsilon (d[0], -1.0, 1e-6);

    /* a full moon rises as the sun sets */
    full = lrg_celestial_moon_orbit_angle (G_PI, 0.5);
    g_assert_cmpfloat_with_epsilon (full, 0.0, 1e-9);
    g_assert_cmpfloat_with_epsilon (lrg_celestial_moon_orbit_angle (1.0, 0.0), 1.0, 1e-9);

    /* night factor: none at noon, full at deep night, between at dusk */
    g_assert_cmpfloat (lrg_celestial_night_factor (1.0f), ==, 0.0f);
    g_assert_cmpfloat (lrg_celestial_night_factor (-0.5f), ==, 1.0f);
    g_assert_cmpfloat (lrg_celestial_night_factor (-0.05f), >, 0.0f);
    g_assert_cmpfloat (lrg_celestial_night_factor (-0.05f), <, 1.0f);
}

static void
test_stars (void)
{
    gfloat a[300];
    gfloat b[300];
    gfloat c[300];
    gfloat bright[100];
    gfloat rotated[3];
    gfloat sun[3];
    guint  i;
    guint  up = 0;

    lrg_celestial_generate_stars (42, 100, a, bright);
    lrg_celestial_generate_stars (42, 100, b, NULL);
    lrg_celestial_generate_stars (43, 100, c, NULL);
    g_assert_cmpmem (a, sizeof a, b, sizeof b);
    g_assert_true (memcmp (a, c, sizeof a) != 0);
    for (i = 0; i < 100; i++)
    {
        gfloat len = sqrtf (a[i * 3] * a[i * 3] + a[i * 3 + 1] * a[i * 3 + 1] +
                            a[i * 3 + 2] * a[i * 3 + 2]);

        g_assert_cmpfloat_with_epsilon (len, 1.0f, 1e-5f);
        g_assert_cmpfloat (bright[i], >=, 0.15f);
        g_assert_cmpfloat (bright[i], <=, 1.0f);
        if (a[i * 3 + 1] > 0.0f)
            up++;
    }
    /* about half the sphere is up */
    g_assert_cmpuint (up, >, 30);
    g_assert_cmpuint (up, <, 70);

    /* the sky turns with the sun: rotating the sunrise point by a
     * quarter turn gives the noon sun */
    lrg_celestial_orbit_direction (0.0, 25.0, sun);
    lrg_celestial_rotate_sky (sun, G_PI / 2.0, 25.0, rotated);
    lrg_celestial_orbit_direction (G_PI / 2.0, 25.0, sun);
    for (i = 0; i < 3; i++)
        g_assert_cmpfloat_with_epsilon (rotated[i], sun[i], 1e-5f);
}

static void
test_thunder_delay (void)
{
    g_assert_cmpfloat_with_epsilon (lrg_celestial_thunder_delay (343.0), 1.0, 1e-12);
    g_assert_cmpfloat_with_epsilon (lrg_celestial_thunder_delay (3430.0), 10.0, 1e-12);
    g_assert_cmpfloat (lrg_celestial_thunder_delay (-5.0), ==, 0.0);
    g_assert_cmpfloat (lrg_celestial_thunder_delay (INFINITY), ==, 0.0);
}

static void
on_thunder (LrgLightning *lightning,
            gpointer      data)
{
    (*(guint *)data)++;
}

static void
test_lightning_distance (void)
{
    g_autoptr(LrgLightning) lightning = lrg_lightning_new ();
    guint thunders = 0;

    g_signal_connect (lightning, "thunder", G_CALLBACK (on_thunder), &thunders);
    /* no random strikes during the test */
    lrg_lightning_set_min_interval (lightning, 1000.0f);
    lrg_lightning_set_max_interval (lightning, 1000.0f);
    lrg_weather_effect_activate (LRG_WEATHER_EFFECT (lightning));
    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (lightning), 1.0f);
    lrg_lightning_set_thunder_from_distance (lightning, TRUE);
    g_assert_true (lrg_lightning_get_thunder_from_distance (lightning));

    /* 686 m away: the thunder takes 2 s, a second strike 343 m away
     * 1 s, and both are heard (neither cancels the other) */
    lrg_lightning_strike_at (lightning, 686.0f, 0.0f);
    g_assert_cmpfloat (lrg_lightning_get_strike_distance (lightning), ==, 686.0f);
    g_assert_true (lrg_lightning_is_flashing (lightning));
    lrg_weather_effect_update (LRG_WEATHER_EFFECT (lightning), 0.5f);
    lrg_lightning_strike_at (lightning, 343.0f, 1.0f);
    g_assert_cmpuint (lrg_lightning_get_pending_thunder_count (lightning), ==, 2);

    lrg_weather_effect_update (LRG_WEATHER_EFFECT (lightning), 0.9f);
    g_assert_cmpuint (thunders, ==, 0);
    lrg_weather_effect_update (LRG_WEATHER_EFFECT (lightning), 0.2f);
    g_assert_cmpuint (thunders, ==, 1);
    g_assert_cmpfloat (lrg_lightning_get_thunder_distance (lightning), ==, 343.0f);
    lrg_weather_effect_update (LRG_WEATHER_EFFECT (lightning), 0.3f);
    g_assert_cmpuint (thunders, ==, 1);
    lrg_weather_effect_update (LRG_WEATHER_EFFECT (lightning), 0.2f);
    g_assert_cmpuint (thunders, ==, 2);
    g_assert_cmpfloat (lrg_lightning_get_thunder_distance (lightning), ==, 686.0f);
    g_assert_cmpuint (lrg_lightning_get_pending_thunder_count (lightning), ==, 0);

    /* invalid strikes are ignored */
    lrg_lightning_strike_at (lightning, -1.0f, 0.0f);
    lrg_lightning_strike_at (lightning, 10.0f, NAN);
    g_assert_cmpuint (lrg_lightning_get_pending_thunder_count (lightning), ==, 0);
}

/* ---------------------------------------------------------------------- */
/* Weather field                                                          */
/* ---------------------------------------------------------------------- */

static void
assert_fields_equal (LrgWeatherField *a,
                     LrgWeatherField *b)
{
    guint x;
    guint y;

    for (y = 0; y < lrg_weather_field_get_height (a); y++)
        for (x = 0; x < lrg_weather_field_get_width (a); x++)
        {
            g_assert_cmpfloat (lrg_weather_field_get_moisture (a, x + 0.5, y + 0.5), ==,
                               lrg_weather_field_get_moisture (b, x + 0.5, y + 0.5));
            g_assert_cmpfloat (lrg_weather_field_get_storm (a, x + 0.5, y + 0.5), ==,
                               lrg_weather_field_get_storm (b, x + 0.5, y + 0.5));
        }
}

static void
test_field_determinism (void)
{
    g_autoptr(LrgWeatherField) a = lrg_weather_field_new (32, 16, 1234);
    g_autoptr(LrgWeatherField) b = lrg_weather_field_new (32, 16, 1234);
    g_autoptr(LrgWeatherField) c = lrg_weather_field_new (32, 16, 99);
    gboolean differs = FALSE;
    guint    i;
    guint    x;

    lrg_weather_field_set_fronts (a, 6.0, 3.0, 0.6);
    lrg_weather_field_set_fronts (b, 6.0, 3.0, 0.6);
    lrg_weather_field_set_fronts (c, 6.0, 3.0, 0.6);
    lrg_weather_field_set_wind (a, 0.7, -0.2);
    lrg_weather_field_set_wind (b, 0.7, -0.2);
    lrg_weather_field_set_wind (c, 0.7, -0.2);

    /* ten days in one call, or in ragged pieces: the same field */
    g_assert_cmpuint (lrg_weather_field_advance (a, 864000.0), ==, 240);
    for (i = 0; i < 240; i++)
    {
        lrg_weather_field_advance (b, 1800.0);
        lrg_weather_field_advance (b, 600.0);
        lrg_weather_field_advance (b, 1200.0);
    }
    lrg_weather_field_advance (c, 864000.0);
    g_assert_cmpuint (lrg_weather_field_get_step_count (a), ==, 240);
    g_assert_cmpuint (lrg_weather_field_get_step_count (b), ==, 240);
    assert_fields_equal (a, b);

    /* another seed forms other fronts */
    for (x = 0; x < 32 && !differs; x++)
        differs = lrg_weather_field_get_moisture (a, x + 0.5, 8.0) !=
                  lrg_weather_field_get_moisture (c, x + 0.5, 8.0);
    g_assert_true (differs);

    /* invalid time does nothing */
    g_assert_cmpuint (lrg_weather_field_advance (a, -5.0), ==, 0);
    g_assert_cmpuint (lrg_weather_field_advance (a, NAN), ==, 0);
    g_assert_null (lrg_weather_field_new (0, 4, 1));
    g_assert_null (lrg_weather_field_new (4, LRG_WEATHER_FIELD_MAX_SIDE + 1, 1));
}

static void
test_field_advection (void)
{
    g_autoptr(LrgWeatherField) f = lrg_weather_field_new (40, 5, 7);
    guint x;
    guint best = 0;
    gfloat peak = -1.0f;

    /* no relaxation, no fronts: a wet column carried east at 1 cell an
     * hour sits 5 cells further after 5 hours */
    lrg_weather_field_set_relaxation (f, 0.0);
    lrg_weather_field_set_fronts (f, 0.0, 1.0, 0.0);
    lrg_weather_field_set_wind (f, 1.0, 0.0);
    lrg_weather_field_set_cell (f, 10, 2, 1.0f, 0.8f);
    lrg_weather_field_advance (f, 5.0 * 3600.0);
    for (x = 0; x < 40; x++)
    {
        gfloat m = lrg_weather_field_get_moisture (f, x + 0.5, 2.5);

        if (m > peak)
        {
            peak = m;
            best = x;
        }
    }
    g_assert_cmpuint (best, ==, 15);
    g_assert_cmpfloat_with_epsilon (peak, 1.0f, 1e-5f);
    g_assert_cmpfloat_with_epsilon (lrg_weather_field_get_storm (f, 15.5, 2.5), 0.8f, 1e-5f);

    /* relaxation pulls back towards the base; storms fade faster */
    lrg_weather_field_set_wind (f, 0.0, 0.0);
    lrg_weather_field_set_relaxation (f, 0.1);
    lrg_weather_field_set_base (f, 15, 2, 0.2f);
    lrg_weather_field_advance (f, 10.0 * 3600.0);
    g_assert_cmpfloat (lrg_weather_field_get_moisture (f, 15.5, 2.5), <, 0.6f);
    g_assert_cmpfloat (lrg_weather_field_get_moisture (f, 15.5, 2.5), >, 0.2f);
    g_assert_cmpfloat (lrg_weather_field_get_storm (f, 15.5, 2.5), <, 0.8f * 0.15f);
    g_assert_cmpfloat (lrg_weather_field_get_base (f, 15, 2), ==, 0.2f);
    g_assert_cmpfloat (lrg_weather_field_get_base (f, 99, 2), ==, 0.0f);
}

static void
test_field_state (void)
{
    g_autoptr(LrgWeatherField) a = lrg_weather_field_new (12, 6, 5);
    g_autoptr(LrgWeatherField) b = lrg_weather_field_new (12, 6, 1);
    g_autoptr(LrgWeatherField) other = lrg_weather_field_new (6, 6, 5);
    g_autoptr(GVariant) state = NULL;
    g_autoptr(GVariant) bad = NULL;
    g_autoptr(GError) error = NULL;
    GVariantDict dict;
    gdouble wx;

    lrg_weather_field_set_fronts (a, 10.0, 2.0, 0.5);
    lrg_weather_field_set_fronts (b, 10.0, 2.0, 0.5);
    lrg_weather_field_set_wind (a, 0.3, 0.1);
    lrg_weather_field_advance (a, 3.5 * 86400.0 + 100.0);
    state = lrg_weather_field_save_state (a);
    g_assert_true (lrg_weather_field_restore_state (b, state, &error));
    g_assert_no_error (error);
    assert_fields_equal (a, b);
    lrg_weather_field_get_wind (b, &wx, NULL);
    g_assert_cmpfloat (wx, ==, 0.3);
    g_assert_cmpuint (lrg_weather_field_get_seed (b), ==, 5);

    /* both go on the same way (seed, step count and carry restored) */
    lrg_weather_field_advance (a, 2.0 * 86400.0);
    lrg_weather_field_advance (b, 2.0 * 86400.0);
    assert_fields_equal (a, b);

    /* another size is refused and changes nothing */
    g_assert_false (lrg_weather_field_restore_state (other, state, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);

    /* out-of-range cells are refused */
    g_variant_dict_init (&dict, state);
    {
        gdouble cells[72];
        guint   i;

        for (i = 0; i < 72; i++)
            cells[i] = 0.5;
        cells[3] = 4.0;
        g_variant_dict_insert_value (&dict, "moisture",
                                     g_variant_new_fixed_array (G_VARIANT_TYPE_DOUBLE, cells, 72,
                                                                sizeof (gdouble)));
    }
    bad = g_variant_ref_sink (g_variant_dict_end (&dict));
    g_assert_false (lrg_weather_field_restore_state (b, bad, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    assert_fields_equal (a, b);

    g_assert_false (lrg_weather_field_restore_state (b, g_variant_new_int32 (4), &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

/* ---------------------------------------------------------------------- */
/* Snow cover                                                             */
/* ---------------------------------------------------------------------- */

static void
test_snow_accumulation (void)
{
    g_autoptr(LrgSnowCover) snow = lrg_snow_cover_new ();

    g_assert_cmpfloat (lrg_snow_cover_get_level (snow), ==, 0.0);
    /* 3 hours of full snowfall: half of the 6-hour fill */
    lrg_snow_cover_update (snow, 3.0 * 3600.0, 1.0, 0.0);
    g_assert_cmpfloat_with_epsilon (lrg_snow_cover_get_level (snow), 0.5, 1e-9);
    /* half snowfall builds half as fast */
    lrg_snow_cover_update (snow, 3.0 * 3600.0, 0.5, 0.0);
    g_assert_cmpfloat_with_epsilon (lrg_snow_cover_get_level (snow), 0.75, 1e-9);
    /* it never passes 1 */
    lrg_snow_cover_update (snow, 48.0 * 3600.0, 1.0, 0.0);
    g_assert_cmpfloat (lrg_snow_cover_get_level (snow), ==, 1.0);
    /* 6 hours of full warmth melt half of it (12-hour melt) */
    lrg_snow_cover_update (snow, 6.0 * 3600.0, 0.0, 1.0);
    g_assert_cmpfloat_with_epsilon (lrg_snow_cover_get_level (snow), 0.5, 1e-9);
    /* snowing into warm air nets out */
    lrg_snow_cover_set_rates (snow, 0.1, 0.1);
    lrg_snow_cover_update (snow, 5.0 * 3600.0, 1.0, 1.0);
    g_assert_cmpfloat_with_epsilon (lrg_snow_cover_get_level (snow), 0.5, 1e-9);
    /* melting past bare stops at 0 */
    lrg_snow_cover_update (snow, 100.0 * 3600.0, 0.0, 1.0);
    g_assert_cmpfloat (lrg_snow_cover_get_level (snow), ==, 0.0);
    /* invalid input is ignored */
    lrg_snow_cover_update (snow, NAN, 1.0, 0.0);
    lrg_snow_cover_update (snow, -3600.0, 1.0, 0.0);
    g_assert_cmpfloat (lrg_snow_cover_get_level (snow), ==, 0.0);
    lrg_snow_cover_set_level (snow, 7.0);
    g_assert_cmpfloat (lrg_snow_cover_get_level (snow), ==, 1.0);
}

static void
test_snow_tracks (void)
{
    g_autoptr(LrgSnowCover) snow = lrg_snow_cover_new ();
    gdouble depth;
    guint   i;

    /* no tracks on bare ground */
    g_assert_false (lrg_snow_cover_stamp (snow, 0.0, 0.0, 0.3, 1.0));
    lrg_snow_cover_set_level (snow, 0.8);

    g_assert_true (lrg_snow_cover_stamp (snow, 10.0, 5.0, 0.4, 1.0));
    g_assert_cmpuint (lrg_snow_cover_get_track_count (snow), ==, 1);
    /* the track is bare in its middle, full snow outside its rim */
    g_assert_cmpfloat_with_epsilon (lrg_snow_cover_get_depth_at (snow, 10.0, 5.0), 0.0, 1e-9);
    g_assert_cmpfloat_with_epsilon (lrg_snow_cover_get_depth_at (snow, 11.0, 5.0), 0.8, 1e-9);
    depth = lrg_snow_cover_get_depth_at (snow, 10.3, 5.0);
    g_assert_cmpfloat (depth, >, 0.0);
    g_assert_cmpfloat (depth, <, 0.8);

    /* standing still deepens, a step adds one */
    g_assert_true (lrg_snow_cover_stamp (snow, 10.05, 5.0, 0.4, 0.5));
    g_assert_cmpuint (lrg_snow_cover_get_track_count (snow), ==, 1);
    g_assert_true (lrg_snow_cover_stamp (snow, 10.6, 5.0, 0.4, 0.6));
    g_assert_cmpuint (lrg_snow_cover_get_track_count (snow), ==, 2);
    g_assert_false (lrg_snow_cover_stamp (snow, 1.0, 1.0, -0.4, 0.6));

    /* fresh snow fills them: 0.6 depth at 4 x (1/6) an hour lasts less
     * than an hour, the full one about 1.5 hours */
    lrg_snow_cover_update (snow, 3600.0, 1.0, 0.0);
    g_assert_cmpuint (lrg_snow_cover_get_track_count (snow), ==, 1);
    g_assert_true (lrg_snow_cover_get_track (snow, 0, NULL, NULL, NULL, &depth));
    g_assert_cmpfloat_with_epsilon (depth, 1.0 - 4.0 / 6.0, 1e-9);
    lrg_snow_cover_update (snow, 3600.0, 1.0, 0.0);
    g_assert_cmpuint (lrg_snow_cover_get_track_count (snow), ==, 0);

    /* the list keeps the newest LRG_SNOW_COVER_MAX_TRACKS */
    for (i = 0; i < LRG_SNOW_COVER_MAX_TRACKS + 10; i++)
        lrg_snow_cover_stamp (snow, (gdouble)i, 0.0, 0.3, 1.0);
    g_assert_cmpuint (lrg_snow_cover_get_track_count (snow), ==, LRG_SNOW_COVER_MAX_TRACKS);
    {
        gdouble x;

        g_assert_true (lrg_snow_cover_get_track (snow, 0, &x, NULL, NULL, NULL));
        g_assert_cmpfloat (x, ==, 10.0);
    }
    g_assert_false (lrg_snow_cover_get_track (snow, LRG_SNOW_COVER_MAX_TRACKS, NULL, NULL, NULL,
                                              NULL));

    /* melting bare drops every track */
    lrg_snow_cover_set_level (snow, 0.0);
    g_assert_cmpuint (lrg_snow_cover_get_track_count (snow), ==, 0);
}

/* ---------------------------------------------------------------------- */
/* Clouds                                                                 */
/* ---------------------------------------------------------------------- */

static void
test_clouds (void)
{
    g_autoptr(LrgCloudLayer) clouds = lrg_cloud_layer_new (3);
    g_autoptr(LrgCloudLayer) same = lrg_cloud_layer_new (3);
    g_autoptr(GrlImage) image = NULL;
    gfloat   noon[3] = { 0.0f, 1.0f, 0.0f };
    gfloat   night[3] = { 0.0f, -0.5f, 0.86f };
    gfloat   before;
    gfloat   d;
    gdouble  sum = 0.0;
    guint    solid = 0;
    guint    i;

    /* clear: no density anywhere */
    g_assert_cmpfloat (lrg_cloud_layer_sample_density (clouds, 120.0, -40.0), ==, 0.0f);
    g_assert_cmpfloat (lrg_cloud_layer_sample_shadow (clouds, 0.0, 0.0, 0.0, noon), ==, 0.0f);

    /* more coverage, more cloud */
    lrg_cloud_layer_set_coverage (clouds, 0.5f);
    lrg_cloud_layer_set_coverage (same, 0.5f);
    for (i = 0; i < 400; i++)
    {
        d = lrg_cloud_layer_sample_density (clouds, i * 250.0, i * 97.0);
        g_assert_cmpfloat (d, ==, lrg_cloud_layer_sample_density (same, i * 250.0, i * 97.0));
        g_assert_cmpfloat (d, >=, 0.0f);
        g_assert_cmpfloat (d, <=, 1.0f);
        sum += d;
    }
    g_assert_cmpfloat (sum, >, 10.0);
    lrg_cloud_layer_set_coverage (same, 1.0f);
    for (i = 0; i < 400; i++)
        if (lrg_cloud_layer_sample_density (same, i * 250.0, i * 97.0) >= 0.99f)
            solid++;
    g_assert_cmpuint (solid, >, 300);

    /* the wind carries the pattern: what was at x is at x + v t later */
    lrg_cloud_layer_set_wind (clouds, 10.0f, 0.0f);
    before = lrg_cloud_layer_sample_density (clouds, 1000.0, 2000.0);
    lrg_cloud_layer_advance (clouds, 60.0);
    g_assert_cmpfloat_with_epsilon (lrg_cloud_layer_sample_density (clouds, 1600.0, 2000.0),
                                    before, 1e-5f);
    lrg_cloud_layer_advance (clouds, NAN);
    g_assert_cmpfloat_with_epsilon (lrg_cloud_layer_sample_density (clouds, 1600.0, 2000.0),
                                    before, 1e-5f);

    /* shadows: the density overhead at noon, scaled; none at night */
    lrg_cloud_layer_set_shadow_strength (clouds, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_cloud_layer_sample_shadow (clouds, 1600.0, 0.0, 2000.0,
                                                                   noon),
                                    before * 0.5f, 1e-5f);
    g_assert_cmpfloat (lrg_cloud_layer_sample_shadow (clouds, 1600.0, 0.0, 2000.0, night), ==,
                       0.0f);

    /* the baked light map */
    image = lrg_cloud_layer_bake_shadow_image (clouds, 0.0, 0.0, 4000.0, 32, noon);
    g_assert_nonnull (image);
    g_assert_cmpint (grl_image_get_width (image), ==, 32);
    g_assert_null (lrg_cloud_layer_bake_shadow_image (clouds, 0.0, 0.0, 4000.0, 2, noon));
    g_assert_null (lrg_cloud_layer_bake_shadow_image (clouds, 0.0, 0.0, -1.0, 32, noon));
}

static void
test_ground_overlay (void)
{
    g_autoptr(LrgGroundOverlay) overlay = lrg_ground_overlay_new ();
    g_autoptr(LrgSnowCover) snow = lrg_snow_cover_new ();
    g_autoptr(LrgCloudLayer) clouds = lrg_cloud_layer_new (1);
    gfloat noon[3] = { 0.0f, 1.0f, 0.0f };
    gfloat night[3] = { 0.0f, -1.0f, 0.0f };

    /* nothing to draw until there is snow or a cloud shadow */
    g_assert_false (lrg_ground_overlay_is_visible (overlay));
    lrg_ground_overlay_set_snow_cover (overlay, snow);
    g_assert_false (lrg_ground_overlay_is_visible (overlay));
    lrg_snow_cover_set_level (snow, 0.3);
    g_assert_true (lrg_ground_overlay_is_visible (overlay));
    lrg_snow_cover_set_level (snow, 0.0);

    lrg_ground_overlay_set_cloud_layer (overlay, clouds);
    lrg_ground_overlay_set_sun_direction (overlay, noon);
    g_assert_false (lrg_ground_overlay_is_visible (overlay));
    lrg_cloud_layer_set_coverage (clouds, 0.6f);
    g_assert_true (lrg_ground_overlay_is_visible (overlay));
    lrg_ground_overlay_set_sun_direction (overlay, night);
    g_assert_false (lrg_ground_overlay_is_visible (overlay));
}

/* ---------------------------------------------------------------------- */
/* Billboard sway and sky bodies                                          */
/* ---------------------------------------------------------------------- */

static void
test_billboard_sway (void)
{
    g_autoptr(LrgBillboardBatch) batch = lrg_billboard_batch_new ();
    GrlVector3 pos = { 0.0f, 0.0f, 0.0f };
    GrlVector3 cam = { 0.0f, 1.0f, 10.0f };
    GrlVector3 still[4];
    GrlVector3 moved[4];
    guint      index;

    index = lrg_billboard_batch_add (batch, 0, &pos, 2.0f, 4.0f);
    lrg_billboard_batch_set_wind (batch, 3.0f, 0.0f, 0.05f, 0.0f, 1.0f);
    lrg_billboard_batch_set_wind_time (batch, 0.0);
    g_assert_true (lrg_billboard_batch_get_corners (batch, index, &cam, NULL, NULL, still));

    /* rigid by default: the wind moves nothing */
    lrg_billboard_batch_set_sway (batch, index, 1.0f);
    g_assert_cmpfloat (lrg_billboard_batch_get_sway (batch, index), ==, 1.0f);
    g_assert_true (lrg_billboard_batch_get_corners (batch, index, &cam, NULL, NULL, moved));

    /* the base stays, the top leans downwind (+X) by lean x height */
    g_assert_cmpfloat (moved[0].x, ==, still[0].x);
    g_assert_cmpfloat (moved[1].x, ==, still[1].x);
    g_assert_cmpfloat_with_epsilon (moved[2].x - still[2].x, 0.2f, 1e-5f);
    g_assert_cmpfloat_with_epsilon (moved[3].x - still[3].x, 0.2f, 1e-5f);
    g_assert_cmpfloat (moved[2].y, ==, still[2].y);

    /* half the weight, half the bend */
    lrg_billboard_batch_set_sway (batch, index, 0.5f);
    g_assert_true (lrg_billboard_batch_get_corners (batch, index, &cam, NULL, NULL, moved));
    g_assert_cmpfloat_with_epsilon (moved[2].x - still[2].x, 0.2f * 128.0f / 255.0f, 1e-5f);
    lrg_billboard_batch_set_sway (batch, index, NAN);
    g_assert_cmpfloat (lrg_billboard_batch_get_sway (batch, index), ==, 0.5f);
}

static void
test_sky_bodies (void)
{
    g_autoptr(LrgSkyBodies) bodies = lrg_sky_bodies_new ();
    GrlVector3 up = { 0.0f, 2.0f, 0.0f };
    GrlVector3 zero = { 0.0f, 0.0f, 0.0f };
    GrlVector3 dir;
    GrlColor   white = { 255, 255, 255, 255 };
    gdouble    phase;
    guint      visible;

    g_assert_false (lrg_sky_bodies_get_moon (bodies, 0, NULL, NULL));
    lrg_sky_bodies_set_moon (bodies, 1, &up, 1.5f, &white, 1.25);
    g_assert_true (lrg_sky_bodies_get_moon (bodies, 1, &phase, &dir));
    g_assert_cmpfloat_with_epsilon (phase, 0.25, 1e-12);
    g_assert_cmpfloat_with_epsilon (dir.y, 1.0f, 1e-6f);
    /* invalid moons are not shown */
    lrg_sky_bodies_set_moon (bodies, 0, &zero, 1.5f, &white, 0.5);
    g_assert_false (lrg_sky_bodies_get_moon (bodies, 0, NULL, NULL));
    lrg_sky_bodies_set_moon (bodies, LRG_SKY_BODIES_MAX_MOONS, &up, 1.5f, &white, 0.5);
    lrg_sky_bodies_hide_moon (bodies, 1);
    g_assert_false (lrg_sky_bodies_get_moon (bodies, 1, NULL, NULL));

    lrg_sky_bodies_set_stars (bodies, 500, 9);
    g_assert_cmpuint (lrg_sky_bodies_get_star_count (bodies), ==, 500);
    g_assert_cmpuint (lrg_sky_bodies_count_visible_stars (bodies), ==, 0);
    lrg_sky_bodies_set_star_visibility (bodies, 1.0f);
    visible = lrg_sky_bodies_count_visible_stars (bodies);
    g_assert_cmpuint (visible, >, 150);
    g_assert_cmpuint (visible, <, 350);

    /* turning the sky changes which stars are up, not their number much */
    lrg_sky_bodies_set_star_rotation (bodies, 2.0, 30.0);
    g_assert_true (lrg_sky_bodies_get_star (bodies, 0, &dir, NULL));
    g_assert_cmpfloat_with_epsilon (sqrtf (dir.x * dir.x + dir.y * dir.y + dir.z * dir.z), 1.0f,
                                    1e-5f);
    g_assert_false (lrg_sky_bodies_get_star (bodies, 500, &dir, NULL));
    lrg_sky_bodies_set_stars (bodies, LRG_SKY_BODIES_MAX_STARS + 5, 9);
    g_assert_cmpuint (lrg_sky_bodies_get_star_count (bodies), ==, LRG_SKY_BODIES_MAX_STARS);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/sky-weather/celestial/moon-phase", test_moon_phase);
    g_test_add_func ("/sky-weather/celestial/moon-disc", test_moon_disc);
    g_test_add_func ("/sky-weather/celestial/orbit", test_orbit);
    g_test_add_func ("/sky-weather/celestial/stars", test_stars);
    g_test_add_func ("/sky-weather/celestial/thunder-delay", test_thunder_delay);
    g_test_add_func ("/sky-weather/lightning/distance", test_lightning_distance);
    g_test_add_func ("/sky-weather/field/determinism", test_field_determinism);
    g_test_add_func ("/sky-weather/field/advection", test_field_advection);
    g_test_add_func ("/sky-weather/field/state", test_field_state);
    g_test_add_func ("/sky-weather/snow/accumulation", test_snow_accumulation);
    g_test_add_func ("/sky-weather/snow/tracks", test_snow_tracks);
    g_test_add_func ("/sky-weather/clouds/density", test_clouds);
    g_test_add_func ("/sky-weather/clouds/ground-overlay", test_ground_overlay);
    g_test_add_func ("/sky-weather/billboard/sway", test_billboard_sway);
    g_test_add_func ("/sky-weather/sky-bodies", test_sky_bodies);

    return g_test_run ();
}
