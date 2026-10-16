/* test-billboard-batch.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgBillboardBatch. Orientation (axis-locked and
 * spherical), pivots, sizes, UV/tint/visibility bookkeeping and
 * validation are headless; drawing runs in a hidden window when a
 * display exists.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#define EPS (1e-5f)

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
    test_window = grl_window_new (8, 8, "lrg-billboard-batch-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

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

static void
assert_vec (const GrlVector3 *v,
            gfloat            x,
            gfloat            y,
            gfloat            z)
{
    g_assert_cmpfloat_with_epsilon (v->x, x, EPS);
    g_assert_cmpfloat_with_epsilon (v->y, y, EPS);
    g_assert_cmpfloat_with_epsilon (v->z, z, EPS);
}

static void
test_billboard_batch_new (void)
{
    g_autoptr(LrgBillboardBatch) batch = lrg_billboard_batch_new ();

    g_assert_nonnull (batch);
    g_assert_cmpuint (lrg_billboard_batch_get_count (batch), ==, 0);
    g_assert_cmpint (lrg_billboard_batch_get_mode (batch), ==, LRG_BILLBOARD_MODE_AXIS_Y);
}

/* A camera straight down +Z looks along -Z: right is +X, up is +Y, and
 * the default bottom-centre pivot sits on the position. */
static void
test_billboard_batch_axis_y_facing (void)
{
    g_autoptr(LrgBillboardBatch) batch = lrg_billboard_batch_new ();
    GrlVector3 pos = vec (1, 2, 3);
    GrlVector3 cam = vec (1, 5, 13);
    GrlVector3 corners[4];
    guint      index;

    index = lrg_billboard_batch_add (batch, 0, &pos, 2, 4);
    g_assert_cmpuint (index, ==, 0);
    g_assert_true (lrg_billboard_batch_get_corners (batch, index, &cam, NULL, NULL, corners));

    assert_vec (&corners[0], 0, 2, 3);   /* bottom-left */
    assert_vec (&corners[1], 2, 2, 3);   /* bottom-right */
    assert_vec (&corners[2], 2, 6, 3);   /* top-right */
    assert_vec (&corners[3], 0, 6, 3);   /* top-left */
}

/* Rotating the camera around the billboard keeps the quad upright and
 * perpendicular to the horizontal view direction. */
static void
test_billboard_batch_axis_y_turns (void)
{
    g_autoptr(LrgBillboardBatch) batch = lrg_billboard_batch_new ();
    GrlVector3 pos = vec (0, 0, 0);
    GrlVector3 cam = vec (10, 50, 0);   /* height does not tilt the quad */
    GrlVector3 corners[4];

    lrg_billboard_batch_add (batch, 0, &pos, 2, 1);
    g_assert_true (lrg_billboard_batch_get_corners (batch, 0, &cam, NULL, NULL, corners));

    /* view along -X: camera right is -Z */
    assert_vec (&corners[0], 0, 0, 1);
    assert_vec (&corners[1], 0, 0, -1);
    assert_vec (&corners[2], 0, 1, -1);

    /* camera directly overhead: a fixed basis, no NaNs */
    cam = vec (0, 9, 0);
    g_assert_true (lrg_billboard_batch_get_corners (batch, 0, &cam, NULL, NULL, corners));
    assert_vec (&corners[0], -1, 0, 0);
    assert_vec (&corners[1], 1, 0, 0);
}

static void
test_billboard_batch_spherical (void)
{
    g_autoptr(LrgBillboardBatch) batch = lrg_billboard_batch_new ();
    GrlVector3 pos = vec (0, 0, 0);
    GrlVector3 cam = vec (0, 0, 10);
    GrlVector3 right = vec (0, 0, -1);
    GrlVector3 up = vec (1, 0, 0);
    GrlVector3 corners[4];
    guint      index;

    lrg_billboard_batch_set_mode (batch, LRG_BILLBOARD_MODE_SPHERICAL);
    index = lrg_billboard_batch_add (batch, 0, &pos, 2, 2);
    lrg_billboard_batch_set_pivot (batch, index, 0.5f, 0.5f);

    g_assert_true (lrg_billboard_batch_get_corners (batch, index, &cam, &right, &up, corners));
    assert_vec (&corners[0], -1, 0, 1);
    assert_vec (&corners[2], 1, 0, -1);

    /* without a camera basis spherical falls back to axis-locked */
    g_assert_true (lrg_billboard_batch_get_corners (batch, index, &cam, NULL, NULL, corners));
    assert_vec (&corners[0], -1, -1, 0);
}

