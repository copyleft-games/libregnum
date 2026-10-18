/* test-light-set.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgLightSet. Light bookkeeping and nearest-N selection
 * are headless; the shader is exercised in a hidden window with
 * LrgStaticBatch and LrgTerrain, reading pixels back to check ambient,
 * point, directional light, the floating origin and the alpha cutout.
 */

#include <glib.h>
#include <math.h>
#include <stdlib.h>
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
    test_window = grl_window_new (8, 8, "lrg-light-set-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

static const GrlColor white = { 255, 255, 255, 255 };

static void
test_light_set_new (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();

    g_assert_nonnull (lights);
    g_assert_cmpuint (lrg_light_set_get_point_light_count (lights), ==, 0);
    g_assert_cmpuint (lrg_light_set_get_active_count (lights), ==, 0);
    g_assert_cmpuint (lrg_light_set_select (lights, 0, 0, 0), ==, 0);
    g_assert_cmpuint (lrg_light_set_get_active_light (lights, 0), ==, LRG_LIGHT_SET_INVALID_LIGHT);

    if (!graphics_available)
    {
        /* no context: no shader, apply only selects */
        g_assert_null (lrg_light_set_get_shader (lights));
        g_assert_false (lrg_light_set_apply (lights, 0, 0, 0));
    }
}

static void
test_light_set_ids (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    guint a;
    guint b;
    guint c;

    a = lrg_light_set_add_point_light (lights, 0, 0, 0, &white, 1);
    b = lrg_light_set_add_point_light (lights, 1, 0, 0, &white, 1);
    g_assert_cmpuint (a, ==, 0);
    g_assert_cmpuint (b, ==, 1);
    g_assert_cmpuint (lrg_light_set_get_point_light_count (lights), ==, 2);

    g_assert_true (lrg_light_set_remove_point_light (lights, a));
    g_assert_false (lrg_light_set_remove_point_light (lights, a));
    g_assert_false (lrg_light_set_remove_point_light (lights, 42));
    g_assert_cmpuint (lrg_light_set_get_point_light_count (lights), ==, 1);

    /* the freed slot is reused */
    c = lrg_light_set_add_point_light (lights, 2, 0, 0, &white, 1);
    g_assert_cmpuint (c, ==, a);
    g_assert_cmpuint (lrg_light_set_get_point_light_count (lights), ==, 2);

    lrg_light_set_set_point_light_position (lights, c, 5, 5, 5);
    lrg_light_set_set_point_light_color (lights, c, &white);
    lrg_light_set_set_point_light_radius (lights, c, 3);
    lrg_light_set_set_point_light_intensity (lights, c, 2);
    lrg_light_set_set_point_light_enabled (lights, c, TRUE);

    lrg_light_set_clear_point_lights (lights);
    g_assert_cmpuint (lrg_light_set_get_point_light_count (lights), ==, 0);
}

/* Twenty lights on the X axis, 10 apart, radius 1. */
static LrgLightSet *
row_of_lights (void)
{
    LrgLightSet *lights = lrg_light_set_new ();
    guint        i;

    for (i = 0; i < 20; i++)
        lrg_light_set_add_point_light (lights, i * 10.0, 0, 0, &white, 1);
    return lights;
}

static void
assert_active (LrgLightSet *lights,
               const guint *expected,
               guint        n)
{
    guint i;

    g_assert_cmpuint (lrg_light_set_get_active_count (lights), ==, n);
    for (i = 0; i < n; i++)
        g_assert_cmpuint (lrg_light_set_get_active_light (lights, i), ==, expected[i]);
}

static void
test_light_set_select (void)
{
    g_autoptr(LrgLightSet) lights = row_of_lights ();
    static const guint first16[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    static const guint middle[3] = { 9, 10, 8 };
    static const guint far_end[3] = { 19, 18, 17 };
    static const guint big[3] = { 0, 9, 10 };
    static const guint near_only[3] = { 0, 9, 10 };
    static const guint only_big[1] = { 0 };

    /* default: the 16 nearest, nearest first */
    g_assert_cmpuint (lrg_light_set_select (lights, -5, 0, 0), ==, 16);
    assert_active (lights, first16, 16);

    /* ties (lights 9 and 10, then 8 and 11) go to the lower id */
    lrg_light_set_set_max_active (lights, 3);
    lrg_light_set_select (lights, 95, 0, 0);
    assert_active (lights, middle, 3);

    lrg_light_set_select (lights, 1000, 0, 0);
    assert_active (lights, far_end, 3);

    /* disabled and zero-intensity lights are skipped */
    lrg_light_set_set_point_light_enabled (lights, 9, FALSE);
    lrg_light_set_set_point_light_intensity (lights, 10, 0);
    lrg_light_set_select (lights, 95, 0, 0);
    g_assert_cmpuint (lrg_light_set_get_active_light (lights, 0), ==, 8);
    g_assert_cmpuint (lrg_light_set_get_active_light (lights, 1), ==, 11);
    lrg_light_set_set_point_light_enabled (lights, 9, TRUE);
    lrg_light_set_set_point_light_intensity (lights, 10, 1);

    /* a big light far away beats small lights near by: distance to its
     * sphere is negative */
    lrg_light_set_set_point_light_radius (lights, 0, 200);
    lrg_light_set_select (lights, 95, 0, 0);
    assert_active (lights, big, 3);

    /* max distance culls everything beyond the spheres' reach: lights 9
     * and 10 are 5 away (4 from their spheres), 8 and 11 are 14 */
    lrg_light_set_set_max_distance (lights, 4.5f);
    lrg_light_set_select (lights, 95, 0, 0);
    assert_active (lights, near_only, 3);
    lrg_light_set_set_max_distance (lights, 3.0f);
    lrg_light_set_select (lights, 95, 0, 0);
    assert_active (lights, only_big, 1);

    /* removing a light drops the selection */
    lrg_light_set_remove_point_light (lights, 5);
    g_assert_cmpuint (lrg_light_set_get_active_count (lights), ==, 0);

    lrg_light_set_set_max_active (lights, 0);
    g_assert_cmpuint (lrg_light_set_select (lights, 95, 0, 0), ==, 0);
}

/* ------------------------------------------------------------------------ */
/* GL                                                                       */
/* ------------------------------------------------------------------------ */

/* floor_batch:
 * A 20 x 20 quad at y = 0 facing up, centred on (cx, 0, cz), white. */
static LrgStaticBatch *
floor_batch (gfloat cx,
             gfloat cz)
{
    LrgStaticBatch *batch = lrg_static_batch_new ();
    gfloat positions[12] = {
        cx - 10, 0, cz - 10,
        cx + 10, 0, cz - 10,
        cx - 10, 0, cz + 10,
        cx + 10, 0, cz + 10
    };
    gfloat normals[12] = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
    gfloat texcoords[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    guint32 indices[6] = { 0, 2, 1, 1, 2, 3 };

    g_assert_true (lrg_static_batch_add_mesh (batch, positions, normals, texcoords, NULL, 4,
                                              indices, 6, NULL, 0, 0, NULL));
    g_assert_true (lrg_static_batch_upload (batch, NULL));
    return batch;
}

/* render_centre:
 * Clears to blue, looks straight down at (0, 0, 0) from above and draws
 * either @batch or @terrain; returns the centre pixel. */
static Color
render_centre (LrgStaticBatch *batch,
               LrgTerrain     *terrain)
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
    if (batch != NULL)
        lrg_static_batch_draw (batch, LRG_STATIC_BATCH_ALL_LAYERS);
    if (terrain != NULL)
        lrg_terrain_draw (terrain);
    EndMode3D ();
    EndTextureMode ();

    shot = LoadImageFromTexture (rt.texture);
    centre = GetImageColor (shot, 8, 8);
    UnloadImage (shot);
    UnloadRenderTexture (rt);
    return centre;
}

static void
test_light_set_shader_static_batch (void)
{
    g_autoptr(LrgLightSet)    lights = NULL;
    g_autoptr(LrgStaticBatch) batch = NULL;
    g_autoptr(GrlImage)       clear_image = NULL;
    g_autoptr(GrlTexture)     clear = NULL;
    GrlShader *shader;
    GrlColor   black = { 0, 0, 0, 255 };
    GrlColor   red = { 255, 0, 0, 255 };
    GrlColor   grey = { 128, 128, 128, 255 };
    GrlVector3 down = { 0, -1, 0 };
    guint      lamp;
    Color      px;

    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return;
    }

    lights = lrg_light_set_new ();
    shader = lrg_light_set_get_shader (lights);
    if (shader == NULL)
    {
        g_test_skip ("GLSL 330 unavailable");
        return;
    }
    g_assert_true (lrg_light_set_get_shader (lights) == shader);

    batch = floor_batch (0, 0);
    lrg_static_batch_set_shader (batch, shader);

    /* ambient only */
    lrg_light_set_set_ambient (lights, &black);
    g_assert_true (lrg_light_set_apply (lights, 0, 10, 0));
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, <, 10);
    g_assert_cmpuint (px.b, <, 10);   /* the floor, not the clear colour */

    lrg_light_set_set_ambient (lights, &grey);
    lrg_light_set_apply (lights, 0, 10, 0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, >, 110);
    g_assert_cmpuint (px.r, <, 145);

    /* a red point light right above the centre */
    lrg_light_set_set_ambient (lights, &black);
    lamp = lrg_light_set_add_point_light (lights, 0, 1, 0, &red, 5);
    lrg_light_set_apply (lights, 0, 10, 0);
    g_assert_cmpuint (lrg_light_set_get_active_count (lights), ==, 1);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, >, 100);
    g_assert_cmpuint (px.g, <, 10);

    /* out of reach: dark again */
    lrg_light_set_set_point_light_position (lights, lamp, 0, 10, 0);
    lrg_light_set_apply (lights, 0, 10, 0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, <, 10);

    /* directional light straight down: full white */
    lrg_light_set_set_directional (lights, &down, &white);
    lrg_light_set_apply (lights, 0, 10, 0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, >, 245);
    g_assert_cmpuint (px.g, >, 245);
    lrg_light_set_set_directional (lights, NULL, NULL);

    /* alpha cutout: a 40 % opaque texture is discarded below the cutoff
     * (pure clear colour) but blends over it without one */
    clear_image = grl_image_new_color (2, 2, &(GrlColor){ 255, 255, 255, 102 });
    clear = grl_texture_new_from_image (clear_image);
    lrg_static_batch_set_material_texture (batch, 0, clear);
    lrg_light_set_set_ambient (lights, &white);
    lrg_light_set_set_alpha_cutoff (lights, 0.5f);
    lrg_light_set_apply (lights, 0, 10, 0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.b, >, 245);
    g_assert_cmpuint (px.r, <, 10);
    lrg_light_set_set_alpha_cutoff (lights, 0.0f);
    lrg_light_set_apply (lights, 0, 10, 0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, >, 80);

    lrg_static_batch_set_shader (batch, NULL);
}

