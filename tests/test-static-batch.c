/* test-static-batch.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgStaticBatch. Merging, grouping, transforms (including
 * normals under non-uniform and mirroring transforms), chunk splitting at
 * the 16-bit limit, bounds and input validation are headless; upload and
 * draw run in a hidden window when a display exists.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

#include "lrg-test-glb.h"

#define EPS (1e-5)

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
    test_window = grl_window_new (8, 8, "lrg-static-batch-test");
    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }
    return TRUE;
}

/* A unit quad in the XY plane facing +Z: 4 vertices, 2 triangles. */
static const gfloat quad_positions[12] = { 0, 0, 0,  1, 0, 0,  1, 1, 0,  0, 1, 0 };
static const gfloat quad_normals[12] = { 0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1 };
static const gfloat quad_uvs[8] = { 0, 0,  1, 0,  1, 1,  0, 1 };
static const guint32 quad_indices[6] = { 0, 1, 2,  0, 2, 3 };

static GrlMatrix
matrix_identity (void)
{
    GrlMatrix m;

    memset (&m, 0, sizeof m);
    m.m0 = m.m5 = m.m10 = m.m15 = 1.0f;
    return m;
}

/* matrix_trs:
 * Non-uniform scale then translation (raylib column-major fields). */
static GrlMatrix
matrix_trs (gfloat tx,
            gfloat ty,
            gfloat tz,
            gfloat sx,
            gfloat sy,
            gfloat sz)
{
    GrlMatrix m = matrix_identity ();

    m.m0 = sx;
    m.m5 = sy;
    m.m10 = sz;
    m.m12 = tx;
    m.m13 = ty;
    m.m14 = tz;
    return m;
}

static gboolean
add_quad (LrgStaticBatch  *batch,
          const GrlMatrix *transform,
          guint            material,
          guint            layer)
{
    return lrg_static_batch_add_mesh (batch, quad_positions, quad_normals, quad_uvs, NULL, 4,
                                      quad_indices, 6, transform, material, layer, NULL);
}

/* ========================================================================== */
/*                                   Merge                                    */
/* ========================================================================== */

static void
test_batch_empty (void)
{
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    guint n = 99;

    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 0);
    g_assert_cmpuint (lrg_static_batch_get_vertex_count (batch), ==, 0);
    g_assert_cmpuint (lrg_static_batch_get_triangle_count (batch), ==, 0);
    g_assert_cmpuint (lrg_static_batch_get_max_chunk_vertices (batch), ==, 65535);
    g_assert_true (lrg_static_batch_is_uploaded (batch));
    g_assert_null (lrg_static_batch_get_chunk_positions (batch, 0, &n));
    g_assert_cmpuint (n, ==, 0);
    g_assert_false (lrg_static_batch_get_chunk_bounds (batch, 0, NULL, NULL, NULL, NULL, NULL, NULL));

    /* An empty mesh is a no-op. */
    g_assert_true (lrg_static_batch_add_mesh (batch, NULL, NULL, NULL, NULL, 0, NULL, 0,
                                              NULL, 0, 0, NULL));
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 0);
}