static void
test_billboard_batch_updates (void)
{
    g_autoptr(LrgBillboardBatch) batch = lrg_billboard_batch_new ();
    GrlVector3   pos = vec (0, 0, 0);
    GrlVector3   moved = vec (5, 0, 0);
    GrlVector3   cam = vec (5, 0, 10);
    GrlVector3   corners[4];
    GrlRectangle uv = { 0.25f, 0.0f, 0.25f, 1.0f };
    GrlColor     tint = { 255, 0, 0, 128 };
    guint        a;
    guint        b;

    a = lrg_billboard_batch_add (batch, 3, &pos, 1, 1);
    b = lrg_billboard_batch_add (batch, 7, &pos, 1, 1);
    g_assert_cmpuint (b, ==, a + 1);

    lrg_billboard_batch_set_position (batch, a, &moved);
    lrg_billboard_batch_set_size (batch, a, 4, 2);
    lrg_billboard_batch_set_pivot (batch, a, 0.0f, 1.0f);
    lrg_billboard_batch_set_uv (batch, a, &uv);
    lrg_billboard_batch_set_uv (batch, a, NULL);
    lrg_billboard_batch_set_tint (batch, a, &tint);
    lrg_billboard_batch_set_tint (batch, a, NULL);
    lrg_billboard_batch_set_material (batch, a, 9);
    lrg_billboard_batch_set_visible (batch, b, FALSE);

    /* pivot (0, 1): the position is the top-left corner */
    g_assert_true (lrg_billboard_batch_get_corners (batch, a, &cam, NULL, NULL, corners));
    assert_vec (&corners[3], 5, 0, 0);
    assert_vec (&corners[1], 9, -2, 0);

    g_assert_false (lrg_billboard_batch_get_corners (batch, 99, &cam, NULL, NULL, corners));

    lrg_billboard_batch_clear (batch);
    g_assert_cmpuint (lrg_billboard_batch_get_count (batch), ==, 0);
}

static void
test_billboard_batch_invalid_size (void)
{
    if (g_test_subprocess ())
    {
        g_autoptr(LrgBillboardBatch) batch = lrg_billboard_batch_new ();
        GrlVector3 pos = vec (0, 0, 0);

        lrg_billboard_batch_add (batch, 0, &pos, 0, 1);
        return;
    }
    g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_failed ();
    g_test_trap_assert_stderr ("*CRITICAL*width > 0.0f*");
}

/* Drawing needs GL: render inside a camera to a hidden window and make
 * sure every material path (textured, untextured, hidden) runs. */
static void
test_billboard_batch_draw (void)
{
    g_autoptr(LrgBillboardBatch) batch = NULL;
    g_autoptr(GrlImage)   image = NULL;
    g_autoptr(GrlTexture) texture = NULL;
    GrlVector3 pos = vec (0, 0, 0);
    GrlVector3 cam_pos = vec (0, 1, 5);
    Camera3D   camera;
    guint      i;

    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return;
    }

    image = grl_image_new_color (4, 4, &(GrlColor){ 255, 255, 255, 255 });
    texture = grl_texture_new_from_image (image);
    g_assert_nonnull (texture);

    batch = lrg_billboard_batch_new ();
    lrg_billboard_batch_set_material_texture (batch, 1, texture);
    for (i = 0; i < 100; i++)
    {
        pos.x = (gfloat)i * 0.1f;
        lrg_billboard_batch_add (batch, i % 2 == 0 ? 1 : 2, &pos, 1, 1);
    }
    lrg_billboard_batch_set_visible (batch, 0, FALSE);

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ cam_pos.x, cam_pos.y, cam_pos.z };
    camera.target = (Vector3){ 0, 0, 0 };
    camera.up = (Vector3){ 0, 1, 0 };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginDrawing ();
    ClearBackground (BLACK);
    BeginMode3D (camera);
    lrg_billboard_batch_draw (batch, &cam_pos);
    lrg_billboard_batch_set_mode (batch, LRG_BILLBOARD_MODE_SPHERICAL);
    lrg_billboard_batch_set_alpha_cutoff (batch, 0.0f);
    lrg_billboard_batch_draw (batch, &cam_pos);
    EndMode3D ();
    EndDrawing ();

    /* dropping the texture keeps the batch drawable */
    lrg_billboard_batch_set_material_texture (batch, 1, NULL);
    BeginDrawing ();
    BeginMode3D (camera);
    lrg_billboard_batch_draw (batch, &cam_pos);
    EndMode3D ();
    EndDrawing ();
}

