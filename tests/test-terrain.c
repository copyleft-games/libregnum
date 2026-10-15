/* test-terrain.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgTerrain. Chunk bookkeeping, validation, bilinear
 * height queries, mesh generation (grouping, splitting, winding, normals
 * across chunk edges, UV variants and repeat) are headless; upload and
 * drawing run in a hidden window when a display exists.
 */

#include <glib.h>
#include <gio/gio.h>
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
    test_window = grl_window_new (8, 8, "lrg-terrain-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

/* flat_heights:
 * (res + 1)² heights all equal to @h. */
static gfloat *
flat_heights (guint  res,
              gfloat h)
{
    guint   n = (res + 1) * (res + 1);
    gfloat *heights = g_new (gfloat, n);
    guint   k;

    for (k = 0; k < n; k++)
        heights[k] = h;
    return heights;
}

static void
test_terrain_new (void)
{
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (8, 64.0f);
    gdouble x;
    gdouble y;
    gdouble z;

    g_assert_nonnull (terrain);
    g_assert_cmpuint (lrg_terrain_get_resolution (terrain), ==, 8);
    g_assert_cmpfloat (lrg_terrain_get_chunk_size (terrain), ==, 64.0f);
    g_assert_cmpuint (lrg_terrain_get_chunk_count (terrain), ==, 0);
    g_assert_cmpuint (lrg_terrain_get_texture_repeat (terrain), ==, 1);
    g_assert_false (lrg_terrain_has_chunk (terrain, 0, 0));
    g_assert_false (lrg_terrain_height_at (terrain, 1.0, 1.0, NULL));
    g_assert_cmpuint (lrg_terrain_get_chunk_mesh_count (terrain, 0, 0), ==, 0);

    lrg_terrain_set_origin (terrain, 1.5, -2.0, 1e7);
    lrg_terrain_get_origin (terrain, &x, &y, &z);
    g_assert_cmpfloat (x, ==, 1.5);
    g_assert_cmpfloat (y, ==, -2.0);
    g_assert_cmpfloat (z, ==, 1e7);
}

static void
test_terrain_chunks (void)
{
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (2, 10.0f);
    g_autofree gfloat *heights = flat_heights (2, 3.0f);
    g_autoptr(GError) error = NULL;
    gdouble mn[3];
    gdouble mx[3];
    gint    cx;
    gint    cz;

    g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, heights, 9, NULL, NULL, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_terrain_set_chunk (terrain, -1, 0, heights, 9, NULL, NULL, &error));
    g_assert_true (lrg_terrain_set_chunk (terrain, -1, 0, heights, 9, NULL, NULL, &error));
    g_assert_cmpuint (lrg_terrain_get_chunk_count (terrain), ==, 2);
    g_assert_true (lrg_terrain_has_chunk (terrain, -1, 0));

    heights[4] = 7.0f;
    g_assert_true (lrg_terrain_set_chunk (terrain, 3, -2, heights, 9, NULL, NULL, &error));
    g_assert_true (lrg_terrain_get_chunk_bounds (terrain, 3, -2,
                                                 &mn[0], &mn[1], &mn[2],
                                                 &mx[0], &mx[1], &mx[2]));
    g_assert_cmpfloat (mn[0], ==, 30.0);
    g_assert_cmpfloat (mn[1], ==, 3.0);
    g_assert_cmpfloat (mn[2], ==, -20.0);
    g_assert_cmpfloat (mx[0], ==, 40.0);
    g_assert_cmpfloat (mx[1], ==, 7.0);
    g_assert_cmpfloat (mx[2], ==, -10.0);
    g_assert_false (lrg_terrain_get_chunk_bounds (terrain, 9, 9, NULL, NULL, NULL, NULL, NULL, NULL));

    lrg_terrain_world_to_chunk (terrain, -0.5, 25.0, &cx, &cz);
    g_assert_cmpint (cx, ==, -1);
    g_assert_cmpint (cz, ==, 2);

    g_assert_true (lrg_terrain_remove_chunk (terrain, -1, 0));
    g_assert_false (lrg_terrain_remove_chunk (terrain, -1, 0));
    g_assert_cmpuint (lrg_terrain_get_chunk_count (terrain), ==, 2);

    lrg_terrain_clear (terrain);
    g_assert_cmpuint (lrg_terrain_get_chunk_count (terrain), ==, 0);
}

