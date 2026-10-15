/* test-sky-dome.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgSkyDome. Keyframe interpolation, the sun's daily
 * arc, the gradient, dome geometry and band mapping are headless;
 * drawing runs in a hidden window (with a pixel read-back) when a
 * display exists.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#define EPS (1e-4f)

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
    test_window = grl_window_new (8, 8, "lrg-sky-dome-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

static void
assert_color (const GrlColor *c,
              guint8          r,
              guint8          g,
              guint8          b,
              guint8          a)
{
    g_assert_cmpuint (c->r, ==, r);
    g_assert_cmpuint (c->g, ==, g);
    g_assert_cmpuint (c->b, ==, b);
    g_assert_cmpuint (c->a, ==, a);
}

static void
test_sky_dome_defaults (void)
{
    g_autoptr(LrgSkyDome) sky = lrg_sky_dome_new ();
    GrlColor   c;
    GrlVector3 sun;

    g_assert_cmpuint (lrg_sky_dome_get_keyframe_count (sky), ==, 4);
    g_assert_cmpfloat (lrg_sky_dome_get_time_of_day (sky), ==, 12.0f);
    g_assert_cmpfloat (lrg_sky_dome_get_radius (sky), ==, LRG_SKY_DOME_DEFAULT_RADIUS);

    /* noon keyframe */
    lrg_sky_dome_get_zenith_color (sky, &c);
    assert_color (&c, 0x3C, 0x78, 0xDC, 0xFF);
    lrg_sky_dome_get_horizon_color (sky, &c);
    assert_color (&c, 0xAA, 0xCD, 0xF0, 0xFF);
    lrg_sky_dome_get_ground_color (sky, &c);
    assert_color (&c, 0xAA, 0xCD, 0xF0, 0xFF);

    /* highest at noon, leaning 30 degrees towards +Z */
    lrg_sky_dome_get_sun_direction (sky, &sun);
    g_assert_cmpfloat_with_epsilon (sun.x, 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (sun.y, cosf (G_PI / 6.0), EPS);
    g_assert_cmpfloat_with_epsilon (sun.z, sinf (G_PI / 6.0), EPS);
}

/* Custom keyframes: linear in between, cyclic across midnight, wrapping
 * of out-of-range times, replacement of equal times. */
static void
test_sky_dome_keyframes (void)
{
    g_autoptr(LrgSkyDome) sky = lrg_sky_dome_new ();
    GrlColor black = { 0, 0, 0, 255 };
    GrlColor white = { 200, 100, 50, 255 };
    GrlColor half  = { 100, 50, 25, 0 };
    GrlColor c;

    lrg_sky_dome_clear_keyframes (sky);
    g_assert_cmpuint (lrg_sky_dome_get_keyframe_count (sky), ==, 0);

    /* no keyframes: colours stay */
    lrg_sky_dome_set_zenith_color (sky, &white);
    lrg_sky_dome_set_time_of_day (sky, 3.0f);
    lrg_sky_dome_get_zenith_color (sky, &c);
    assert_color (&c, 200, 100, 50, 255);

    /* one keyframe: constant */
    lrg_sky_dome_add_keyframe (sky, 20.0f, &black, &black, &half);
    lrg_sky_dome_set_time_of_day (sky, 7.0f);
    lrg_sky_dome_get_zenith_color (sky, &c);
    assert_color (&c, 0, 0, 0, 255);
    lrg_sky_dome_get_sun_color (sky, &c);
    assert_color (&c, 100, 50, 25, 0);

    /* 20:00 black, 8:00 white: 14:00 halfway, 2:00 halfway across midnight */
    lrg_sky_dome_add_keyframe (sky, 8.0f, &white, &white, &white);
    g_assert_cmpuint (lrg_sky_dome_get_keyframe_count (sky), ==, 2);
    lrg_sky_dome_set_time_of_day (sky, 14.0f);
    lrg_sky_dome_get_zenith_color (sky, &c);
    assert_color (&c, 100, 50, 25, 255);
    lrg_sky_dome_set_time_of_day (sky, 2.0f);
    lrg_sky_dome_get_horizon_color (sky, &c);
    assert_color (&c, 100, 50, 25, 255);
    /* 11:00 = 3 h after white over a 12 h span */
    lrg_sky_dome_set_time_of_day (sky, 11.0f);
    lrg_sky_dome_get_zenith_color (sky, &c);
    assert_color (&c, 150, 75, 38, 255);

    /* -13 and 35 wrap to 11 */
    lrg_sky_dome_set_time_of_day (sky, -13.0f);
    g_assert_cmpfloat_with_epsilon (lrg_sky_dome_get_time_of_day (sky), 11.0f, EPS);
    lrg_sky_dome_set_time_of_day (sky, 35.0f);
    g_assert_cmpfloat_with_epsilon (lrg_sky_dome_get_time_of_day (sky), 11.0f, EPS);
    lrg_sky_dome_get_zenith_color (sky, &c);
    assert_color (&c, 150, 75, 38, 255);

    /* exactly on a keyframe */
    lrg_sky_dome_set_time_of_day (sky, 20.0f);
    lrg_sky_dome_get_zenith_color (sky, &c);
    assert_color (&c, 0, 0, 0, 255);

    /* same time replaces */
    lrg_sky_dome_add_keyframe (sky, 8.0f, &black, &black, &black);
    g_assert_cmpuint (lrg_sky_dome_get_keyframe_count (sky), ==, 2);
}