/* lit tints: the batch's own lights, the bound set, emissive and unlit */
static void
test_billboard_batch_lighting (void)
{
    g_autoptr(LrgBillboardBatch) batch = lrg_billboard_batch_new ();
    g_autoptr(LrgLightSet) own = lrg_light_set_new ();
    g_autoptr(LrgLightSet) bound = lrg_light_set_new ();
    GrlVector3 pos = { 0.0f, 0.0f, 0.0f };
    GrlColor half = { 128, 128, 128, 255 };
    GrlColor dark = { 51, 51, 51, 255 };
    GrlColor red = { 255, 0, 0, 255 };
    GrlColor tint = { 200, 100, 50, 128 };
    GrlColor out;
    guint index;

    index = lrg_billboard_batch_add (batch, 0, &pos, 1.0f, 2.0f);
    g_assert_true (lrg_billboard_batch_get_lit (batch));
    g_assert_null (lrg_billboard_batch_get_light_set (batch));
    g_assert_false (lrg_billboard_batch_get_emissive (batch, index));
    g_assert_false (lrg_billboard_batch_get_emissive (batch, 99));
    g_assert_false (lrg_billboard_batch_get_lit_tint (batch, 99, &out));

    /* no lights: the plain tint */
    g_assert_true (lrg_billboard_batch_get_lit_tint (batch, index, &out));
    g_assert_cmpuint (out.r, ==, 255);
    g_assert_cmpuint (out.a, ==, 255);

    /* the bound set lights batches without their own */
    lrg_light_set_set_ambient (bound, &dark);
    lrg_light_set_bind (bound);
    lrg_billboard_batch_get_lit_tint (batch, index, &out);
    g_assert_cmpuint (out.r, ==, 51);
    g_assert_cmpuint (out.g, ==, 51);

    /* the batch's own set wins; its tint multiplies, alpha is kept */
    lrg_light_set_set_ambient (own, &half);
    lrg_billboard_batch_set_light_set (batch, own);
    g_assert_true (lrg_billboard_batch_get_light_set (batch) == own);
    lrg_billboard_batch_set_tint (batch, index, &tint);
    lrg_billboard_batch_get_lit_tint (batch, index, &out);
    g_assert_cmpuint (out.r, ==, 100);
    g_assert_cmpuint (out.g, ==, 50);
    g_assert_cmpuint (out.b, ==, 25);
    g_assert_cmpuint (out.a, ==, 128);

    /* a point light at the quad's centre (pivot at the feet, height 2:
     * centre y = 1) over-brightens and clamps */
    lrg_light_set_add_point_light (own, 0.0, 1.0, 0.0, &red, 4.0f);
    lrg_light_set_select (own, 0.0, 0.0, 0.0);
    lrg_billboard_batch_get_lit_tint (batch, index, &out);
    g_assert_cmpuint (out.r, ==, 255);
    g_assert_cmpuint (out.g, ==, 50);

    /* emissive quads keep their tint */
    lrg_billboard_batch_set_emissive (batch, index, TRUE);
    g_assert_true (lrg_billboard_batch_get_emissive (batch, index));
    lrg_billboard_batch_get_lit_tint (batch, index, &out);
    g_assert_cmpuint (out.r, ==, 200);
    g_assert_cmpuint (out.b, ==, 50);
    lrg_billboard_batch_set_emissive (batch, index, FALSE);

    /* an unlit batch ignores every light set */
    lrg_billboard_batch_set_lit (batch, FALSE);
    g_assert_false (lrg_billboard_batch_get_lit (batch));
    lrg_billboard_batch_get_lit_tint (batch, index, &out);
    g_assert_cmpuint (out.r, ==, 200);

    /* back to the bound set */
    lrg_billboard_batch_set_lit (batch, TRUE);
    lrg_billboard_batch_set_light_set (batch, NULL);
    lrg_billboard_batch_get_lit_tint (batch, index, &out);
    g_assert_cmpuint (out.r, ==, 40);
    lrg_light_set_unbind ();
    lrg_billboard_batch_get_lit_tint (batch, index, &out);
    g_assert_cmpuint (out.r, ==, 200);
}

/* drawn lighting: the quad's centre goes through the pushed transform
 * before sampling, so a translated batch is lit where it appears */