static void
test_batch_merge_groups (void)
{
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    GrlMatrix      right = matrix_trs (2, 0, 0, 1, 1, 1);
    const guint16 *indices;
    const guint8  *colors;
    guint          n = 0;

    g_assert_true (add_quad (batch, NULL, 7, 0));
    g_assert_true (add_quad (batch, &right, 7, 0));
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 1);
    g_assert_cmpuint (lrg_static_batch_get_chunk_vertex_count (batch, 0), ==, 8);
    g_assert_cmpuint (lrg_static_batch_get_chunk_index_count (batch, 0), ==, 12);
    g_assert_cmpuint (lrg_static_batch_get_chunk_material (batch, 0), ==, 7);
    g_assert_cmpuint (lrg_static_batch_get_chunk_layer (batch, 0), ==, 0);

    /* The second mesh's indices are offset by the first's vertices. */
    indices = lrg_static_batch_get_chunk_indices (batch, 0, &n);
    g_assert_cmpuint (n, ==, 12);
    g_assert_cmpuint (indices[0], ==, 0);
    g_assert_cmpuint (indices[5], ==, 3);
    g_assert_cmpuint (indices[6], ==, 4);
    g_assert_cmpuint (indices[11], ==, 7);

    /* Default colours are white. */
    colors = lrg_static_batch_get_chunk_colors (batch, 0, &n);
    g_assert_cmpuint (n, ==, 32);
    g_assert_cmpuint (colors[0], ==, 255);
    g_assert_cmpuint (colors[31], ==, 255);

    /* Other material or other layer: separate chunks. */
    g_assert_true (add_quad (batch, NULL, 8, 0));
    g_assert_true (add_quad (batch, NULL, 7, 3));
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 3);
    g_assert_cmpuint (lrg_static_batch_get_chunk_material (batch, 1), ==, 8);
    g_assert_cmpuint (lrg_static_batch_get_chunk_layer (batch, 2), ==, 3);

    /* Back to (7, 0): appended to the first chunk. */
    g_assert_true (add_quad (batch, NULL, 7, 0));
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 3);
    g_assert_cmpuint (lrg_static_batch_get_chunk_vertex_count (batch, 0), ==, 12);
    g_assert_cmpuint (lrg_static_batch_get_vertex_count (batch), ==, 20);
    g_assert_cmpuint (lrg_static_batch_get_triangle_count (batch), ==, 10);

    lrg_static_batch_clear (batch);
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 0);
}

static void
test_batch_transform_bounds (void)
{
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    GrlMatrix     m = matrix_trs (10, 20, 30, 2, 3, 4);
    const gfloat *p;
    const gfloat *normals;
    guint         n = 0;
    gfloat        min_x, min_y, min_z, max_x, max_y, max_z;

    g_assert_true (add_quad (batch, &m, 0, 1));
    p = lrg_static_batch_get_chunk_positions (batch, 0, &n);
    g_assert_cmpuint (n, ==, 12);
    /* Vertex 2 = (1, 1, 0) -> (12, 23, 30). */
    g_assert_cmpfloat_with_epsilon (p[6], 12, EPS);
    g_assert_cmpfloat_with_epsilon (p[7], 23, EPS);
    g_assert_cmpfloat_with_epsilon (p[8], 30, EPS);

    g_assert_true (lrg_static_batch_get_chunk_bounds (batch, 0, &min_x, &min_y, &min_z,
                                                      &max_x, &max_y, &max_z));
    g_assert_cmpfloat_with_epsilon (min_x, 10, EPS);
    g_assert_cmpfloat_with_epsilon (min_y, 20, EPS);
    g_assert_cmpfloat_with_epsilon (min_z, 30, EPS);
    g_assert_cmpfloat_with_epsilon (max_x, 12, EPS);
    g_assert_cmpfloat_with_epsilon (max_y, 23, EPS);
    g_assert_cmpfloat_with_epsilon (max_z, 30, EPS);

    /* +Z normals under a positive scale stay +Z and unit length. */
    normals = lrg_static_batch_get_chunk_normals (batch, 0, &n);
    g_assert_cmpfloat_with_epsilon (normals[2], 1, EPS);
    g_assert_cmpfloat_with_epsilon (normals[0], 0, EPS);

    /* Bounds grow as meshes are appended; optional outputs may be NULL. */
    m = matrix_trs (-5, 0, 0, 1, 1, 1);
    g_assert_true (add_quad (batch, &m, 0, 1));
    g_assert_true (lrg_static_batch_get_chunk_bounds (batch, 0, &min_x, NULL, NULL, &max_x, NULL, NULL));
    g_assert_cmpfloat_with_epsilon (min_x, -5, EPS);
    g_assert_cmpfloat_with_epsilon (max_x, 12, EPS);
}