static void
test_sky_dome_sun_arc (void)
{
    g_autoptr(LrgSkyDome) sky = lrg_sky_dome_new ();
    GrlVector3 sun;
    GrlVector3 dir = { 0.0f, 0.0f, 0.0f };

    lrg_sky_dome_set_time_of_day (sky, 6.0f);
    lrg_sky_dome_get_sun_direction (sky, &sun);
    g_assert_cmpfloat_with_epsilon (sun.x, 1.0f, EPS);
    g_assert_cmpfloat_with_epsilon (sun.y, 0.0f, EPS);

    lrg_sky_dome_set_time_of_day (sky, 18.0f);
    lrg_sky_dome_get_sun_direction (sky, &sun);
    g_assert_cmpfloat_with_epsilon (sun.x, -1.0f, EPS);

    lrg_sky_dome_set_sun_tilt (sky, 0.0f);
    lrg_sky_dome_set_time_of_day (sky, 0.0f);
    lrg_sky_dome_get_sun_direction (sky, &sun);
    g_assert_cmpfloat_with_epsilon (sun.y, -1.0f, EPS);

    /* explicit direction is normalized; zero is ignored */
    dir.x = 0.0f;
    dir.y = 3.0f;
    dir.z = 4.0f;
    lrg_sky_dome_set_sun_direction (sky, &dir);
    lrg_sky_dome_get_sun_direction (sky, &sun);
    g_assert_cmpfloat_with_epsilon (sun.y, 0.6f, EPS);
    g_assert_cmpfloat_with_epsilon (sun.z, 0.8f, EPS);
    dir.y = 0.0f;
    dir.z = 0.0f;
    lrg_sky_dome_set_sun_direction (sky, &dir);
    lrg_sky_dome_get_sun_direction (sky, &sun);
    g_assert_cmpfloat_with_epsilon (sun.z, 0.8f, EPS);
}