/* Terrain far from world zero, drawn origin-relative, lit by a point
 * light given in world coordinates with the same origin. */
static void
test_light_set_shader_terrain_origin (void)
{
    g_autoptr(LrgLightSet) lights = NULL;
    g_autoptr(LrgTerrain)  terrain = NULL;
    gfloat     heights[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    GrlColor   black = { 0, 0, 0, 255 };
    GrlColor   green = { 0, 255, 0, 255 };
    GrlShader *shader;
    Color      px;

    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return;
    }

    lights = lrg_light_set_new ();
    shader = lrg_light_set_get_shader (lights);
    if (shader == NULL)
    {
        g_test_skip ("GLSL 330 unavailable");
        return;
    }

    /* chunk (5000, 5000) of size 20 spans [100000, 100020] */
    terrain = lrg_terrain_new (2, 20.0f);
    g_assert_true (lrg_terrain_set_chunk (terrain, 5000, 5000, heights, 9, NULL, NULL, NULL));
    g_assert_true (lrg_terrain_upload (terrain, NULL));
    lrg_terrain_set_shader (terrain, shader);
    lrg_terrain_set_origin (terrain, 100010.0, 0.0, 100010.0);

    lrg_light_set_set_ambient (lights, &black);
    lrg_light_set_set_origin (lights, 100010.0, 0.0, 100010.0);
    lrg_light_set_add_point_light (lights, 100010.0, 1.0, 100010.0, &green, 5);
    lrg_light_set_apply (lights, 100010.0, 10.0, 100010.0);
    px = render_centre (NULL, terrain);
    g_assert_cmpuint (px.g, >, 100);
    g_assert_cmpuint (px.r, <, 10);

    /* without the origin the light lands far away */
    lrg_light_set_set_origin (lights, 0, 0, 0);
    lrg_light_set_apply (lights, 100010.0, 10.0, 100010.0);
    px = render_centre (NULL, terrain);
    g_assert_cmpuint (px.g, <, 10);
    g_assert_cmpuint (px.b, <, 10);

    lrg_terrain_set_shader (terrain, NULL);
}