static void
test_terrain_validation (void)
{
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (2, 10.0f);
    g_autofree gfloat *heights = flat_heights (2, 0.0f);
    guint8 variants[4] = { 0, 7, 8, 0 };
    GError *error = NULL;

    g_assert_false (lrg_terrain_set_chunk (terrain, 0, 0, heights, 8, NULL, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    heights[3] = NAN;
    g_assert_false (lrg_terrain_set_chunk (terrain, 0, 0, heights, 9, NULL, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    heights[3] = 0.0f;

    g_assert_false (lrg_terrain_set_chunk (terrain, 0, 0, heights, 9, NULL, variants, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_assert_cmpuint (lrg_terrain_get_chunk_count (terrain), ==, 0);
}

/* Bilinear interpolation inside a cell, exact at grid vertices, and
 * chunks at negative coordinates. */
static void
test_terrain_height_at (void)
{
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (2, 10.0f);
    /* 3 x 3 grid, cell size 5; row j = 0 is z = 0 */
    gfloat heights[9] = {
        0.0f, 2.0f, 4.0f,
        2.0f, 4.0f, 6.0f,
        4.0f, 6.0f, 20.0f
    };
    gfloat h;

    g_assert_true (lrg_terrain_set_chunk (terrain, -1, -1, heights, 9, NULL, NULL, NULL));

    /* chunk (-1, -1) spans [-10, 0] on both axes */
    g_assert_true (lrg_terrain_height_at (terrain, -10.0, -10.0, &h));
    g_assert_cmpfloat_with_epsilon (h, 0.0f, EPS);
    g_assert_true (lrg_terrain_height_at (terrain, -5.0, -10.0, &h));
    g_assert_cmpfloat_with_epsilon (h, 2.0f, EPS);
    g_assert_true (lrg_terrain_height_at (terrain, -7.5, -7.5, &h));
    g_assert_cmpfloat_with_epsilon (h, 2.0f, EPS);   /* (0 + 2 + 2 + 4) / 4 */
    /* last cell: (4 + 6 + 6 + 20) / 4 at its centre */
    g_assert_true (lrg_terrain_height_at (terrain, -2.5, -2.5, &h));
    g_assert_cmpfloat_with_epsilon (h, 9.0f, EPS);
    /* quarter of the way across x in cell (0, 0) at z = -10 */
    g_assert_true (lrg_terrain_height_at (terrain, -8.75, -10.0, &h));
    g_assert_cmpfloat_with_epsilon (h, 0.5f, EPS);

    g_assert_false (lrg_terrain_height_at (terrain, 0.5, -5.0, &h));
    g_assert_false (lrg_terrain_height_at (terrain, -5.0, -10.5, &h));
    g_assert_false (lrg_terrain_height_at (terrain, NAN, -5.0, &h));
}

/* Cells are grouped by material in ascending order; each cell has four
 * vertices and six indices. */
static void
test_terrain_mesh_grouping (void)
{
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (2, 10.0f);
    g_autofree gfloat *heights = flat_heights (2, 1.0f);
    guint          materials[4] = { 9, 3, 9, 9 };
    const gfloat  *positions;
    const guint16 *indices;
    guint          n;

    g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, heights, 9, materials, NULL, NULL));
    g_assert_cmpuint (lrg_terrain_get_chunk_mesh_count (terrain, 0, 0), ==, 2);
    g_assert_cmpuint (lrg_terrain_get_chunk_mesh_material (terrain, 0, 0, 0), ==, 3);
    g_assert_cmpuint (lrg_terrain_get_chunk_mesh_material (terrain, 0, 0, 1), ==, 9);
    g_assert_cmpuint (lrg_terrain_get_chunk_mesh_material (terrain, 0, 0, 7), ==, 0);

    /* material 3 is cell (1, 0): x in [5, 10], z in [0, 5], chunk-local */
    positions = lrg_terrain_get_chunk_mesh_positions (terrain, 0, 0, 0, &n);
    g_assert_cmpuint (n, ==, 12);
    g_assert_cmpfloat (positions[0], ==, 5.0f);
    g_assert_cmpfloat (positions[1], ==, 1.0f);
    g_assert_cmpfloat (positions[2], ==, 0.0f);
    g_assert_cmpfloat (positions[9], ==, 10.0f);
    g_assert_cmpfloat (positions[11], ==, 5.0f);

    indices = lrg_terrain_get_chunk_mesh_indices (terrain, 0, 0, 1, &n);
    g_assert_cmpuint (n, ==, 18);
    g_assert_cmpuint (indices[17], ==, 11);
    g_assert_cmpuint (lrg_terrain_get_chunk_mesh_positions (terrain, 0, 0, 1, &n) != NULL, ==, TRUE);
    g_assert_cmpuint (n, ==, 36);
    g_assert_null (lrg_terrain_get_chunk_mesh_normals (terrain, 0, 0, 5, &n));
    g_assert_cmpuint (n, ==, 0);
}

/* 128² cells of one material need 65536 vertices: one more than fits. */
static void
test_terrain_mesh_split (void)
{
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (128, 128.0f);
    g_autofree gfloat *heights = flat_heights (128, 0.0f);
    guint n0;
    guint n1;

    g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, heights, 129 * 129, NULL, NULL, NULL));
    g_assert_cmpuint (lrg_terrain_get_chunk_mesh_count (terrain, 0, 0), ==, 2);
    lrg_terrain_get_chunk_mesh_positions (terrain, 0, 0, 0, &n0);
    lrg_terrain_get_chunk_mesh_positions (terrain, 0, 0, 1, &n1);
    g_assert_cmpuint (n0 / 3, <=, LRG_TERRAIN_MAX_MESH_VERTICES);
    g_assert_cmpuint (n0 / 3 + n1 / 3, ==, 128 * 128 * 4);
}

/* Every triangle's geometric normal points up (front face under raylib's
 * counter-clockwise rule), on bumpy terrain too. */
static void
test_terrain_winding (void)
{
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (4, 8.0f);
    gfloat         heights[25];
    const gfloat  *p;
    const guint16 *idx;
    guint          n_idx;
    guint          k;

    for (k = 0; k < 25; k++)
        heights[k] = sinf ((gfloat)k * 1.3f) * 0.7f;
    g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, heights, 25, NULL, NULL, NULL));

    p = lrg_terrain_get_chunk_mesh_positions (terrain, 0, 0, 0, NULL);
    idx = lrg_terrain_get_chunk_mesh_indices (terrain, 0, 0, 0, &n_idx);
    g_assert_cmpuint (n_idx, ==, 16 * 6);
    for (k = 0; k < n_idx; k += 3)
    {
        const gfloat *a = &p[idx[k] * 3];
        const gfloat *b = &p[idx[k + 1] * 3];
        const gfloat *c = &p[idx[k + 2] * 3];
        gfloat e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        gfloat e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        gfloat ny = e1[2] * e2[0] - e1[0] * e2[2];

        g_assert_cmpfloat (ny, >, 0.0f);
    }
}

