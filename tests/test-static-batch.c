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
#include <rlgl.h>

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
/*                     Reference geometry (independent math)                  */
/* ========================================================================== */

/* matrix_trs_y:
 * T * Ry(angle) * S in raylib field form (column vectors, translation in
 * m12..m14): how a game places a piece. */
static GrlMatrix
matrix_trs_y (gfloat tx,
              gfloat ty,
              gfloat tz,
              gfloat angle,
              gfloat sx,
              gfloat sy,
              gfloat sz)
{
    GrlMatrix m = matrix_identity ();
    gfloat    c = cosf (angle);
    gfloat    s = sinf (angle);

    m.m0 = c * sx;   m.m4 = 0;    m.m8 = s * sz;
    m.m1 = 0;        m.m5 = sy;   m.m9 = 0;
    m.m2 = -s * sx;  m.m6 = 0;    m.m10 = c * sz;
    m.m12 = tx;
    m.m13 = ty;
    m.m14 = tz;
    return m;
}

/* to_columns / from_columns:
 * raylib field form <-> column-major doubles, cm[col * 4 + row]. */
static void
to_columns (const GrlMatrix *m,
            gdouble         *cm)
{
    cm[0] = m->m0;   cm[1] = m->m1;   cm[2] = m->m2;   cm[3] = m->m3;
    cm[4] = m->m4;   cm[5] = m->m5;   cm[6] = m->m6;   cm[7] = m->m7;
    cm[8] = m->m8;   cm[9] = m->m9;   cm[10] = m->m10; cm[11] = m->m11;
    cm[12] = m->m12; cm[13] = m->m13; cm[14] = m->m14; cm[15] = m->m15;
}

static GrlMatrix
from_columns (const gdouble *cm)
{
    GrlMatrix m;

    m.m0 = (gfloat)cm[0];   m.m1 = (gfloat)cm[1];   m.m2 = (gfloat)cm[2];   m.m3 = (gfloat)cm[3];
    m.m4 = (gfloat)cm[4];   m.m5 = (gfloat)cm[5];   m.m6 = (gfloat)cm[6];   m.m7 = (gfloat)cm[7];
    m.m8 = (gfloat)cm[8];   m.m9 = (gfloat)cm[9];   m.m10 = (gfloat)cm[10]; m.m11 = (gfloat)cm[11];
    m.m12 = (gfloat)cm[12]; m.m13 = (gfloat)cm[13]; m.m14 = (gfloat)cm[14]; m.m15 = (gfloat)cm[15];
    return m;
}

/* compose:
 * outer * inner as column-vector math (inner applies first). */