/* CPU evaluation matches the shader's model term by term */
static void
test_light_set_sample (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlColor gray = { 51, 51, 51, 255 };
    GrlColor red = { 255, 0, 0, 255 };
    GrlColor sun = { 0, 0, 255, 255 };
    GrlVector3 at = { 0.0f, 0.0f, 0.0f };
    GrlVector3 up = { 0.0f, 2.0f, 0.0f };
    GrlVector3 down = { 0.0f, -1.0f, 0.0f };
    GrlVector3 side = { 1.0f, 0.0f, 0.0f };
    GrlVector3 zero = { 0.0f, 0.0f, 0.0f };
    gfloat rgb[3];

    /* ambient only (nothing selected yet) */
    lrg_light_set_set_ambient (lights, &gray);
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 0.2f, 1e-5);
    g_assert_cmpfloat_with_epsilon (rgb[2], 0.2f, 1e-5);

    /* the directional light: Lambert with a normal, full without */
    lrg_light_set_set_directional (lights, &down, &sun);
    lrg_light_set_sample (lights, &at, &up, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[2], 1.2f, 1e-5);
    lrg_light_set_sample (lights, &at, &side, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[2], 0.2f, 1e-5);
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[2], 1.2f, 1e-5);
    /* a zero normal counts as none */
    lrg_light_set_sample (lights, &at, &zero, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[2], 1.2f, 1e-5);
    lrg_light_set_set_directional (lights, NULL, NULL);

    /* a point light 2 above with radius 4: k = 0.5, k² = 0.25; only
     * after a selection */
    lrg_light_set_add_point_light (lights, 0.0, 2.0, 0.0, &red, 4.0f);
    lrg_light_set_sample (lights, &at, &up, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 0.2f, 1e-5);
    lrg_light_set_select (lights, 0.0, 0.0, 0.0);
    lrg_light_set_sample (lights, &at, &up, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 0.45f, 1e-5);
    g_assert_cmpfloat_with_epsilon (rgb[1], 0.2f, 1e-5);
    /* facing away: no Lambert contribution; no normal: full */
    lrg_light_set_sample (lights, &at, &down, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 0.2f, 1e-5);
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 0.45f, 1e-5);

    /* intensity scales, beyond the radius nothing */
    lrg_light_set_set_point_light_intensity (lights, 0, 2.0f);
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 0.7f, 1e-5);
    at.x = 10.0f;
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 0.2f, 1e-5);

    /* the origin: sample positions are draw space (world - origin) */
    lrg_light_set_set_point_light_intensity (lights, 0, 1.0f);
    lrg_light_set_set_point_light_position (lights, 0, 100.0, 2.0, 0.0);
    lrg_light_set_set_origin (lights, 100.0, 0.0, 0.0);
    lrg_light_set_select (lights, 100.0, 0.0, 0.0);
    at.x = 0.0f;
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 0.45f, 1e-5);
}

/* the light grid: cell lists, closest first, around the camera's cell */
static void
test_light_set_grid (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlColor red = { 255, 0, 0, 255 };
    GrlVector3 at = { 0.0f, 0.0f, 0.0f };
    gfloat rgb[3];
    guint i;
    guint far_light;

    /* 40 lights along +X every 10 units: the nearest 16 of the camera
     * at 0 never reach x = 300, the grid does */
    for (i = 0; i < 40; i++)
        lrg_light_set_add_point_light (lights, (gdouble)i * 10.0, 1.0, 0.0, &red, 4.0f);
    far_light = 30;

    /* no grid: nothing listed */
    lrg_light_set_select (lights, 0, 0, 0);
    g_assert_cmpuint (lrg_light_set_get_grid_light_count (lights, 300.0, 0.0), ==, 0);

    lrg_light_set_set_grid (lights, 8.0f, 128);
    lrg_light_set_select (lights, 0, 0, 0);
    g_assert_cmpuint (lrg_light_set_get_active_count (lights), ==, 16);
    /* the cell [296, 304) holds light 30 (and nothing else in reach) */
    g_assert_cmpuint (lrg_light_set_get_grid_light_count (lights, 300.0, 0.0), ==, 1);
    g_assert_cmpuint (lrg_light_set_get_grid_light (lights, 300.0, 0.0, 0), ==, far_light);
    g_assert_cmpuint (lrg_light_set_get_grid_light (lights, 300.0, 0.0, 1), ==,
                      LRG_LIGHT_SET_INVALID_LIGHT);
    /* an empty cell between two lights' reach */
    g_assert_cmpuint (lrg_light_set_get_grid_light_count (lights, 305.0, 50.0), ==, 0);

    /* the CPU sample reads the cell: k = 0.75 at 1 below the light */
    at.x = 300.0f;
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 64.0f / 255.0f + 0.75f * 0.75f, 1e-4);

    /* outside the grid (128 cells of 8 around the camera): no light */
    g_assert_cmpuint (lrg_light_set_get_grid_light_count (lights, 600.0, 0.0), ==, 0);
    g_assert_cmpuint (lrg_light_set_get_grid_light_count (lights, -600.0, 0.0), ==, 0);

    /* a crowded cell keeps the 16 closest relative to their reach */
    lrg_light_set_clear_point_lights (lights);
    for (i = 0; i < 20; i++)
        lrg_light_set_add_point_light (lights, 0.5 + (gdouble)i * 0.25, 0.0, 0.5, &red,
                                       10.0f);
    lrg_light_set_add_point_light (lights, 40.0, 0.0, 0.5, &red, 39.0f);
    lrg_light_set_select (lights, 0, 0, 0);
    g_assert_cmpuint (lrg_light_set_get_grid_light_count (lights, 1.0, 1.0), ==,
                      LRG_LIGHT_SET_GRID_SLOTS);
    for (i = 0; i < LRG_LIGHT_SET_GRID_SLOTS; i++)
        g_assert_cmpuint (lrg_light_set_get_grid_light (lights, 1.0, 1.0, i), <, 20);

    /* disabled lights stay out; removing drops the lists until selected */
    lrg_light_set_set_point_light_enabled (lights, 0, FALSE);
    lrg_light_set_select (lights, 0, 0, 0);
    for (i = 0; i < LRG_LIGHT_SET_GRID_SLOTS; i++)
        g_assert_cmpuint (lrg_light_set_get_grid_light (lights, 1.0, 1.0, i), !=, 0);
    lrg_light_set_remove_point_light (lights, 1);
    g_assert_cmpuint (lrg_light_set_get_grid_light_count (lights, 1.0, 1.0), ==, 0);

    /* turning the grid off */
    lrg_light_set_set_grid (lights, 0.0f, 0);
    lrg_light_set_select (lights, 0, 0, 0);
    g_assert_cmpuint (lrg_light_set_get_grid_light_count (lights, 1.0, 1.0), ==, 0);
}