static void
test_sky_dome_gradient (void)
{
    g_autoptr(LrgSkyDome) sky = lrg_sky_dome_new ();
    GrlColor zenith  = { 0, 0, 200, 255 };
    GrlColor horizon = { 200, 200, 200, 255 };
    GrlColor ground  = { 0, 100, 0, 255 };
    GrlVector3 up    = { 0.0f, 5.0f, 0.0f };
    GrlVector3 flat  = { 2.0f, 0.0f, 0.0f };
    GrlVector3 down  = { 0.0f, -1.0f, 0.0f };
    GrlVector3 mid   = { 1.0f, 1.0f, 0.0f };
    GrlColor c;

    lrg_sky_dome_set_zenith_color (sky, &zenith);
    lrg_sky_dome_set_horizon_color (sky, &horizon);

    lrg_sky_dome_get_color_at (sky, &up, &c);
    assert_color (&c, 0, 0, 200, 255);
    lrg_sky_dome_get_color_at (sky, &flat, &c);
    assert_color (&c, 200, 200, 200, 255);
    lrg_sky_dome_get_color_at (sky, &down, &c);
    assert_color (&c, 200, 200, 200, 255);   /* ground follows horizon */
    /* 45 degrees: sin = 0.7071 */
    lrg_sky_dome_get_color_at (sky, &mid, &c);
    assert_color (&c, 59, 59, 200, 255);

    lrg_sky_dome_set_ground_color (sky, &ground);
    lrg_sky_dome_get_color_at (sky, &down, &c);
    assert_color (&c, 0, 100, 0, 255);
    lrg_sky_dome_get_ground_color (sky, &c);
    assert_color (&c, 0, 100, 0, 255);
    lrg_sky_dome_set_ground_color (sky, NULL);
    lrg_sky_dome_get_ground_color (sky, &c);
    assert_color (&c, 200, 200, 200, 255);
}

/* Vertex layout, radius, ring colours and inward-facing winding. */
static void
test_sky_dome_geometry (void)
{
    g_autoptr(LrgSkyDome) sky = lrg_sky_dome_new ();
    GrlColor       zenith  = { 10, 20, 30, 255 };
    GrlColor       horizon = { 40, 50, 60, 255 };
    GrlColor       ground  = { 70, 80, 90, 255 };
    const gfloat  *p;
    const guint8  *col;
    const guint16 *idx;
    guint          n_p;
    guint          n_c;
    guint          n_i;
    guint          cols = 7;
    guint          k;
    guint          checked = 0;

    lrg_sky_dome_set_resolution (sky, 6, 3);
    lrg_sky_dome_set_radius (sky, 10.0f);
    lrg_sky_dome_set_zenith_color (sky, &zenith);
    lrg_sky_dome_set_horizon_color (sky, &horizon);
    lrg_sky_dome_set_ground_color (sky, &ground);

    p = lrg_sky_dome_get_positions (sky, &n_p);
    col = lrg_sky_dome_get_colors (sky, &n_c);
    idx = lrg_sky_dome_get_indices (sky, &n_i);
    g_assert_cmpuint (n_p, ==, 7 * cols * 3);
    g_assert_cmpuint (n_c, ==, 7 * cols * 4);
    g_assert_cmpuint (n_i, ==, 6 * 6 * 6);

    for (k = 0; k < n_p; k += 3)
    {
        gfloat r = sqrtf (p[k] * p[k] + p[k + 1] * p[k + 1] + p[k + 2] * p[k + 2]);

        g_assert_cmpfloat_with_epsilon (r, 10.0f, EPS);
    }

    /* row 0 nadir, row 3 horizon, row 6 zenith; azimuth 0 is -Z and
     * azimuth 90 (column 1.5 of 6) lies towards +X */
    g_assert_cmpfloat_with_epsilon (p[1], -10.0f, EPS);
    g_assert_cmpuint (col[0], ==, 70);
    g_assert_cmpfloat_with_epsilon (p[(3 * cols) * 3 + 1], 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (p[(3 * cols) * 3 + 2], -10.0f, EPS);
    g_assert_cmpfloat (p[(3 * cols + 1) * 3], >, 0.0f);
    g_assert_cmpuint (col[(3 * cols) * 4], ==, 40);
    g_assert_cmpfloat_with_epsilon (p[(6 * cols) * 3 + 1], 10.0f, EPS);
    g_assert_cmpuint (col[(6 * cols) * 4 + 2], ==, 30);
    /* the seam column duplicates column 0 */
    g_assert_cmpfloat_with_epsilon (p[(3 * cols + 6) * 3 + 2], -10.0f, EPS);

    /* every non-degenerate triangle faces the centre */
    for (k = 0; k < n_i; k += 3)
    {
        const gfloat *a = &p[idx[k] * 3];
        const gfloat *b = &p[idx[k + 1] * 3];
        const gfloat *c = &p[idx[k + 2] * 3];
        gfloat e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        gfloat e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        gfloat n[3];
        gfloat len;

        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len < 1e-3f)
            continue;
        g_assert_cmpfloat (n[0] * (a[0] + b[0] + c[0]) + n[1] * (a[1] + b[1] + c[1])
                           + n[2] * (a[2] + b[2] + c[2]), <, 0.0f);
        checked++;
    }
    g_assert_cmpuint (checked, >=, 6 * 6 * 2 - 2 * 6);

    /* time of day rebuilds the colours */
    lrg_sky_dome_set_time_of_day (sky, 12.0f);
    col = lrg_sky_dome_get_colors (sky, NULL);
    g_assert_cmpuint (col[(6 * cols) * 4], ==, 0x3C);
}

