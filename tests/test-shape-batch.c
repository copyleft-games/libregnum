/* test-shape-batch.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shape batches (lrg_shape3d_begin_batch()): the instanced draw of the
 * queued cubes, spheres, cylinders and cones renders what drawing them
 * one by one renders, translucent shapes still blend over what came
 * before them, and the queue survives shapes freed while it holds them.
 * The GL tests open a hidden window and skip without a display.
 */

#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>
#include <rlgl.h>

#define TARGET_W (320)
#define TARGET_H (240)

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

    SetTraceLogLevel (LOG_WARNING);
    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (8, 8, "lrg-shape-batch-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

#define SKIP_WITHOUT_GL() \
    do { \
        if (!graphics_available) \
        { \
            g_test_skip ("Graphics context not available"); \
            return; \
        } \
    } while (0)

/* make_scene:
 * A grid of every batched shape kind, rotated and scaled (some cubes
 * non-uniformly), in colours that differ per shape. With @kinds 1 only
 * cubes, 2 only the mesh shapes, 3 both. */
static GPtrArray *
make_scene (guint kinds)
{
    GPtrArray *shapes = g_ptr_array_new_with_free_func (g_object_unref);
    guint      i;

    for (i = 0; i < 48; i++)
    {
        g_autoptr(GrlColor) color = NULL;
        gfloat              x = (gfloat)(i % 8) * 1.6f - 5.6f;
        gfloat              z = (gfloat)(i / 8) * 1.6f - 4.0f;
        LrgShape3D         *shape = NULL;
        guint               kind = i % 4;

        color = grl_color_new ((guint8)(40 + i * 37), (guint8)(90 + i * 53),
                               (guint8)(200 - i * 3), 255);
        if (kind == 0 && (kinds & 1) != 0)
        {
            shape = LRG_SHAPE3D (lrg_cube3d_new_full (x, 0.3f, z, 0.9f, 0.6f + 0.02f * i,
                                                      0.7f, color));
        }
        else if (kind == 1 && (kinds & 2) != 0)
        {
            shape = LRG_SHAPE3D (lrg_sphere3d_new_full (x, 0.4f, z, 0.55f, color));
        }
        else if (kind == 2 && (kinds & 2) != 0)
        {
            shape = LRG_SHAPE3D (lrg_cylinder3d_new_full (x, 0.0f, z, 0.4f, 0.9f, 24, color));
        }
        else if (kind == 3 && (kinds & 2) != 0)
        {
            shape = LRG_SHAPE3D (lrg_cone3d_new_full (x, 0.0f, z, 0.5f, 0.0f, 1.0f, 20,
                                                      color));
        }
        if (shape == NULL)
            continue;
        lrg_shape3d_set_rotation_xyz (shape, 0.1f * (gfloat)i, 0.37f * (gfloat)i,
                                      (i % 3 == 0) ? 0.2f : 0.0f);
        if (i % 5 == 0)
            lrg_shape3d_set_scale_xyz (shape, 1.2f, 0.8f, 1.1f);
        g_ptr_array_add (shapes, shape);
    }
    return shapes;
}

/* render:
 * Draws @shapes (and then @overlay, when given) into a fresh render
 * texture, in a batch when @batched, and returns the pixels. */
static Image
render (GPtrArray *shapes,
        LrgShape  *overlay,
        gboolean   batched)
{
    RenderTexture2D target = LoadRenderTexture (TARGET_W, TARGET_H);
    Camera3D        camera;
    Image           image;
    guint           i;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 2.0f, 9.0f, 11.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 50.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode (target);
    ClearBackground (BLACK);
    BeginMode3D (camera);
    if (batched)
        lrg_shape3d_begin_batch ();
    for (i = 0; i < shapes->len; i++)
        lrg_drawable_draw (LRG_DRAWABLE (g_ptr_array_index (shapes, i)), 0.0f);
    if (overlay != NULL)
        lrg_drawable_draw (LRG_DRAWABLE (overlay), 0.0f);
    if (batched)
        lrg_shape3d_end_batch ();
    EndMode3D ();
    EndTextureMode ();

    image = LoadImageFromTexture (target.texture);
    UnloadRenderTexture (target);
    return image;
}

/* compare: counts the lit pixels of @a and the pixels where @a and @b
 * differ by more than @tolerance in a channel */
static void
compare (Image  a,
         Image  b,
         gint   tolerance,
         guint *out_lit,
         guint *out_differ)
{
    const guint8 *pa = a.data;
    const guint8 *pb = b.data;
    guint         n = (guint)(a.width * a.height);
    guint         p;

    g_assert_cmpint (a.width, ==, b.width);
    g_assert_cmpint (a.height, ==, b.height);
    g_assert_cmpint (a.format, ==, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    *out_lit = 0;
    *out_differ = 0;
    for (p = 0; p < n; p++)
    {
        guint k;

        if (pa[p * 4] != 0 || pa[p * 4 + 1] != 0 || pa[p * 4 + 2] != 0)
            (*out_lit)++;
        for (k = 0; k < 4; k++)
        {
            if (ABS ((gint)pa[p * 4 + k] - (gint)pb[p * 4 + k]) > tolerance)
            {
                (*out_differ)++;
                break;
            }
        }
    }
}

/* Outside a context, or outside a batch, nothing queues; batches nest
 * and an unbalanced end is caught. */
static void
test_batch_state (void)
{
    g_assert_false (lrg_shape3d_is_batching ());
    lrg_shape3d_flush_batch ();

    if (!graphics_available)
        return;

    lrg_shape3d_begin_batch ();
    g_assert_true (lrg_shape3d_is_batching ());
    lrg_shape3d_begin_batch ();
    lrg_shape3d_end_batch ();
    g_assert_true (lrg_shape3d_is_batching ());
    lrg_shape3d_end_batch ();
    g_assert_false (lrg_shape3d_is_batching ());
}

/* The spheres, cylinders and cones: the instanced draw uses the very
 * MVP and colour DrawMesh() would, so the image is the same pixel for
 * pixel. */
static void
test_batch_mesh_shapes_match (void)
{
    g_autoptr(GPtrArray) shapes = NULL;
    Image                one_by_one;
    Image                batched;
    guint                lit;
    guint                differ;

    SKIP_WITHOUT_GL ();

    shapes = make_scene (2);
    one_by_one = render (shapes, NULL, FALSE);
    batched = render (shapes, NULL, TRUE);
    compare (one_by_one, batched, 0, &lit, &differ);
    g_test_message ("mesh shapes: %u lit, %u differ", lit, differ);
    g_assert_cmpuint (lit, >, TARGET_W * TARGET_H / 20);
    g_assert_cmpuint (differ, ==, 0);
    UnloadImage (one_by_one);
    UnloadImage (batched);
}

/* The cubes: one by one they go through rlgl's immediate batch (the CPU
 * transforms each corner), batched through the unit cube's MVP; the
 * same triangles, rounded slightly differently, so at most a few edge
 * pixels may flip, and by no more than the colour step to a neighbour. */
static void
test_batch_cubes_match (void)
{
    g_autoptr(GPtrArray) shapes = NULL;
    Image                one_by_one;
    Image                batched;
    guint                lit;
    guint                differ;

    SKIP_WITHOUT_GL ();

    shapes = make_scene (1);
    one_by_one = render (shapes, NULL, FALSE);
    batched = render (shapes, NULL, TRUE);
    compare (one_by_one, batched, 0, &lit, &differ);
    g_test_message ("cubes: %u lit, %u differ", lit, differ);
    g_assert_cmpuint (lit, >, TARGET_W * TARGET_H / 40);
    g_assert_cmpuint (differ, <=, lit / 200);
    UnloadImage (one_by_one);
    UnloadImage (batched);
}

/* A translucent sphere drawn last over the batched shapes blends over
 * them: the batch is drawn before it, as one by one. */
static void
test_batch_translucent_after (void)
{
    g_autoptr(GPtrArray)   shapes = NULL;
    g_autoptr(LrgSphere3D) glass = NULL;
    g_autoptr(GrlColor)    tint = NULL;
    Image                  one_by_one;
    Image                  batched;
    guint                  lit;
    guint                  differ;

    SKIP_WITHOUT_GL ();

    shapes = make_scene (2);
    tint = grl_color_new (255, 255, 255, 120);
    glass = lrg_sphere3d_new_full (0.0f, 1.0f, 0.0f, 3.0f, tint);
    one_by_one = render (shapes, LRG_SHAPE (glass), FALSE);
    batched = render (shapes, LRG_SHAPE (glass), TRUE);
    compare (one_by_one, batched, 0, &lit, &differ);
    g_test_message ("translucent overlay: %u lit, %u differ", lit, differ);
    g_assert_cmpuint (differ, ==, 0);
    UnloadImage (one_by_one);
    UnloadImage (batched);
}

/* A shape freed while the batch holds its draw: the shared mesh stays
 * alive until the batch has drawn it. */
static void
test_batch_shape_freed_while_queued (void)
{
    g_autoptr(GPtrArray) shapes = NULL;
    g_autoptr(GrlColor)  color = NULL;
    RenderTexture2D      target;
    Camera3D             camera;
    Image                image;
    LrgCylinder3D       *odd;
    const guint8        *px;
    guint                lit = 0;
    guint                p;

    SKIP_WITHOUT_GL ();

    color = grl_color_new (200, 40, 40, 255);
    /* a size no other shape uses: its mesh's only reference */
    odd = lrg_cylinder3d_new_full (0.0f, 0.0f, 0.0f, 1.3f, 2.1f, 17, color);

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 0.0f, 4.0f, 6.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 50.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    target = LoadRenderTexture (64, 64);
    BeginTextureMode (target);
    ClearBackground (BLACK);
    BeginMode3D (camera);
    lrg_shape3d_begin_batch ();
    lrg_drawable_draw (LRG_DRAWABLE (odd), 0.0f);
    g_object_unref (odd);
    lrg_shape3d_end_batch ();
    EndMode3D ();
    EndTextureMode ();

    image = LoadImageFromTexture (target.texture);
    px = image.data;
    for (p = 0; p < 64 * 64; p++)
        if (px[p * 4] == 200 && px[p * 4 + 1] == 40)
            lit++;
    g_assert_cmpuint (lit, >, 100);
    UnloadImage (image);
    UnloadRenderTexture (target);
}

int
main (int   argc,
      char *argv[])
{
    int result;

    g_test_init (&argc, &argv, NULL);
    graphics_available = init_graphics_context ();

    g_test_add_func ("/shape-batch/state", test_batch_state);
    g_test_add_func ("/shape-batch/mesh-shapes-match", test_batch_mesh_shapes_match);
    g_test_add_func ("/shape-batch/cubes-match", test_batch_cubes_match);
    g_test_add_func ("/shape-batch/translucent-after", test_batch_translucent_after);
    g_test_add_func ("/shape-batch/shape-freed-while-queued", test_batch_shape_freed_while_queued);

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