static void
test_batch_normals (void)
{
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    /* One triangle with a 45-degree normal in XY. */
    static const gfloat tri[9] = { 0, 0, 0,  0, 0, 1,  1, -1, 0 };
    static const gfloat diag[9] = { 0.70710678f, 0.70710678f, 0,
                                    0.70710678f, 0.70710678f, 0,
                                    0.70710678f, 0.70710678f, 0 };
    GrlMatrix      stretch = matrix_trs (0, 0, 0, 2, 1, 1);
    GrlMatrix      rotate = matrix_identity ();
    GrlMatrix      mirror = matrix_trs (0, 0, 0, -1, 1, 1);
    const gfloat  *n;
    const guint16 *idx;
    guint          count;
    gdouble        len;

    /* Non-uniform scale: normals use the inverse transpose, (x/2, y, z). */
    g_assert_true (lrg_static_batch_add_mesh (batch, tri, diag, NULL, NULL, 3, NULL, 0,
                                              &stretch, 0, 0, NULL));
    n = lrg_static_batch_get_chunk_normals (batch, 0, &count);
    len = sqrt (0.25 + 1.0);
    g_assert_cmpfloat_with_epsilon (n[0], 0.5 / len, 1e-4);
    g_assert_cmpfloat_with_epsilon (n[1], 1.0 / len, 1e-4);

    /* 90 degrees about Z: +X-ish normal rotates like the geometry. */
    rotate.m0 = 0;  rotate.m4 = -1;
    rotate.m1 = 1;  rotate.m5 = 0;
    g_assert_true (lrg_static_batch_add_mesh (batch, tri, diag, NULL, NULL, 3, NULL, 0,
                                              &rotate, 1, 0, NULL));
    n = lrg_static_batch_get_chunk_normals (batch, 1, &count);
    g_assert_cmpfloat_with_epsilon (n[0], -0.70710678, 1e-4);
    g_assert_cmpfloat_with_epsilon (n[1], 0.70710678, 1e-4);

    /* Mirror in X: normal x flips and the winding is reversed. */
    g_assert_true (lrg_static_batch_add_mesh (batch, tri, diag, NULL, NULL, 3, NULL, 0,
                                              &mirror, 2, 0, NULL));
    n = lrg_static_batch_get_chunk_normals (batch, 2, &count);
    g_assert_cmpfloat_with_epsilon (n[0], -0.70710678, 1e-4);
    g_assert_cmpfloat_with_epsilon (n[1], 0.70710678, 1e-4);
    idx = lrg_static_batch_get_chunk_indices (batch, 2, &count);
    g_assert_cmpuint (count, ==, 3);
    g_assert_cmpuint (idx[0], ==, 0);
    g_assert_cmpuint (idx[1], ==, 2);
    g_assert_cmpuint (idx[2], ==, 1);

    /* Missing normals default to +Y (transformed). */
    g_assert_true (lrg_static_batch_add_mesh (batch, tri, NULL, NULL, NULL, 3, NULL, 0,
                                              NULL, 3, 0, NULL));
    n = lrg_static_batch_get_chunk_normals (batch, 3, &count);
    g_assert_cmpfloat_with_epsilon (n[1], 1.0, EPS);
}

static void
test_batch_split_whole_meshes (void)
{
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    guint i;

    /* Quads (4 vertices) never share a 6-vertex chunk. */
    lrg_static_batch_set_max_chunk_vertices (batch, 6);
    g_assert_cmpuint (lrg_static_batch_get_max_chunk_vertices (batch), ==, 6);
    for (i = 0; i < 4; i++)
        g_assert_true (add_quad (batch, NULL, 0, 0));
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 4);
    for (i = 0; i < 4; i++)
        g_assert_cmpuint (lrg_static_batch_get_chunk_vertex_count (batch, i), ==, 4);

    lrg_static_batch_set_max_chunk_vertices (batch, 1);
    g_assert_cmpuint (lrg_static_batch_get_max_chunk_vertices (batch), ==, 3);
    lrg_static_batch_set_max_chunk_vertices (batch, 1000000);
    g_assert_cmpuint (lrg_static_batch_get_max_chunk_vertices (batch), ==, 65535);
}