/* the soft light limit: identity to the knee, then towards the limit */
static void
test_light_set_limit (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlColor white_light = { 255, 255, 255, 255 };
    GrlVector3 at = { 0.0f, 0.0f, 0.0f };
    gfloat rgb[3];
    gfloat v;

    g_assert_cmpfloat (lrg_light_set_get_light_limit (lights), ==, 0.0f);
    g_assert_cmpfloat (lrg_light_set_limit_light (0.0f, 5.0f), ==, 5.0f);
    g_assert_cmpfloat (lrg_light_set_limit_light (1.0f, 0.5f), ==, 0.5f);
    g_assert_cmpfloat (lrg_light_set_limit_light (1.0f, 0.75f), ==, 0.75f);
    v = lrg_light_set_limit_light (1.0f, 0.8f);
    g_assert_cmpfloat (v, >, 0.78f);
    g_assert_cmpfloat (v, <, 0.8f);
    v = lrg_light_set_limit_light (1.0f, 10.0f);
    g_assert_cmpfloat (v, >, 0.99f);
    g_assert_cmpfloat (v, <=, 1.0f);

    /* the CPU sample applies it to the sum */
    lrg_light_set_set_ambient (lights, &white_light);
    lrg_light_set_add_point_light (lights, 0.0, 0.0, 0.0, &white_light, 10.0f);
    lrg_light_set_select (lights, 0, 0, 0);
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 2.0f, 1e-5);
    lrg_light_set_set_light_limit (lights, 1.0f);
    g_assert_cmpfloat (lrg_light_set_get_light_limit (lights), ==, 1.0f);
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], lrg_light_set_limit_light (1.0f, 2.0f), 1e-6);
    g_assert_cmpfloat (rgb[0], <, 1.0f);
}

/* texture sampling: which modes use mipmaps; nothing without a context */
static void
test_texture_sampling (void)
{
    g_assert_false (lrg_texture_sampling_uses_mipmaps (LRG_TEXTURE_SAMPLING_POINT));
    g_assert_true (lrg_texture_sampling_uses_mipmaps (LRG_TEXTURE_SAMPLING_POINT_MIPMAPPED));
    g_assert_false (lrg_texture_sampling_uses_mipmaps (LRG_TEXTURE_SAMPLING_BILINEAR));
    g_assert_true (lrg_texture_sampling_uses_mipmaps (LRG_TEXTURE_SAMPLING_TRILINEAR));
    {
        GEnumClass *klass = g_type_class_ref (LRG_TYPE_TEXTURE_SAMPLING);

        g_assert_cmpstr (g_enum_get_value (klass, LRG_TEXTURE_SAMPLING_POINT_MIPMAPPED)->value_nick,
                         ==, "point-mipmapped");
        g_type_class_unref (klass);
    }

    if (graphics_available)
    {
        g_autoptr(GrlImage)   image = grl_image_new_color (8, 8, &white);
        g_autoptr(GrlTexture) texture = grl_texture_new_from_image (image);

        g_assert_true (lrg_texture_set_sampling (texture, LRG_TEXTURE_SAMPLING_POINT_MIPMAPPED));
        g_assert_cmpint (grl_texture_get_mipmaps (texture), >, 1);
        g_assert_true (lrg_texture_set_sampling (texture, LRG_TEXTURE_SAMPLING_POINT));
    }
}

/* GPU: the grid lights a floor far from the camera's 16 nearest
 * lights; the light limit caps a saturating light; an emission map adds
 * its colour regardless of the light */
static void
test_light_set_shader_grid_emission (void)
{
    g_autoptr(LrgLightSet)    lights = NULL;
    g_autoptr(LrgStaticBatch) batch = NULL;
    g_autoptr(GrlImage)       mask_image = NULL;
    g_autoptr(GrlTexture)     mask = NULL;
    GrlShader *shader;
    GrlColor   black = { 0, 0, 0, 255 };
    GrlColor   red = { 255, 0, 0, 255 };
    GrlColor   yellow = { 255, 255, 0, 255 };
    guint      i;
    Color      px;

    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return;
    }
    lights = lrg_light_set_new ();
    shader = lrg_light_set_get_shader (lights);
    if (shader == NULL)
    {
        g_test_skip ("GLSL 330 unavailable");
        return;
    }
    batch = floor_batch (0, 0);
    lrg_static_batch_set_shader (batch, shader);
    lrg_light_set_set_ambient (lights, &black);

    /* 20 dim lights near a "camera" at x = -200 fill the nearest 16; the
     * red lamp over the floor at the origin is the 21st */
    for (i = 0; i < 20; i++)
        lrg_light_set_add_point_light (lights, -200.0 + (gdouble)i, 1.0, 0.0, &black, 2.0f);
    lrg_light_set_add_point_light (lights, 0.0, 1.0, 0.0, &red, 5.0f);
    lrg_light_set_apply (lights, -200.0, 10.0, 0.0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, <, 10);
    lrg_light_set_set_grid (lights, 8.0f, 64);
    lrg_light_set_apply (lights, -200.0, 10.0, 0.0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, >, 100);
    g_assert_cmpuint (px.g, <, 10);

    /* a light of intensity 4 right above saturates; the limit keeps it
     * below the limit */
    lrg_light_set_set_point_light_intensity (lights, 20, 4.0f);
    lrg_light_set_apply (lights, -200.0, 10.0, 0.0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, >, 250);
    lrg_light_set_set_light_limit (lights, 0.5f);
    lrg_light_set_apply (lights, -200.0, 10.0, 0.0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, >, 110);
    g_assert_cmpuint (px.r, <, 130);
    lrg_light_set_set_light_limit (lights, 0.0f);

    /* emission: a white mask adds the emission colour in the dark */
    lrg_light_set_set_point_light_enabled (lights, 20, FALSE);
    lrg_light_set_apply (lights, -200.0, 10.0, 0.0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, <, 10);
    mask_image = grl_image_new_color (2, 2, &white);
    mask = grl_texture_new_from_image (mask_image);
    lrg_static_batch_set_material_emission (batch, 0, mask);
    lrg_light_set_set_emission (lights, &yellow, 0.5f);
    lrg_light_set_apply (lights, -200.0, 10.0, 0.0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, >, 115);
    g_assert_cmpuint (px.r, <, 140);
    g_assert_cmpuint (px.g, >, 115);
    g_assert_cmpuint (px.b, <, 10);
    /* no emission colour: dark; no map: dark */
    lrg_light_set_set_emission (lights, NULL, 1.0f);
    lrg_light_set_apply (lights, -200.0, 10.0, 0.0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, <, 10);
    lrg_light_set_set_emission (lights, &yellow, 1.0f);
    lrg_static_batch_set_material_emission (batch, 0, NULL);
    lrg_light_set_apply (lights, -200.0, 10.0, 0.0);
    px = render_centre (batch, NULL);
    g_assert_cmpuint (px.r, <, 10);

    lrg_static_batch_set_shader (batch, NULL);
}