static void
test_sky_dome_band (void)
{
    g_autoptr(LrgSkyDome) sky = lrg_sky_dome_new ();
    g_autoptr(GrlTexture) east = g_object_new (GRL_TYPE_TEXTURE, NULL);
    g_autoptr(GrlTexture) west = g_object_new (GRL_TYPE_TEXTURE, NULL);
    guint  index = 99;
    gfloat u = -1.0f;

    g_assert_false (lrg_sky_dome_get_band_uv (sky, 0.0f, &index, &u));

    lrg_sky_dome_set_band_texture (sky, east);
    g_assert_true (lrg_sky_dome_get_band_uv (sky, 90.0f, &index, &u));
    g_assert_cmpuint (index, ==, 0);
    g_assert_cmpfloat_with_epsilon (u, 0.25f, EPS);
    g_assert_true (lrg_sky_dome_get_band_uv (sky, -90.0f, NULL, &u));
    g_assert_cmpfloat_with_epsilon (u, 0.75f, EPS);

    lrg_sky_dome_set_band_heading (sky, 90.0f);
    g_assert_true (lrg_sky_dome_get_band_uv (sky, 90.0f, NULL, &u));
    g_assert_cmpfloat_with_epsilon (u, 0.0f, EPS);
    g_assert_true (lrg_sky_dome_get_band_uv (sky, 45.0f, NULL, &u));
    g_assert_cmpfloat_with_epsilon (u, 0.875f, EPS);

    /* halves: [90, 270) first, [270, 450) second */
    lrg_sky_dome_set_band_halves (sky, east, west);
    g_assert_true (lrg_sky_dome_get_band_uv (sky, 180.0f, &index, &u));
    g_assert_cmpuint (index, ==, 0);
    g_assert_cmpfloat_with_epsilon (u, 0.5f, EPS);
    g_assert_true (lrg_sky_dome_get_band_uv (sky, 0.0f, &index, &u));
    g_assert_cmpuint (index, ==, 1);
    g_assert_cmpfloat_with_epsilon (u, 0.5f, EPS);
    g_assert_true (lrg_sky_dome_get_band_uv (sky, 270.0f, &index, &u));
    g_assert_cmpuint (index, ==, 1);
    g_assert_cmpfloat_with_epsilon (u, 0.0f, EPS);

    /* a single texture replaces the halves; NULL removes the band */
    lrg_sky_dome_set_band_texture (sky, west);
    g_assert_true (lrg_sky_dome_get_band_uv (sky, 0.0f, &index, &u));
    g_assert_cmpuint (index, ==, 0);
    g_assert_cmpfloat_with_epsilon (u, 0.75f, EPS);
    lrg_sky_dome_set_band_texture (sky, NULL);
    g_assert_false (lrg_sky_dome_get_band_uv (sky, 0.0f, NULL, NULL));
}

/* render_pixel:
 * Draws the sky into a 32x32 target looking from the origin towards
 * @target and returns the centre pixel. */