/* A triangle strip-like grid mesh: n_columns + 1 vertices along two rows,
 * 2 * n_columns triangles sharing vertices. */
static void
make_ribbon (guint     n_columns,
             gfloat  **positions,
             guint32 **indices,
             guint    *n_vertices,
             guint    *n_indices)
{
    guint c;

    *n_vertices = (n_columns + 1) * 2;
    *n_indices = n_columns * 6;
    *positions = g_new (gfloat, *n_vertices * 3);
    *indices = g_new (guint32, *n_indices);
    for (c = 0; c <= n_columns; c++)
    {
        (*positions)[(c * 2) * 3 + 0] = (gfloat)c;
        (*positions)[(c * 2) * 3 + 1] = 0;
        (*positions)[(c * 2) * 3 + 2] = 0;
        (*positions)[(c * 2 + 1) * 3 + 0] = (gfloat)c;
        (*positions)[(c * 2 + 1) * 3 + 1] = 1;
        (*positions)[(c * 2 + 1) * 3 + 2] = 0;
    }
    for (c = 0; c < n_columns; c++)
    {
        guint32 a = c * 2, b = c * 2 + 1, d = c * 2 + 2, e = c * 2 + 3;

        (*indices)[c * 6 + 0] = a;
        (*indices)[c * 6 + 1] = d;
        (*indices)[c * 6 + 2] = b;
        (*indices)[c * 6 + 3] = b;
        (*indices)[c * 6 + 4] = d;
        (*indices)[c * 6 + 5] = e;
    }
}

static void
test_batch_split_triangles (void)
{
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    g_autofree gfloat  *positions = NULL;
    g_autofree guint32 *indices = NULL;
    guint               n_vertices, n_indices;
    guint               chunk, t, total_triangles = 0, source_triangle = 0;

    /* 20 columns = 42 vertices, 40 triangles; 7-vertex chunks force a
     * split inside the mesh. */
    make_ribbon (20, &positions, &indices, &n_vertices, &n_indices);
    lrg_static_batch_set_max_chunk_vertices (batch, 7);
    g_assert_true (lrg_static_batch_add_mesh (batch, positions, NULL, NULL, NULL, n_vertices,
                                              indices, n_indices, NULL, 0, 0, NULL));
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), >, 5);

    /* Every chunk respects the limit, and walking the chunks in order
     * reproduces every source triangle's positions exactly. */
    for (chunk = 0; chunk < lrg_static_batch_get_chunk_count (batch); chunk++)
    {
        guint          n_pos, n_idx;
        const gfloat  *p = lrg_static_batch_get_chunk_positions (batch, chunk, &n_pos);
        const guint16 *idx = lrg_static_batch_get_chunk_indices (batch, chunk, &n_idx);

        g_assert_cmpuint (n_pos / 3, <=, 7);
        for (t = 0; t < n_idx; t++)
        {
            guint32 src = indices[source_triangle * 3 + t % 3];

            g_assert_cmpuint (idx[t], <, n_pos / 3);
            g_assert_cmpfloat (p[idx[t] * 3 + 0], ==, positions[src * 3 + 0]);
            g_assert_cmpfloat (p[idx[t] * 3 + 1], ==, positions[src * 3 + 1]);
            if (t % 3 == 2)
                source_triangle++;
        }
        total_triangles += n_idx / 3;
    }
    g_assert_cmpuint (total_triangles, ==, 40);
    g_assert_cmpuint (source_triangle, ==, 40);
}