/* the bound light set is process wide and holds a reference */
static void
test_light_set_bind (void)
{
    LrgLightSet *lights = lrg_light_set_new ();
    GWeakRef weak;

    g_assert_null (lrg_light_set_get_bound ());
    g_weak_ref_init (&weak, lights);

    lrg_light_set_bind (lights);
    g_assert_true (lrg_light_set_get_bound () == lights);
    g_object_unref (lights);
    /* still alive through the binding */
    lights = g_weak_ref_get (&weak);
    g_assert_nonnull (lights);
    g_object_unref (lights);

    lrg_light_set_unbind ();
    g_assert_null (lrg_light_set_get_bound ());
    g_assert_null (g_weak_ref_get (&weak));
    g_weak_ref_clear (&weak);

    /* unbinding twice is harmless */
    lrg_light_set_unbind ();
}

/* ------------------------------------------------------------------------ */
/* M14: clusters, shadow priorities, air                                    */
/* ------------------------------------------------------------------------ */

/* Two storeys of lights over the same cell: a flat grid of four slots
 * mixes them up, clusters keep each storey's own. */
static void
test_light_set_clusters (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlColor red = { 255, 0, 0, 255 };
    GrlVector3 at = { 1.0f, 8.5f, 1.0f };
    gfloat rgb[3];
    guint i;
    guint n;

    /* storey 0 at y = 1 (ids 0 .. 5), storey 1 at y = 9 (ids 6 .. 11);
     * the upper ones are a little closer in XZ */
    for (i = 0; i < 6; i++)
        lrg_light_set_add_point_light (lights, 1.0 + 0.3 * (gdouble)i, 1.0, 1.0, &red, 3.0f);
    for (i = 0; i < 6; i++)
        lrg_light_set_add_point_light (lights, 1.0 + 0.1 * (gdouble)i, 9.0, 1.0, &red, 3.0f);

    lrg_light_set_set_grid (lights, 8.0f, 16);
    lrg_light_set_set_grid_slots (lights, 4);
    g_assert_cmpuint (lrg_light_set_get_grid_slots (lights), ==, 4);
    lrg_light_set_select (lights, 0, 0, 0);
    /* flat: one list for every height, four lights in all */
    g_assert_cmpuint (lrg_light_set_get_cluster_light_count (lights, 1.0, 0.5, 1.0), ==, 4);
    g_assert_cmpuint (lrg_light_set_get_cluster_light_count (lights, 1.0, 8.5, 1.0), ==, 4);
    g_assert_cmpuint (lrg_light_set_get_cluster_light (lights, 1.0, 0.5, 1.0, 0), ==,
                      lrg_light_set_get_cluster_light (lights, 1.0, 8.5, 1.0, 0));

    /* clusters of 2 units: the lower storey's layer lists only its own */
    lrg_light_set_set_grid_layers (lights, 2.0f, 8);
    lrg_light_set_select (lights, 0, 0, 0);
    n = lrg_light_set_get_cluster_light_count (lights, 1.0, 0.5, 1.0);
    g_assert_cmpuint (n, ==, 4);
    for (i = 0; i < n; i++)
        g_assert_cmpuint (lrg_light_set_get_cluster_light (lights, 1.0, 0.5, 1.0, i), <, 6);
    n = lrg_light_set_get_cluster_light_count (lights, 1.0, 8.5, 1.0);
    g_assert_cmpuint (n, ==, 4);
    for (i = 0; i < n; i++)
        g_assert_cmpuint (lrg_light_set_get_cluster_light (lights, 1.0, 8.5, 1.0, i), >=, 6);
    /* the layer between the storeys (y 4 .. 6) is out of both reaches */
    g_assert_cmpuint (lrg_light_set_get_cluster_light_count (lights, 1.0, 5.0, 1.0), ==, 0);
    /* the outer layers reach on: far below still finds the lower storey */
    g_assert_cmpuint (lrg_light_set_get_cluster_light_count (lights, 1.0, -100.0, 1.0), ==, 0);

    /* the CPU sample reads the cluster of the point's own height */
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat (rgb[0], >, 64.0f / 255.0f + 0.1f);
    at.y = 5.0f;
    lrg_light_set_sample (lights, &at, NULL, rgb);
    g_assert_cmpfloat_with_epsilon (rgb[0], 64.0f / 255.0f, 1e-4);

    /* more slots: all six of a storey */
    lrg_light_set_set_grid_slots (lights, 32);
    lrg_light_set_select (lights, 0, 0, 0);
    g_assert_cmpuint (lrg_light_set_get_cluster_light_count (lights, 1.0, 0.5, 1.0), ==, 6);
    g_assert_cmpuint (lrg_light_set_get_grid_slots (lights), ==, 32);
}

/* the vertical reach of a cluster: a light reaching into a layer from
 * above lists there; one whose sphere stops short does not */
static void
test_light_set_cluster_reach (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlColor red = { 255, 0, 0, 255 };

    lrg_light_set_add_point_light (lights, 1.0, 6.0, 1.0, &red, 3.0f);   /* reaches y 3 .. 9 */
    lrg_light_set_set_grid (lights, 8.0f, 8);
    lrg_light_set_set_grid_layers (lights, 2.0f, 8);
    lrg_light_set_select (lights, 0, 0, 0);
    g_assert_cmpuint (lrg_light_set_get_cluster_light_count (lights, 1.0, 3.5, 1.0), ==, 1);
    g_assert_cmpuint (lrg_light_set_get_cluster_light_count (lights, 1.0, 1.5, 1.0), ==, 0);
    /* the camera's layer serves the old XZ queries */
    g_assert_cmpuint (lrg_light_set_get_grid_light_count (lights, 1.0, 1.0), ==, 0);
    lrg_light_set_select (lights, 0, 6, 0);
    g_assert_cmpuint (lrg_light_set_get_grid_light_count (lights, 1.0, 1.0), ==, 1);
}