static void
assert_normal (const gfloat *n,
               gfloat        x,
               gfloat        y,
               gfloat        z)
{
    g_assert_cmpfloat_with_epsilon (n[0], x, EPS);
    g_assert_cmpfloat_with_epsilon (n[1], y, EPS);
    g_assert_cmpfloat_with_epsilon (n[2], z, EPS);
}

/* Normals of a plane, and edge normals that sample the neighbouring chunk
 * once it is loaded (and stop when it is removed). */
static void
test_terrain_normals (void)
{
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (2, 2.0f);
    g_autofree gfloat *flat = flat_heights (2, 0.0f);
    gfloat         slope[9];
    gfloat         len;
    const gfloat  *n;
    guint          count;
    guint          k;

    /* h = 0.5 x: normal (-0.5, 1, 0) normalized, everywhere */
    for (k = 0; k < 9; k++)
        slope[k] = 0.5f * (gfloat)(k % 3);
    g_assert_true (lrg_terrain_set_chunk (terrain, 5, 5, slope, 9, NULL, NULL, NULL));
    n = lrg_terrain_get_chunk_mesh_normals (terrain, 5, 5, 0, &count);
    g_assert_cmpuint (count, ==, 4 * 4 * 3);
    len = sqrtf (1.25f);
    for (k = 0; k < count; k += 3)
        assert_normal (&n[k], -0.5f / len, 1.0f / len, 0.0f);

    /* flat chunk (0, 0): its +X edge has normal +Y while alone. Cell
     * (1, 0) is the second cell, vertices 4 .. 7; its corner 1 (vertex 5)
     * is grid vertex (i 2, j 0) on the edge. */
    g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, flat, 9, NULL, NULL, NULL));
    n = lrg_terrain_get_chunk_mesh_normals (terrain, 0, 0, 0, NULL);
    assert_normal (&n[5 * 3], 0.0f, 1.0f, 0.0f);

    /* a neighbour at +X that rises by 1 one cell in: the shared edge
     * vertex (i = 2) now differences across the border:
     * dh/dx = (1 - 0) / (2 * 1) */
    {
        gfloat rise[9];

        for (k = 0; k < 9; k++)
            rise[k] = (k % 3 == 0) ? 0.0f : 1.0f;
        g_assert_true (lrg_terrain_set_chunk (terrain, 1, 0, rise, 9, NULL, NULL, NULL));
    }
    n = lrg_terrain_get_chunk_mesh_normals (terrain, 0, 0, 0, NULL);
    len = sqrtf (1.25f);
    assert_normal (&n[5 * 3], -0.5f / len, 1.0f / len, 0.0f);
    /* interior vertex (i 1) is unaffected */
    assert_normal (&n[4 * 3], 0.0f, 1.0f, 0.0f);

    g_assert_true (lrg_terrain_remove_chunk (terrain, 1, 0));
    n = lrg_terrain_get_chunk_mesh_normals (terrain, 0, 0, 0, NULL);
    assert_normal (&n[5 * 3], 0.0f, 1.0f, 0.0f);
}