static void
test_batch_split_u16_limit (void)
{
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    g_autofree gfloat *positions = NULL;
    const guint16     *idx;
    guint              n_vertices = 70002;   /* 23334 unindexed triangles */
    guint              i, n_idx;

    positions = g_new (gfloat, n_vertices * 3);
    for (i = 0; i < n_vertices * 3; i++)
        positions[i] = (gfloat)(i % 97);

    g_assert_true (lrg_static_batch_add_mesh (batch, positions, NULL, NULL, NULL, n_vertices,
                                              NULL, 0, NULL, 4, 2, NULL));
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 2);
    /* 21845 triangles fill 65535 vertices; the rest spill over. */
    g_assert_cmpuint (lrg_static_batch_get_chunk_vertex_count (batch, 0), ==, 65535);
    g_assert_cmpuint (lrg_static_batch_get_chunk_vertex_count (batch, 1), ==, 70002 - 65535);
    g_assert_cmpuint (lrg_static_batch_get_triangle_count (batch), ==, 23334);

    idx = lrg_static_batch_get_chunk_indices (batch, 0, &n_idx);
    g_assert_cmpuint (idx[n_idx - 1], ==, 65534);

    /* A further small mesh for the same key goes into the second chunk. */
    g_assert_true (add_quad (batch, NULL, 4, 2));
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 2);
    g_assert_cmpuint (lrg_static_batch_get_chunk_vertex_count (batch, 1), ==, 70002 - 65535 + 4);
}