static void
test_light_set_shadow_priority (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    g_autoptr(LrgShadowRenderer) shadows = lrg_shadow_renderer_new ();
    GrlColor white_c = { 255, 255, 255, 255 };
    gfloat camera[3] = { 0, 0, 0 };
    gfloat forward[3] = { 0, 0, -1 };
    gfloat up[3] = { 0, 1, 0 };
    guint near_low;
    guint far_high;
    guint mid_high;
    guint none;

    near_low = lrg_light_set_add_point_light (lights, 1, 0, 0, &white_c, 4);
    far_high = lrg_light_set_add_point_light (lights, 30, 0, 0, &white_c, 4);
    mid_high = lrg_light_set_add_point_light (lights, 10, 0, 0, &white_c, 4);
    none = lrg_light_set_add_point_light (lights, 0, 0, 0, &white_c, 4);
    g_assert_cmpuint (lrg_light_set_get_point_light_shadow (lights, near_low), ==, 0);
    lrg_light_set_set_point_light_shadow (lights, near_low, 1);
    lrg_light_set_set_point_light_shadow (lights, far_high, 5);
    lrg_light_set_set_point_light_shadow (lights, mid_high, 5);
    g_assert_cmpuint (lrg_light_set_get_point_light_shadow (lights, far_high), ==, 5);
    g_assert_cmpuint (lrg_light_set_get_point_light_shadow (lights, 99), ==, 0);

    lrg_light_set_set_shadow_renderer (lights, shadows);
    g_assert_true (lrg_light_set_get_shadow_renderer (lights) == shadows);

    /* a budget of two: both high ones, the nearer first */
    lrg_shadow_renderer_set_points (shadows, 64, 2, 0.0f);
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60, 1, 0.1f);
    g_assert_cmpuint (lrg_shadow_renderer_get_point_count (shadows), ==, 2);
    g_assert_cmpuint (lrg_shadow_renderer_get_point_light (shadows, 0), ==, mid_high);
    g_assert_cmpuint (lrg_shadow_renderer_get_point_light (shadows, 1), ==, far_high);
    g_assert_cmpint (lrg_shadow_renderer_get_light_slot (shadows, near_low), ==,
                     LRG_SHADOW_NO_SLOT);
    g_assert_cmpint (lrg_shadow_renderer_get_light_slot (shadows, none), ==, LRG_SHADOW_NO_SLOT);

    /* a reach of 10: the far one drops out, the low one comes in */
    lrg_shadow_renderer_set_points (shadows, 64, 2, 10.0f);
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60, 1, 0.1f);
    g_assert_cmpuint (lrg_shadow_renderer_get_point_count (shadows), ==, 2);
    g_assert_cmpint (lrg_shadow_renderer_get_light_slot (shadows, mid_high), ==, 0);
    g_assert_cmpint (lrg_shadow_renderer_get_light_slot (shadows, near_low), ==, 1);

    /* disabled lights never get a cube */
    lrg_light_set_set_point_light_enabled (lights, mid_high, FALSE);
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 60, 1, 0.1f);
    g_assert_cmpint (lrg_shadow_renderer_get_light_slot (shadows, mid_high), ==,
                     LRG_SHADOW_NO_SLOT);
    /* no directional light: point passes only */
    g_assert_cmpuint (lrg_shadow_renderer_get_cascade_count (shadows), ==, 0);
    g_assert_cmpuint (lrg_shadow_renderer_get_pass_count (shadows), ==,
                      6 * lrg_shadow_renderer_get_point_count (shadows));
    lrg_light_set_set_shadow_renderer (lights, NULL);
    g_assert_null (lrg_light_set_get_shadow_renderer (lights));
}

static void
test_light_set_air (void)
{
    g_autoptr(LrgLightSet) lights = lrg_light_set_new ();
    GrlVector3 from = { 0, 0, 0 };
    GrlVector3 near_pt = { 0, 0, -10 };
    GrlVector3 far_pt = { 0, 0, -40 };
    GrlVector3 down = { 0, 0, 1 };      /* light travels toward +Z */
    GrlColor black = { 0, 0, 0, 255 };
    GrlColor white_c = { 255, 255, 255, 255 };
    gfloat rgb_near[3];
    gfloat rgb_far[3];
    gfloat rgb_toward[3];
    gfloat rgb_away[3];
    gfloat t;
    gfloat sum;
    gfloat density;
    gfloat g;
    guint steps;
    guint i;

    /* off: nothing scattered, nothing dimmed */
    g_assert_false (lrg_light_set_get_air (lights, NULL, NULL, NULL));
    t = lrg_light_set_sample_air (lights, &from, &far_pt, rgb_far);
    g_assert_cmpfloat (t, ==, 1.0f);
    g_assert_cmpfloat (rgb_far[0], ==, 0.0f);

    /* the phase function integrates to 1 and favours forward light */
    sum = 0.0f;
    for (i = 0; i < 2000; i++)
    {
        gfloat mu = -1.0f + ((gfloat)i + 0.5f) * (2.0f / 2000.0f);

        sum += lrg_light_set_air_phase (0.6f, mu) * 2.0f * (gfloat)G_PI * (2.0f / 2000.0f);
    }
    g_assert_cmpfloat_with_epsilon (sum, 1.0f, 0.01f);
    g_assert_cmpfloat (lrg_light_set_air_phase (0.6f, 1.0f), >, lrg_light_set_air_phase (0.6f, -1.0f));
    g_assert_cmpfloat_with_epsilon (lrg_light_set_air_phase (0.0f, 0.3f),
                                    1.0f / (4.0f * (gfloat)G_PI), 1e-6);

    /* ambient air: more light and less transmittance with distance */
    lrg_light_set_set_air (lights, 0.02f, 0.5f, 1.0f, 16, 100.0f, NULL);
    g_assert_true (lrg_light_set_get_air (lights, &density, &g, &steps));
    g_assert_cmpfloat (density, ==, 0.02f);
    g_assert_cmpuint (steps, ==, 16);
    lrg_light_set_set_ambient (lights, &white_c);
    t = lrg_light_set_sample_air (lights, &from, &near_pt, rgb_near);
    g_assert_cmpfloat_with_epsilon (t, expf (-0.2f), 1e-5);
    lrg_light_set_sample_air (lights, &from, &far_pt, rgb_far);
    g_assert_cmpfloat (rgb_far[0], >, rgb_near[0]);
    /* the integral of a constant source: (1 - T) x source / density x density */
    g_assert_cmpfloat_with_epsilon (rgb_near[0], (1.0f - expf (-0.2f)) / (4.0f * (gfloat)G_PI),
                                    1e-3);

    /* the march stops at its distance: beyond it nothing changes */
    lrg_light_set_set_air (lights, 0.02f, 0.5f, 1.0f, 16, 10.0f, NULL);
    t = lrg_light_set_sample_air (lights, &from, &far_pt, rgb_far);
    g_assert_cmpfloat_with_epsilon (t, expf (-0.2f), 1e-5);
    g_assert_cmpfloat_with_epsilon (rgb_far[0], rgb_near[0], 1e-3);

    /* a directional light: looking toward it scatters more (g > 0) */
    lrg_light_set_set_air (lights, 0.02f, 0.5f, 1.0f, 16, 100.0f, NULL);
    lrg_light_set_set_ambient (lights, &black);
    lrg_light_set_set_directional (lights, &down, &white_c);
    {
        GrlVector3 toward = { 0, 0, -10 };   /* against the light's travel */
        GrlVector3 away = { 0, 0, 10 };

        lrg_light_set_sample_air (lights, &from, &toward, rgb_toward);
        lrg_light_set_sample_air (lights, &from, &away, rgb_away);
    }
    g_assert_cmpfloat (rgb_toward[0], >, rgb_away[0] * 4.0f);

    /* a tint colours it; clearing turns it off */
    lrg_light_set_set_air (lights, 0.02f, 0.5f, 1.0f, 16, 100.0f, &(GrlColor){ 255, 0, 0, 255 });
    lrg_light_set_sample_air (lights, &from, &near_pt, rgb_near);
    g_assert_cmpfloat (rgb_near[1], ==, 0.0f);
    lrg_light_set_clear_air (lights);
    g_assert_false (lrg_light_set_get_air (lights, NULL, NULL, NULL));
}