static Color
render_pixel (LrgSkyDome *sky,
              Vector3     target,
              Vector3     up)
{
    RenderTexture2D rt = LoadRenderTexture (32, 32);
    GrlVector3      eye = { 0.0f, 0.0f, 0.0f };
    Camera3D        camera;
    Image           shot;
    Color           centre;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 0, 0, 0 };
    camera.target = target;
    camera.up = up;
    camera.fovy = 30.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode (rt);
    ClearBackground (BLACK);
    BeginMode3D (camera);
    lrg_sky_dome_draw (sky, &eye);
    EndMode3D ();
    EndTextureMode ();

    shot = LoadImageFromTexture (rt.texture);
    centre = GetImageColor (shot, 16, 16);
    UnloadImage (shot);
    UnloadRenderTexture (rt);
    return centre;
}

static void
test_sky_dome_draw (void)
{
    g_autoptr(LrgSkyDome) sky = NULL;
    g_autoptr(GrlImage)   red_image = NULL;
    g_autoptr(GrlImage)   green_image = NULL;
    g_autoptr(GrlTexture) red = NULL;
    g_autoptr(GrlTexture) green = NULL;
    GrlColor   zenith = { 0, 0, 255, 255 };
    GrlColor   off = { 0, 0, 0, 0 };
    Color      px;

    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return;
    }

    red_image = grl_image_new_color (4, 4, &(GrlColor){ 255, 0, 0, 255 });
    green_image = grl_image_new_color (4, 4, &(GrlColor){ 0, 255, 0, 255 });
    red = grl_texture_new_from_image (red_image);
    green = grl_texture_new_from_image (green_image);

    sky = lrg_sky_dome_new ();
    lrg_sky_dome_set_zenith_color (sky, &zenith);
    lrg_sky_dome_set_sun_color (sky, &off);

    /* straight up: the zenith colour */
    px = render_pixel (sky, (Vector3){ 0, 1, 0 }, (Vector3){ 0, 0, -1 });
    g_assert_cmpuint (px.b, >, 200);
    g_assert_cmpuint (px.r, <, 60);

    /* halves: first from azimuth 0 (-Z) to 180, second the rest */
    lrg_sky_dome_set_band_halves (sky, red, green);
    px = render_pixel (sky, (Vector3){ 1, 0.2f, 0 }, (Vector3){ 0, 1, 0 });
    g_assert_cmpuint (px.r, >, 200);
    g_assert_cmpuint (px.g, <, 60);
    px = render_pixel (sky, (Vector3){ -1, 0.2f, 0 }, (Vector3){ 0, 1, 0 });
    g_assert_cmpuint (px.g, >, 200);
    g_assert_cmpuint (px.r, <, 60);

    /* the sun: looking at it shows the sun colour over the band */
    lrg_sky_dome_set_band_texture (sky, NULL);
    lrg_sky_dome_set_sun_color (sky, &(GrlColor){ 255, 255, 0, 255 });
    lrg_sky_dome_set_sun_direction (sky, &(GrlVector3){ 0, 1, -1 });
    px = render_pixel (sky, (Vector3){ 0, 1, -1 }, (Vector3){ 0, 1, 0 });
    g_assert_cmpuint (px.r, >, 200);
    g_assert_cmpuint (px.g, >, 200);
    g_assert_cmpuint (px.b, <, 60);

    /* drawing restored the depth state for the world */
    BeginDrawing ();
    ClearBackground (BLACK);
    EndDrawing ();
}

int
main (int   argc,
      char *argv[])
{
    gint result;

    g_test_init (&argc, &argv, NULL);

    graphics_available = init_graphics_context ();

    g_test_add_func ("/sky-dome/defaults", test_sky_dome_defaults);
    g_test_add_func ("/sky-dome/keyframes", test_sky_dome_keyframes);
    g_test_add_func ("/sky-dome/sun-arc", test_sky_dome_sun_arc);
    g_test_add_func ("/sky-dome/gradient", test_sky_dome_gradient);
    g_test_add_func ("/sky-dome/geometry", test_sky_dome_geometry);
    g_test_add_func ("/sky-dome/band", test_sky_dome_band);
    g_test_add_func ("/sky-dome/draw", test_sky_dome_draw);

    result = g_test_run ();

    g_clear_object (&test_window);
    return result;
}
