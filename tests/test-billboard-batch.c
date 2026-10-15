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
    g_test_add_func ("/billboard-batch/draw", test_billboard_batch_draw);

    result = g_test_run ();

    g_clear_object (&test_window);
    return result;
}