/* occluder_batch:
 * A 1.6 x 1.6 quad at y = @y centred on the origin, facing down, so the
 * camera above culls it while a shadow pass (no culling) draws it. */
static LrgStaticBatch *
occluder_batch (gfloat y)
{
    LrgStaticBatch *batch = lrg_static_batch_new ();
    gfloat positions[12] = {
        -0.8f, y, -0.8f,
         0.8f, y, -0.8f,
        -0.8f, y,  0.8f,
         0.8f, y,  0.8f
    };
    gfloat normals[12] = { 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0 };
    gfloat texcoords[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    guint32 indices[6] = { 0, 1, 2, 1, 3, 2 };

    g_assert_true (lrg_static_batch_add_mesh (batch, positions, normals, texcoords, NULL, 4,
                                              indices, 6, NULL, 0, 0, NULL));
    g_assert_true (lrg_static_batch_upload (batch, NULL));
    return batch;
}

/* render_shot:
 * As render_centre() with two batches; the whole 16 x 16 image. */
static Image
render_shot (LrgStaticBatch *a,
             LrgStaticBatch *b)
{
    RenderTexture2D rt = LoadRenderTexture (16, 16);
    Camera3D        camera;
    Image           shot;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 0, 10, 0.01f };
    camera.target = (Vector3){ 0, 0, 0 };
    camera.up = (Vector3){ 0, 0, -1 };
    camera.fovy = 30.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode (rt);
    ClearBackground ((Color){ 0, 0, 255, 255 });
    BeginMode3D (camera);
    lrg_static_batch_draw (a, LRG_STATIC_BATCH_ALL_LAYERS);
    if (b != NULL)
        lrg_static_batch_draw (b, LRG_STATIC_BATCH_ALL_LAYERS);
    EndMode3D ();
    EndTextureMode ();

    shot = LoadImageFromTexture (rt.texture);
    UnloadRenderTexture (rt);
    return shot;
}

/* render_casters:
 * Every pass of @shadows with the floor and the occluder as casters. */
static void
render_casters (LrgShadowRenderer *shadows,
                LrgStaticBatch    *floor,
                LrgStaticBatch    *occluder)
{
    GrlShader *caster = lrg_shadow_renderer_get_caster_shader (shadows);
    guint pass;

    g_assert_nonnull (caster);
    lrg_static_batch_set_shader (floor, caster);
    lrg_static_batch_set_shader (occluder, caster);
    for (pass = 0; pass < lrg_shadow_renderer_get_pass_count (shadows); pass++)
    {
        g_assert_true (lrg_shadow_renderer_begin_pass (shadows, pass));
        lrg_static_batch_draw (floor, LRG_STATIC_BATCH_ALL_LAYERS);
        lrg_static_batch_draw (occluder, LRG_STATIC_BATCH_ALL_LAYERS);
        lrg_shadow_renderer_end_pass (shadows);
    }
    g_assert_true (lrg_shadow_renderer_is_ready (shadows));
}

/* The sun straight down and a point light above an occluder: the floor
 * under it goes dark, the floor around it stays lit. */