static void
test_billboard_batch_draw_lit (void)
{
    g_autoptr(LrgBillboardBatch) batch = NULL;
    g_autoptr(LrgLightSet) lights = NULL;
    g_autoptr(GrlImage)   image = NULL;
    g_autoptr(GrlTexture) texture = NULL;
    GrlVector3 pos = vec (0, 0, 0);
    GrlVector3 cam_pos = vec (5, 1, 5);
    GrlColor   dark = { 26, 26, 26, 255 };
    GrlColor   red = { 255, 0, 0, 255 };
    RenderTexture2D rt;
    Camera3D   camera;
    Image      shot;
    Color      centre;

    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return;
    }

    image = grl_image_new_color (4, 4, &(GrlColor){ 255, 255, 255, 255 });
    texture = grl_texture_new_from_image (image);
    batch = lrg_billboard_batch_new ();
    lrg_billboard_batch_set_material_texture (batch, 0, texture);
    lrg_billboard_batch_add (batch, 0, &pos, 2, 2);

    lights = lrg_light_set_new ();
    lrg_light_set_set_ambient (lights, &dark);
    lrg_light_set_add_point_light (lights, 5.0, 1.0, 0.0, &red, 2.0f);
    lrg_light_set_select (lights, 5.0, 1.0, 5.0);
    lrg_light_set_bind (lights);

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 5, 1, 5 };
    camera.target = (Vector3){ 5, 1, 0 };
    camera.up = (Vector3){ 0, 1, 0 };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    rt = LoadRenderTexture (16, 16);
    BeginTextureMode (rt);
    ClearBackground ((Color){ 0, 0, 255, 255 });
    BeginMode3D (camera);
    rlPushMatrix ();
    rlTranslatef (5.0f, 0.0f, 0.0f);
    lrg_billboard_batch_draw (batch, &cam_pos);
    rlPopMatrix ();
    EndMode3D ();
    EndTextureMode ();
    lrg_light_set_unbind ();

    shot = LoadImageFromTexture (rt.texture);
    centre = GetImageColor (shot, 8, 8);
    UnloadImage (shot);
    UnloadRenderTexture (rt);

    /* ambient 0.1 plus the full red light at the centre */
    g_assert_cmpuint (centre.r, >, 240);
    g_assert_cmpuint (centre.g, <, 40);
    g_assert_cmpuint (centre.b, <, 40);
}

/* draws @batch three times (the GPU path uploads after two settled
 * draws) into a 16 x 16 target from (5, 1, 5) looking down -Z, under a
 * translation of +5 on X, and returns the centre pixel */
static Color
draw_centre (LrgBillboardBatch *batch,
             guint              frames)
{
    GrlVector3      cam_pos = vec (0, 1, 5);
    RenderTexture2D rt;
    Camera3D        camera;
    Image           shot;
    Color           centre;
    guint           f;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 5, 1, 5 };
    camera.target = (Vector3){ 5, 1, 0 };
    camera.up = (Vector3){ 0, 1, 0 };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    rt = LoadRenderTexture (16, 16);
    for (f = 0; f < frames; f++)
    {
        BeginTextureMode (rt);
        ClearBackground ((Color){ 0, 0, 255, 255 });
        BeginMode3D (camera);
        rlPushMatrix ();
        rlTranslatef (5.0f, 0.0f, 0.0f);
        /* the eye in the batch's (translated) space */
        lrg_billboard_batch_draw (batch, &cam_pos);
        rlPopMatrix ();
        EndMode3D ();
        EndTextureMode ();
    }
    shot = LoadImageFromTexture (rt.texture);
    centre = GetImageColor (shot, 8, 8);
    UnloadImage (shot);
    UnloadRenderTexture (rt);
    return centre;
}

/* the GPU path: a large settled batch is uploaded and lit in the vertex
 * shader like the immediate path lights on the CPU; a billboard moved
 * afterwards leaves its old place (its uploaded quad is zeroed) and is
 * drawn where it went; a hidden one disappears */