/* One cell, each variant: corner texture coordinates. Corners are
 * (s, t) = (0, 0), (1, 0), (0, 1), (1, 1) before the variant. */
static void
test_terrain_uv_variants (void)
{
    static const struct
    {
        guint8 variant;
        gfloat uv[8];
    } cases[] = {
        { LRG_TERRAIN_UV_NONE,      { 0, 0, 1, 0, 0, 1, 1, 1 } },
        { LRG_TERRAIN_UV_ROTATE_90, { 0, 1, 0, 0, 1, 1, 1, 0 } },
        { LRG_TERRAIN_UV_FLIP_U,    { 1, 0, 0, 0, 1, 1, 0, 1 } },
        { LRG_TERRAIN_UV_FLIP_V,    { 0, 1, 1, 1, 0, 0, 1, 0 } },
        { LRG_TERRAIN_UV_FLIP_U | LRG_TERRAIN_UV_FLIP_V, { 1, 1, 0, 1, 1, 0, 0, 0 } },
        { LRG_TERRAIN_UV_ROTATE_90 | LRG_TERRAIN_UV_FLIP_U, { 1, 1, 1, 0, 0, 1, 0, 0 } }
    };
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (1, 4.0f);
    g_autofree gfloat *heights = flat_heights (1, 0.0f);
    guint c;

    for (c = 0; c < G_N_ELEMENTS (cases); c++)
    {
        const gfloat *uv;
        guint         n;
        guint         k;

        g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, heights, 4, NULL,
                                              &cases[c].variant, NULL));
        uv = lrg_terrain_get_chunk_mesh_texcoords (terrain, 0, 0, 0, &n);
        g_assert_cmpuint (n, ==, 8);
        for (k = 0; k < 8; k++)
            g_assert_cmpfloat_with_epsilon (uv[k], cases[c].uv[k], EPS);
    }
}