static void
test_light_set_shader_shadows (void)
{
    g_autoptr(LrgLightSet)       lights = NULL;
    g_autoptr(LrgShadowRenderer) shadows = NULL;
    g_autoptr(LrgStaticBatch)    floor = NULL;
    g_autoptr(LrgStaticBatch)    occluder = NULL;
    GrlShader *shader;
    GrlColor   black = { 0, 0, 0, 255 };
    GrlVector3 down = { 0.05f, -1, 0.02f };
    gfloat camera[3] = { 0, 10, 0.01f };
    gfloat forward[3] = { 0, -1, -0.001f };
    gfloat up[3] = { 0, 0, -1 };
    Image  shot;
    Color  centre;
    Color  edge;
    guint  lamp;

    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return;
    }
    lights = lrg_light_set_new ();
    shader = lrg_light_set_get_shader (lights);
    if (shader == NULL)
    {
        g_test_skip ("GLSL 330 unavailable");
        return;
    }
    shadows = lrg_shadow_renderer_new ();
    lrg_shadow_renderer_set_sun (shadows, 512, 2, 30.0f, 0.5f);
    floor = floor_batch (0, 0);
    occluder = occluder_batch (2.0f);
    lrg_light_set_set_ambient (lights, &black);
    lrg_light_set_set_directional (lights, &down, &white);
    lrg_light_set_set_shadow_renderer (lights, shadows);

    /* 1. the sun: without passes no shadow, with them the centre darkens */
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 30, 1, 0.1f);
    g_assert_cmpuint (lrg_shadow_renderer_get_cascade_count (shadows), ==, 2);
    g_assert_false (lrg_shadow_renderer_is_ready (shadows));
    lrg_light_set_apply (lights, 0, 10, 0);
    lrg_static_batch_set_shader (floor, shader);
    lrg_static_batch_set_shader (occluder, shader);
    shot = render_shot (floor, occluder);
    centre = GetImageColor (shot, 8, 8);
    UnloadImage (shot);
    g_assert_cmpuint (centre.r, >, 200);

    render_casters (shadows, floor, occluder);
    lrg_light_set_apply (lights, 0, 10, 0);
    lrg_static_batch_set_shader (floor, shader);
    lrg_static_batch_set_shader (occluder, shader);
    shot = render_shot (floor, occluder);
    centre = GetImageColor (shot, 8, 8);
    edge = GetImageColor (shot, 1, 1);
    UnloadImage (shot);
    g_assert_cmpuint (centre.r, <, 40);     /* under the occluder */
    g_assert_cmpuint (edge.r, >, 200);      /* open floor: no acne */
    g_assert_cmpuint (edge.g, >, 200);      /* the floor, not the clear colour */

    /* 2. a point light above the occluder, no sun */
    lrg_light_set_set_directional (lights, NULL, NULL);
    lamp = lrg_light_set_add_point_light (lights, 0, 4, 0, &white, 12);
    lrg_light_set_set_point_light_shadow (lights, lamp, 1);
    lrg_shadow_renderer_set_points (shadows, 128, 1, 0.0f);
    lrg_shadow_renderer_update (shadows, lights, camera, forward, up, 30, 1, 0.1f);
    g_assert_cmpuint (lrg_shadow_renderer_get_pass_count (shadows), ==, 6);
    render_casters (shadows, floor, occluder);
    lrg_light_set_apply (lights, 0, 10, 0);
    lrg_static_batch_set_shader (floor, shader);
    lrg_static_batch_set_shader (occluder, shader);
    shot = render_shot (floor, occluder);
    centre = GetImageColor (shot, 8, 8);
    edge = GetImageColor (shot, 1, 1);
    UnloadImage (shot);
    g_assert_cmpuint (centre.r, <, 20);
    g_assert_cmpuint (edge.r, >, 40);

    /* strength 0: the shadow is gone */
    lrg_shadow_renderer_set_strength (shadows, 0.0f);
    lrg_light_set_apply (lights, 0, 10, 0);
    shot = render_shot (floor, occluder);
    centre = GetImageColor (shot, 8, 8);
    UnloadImage (shot);
    g_assert_cmpuint (centre.r, >, 90);

    lrg_static_batch_set_shader (floor, NULL);
    lrg_static_batch_set_shader (occluder, NULL);
    lrg_shadow_renderer_release_gpu (shadows);
    g_assert_false (lrg_shadow_renderer_is_ready (shadows));
}

/* M14: the grade's lookup table on the GPU: a red texture drawn through
 * a red/blue swapping table comes out blue */
static void
test_color_grade_lut_shader (void)
{
    g_autoptr(LrgColorGrade) grade = NULL;
    g_autoptr(LrgColorLut)   swap = NULL;
    g_autoptr(GrlImage)      image = NULL;
    g_autoptr(GrlTexture)    texture = NULL;
    GrlRectangle src = { 0, 0, 4, 4 };
    GrlRectangle dst = { 0, 0, 16, 16 };
    RenderTexture2D rt;
    Image shot;
    Color px;
    gfloat rgb[3] = { 200.0f / 255.0f, 40.0f / 255.0f, 0.0f };
    gfloat ref[3];
    static const gchar cube[] =
        "LUT_3D_SIZE 2\n0 0 0\n0 0 1\n0 1 0\n0 1 1\n1 0 0\n1 0 1\n1 1 0\n1 1 1\n";

    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return;
    }
    grade = lrg_color_grade_new ();
    if (lrg_color_grade_get_shader (grade) == NULL)
    {
        g_test_skip ("GLSL 330 unavailable");
        return;
    }
    swap = lrg_color_lut_new_from_cube (cube, -1, NULL);
    lrg_color_grade_set_lut (grade, swap, 1.0f);
    image = grl_image_new_color (4, 4, &(GrlColor){ 200, 40, 0, 255 });
    texture = grl_texture_new_from_image (image);

    rt = LoadRenderTexture (16, 16);
    BeginTextureMode (rt);
    ClearBackground ((Color){ 0, 0, 0, 255 });
    g_assert_true (lrg_color_grade_draw_texture (grade, texture, &src, &dst));
    EndTextureMode ();
    shot = LoadImageFromTexture (rt.texture);
    px = GetImageColor (shot, 8, 8);
    UnloadImage (shot);
    UnloadRenderTexture (rt);

    /* the CPU reference says (0, 40, 200); the strip is 8-bit */
    lrg_color_grade_apply_rgb (grade, rgb, ref);
    g_assert_cmpint (abs ((gint)px.b - (gint)(ref[2] * 255.0f + 0.5f)), <=, 3);
    g_assert_cmpint (abs ((gint)px.g - (gint)(ref[1] * 255.0f + 0.5f)), <=, 3);
    g_assert_cmpuint (px.r, <=, 3);
}

int
main (int   argc,
      char *argv[])
{
    gint result;

    g_test_init (&argc, &argv, NULL);

    graphics_available = init_graphics_context ();

    g_test_add_func ("/light-set/new", test_light_set_new);
    g_test_add_func ("/light-set/ids", test_light_set_ids);
    g_test_add_func ("/light-set/select", test_light_set_select);
    g_test_add_func ("/light-set/sample", test_light_set_sample);
    g_test_add_func ("/light-set/bind", test_light_set_bind);
    g_test_add_func ("/light-set/grid", test_light_set_grid);
    g_test_add_func ("/light-set/limit", test_light_set_limit);
    g_test_add_func ("/light-set/texture-sampling", test_texture_sampling);
    g_test_add_func ("/light-set/shader-grid-emission", test_light_set_shader_grid_emission);
    g_test_add_func ("/light-set/shader-static-batch", test_light_set_shader_static_batch);
    g_test_add_func ("/light-set/shader-terrain-origin", test_light_set_shader_terrain_origin);
    g_test_add_func ("/light-set/clusters", test_light_set_clusters);
    g_test_add_func ("/light-set/cluster-reach", test_light_set_cluster_reach);
    g_test_add_func ("/light-set/shadow-priority", test_light_set_shadow_priority);
    g_test_add_func ("/light-set/air", test_light_set_air);
    g_test_add_func ("/light-set/shader-shadows", test_light_set_shader_shadows);
    g_test_add_func ("/light-set/color-grade-lut-shader", test_color_grade_lut_shader);

    result = g_test_run ();

    g_clear_object (&test_window);
    return result;
}