static void
test_billboard_batch_gpu_path (void)
{
    g_autoptr(LrgBillboardBatch) batch = NULL;
    g_autoptr(LrgLightSet) lights = NULL;
    g_autoptr(GrlImage)   image = NULL;
    g_autoptr(GrlTexture) texture = NULL;
    GrlVector3 pos = vec (0, 0, 0);
    GrlVector3 away = vec (0, 0, 50);
    GrlColor   dark = { 26, 26, 26, 255 };
    GrlColor   red = { 255, 0, 0, 255 };
    GrlColor   green = { 0, 255, 0, 255 };
    Color      centre;
    guint      i;

    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return;
    }

    image = grl_image_new_color (4, 4, &(GrlColor){ 255, 255, 255, 255 });
    texture = grl_texture_new_from_image (image);
    batch = lrg_billboard_batch_new ();
    lrg_billboard_batch_set_material_texture (batch, 0, texture);
    lrg_billboard_batch_set_material_texture (batch, 1, texture);
    /* billboard 0 in front of the eye, 199 behind it (two materials) */
    lrg_billboard_batch_add (batch, 0, &pos, 2, 2);
    for (i = 1; i < 200; i++)
    {
        GrlVector3 behind = vec ((gfloat)i * 0.01f, 0, 20);

        lrg_billboard_batch_add (batch, i % 2, &behind, 1, 1);
    }

    lights = lrg_light_set_new ();
    lrg_light_set_set_ambient (lights, &dark);
    lrg_light_set_add_point_light (lights, 5.0, 1.0, 0.0, &red, 2.0f);
    lrg_light_set_select (lights, 5.0, 1.0, 5.0);
    lrg_light_set_bind (lights);

    /* ambient 0.1 plus the full red light at the centre, on the GPU */
    centre = draw_centre (batch, 3);
    g_assert_cmpuint (centre.r, >, 240);
    g_assert_cmpuint (centre.g, <, 40);
    g_assert_cmpuint (centre.b, <, 40);

    /* moved away: the uploaded quad no longer covers the centre */
    lrg_billboard_batch_set_position (batch, 0, &away);
    centre = draw_centre (batch, 1);
    g_assert_cmpuint (centre.b, >, 240);
    g_assert_cmpuint (centre.r, <, 16);

    /* moved back: drawn again (now from the loose list) */
    lrg_billboard_batch_set_position (batch, 0, &pos);
    centre = draw_centre (batch, 1);
    g_assert_cmpuint (centre.r, >, 240);

    /* hidden: gone */
    lrg_billboard_batch_set_visible (batch, 0, FALSE);
    centre = draw_centre (batch, 1);
    g_assert_cmpuint (centre.b, >, 240);
    lrg_light_set_unbind ();

    /* unlit, a fresh batch: the tint alone */
    g_clear_object (&batch);
    batch = lrg_billboard_batch_new ();
    lrg_billboard_batch_set_material_texture (batch, 0, texture);
    lrg_billboard_batch_add (batch, 0, &pos, 2, 2);
    for (i = 1; i < 200; i++)
    {
        GrlVector3 behind = vec ((gfloat)i * 0.01f, 0, 20);

        lrg_billboard_batch_add (batch, 0, &behind, 1, 1);
    }
    lrg_billboard_batch_set_lit (batch, FALSE);
    centre = draw_centre (batch, 3);
    g_assert_cmpuint (centre.r, >, 240);
    g_assert_cmpuint (centre.g, >, 240);
    lrg_billboard_batch_set_tint (batch, 0, &green);
    centre = draw_centre (batch, 1);
    g_assert_cmpuint (centre.r, <, 16);
    g_assert_cmpuint (centre.g, >, 240);
}

int
main (int   argc,
      char *argv[])
{
    gint result;

    g_test_init (&argc, &argv, NULL);

    graphics_available = init_graphics_context ();

    g_test_add_func ("/billboard-batch/new", test_billboard_batch_new);
    g_test_add_func ("/billboard-batch/axis-y-facing", test_billboard_batch_axis_y_facing);
    g_test_add_func ("/billboard-batch/axis-y-turns", test_billboard_batch_axis_y_turns);
    g_test_add_func ("/billboard-batch/spherical", test_billboard_batch_spherical);
    g_test_add_func ("/billboard-batch/updates", test_billboard_batch_updates);
    g_test_add_func ("/billboard-batch/invalid-size", test_billboard_batch_invalid_size);
    g_test_add_func ("/billboard-batch/lighting", test_billboard_batch_lighting);
    g_test_add_func ("/billboard-batch/draw", test_billboard_batch_draw);
    g_test_add_func ("/billboard-batch/draw-lit", test_billboard_batch_draw_lit);
    g_test_add_func ("/billboard-batch/gpu-path", test_billboard_batch_gpu_path);

    result = g_test_run ();

    g_clear_object (&test_window);
    return result;
}
