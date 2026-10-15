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
    g_test_add_func ("/light-set/shader-static-batch", test_light_set_shader_static_batch);
    g_test_add_func ("/light-set/shader-terrain-origin", test_light_set_shader_terrain_origin);

    result = g_test_run ();

    g_clear_object (&test_window);
    return result;
}