static GrlMatrix
compose (const GrlMatrix *outer,
         const GrlMatrix *inner)
{
    gdouble a[16], b[16], r[16];
    gint    col, row, k;

    to_columns (outer, a);
    to_columns (inner, b);
    for (col = 0; col < 4; col++)
        for (row = 0; row < 4; row++)
        {
            r[col * 4 + row] = 0.0;
            for (k = 0; k < 4; k++)
                r[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
        }
    return from_columns (r);
}

/*
 * SoupGroup:
 *
 * Expected triangle soup (9 floats per triangle, world space, in append
 * order) for one (material, layer) pair.
 */
typedef struct
{
    guint   material;
    guint   layer;
    GArray *soup;
} SoupGroup;

static void
soup_group_free (gpointer data)
{
    SoupGroup *group = data;

    g_array_unref (group->soup);
    g_free (group);
}

static SoupGroup *
soup_group (GPtrArray *groups,
            guint      material,
            guint      layer)
{
    SoupGroup *group;
    guint      i;

    for (i = 0; i < groups->len; i++)
    {
        group = g_ptr_array_index (groups, i);
        if (group->material == material && group->layer == layer)
            return group;
    }
    group = g_new0 (SoupGroup, 1);
    group->material = material;
    group->layer = layer;
    group->soup = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_ptr_array_add (groups, group);
    return group;
}

/* expect_mesh:
 * Appends the triangles of one source mesh, transformed by @m, to the
 * expected soup. @indices16 / @indices32 may both be NULL for a plain
 * triangle list. */
static void
expect_mesh (GPtrArray       *groups,
             guint            material,
             guint            layer,
             const gfloat    *positions,
             guint            n_vertices,
             const guint16   *indices16,
             const guint32   *indices32,
             guint            n_indices,
             const GrlMatrix *m)
{
    SoupGroup *group = soup_group (groups, material, layer);
    gdouble    cm[16];
    guint      k;

    to_columns (m, cm);
    if (indices16 == NULL && indices32 == NULL)
        n_indices = n_vertices;
    for (k = 0; k < n_indices; k++)
    {
        guint         v = indices16 != NULL ? indices16[k] : indices32 != NULL ? indices32[k] : k;
        const gfloat *p = positions + v * 3;
        gfloat        w[3];
        gint          row;

        for (row = 0; row < 3; row++)
            w[row] = (gfloat)(cm[row] * p[0] + cm[4 + row] * p[1] + cm[8 + row] * p[2] + cm[12 + row]);
        g_array_append_vals (group->soup, w, 3);
    }
}

/* actual_soup:
 * The batch's triangles for (material, layer): every matching chunk in
 * chunk order, each index resolved to its merged position. */
static GArray *
actual_soup (LrgStaticBatch *batch,
             guint           material,
             guint           layer)
{
    GArray *soup = g_array_new (FALSE, FALSE, sizeof (gfloat));
    guint   c;

    for (c = 0; c < lrg_static_batch_get_chunk_count (batch); c++)
    {
        const gfloat  *positions;
        const guint16 *indices;
        guint          n_floats = 0;
        guint          n_indices = 0;
        guint          k;

        if (lrg_static_batch_get_chunk_material (batch, c) != material ||
            lrg_static_batch_get_chunk_layer (batch, c) != layer)
            continue;
        positions = lrg_static_batch_get_chunk_positions (batch, c, &n_floats);
        indices = lrg_static_batch_get_chunk_indices (batch, c, &n_indices);
        g_assert_cmpuint (n_indices % 3, ==, 0);
        for (k = 0; k < n_indices; k++)
        {
            g_assert_cmpuint ((guint)indices[k] * 3 + 2, <, n_floats);
            g_array_append_vals (soup, positions + indices[k] * 3, 3);
        }
    }
    return soup;
}

/* assert_batch_matches:
 * Every expected group equals the batch's soup, and the batch holds no
 * triangles beyond the expected ones. */
static void
assert_batch_matches (LrgStaticBatch *batch,
                      GPtrArray      *groups,
                      gdouble         tolerance)
{
    guint total = 0;
    guint i;

    for (i = 0; i < groups->len; i++)
    {
        SoupGroup        *group = g_ptr_array_index (groups, i);
        g_autoptr(GArray) soup = actual_soup (batch, group->material, group->layer);
        guint             k;

        g_assert_cmpuint (soup->len, ==, group->soup->len);
        for (k = 0; k < soup->len; k++)
        {
            gdouble want = g_array_index (group->soup, gfloat, k);
            gdouble got = g_array_index (soup, gfloat, k);

            g_assert_cmpfloat_with_epsilon (got, want, tolerance * MAX (1.0, fabs (want)));
        }
        total += soup->len / 9;
    }
    g_assert_cmpuint (total, ==, lrg_static_batch_get_triangle_count (batch));
}

static void
test_batch_matches_sources (void)
{
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    g_autoptr(GPtrArray)      groups = g_ptr_array_new_with_free_func (soup_group_free);
    g_autoptr(GError)         error = NULL;
    static const gfloat       list[18] = { 0, 0, 0,  1, 0, 1,  1, 0, 0,
                                           0, 0, 0,  0, 0, 1,  1, 0, 1 };
    static const guint16      want_indices[18] = { 0, 1, 2, 0, 2, 3,
                                                   4, 5, 6, 7, 8, 9,
                                                   10, 11, 12, 10, 12, 13 };
    GrlMatrix                 a = matrix_trs_y (1, 2, 3, 0.7f, 2, 1, 0.5f);
    GrlMatrix                 b = matrix_trs_y (-4, 0, 1, -1.2f, 1, 3, 1);
    GrlMatrix                 c = matrix_trs_y (0, -1, 7, 2.5f, 0.25f, 0.25f, 0.25f);
    const guint16            *indices;
    guint                     n = 0;

    /* Indexed quad, non-indexed triangle list, indexed quad again. */
    g_assert_true (add_quad (batch, &a, 3, 1));
    expect_mesh (groups, 3, 1, quad_positions, 4, NULL, quad_indices, 6, &a);
    g_assert_true (lrg_static_batch_add_mesh (batch, list, NULL, NULL, NULL, 6, NULL, 0,
                                              &b, 3, 1, &error));
    g_assert_no_error (error);
    expect_mesh (groups, 3, 1, list, 6, NULL, NULL, 0, &b);
    g_assert_true (add_quad (batch, &c, 3, 1));
    expect_mesh (groups, 3, 1, quad_positions, 4, NULL, quad_indices, 6, &c);

    g_assert_cmpuint (lrg_static_batch_get_chunk_count (batch), ==, 1);
    indices = lrg_static_batch_get_chunk_indices (batch, 0, &n);
    g_assert_cmpuint (n, ==, G_N_ELEMENTS (want_indices));
    g_assert_true (memcmp (indices, want_indices, sizeof want_indices) == 0);
    assert_batch_matches (batch, groups, 1e-5);
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

/*
 * Piece:
 *
 * One model placed in the world, for the GL comparison test.
 */
typedef struct
{
    GrlModel  *model;   /* borrowed from the asset manager */
    GrlMatrix  world;
} Piece;

/* find_asset:
 * A file of the parent game's asset tree (read-only), or NULL. */
static gchar *
find_asset (const gchar *relative)
{
    static const gchar *const roots[] = { "../../../data/assets", "../../data/assets", NULL };
    guint i;

    for (i = 0; roots[i] != NULL; i++)
    {
        g_autofree gchar *path = g_build_filename (roots[i], relative, NULL);

        /* Absolute, so the asset manager needs no search path. */
        if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
            return g_canonicalize_filename (path, NULL);
    }
    return NULL;
}

/* model_mesh_material:
 * The batch material key of mesh @i: its albedo texture id. */
static guint
model_mesh_material (const Model *raw,
                     gint         i)
{
    if (raw->materials == NULL || raw->meshMaterial == NULL ||
        raw->materials[raw->meshMaterial[i]].maps == NULL)
        return 0;
    return raw->materials[raw->meshMaterial[i]].maps[MATERIAL_MAP_ALBEDO].texture.id;
}

/* draw_reference:
 * Draws every piece mesh by mesh with raylib's DrawMesh() and
 * world * model.transform, using the same flat material the batch uses. */
static void
draw_reference (GArray *pieces)
{
    guint p;

    for (p = 0; p < pieces->len; p++)
    {
        Piece    *piece = &g_array_index (pieces, Piece, p);
        Model    *raw = grl_model_get_handle (piece->model);
        GrlMatrix model_transform;
        GrlMatrix full;
        gint      i;

        memcpy (&model_transform, &raw->transform, sizeof model_transform);
        full = compose (&piece->world, &model_transform);
        for (i = 0; i < raw->meshCount; i++)
        {
            MaterialMap maps[16];
            Material    material;
            Matrix      m;
            guint       texture = model_mesh_material (raw, i);

            memset (maps, 0, sizeof maps);
            memset (&material, 0, sizeof material);
            material.shader.id = rlGetShaderIdDefault ();
            material.shader.locs = rlGetShaderLocsDefault ();
            if (texture != 0)
                maps[MATERIAL_MAP_ALBEDO].texture = raw->materials[raw->meshMaterial[i]].maps[MATERIAL_MAP_ALBEDO].texture;
            else
            {
                maps[MATERIAL_MAP_ALBEDO].texture.id = rlGetTextureIdDefault ();
                maps[MATERIAL_MAP_ALBEDO].texture.width = 1;
                maps[MATERIAL_MAP_ALBEDO].texture.height = 1;
                maps[MATERIAL_MAP_ALBEDO].texture.mipmaps = 1;
                maps[MATERIAL_MAP_ALBEDO].texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            }
            maps[MATERIAL_MAP_ALBEDO].color = WHITE;
            material.maps = maps;
            memcpy (&m, &full, sizeof m);
            DrawMesh (raw->meshes[i], material, m);
        }
    }
}

/* render_scene:
 * Renders either the batch or the per-mesh reference into a fresh render
 * texture and returns its pixels (RGBA8). */
static Image
render_scene (LrgStaticBatch *batch,
              GArray         *pieces)
{
    RenderTexture2D target = LoadRenderTexture (320, 240);
    Camera3D        camera;
    Image           image;

    memset (&camera, 0, sizeof camera);
    camera.position = (Vector3) { 0, 14, 20 };
    camera.target = (Vector3) { 0, 1, 0 };
    camera.up = (Vector3) { 0, 1, 0 };
    camera.fovy = 50;
    camera.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode (target);
    ClearBackground (BLACK);
    BeginMode3D (camera);
    if (batch != NULL)
        lrg_static_batch_draw (batch, LRG_STATIC_BATCH_ALL_LAYERS);
    else
        draw_reference (pieces);
    EndMode3D ();
    EndTextureMode ();

    image = LoadImageFromTexture (target.texture);
    UnloadRenderTexture (target);
    return image;
}

/* Real and generated models (with a non-identity model transform, a
 * non-indexed mesh and several meshes per model) batched with game-style
 * world matrices: the merged CPU geometry equals each mesh transformed by
 * world * model.transform, and the uploaded batch renders the same image
 * as drawing every mesh with DrawMesh(). */
static void
test_batch_gl_models_match (void)
{
    static const gchar *const real_models[] = {
        "quaternius-medieval-village/models/Wall_UnevenBrick_Straight.gltf",
        "quaternius-medieval-village/models/Roof_RoundTiles_6x6.gltf",
        "quaternius-medieval-village/models/Floor_WoodDark.gltf",
        "quaternius-fantasy-props/models/Table_Large.gltf",
        "kenney-fantasy-town-kit/models/fence.glb",
        NULL
    };
    g_autoptr(LrgStaticBatch)  batch = NULL;
    g_autoptr(LrgAssetManager) manager = NULL;
    g_autoptr(GPtrArray)       groups = g_ptr_array_new_with_free_func (soup_group_free);
    g_autoptr(GArray)          pieces = g_array_new (FALSE, FALSE, sizeof (Piece));
    g_autoptr(GError)          error = NULL;
    g_autofree gchar          *generated = NULL;
    GrlMatrix                  model_transform = matrix_trs_y (0, 0.5f, 0, 0.3f, 1.5f, 1.5f, 1.5f);
    GrlModel                  *model;
    Model                     *raw;
    Image                      batch_image;
    Image                      reference_image;
    const guint8              *a;
    const guint8              *b;
    guint                      n_real = 0;
    guint                      lit = 0;
    guint                      mismatched = 0;
    guint                      n_pixels;
    guint                      p;
    guint                      k;
    gint                       i;

    if (!graphics_available)
    {
        g_test_skip ("Graphics context not available");
        return;
    }

    manager = lrg_asset_manager_new ();
    batch = lrg_static_batch_new ();

    /* Generated two-mesh model, one mesh without indices, with a model
     * transform, placed twice. */
    generated = g_build_filename (g_get_tmp_dir (), "lrg-batch-two-mesh.glb", NULL);
    test_glb_write_two_mesh_model (generated);
    model = lrg_asset_manager_load_model (manager, generated, &error);
    g_assert_no_error (error);
    raw = grl_model_get_handle (model);
    g_assert_cmpint (raw->meshCount, ==, 2);
    g_assert_nonnull (raw->meshes[0].indices);
    g_assert_null (raw->meshes[1].indices);
    memcpy (&raw->transform, &model_transform, sizeof raw->transform);
    {
        Piece first = { model, matrix_trs_y (-5, 0, 4, 0.9f, 2, 2, 2) };
        Piece second = { model, matrix_trs_y (5, 0, 5, -2.1f, 1.5f, 3, 1.5f) };

        g_array_append_val (pieces, first);
        g_array_append_val (pieces, second);
    }

    /* The real pieces the game batches, when the asset tree is present. */
    for (k = 0; real_models[k] != NULL; k++)
    {
        g_autofree gchar *path = find_asset (real_models[k]);
        Piece             piece;

        if (path == NULL)
            continue;
        piece.model = lrg_asset_manager_load_model (manager, path, &error);
        g_assert_no_error (error);
        piece.world = matrix_trs_y (-6.0f + 3.5f * (gfloat)k, 0, -3.0f + (gfloat)(k % 2) * 2.0f,
                                    0.4f + 1.3f * (gfloat)k, 0.9f, 1.1f, 0.8f);
        g_array_append_val (pieces, piece);
        n_real++;
    }
    if (n_real == 0)
        g_test_message ("Real model assets not found; generated fixtures only");

    /* Batch and build the expected soups. */
    for (p = 0; p < pieces->len; p++)
    {
        Piece    *piece = &g_array_index (pieces, Piece, p);
        GrlMatrix mt;
        GrlMatrix full;

        g_assert_true (lrg_static_batch_add_model (batch, piece->model, &piece->world, 0, &error));
        g_assert_no_error (error);

        raw = grl_model_get_handle (piece->model);
        memcpy (&mt, &raw->transform, sizeof mt);
        full = compose (&piece->world, &mt);
        for (i = 0; i < raw->meshCount; i++)
            expect_mesh (groups, model_mesh_material (raw, i), 0, raw->meshes[i].vertices,
                         (guint)raw->meshes[i].vertexCount, raw->meshes[i].indices, NULL,
                         raw->meshes[i].indices != NULL ? (guint)raw->meshes[i].triangleCount * 3 : 0,
                         &full);
    }
    assert_batch_matches (batch, groups, 1e-4);

    /* GPU: the batch must render like the per-mesh reference. */
    g_assert_true (lrg_static_batch_upload (batch, &error));
    g_assert_no_error (error);
    batch_image = render_scene (batch, NULL);
    reference_image = render_scene (NULL, pieces);
    g_assert_cmpint (batch_image.format, ==, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    g_assert_cmpint (reference_image.format, ==, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    a = batch_image.data;
    b = reference_image.data;
    n_pixels = (guint)(batch_image.width * batch_image.height);
    for (k = 0; k < n_pixels; k++)
    {
        const guint8 *pa = a + k * 4;
        const guint8 *pb = b + k * 4;
        gint          c;
        gint          diff = 0;

        for (c = 0; c < 3; c++)
            diff = MAX (diff, ABS ((gint)pa[c] - (gint)pb[c]));
        if (pb[0] + pb[1] + pb[2] > 0)
            lit++;
        if (diff > 32)
            mismatched++;
    }
    g_test_message ("lit %u, mismatched %u of %u pixels (%u real models)",
                    lit, mismatched, n_pixels, n_real);
    g_assert_cmpuint (lit, >, n_pixels / 50);
    g_assert_cmpuint (mismatched, <, n_pixels / 100);
    UnloadImage (batch_image);
    UnloadImage (reference_image);

    g_clear_object (&batch);
    g_clear_object (&manager);
    g_remove (generated);
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
    g_test_add_func ("/static-batch/matches-sources", test_batch_matches_sources);
    g_test_add_func ("/static-batch/upload-headless", test_batch_upload_headless);
    g_test_add_func ("/static-batch/gl/upload-draw", test_batch_gl);
    g_test_add_func ("/static-batch/gl/models-match-draw-mesh", test_batch_gl_models_match);

    result = g_test_run ();
    g_clear_object (&test_window);
    return result;
}