static void
test_batch_invalid (void)
{
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    g_autoptr(GError) error = NULL;
    static const guint32 bad_index[3] = { 0, 1, 4 };
    static const gfloat  nan_positions[12] = { 0, 0, 0,  1, 0, 0,  1, NAN, 0,  0, 1, 0 };
    GrlMatrix singular = matrix_trs (0, 0, 0, 1, 0, 1);
    GrlMatrix infinite = matrix_identity ();

    infinite.m12 = INFINITY;

    g_assert_false (lrg_static_batch_add_mesh (batch, quad_positions, NULL, NULL, NULL, 4,
                                               quad_indices, 6, NULL, 0, 32, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (lrg_static_batch_add_mesh (batch, quad_positions, NULL, NULL, NULL, 4,
                                               quad_indices, 5, NULL, 0, 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    /* Unindexed vertex count must be a multiple of 3. */
    g_assert_false (lrg_static_batch_add_mesh (batch, quad_positions, NULL, NULL, NULL, 4,
                                               NULL, 0, NULL, 0, 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (lrg_static_batch_add_mesh (batch, quad_positions, NULL, NULL, NULL, 4,
                                               bad_index, 3, NULL, 0, 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (lrg_static_batch_add_mesh (batch, nan_positions, NULL, NULL, NULL, 4,
                                               quad_indices, 6, NULL, 0, 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (add_quad (batch, &singular, 0, 0));
    g_assert_false (lrg_static_batch_add_mesh (batch, quad_positions, NULL, NULL, NULL, 4,
                                               quad_indices, 6, &infinite, 0, 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_assert_false (lrg_static_batch_add_mesh (batch, NULL, NULL, NULL, NULL, 3,
                                               NULL, 0, NULL, 0, 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);

    /* Nothing was added by any failure. */
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 0);
}

static void
test_batch_upload_headless (void)
{
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    g_autoptr(GError) error = NULL;

    if (graphics_available)
    {
        g_test_skip ("A graphics context exists; covered by /static-batch/gl/upload-draw");
        return;
    }
    g_assert_true (add_quad (batch, NULL, 0, 0));
    g_assert_false (lrg_static_batch_is_uploaded (batch));
    g_assert_false (lrg_static_batch_upload (batch, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED);
    /* Drawing un-uploaded chunks is a no-op. */
    lrg_static_batch_draw (batch, LRG_STATIC_BATCH_ALL_LAYERS);
    lrg_static_batch_unload (batch);
}

/* ========================================================================== */
/*                               GL (display)                                 */
/* ========================================================================== */

static void
test_batch_gl (void)
{
    g_autoptr(LrgStaticBatch)  batch = NULL;
    g_autoptr(LrgAssetManager) manager = NULL;
    g_autoptr(GrlMesh)         cube = NULL;
    g_autoptr(GrlImage)        image = NULL;
    g_autoptr(GrlTexture)      texture = NULL;
    g_autoptr(GError)          error = NULL;
    g_autofree gchar          *path = NULL;
    GrlMatrix                  offset = matrix_trs (0, 0, -3, 1, 1, 1);
    GrlColor                   half = { 255, 255, 255, 128 };
    GrlModel                  *rock;
    Camera3D                   camera;

    if (!graphics_available)
    {
        g_test_skip ("Graphics context not available");
        return;
    }

    batch = lrg_static_batch_new ();

    /* A GrlMesh keeps its CPU arrays after upload. */
    cube = grl_mesh_new_cube (1, 1, 1);
    g_assert_true (lrg_static_batch_add_grl_mesh (batch, cube, &offset, 5, 1, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_static_batch_get_chunk_vertex_count (batch, 0), ==, 24);
    g_assert_cmpuint (lrg_static_batch_get_triangle_count (batch), ==, 12);

    /* A model from the asset manager. */
    path = g_build_filename (g_get_tmp_dir (), "lrg-batch-rock.glb", NULL);
    test_glb_write_static_triangle (path);
    manager = lrg_asset_manager_new ();
    rock = lrg_asset_manager_load_model (manager, path, &error);
    g_assert_no_error (error);
    g_assert_true (lrg_static_batch_add_model (batch, rock, NULL, 2, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 2);
    g_assert_cmpuint (lrg_static_batch_get_chunk_layer (batch, 1), ==, 2);

    image = grl_image_new_color (4, 4, &half);
    texture = grl_texture_new_from_image (image);
    lrg_static_batch_set_material_texture (batch, 5, texture);
    lrg_static_batch_set_layer_tint (batch, 2, &half);
    lrg_static_batch_set_shader (batch, NULL);

    g_assert_false (lrg_static_batch_is_uploaded (batch));
    g_assert_true (lrg_static_batch_upload (batch, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_static_batch_is_uploaded (batch));

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3) { 0, 2, 4 };
    camera.up = (Vector3) { 0, 1, 0 };
    camera.fovy = 45;
    camera.projection = CAMERA_PERSPECTIVE;
    BeginDrawing ();
    ClearBackground (BLACK);
    BeginMode3D (camera);
    lrg_static_batch_draw (batch, LRG_STATIC_BATCH_ALL_LAYERS);
    lrg_static_batch_draw (batch, 1u << 2);
    lrg_static_batch_draw_chunk (batch, 0);
    lrg_static_batch_draw_chunk (batch, 99);
    EndMode3D ();
    EndDrawing ();

    /* Adding after upload re-marks the chunk; re-upload replaces it. */
    g_assert_true (add_quad (batch, NULL, 5, 1));
    g_assert_false (lrg_static_batch_is_uploaded (batch));
    g_assert_true (lrg_static_batch_upload (batch, &error));
    g_assert_true (lrg_static_batch_is_uploaded (batch));

    lrg_static_batch_unload (batch);
    g_assert_false (lrg_static_batch_is_uploaded (batch));
    g_assert_true (lrg_static_batch_upload (batch, &error));

    lrg_static_batch_set_material_texture (batch, 5, NULL);
    g_clear_object (&batch);
    g_clear_object (&manager);
    g_remove (path);
}

int
main (int   argc,
      char *argv[])
{
    int result;

    g_test_init (&argc, &argv, NULL);
    graphics_available = init_graphics_context ();

    g_test_add_func ("/static-batch/empty", test_batch_empty);
    g_test_add_func ("/static-batch/merge-groups", test_batch_merge_groups);
    g_test_add_func ("/static-batch/transform-bounds", test_batch_transform_bounds);
    g_test_add_func ("/static-batch/normals", test_batch_normals);
    g_test_add_func ("/static-batch/split/whole-meshes", test_batch_split_whole_meshes);
    g_test_add_func ("/static-batch/split/triangles", test_batch_split_triangles);
    g_test_add_func ("/static-batch/split/u16-limit", test_batch_split_u16_limit);
    g_test_add_func ("/static-batch/invalid", test_batch_invalid);
    g_test_add_func ("/static-batch/upload-headless", test_batch_upload_headless);
    g_test_add_func ("/static-batch/gl/upload-draw", test_batch_gl);

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