/* A texture spanning 2 x 2 cells continues across chunks, including
 * chunks at negative coordinates. */
static void
test_terrain_texture_repeat (void)
{
    g_autoptr(LrgTerrain) terrain = lrg_terrain_new (3, 3.0f);
    g_autofree gfloat *heights = flat_heights (3, 0.0f);
    const gfloat *uv;

    g_assert_true (lrg_terrain_set_chunk (terrain, -1, 0, heights, 16, NULL, NULL, NULL));
    g_assert_true (lrg_terrain_set_chunk (terrain, 0, 0, heights, 16, NULL, NULL, NULL));
    lrg_terrain_set_texture_repeat (terrain, 2);
    g_assert_cmpuint (lrg_terrain_get_texture_repeat (terrain), ==, 2);

    /* chunk 0, cell (1, 0): u 0.5 .. 1 */
    uv = lrg_terrain_get_chunk_mesh_texcoords (terrain, 0, 0, 0, NULL);
    g_assert_cmpfloat_with_epsilon (uv[8], 0.5f, EPS);
    g_assert_cmpfloat_with_epsilon (uv[10], 1.0f, EPS);
    /* chunk -1, cell (2, 0) is global column -1: u 0.5 .. 1, so it meets
     * global column 0 (u 0 .. 0.5) seamlessly */
    uv = lrg_terrain_get_chunk_mesh_texcoords (terrain, -1, 0, 0, NULL);
    g_assert_cmpfloat_with_epsilon (uv[16], 0.5f, EPS);
    g_assert_cmpfloat_with_epsilon (uv[18], 1.0f, EPS);
    /* chunk -1, cell (1, 1): global (-2, 1): u 0 .. 0.5, v 0.5 .. 1 */
    g_assert_cmpfloat_with_epsilon (uv[4 * 8 + 0], 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (uv[4 * 8 + 1], 0.5f, EPS);
    g_assert_cmpfloat_with_epsilon (uv[4 * 8 + 6], 0.5f, EPS);
    g_assert_cmpfloat_with_epsilon (uv[4 * 8 + 7], 1.0f, EPS);
}

static void
test_terrain_upload_headless (void)
{
    g_autoptr(LrgTerrain) terrain = NULL;
    g_autoptr(GError) error = NULL;

    if (graphics_available)
    {
        g_test_skip ("A graphics context exists");
        return;
    }

    terrain = lrg_terrain_new (2, 1.0f);
    g_assert_false (lrg_terrain_upload (terrain, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
}

/* Upload, draw with an origin, edit a chunk (re-upload), drop it. */
static void
test_terrain_draw (void)
{
    g_autoptr(LrgTerrain) terrain = NULL;
    g_autoptr(GrlImage)   image = NULL;
    g_autoptr(GrlTexture) texture = NULL;
    g_autoptr(GError)     error = NULL;
    g_autofree gfloat    *heights = flat_heights (8, 0.0f);
    guint      materials[64];
    Camera3D   camera;
    guint      k;

    if (!graphics_available)
    {
        g_test_skip ("No display available (headless environment)");
        return;
    }

    image = grl_image_new_color (4, 4, &(GrlColor){ 40, 160, 40, 255 });
    texture = grl_texture_new_from_image (image);
    g_assert_nonnull (texture);

    for (k = 0; k < 64; k++)
        materials[k] = k % 3;

    terrain = lrg_terrain_new (8, 32.0f);
    lrg_terrain_set_material_texture (terrain, 1, texture);
    g_assert_true (lrg_terrain_set_chunk (terrain, 1000, 1000, heights, 81, materials, NULL, &error));
    g_assert_true (lrg_terrain_set_chunk (terrain, 1001, 1000, heights, 81, NULL, NULL, &error));
    g_assert_false (lrg_terrain_is_uploaded (terrain));
    g_assert_true (lrg_terrain_upload (terrain, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_terrain_is_uploaded (terrain));

    /* far from world zero; the origin keeps it near the camera */
    lrg_terrain_set_origin (terrain, 32000.0, 0.0, 32000.0);

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3){ 16, 20, 60 };
    camera.target = (Vector3){ 16, 0, 16 };
    camera.up = (Vector3){ 0, 1, 0 };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginDrawing ();
    ClearBackground (BLACK);
    BeginMode3D (camera);
    lrg_terrain_draw (terrain);
    lrg_terrain_draw_chunk (terrain, 1000, 1000);
    lrg_terrain_draw_chunk (terrain, 0, 0);
    EndMode3D ();
    EndDrawing ();

    /* render to a texture and read back: the centre pixel shows the
     * green terrain, so the origin offset put the chunk in front of the
     * camera */
    {
        RenderTexture2D target = LoadRenderTexture (16, 16);
        Image           shot;
        Color           centre;

        for (k = 0; k < 3; k++)
            lrg_terrain_set_material_texture (terrain, k, texture);
        BeginTextureMode (target);
        ClearBackground (BLACK);
        BeginMode3D (camera);
        lrg_terrain_draw (terrain);
        EndMode3D ();
        EndTextureMode ();
        shot = LoadImageFromTexture (target.texture);
        centre = GetImageColor (shot, 8, 8);
        UnloadImage (shot);
        UnloadRenderTexture (target);
        g_assert_cmpuint (centre.g, >, 100);
        g_assert_cmpuint (centre.r, <, 100);
        for (k = 0; k < 3; k++)
            lrg_terrain_set_material_texture (terrain, k, NULL);
        lrg_terrain_set_material_texture (terrain, 1, texture);
    }

    /* a neighbour invalidates the edge normals of chunk (1000, 1000) */
    g_assert_true (lrg_terrain_set_chunk (terrain, 1000, 1001, heights, 81, NULL, NULL, &error));
    g_assert_false (lrg_terrain_is_uploaded (terrain));
    g_assert_true (lrg_terrain_upload (terrain, &error));
    g_assert_true (lrg_terrain_is_uploaded (terrain));

    lrg_terrain_set_material_texture (terrain, 1, NULL);
    BeginDrawing ();
    BeginMode3D (camera);
    lrg_terrain_draw (terrain);
    EndMode3D ();
    EndDrawing ();

    lrg_terrain_unload (terrain);
    g_assert_false (lrg_terrain_is_uploaded (terrain));
}

int
main (int   argc,
      char *argv[])
{
    gint result;

    g_test_init (&argc, &argv, NULL);

    graphics_available = init_graphics_context ();

    g_test_add_func ("/terrain/new", test_terrain_new);
    g_test_add_func ("/terrain/chunks", test_terrain_chunks);
    g_test_add_func ("/terrain/validation", test_terrain_validation);
    g_test_add_func ("/terrain/height-at", test_terrain_height_at);
    g_test_add_func ("/terrain/mesh-grouping", test_terrain_mesh_grouping);
    g_test_add_func ("/terrain/mesh-split", test_terrain_mesh_split);
    g_test_add_func ("/terrain/winding", test_terrain_winding);
    g_test_add_func ("/terrain/normals", test_terrain_normals);
    g_test_add_func ("/terrain/uv-variants", test_terrain_uv_variants);
    g_test_add_func ("/terrain/texture-repeat", test_terrain_texture_repeat);
    g_test_add_func ("/terrain/upload-headless", test_terrain_upload_headless);
    g_test_add_func ("/terrain/draw", test_terrain_draw);

    result = g_test_run ();

    g_clear_object (&test_window);
    return result;
}
