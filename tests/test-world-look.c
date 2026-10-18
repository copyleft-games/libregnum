/* test-world-look.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the world-look helpers: LrgTerrainDetail (landform
 * noise determinism, bounds, weights), LrgWaterSurface (Gerstner wave
 * math, the origin phase, patches and shore factors), LrgGrassField
 * (placement determinism and density), LrgTreeMesh (procedural trees:
 * vertex counts, bounds, determinism), LrgHorizonRing (ring geometry)
 * and LrgHeatHaze (the distortion's bounds). All headless.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>

#define EPS (1e-4f)

/* --- LrgTerrainDetail --- */

static void
test_detail_deterministic (void)
{
    g_autoptr(LrgTerrainDetail) a = lrg_terrain_detail_new (1234);
    g_autoptr(LrgTerrainDetail) b = lrg_terrain_detail_new (1234);
    g_autoptr(LrgTerrainDetail) c = lrg_terrain_detail_new (99);
    guint i;
    guint differ;

    lrg_terrain_detail_set_amplitude (a, 8.0f);
    lrg_terrain_detail_set_amplitude (b, 8.0f);
    lrg_terrain_detail_set_amplitude (c, 8.0f);
    differ = 0;
    for (i = 0; i < 500; i++)
    {
        gdouble x = (gdouble)i * 37.13 - 9000.0;
        gdouble z = (gdouble)i * -11.7 + 412345.5;
        gfloat va = lrg_terrain_detail_sample (a, x, z);

        /* bit for bit the same for the same seed, any order */
        g_assert_true (va == lrg_terrain_detail_sample (b, x, z));
        g_assert_true (va == lrg_terrain_detail_sample (a, x, z));
        g_assert_cmpfloat (va, >=, -8.0f);
        g_assert_cmpfloat (va, <=, 8.0f);
        if (fabsf (va - lrg_terrain_detail_sample (c, x, z)) > 0.01f)
            differ++;
    }
    /* another seed is another landscape */
    g_assert_cmpuint (differ, >, 400);
}

static void
test_detail_continuous (void)
{
    g_autoptr(LrgTerrainDetail) d = lrg_terrain_detail_new (7);
    gfloat lo = G_MAXFLOAT;
    gfloat hi = -G_MAXFLOAT;
    guint i;

    lrg_terrain_detail_set_amplitude (d, 10.0f);
    lrg_terrain_detail_set_wavelength (d, 100.0);
    for (i = 0; i < 2000; i++)
    {
        gdouble x = (gdouble)i * 0.5;
        gfloat v0 = lrg_terrain_detail_sample (d, x, 300.0);
        gfloat v1 = lrg_terrain_detail_sample (d, x + 0.01, 300.0);

        /* a centimetre apart: no jumps (C1 value noise, 4 octaves) */
        g_assert_cmpfloat (fabsf (v1 - v0), <, 0.05f);
        lo = MIN (lo, v0);
        hi = MAX (hi, v0);
    }
    /* it does make relief */
    g_assert_cmpfloat (hi - lo, >, 2.0f);

    /* zero amplitude: no detail; settings clamp */
    lrg_terrain_detail_set_amplitude (d, 0.0f);
    g_assert_cmpfloat (lrg_terrain_detail_sample (d, 12.0, 34.0), ==, 0.0f);
    lrg_terrain_detail_set_octaves (d, 99);
    g_assert_cmpuint (lrg_terrain_detail_get_octaves (d), ==, LRG_TERRAIN_DETAIL_MAX_OCTAVES);
    lrg_terrain_detail_set_ridge (d, 3.0f);
    g_assert_cmpfloat (lrg_terrain_detail_get_ridge (d), ==, 1.0f);
    lrg_terrain_detail_set_wavelength (d, -5.0);
    g_assert_cmpfloat (lrg_terrain_detail_get_wavelength (d), ==, 100.0);
    g_assert_cmpfloat (lrg_terrain_detail_sample (d, NAN, 1.0), ==, 0.0f);
}

static void
test_detail_apply (void)
{
    g_autoptr(LrgTerrainDetail) d = lrg_terrain_detail_new (42);
    gfloat heights[4 * 3];
    gfloat weights[4 * 3];
    guint r;
    guint c;

    lrg_terrain_detail_set_amplitude (d, 5.0f);
    for (r = 0; r < 12; r++)
    {
        heights[r] = 100.0f;
        weights[r] = r % 2 == 0 ? 1.0f : 0.0f;
    }
    g_assert_true (lrg_terrain_detail_apply (d, heights, 12, 4, 3, 1000.0, 2000.0, 2.0, -3.0,
                                             weights, 0.5f));
    for (r = 0; r < 3; r++)
    {
        for (c = 0; c < 4; c++)
        {
            guint i = r * 4 + c;
            gfloat expect;

            expect = weights[i] > 0.0f ?
                     100.0f + 0.5f * lrg_terrain_detail_sample (d, 1000.0 + c * 2.0, 2000.0 - r * 3.0) :
                     100.0f;
            g_assert_cmpfloat_with_epsilon (heights[i], expect, EPS);
        }
    }
    /* a size that does not match changes nothing */
    g_assert_false (lrg_terrain_detail_apply (d, heights, 11, 4, 3, 0.0, 0.0, 1.0, 1.0, NULL, 1.0f));
}

/* --- LrgWaterSurface --- */

static void
test_water_waves (void)
{
    g_autoptr(LrgWaterSurface) w = lrg_water_surface_new ();
    LrgWaterWave wave;
    LrgWaterWave got;
    gfloat off[3];
    gfloat n[3];
    gfloat k;
    guint i;

    /* no waves: a flat mirror */
    lrg_water_surface_set_wave_count (w, 0);
    lrg_water_surface_evaluate (w, 3.0f, 4.0f, 1.0f, off, n);
    g_assert_cmpfloat (off[1], ==, 0.0f);
    g_assert_cmpfloat_with_epsilon (n[1], 1.0f, EPS);
    g_assert_cmpfloat (lrg_water_surface_get_max_height (w), ==, 0.0f);

    /* one sine swell along +X: height A sin(k x - w t) */
    memset (&wave, 0, sizeof (wave));
    wave.direction[0] = 2.0f;
    wave.amplitude = 0.5f;
    wave.wavelength = 10.0f;
    wave.speed = 2.0f;
    wave.steepness = 0.0f;
    lrg_water_surface_set_wave (w, 0, &wave);
    g_assert_true (lrg_water_surface_get_wave (w, 0, &got));
    g_assert_cmpfloat_with_epsilon (got.direction[0], 1.0f, EPS);
    g_assert_false (lrg_water_surface_get_wave (w, 1, &got));
    k = (gfloat)(2.0 * G_PI) / 10.0f;
    for (i = 0; i < 20; i++)
    {
        gfloat x = (gfloat)i * 0.7f;
        gfloat t = (gfloat)i * 0.13f;
        gfloat len;

        lrg_water_surface_evaluate (w, x, 1.0f, t, off, n);
        g_assert_cmpfloat_with_epsilon (off[1], 0.5f * sinf (k * x - 2.0f * k * t), 1e-3f);
        g_assert_cmpfloat_with_epsilon (off[0], 0.0f, EPS);
        len = sqrtf (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        g_assert_cmpfloat_with_epsilon (len, 1.0f, EPS);
        /* the slope: n.x = -dh/dx (normalised) */
        g_assert_cmpfloat_with_epsilon (n[0] / n[1], -0.5f * k * cosf (k * x - 2.0f * k * t), 1e-3f);
    }
    g_assert_cmpfloat_with_epsilon (lrg_water_surface_get_max_height (w), 0.5f, EPS);

    /* steepness pulls points towards the crests, never past the fold
     * (Q A k <= 1: at most 1 / k) */
    wave.steepness = 1.0f;
    lrg_water_surface_set_wave (w, 0, &wave);
    lrg_water_surface_evaluate (w, 0.0f, 0.0f, 0.0f, off, NULL);
    g_assert_cmpfloat (fabsf (off[0]), <=, 1.0f / k + EPS);
    g_assert_cmpfloat (fabsf (off[0]), >, 0.01f);

    /* the wave scale */
    lrg_water_surface_set_wave_scale (w, 0.0f);
    lrg_water_surface_evaluate (w, 2.0f, 0.0f, 0.3f, off, n);
    g_assert_cmpfloat (off[1], ==, 0.0f);
    lrg_water_surface_set_wave_scale (w, 1.0f);

    /* invalid input is clamped */
    wave.direction[0] = 0.0f;
    wave.direction[1] = 0.0f;
    wave.wavelength = -1.0f;
    lrg_water_surface_set_wave (w, 1, &wave);
    g_assert_true (lrg_water_surface_get_wave (w, 1, &got));
    g_assert_cmpfloat (got.direction[0], ==, 1.0f);
    g_assert_cmpfloat (got.wavelength, >, 0.0f);
}

static void
test_water_origin (void)
{
    g_autoptr(LrgWaterSurface) a = lrg_water_surface_new ();
    g_autoptr(LrgWaterSurface) b = lrg_water_surface_new ();
    guint i;

    /* the same water seen from a shifted draw space */
    lrg_water_surface_set_origin (b, 1000.0, -250.0);
    for (i = 0; i < 30; i++)
    {
        gfloat x = (gfloat)i * 1.3f;
        gfloat z = (gfloat)i * -0.9f;
        gfloat oa[3];
        gfloat ob[3];
        gfloat na[3];
        gfloat nb[3];

        lrg_water_surface_evaluate (a, x + 1000.0f, z - 250.0f, 2.0f, oa, na);
        lrg_water_surface_evaluate (b, x, z, 2.0f, ob, nb);
        g_assert_cmpfloat_with_epsilon (oa[1], ob[1], 2e-3f);
        g_assert_cmpfloat_with_epsilon (na[0], nb[0], 2e-3f);
    }
}

static gfloat
edge_foam (gfloat   x,
           gfloat   z,
           gpointer user_data)
{
    /* shore along x = 0 */
    (void)z;
    (void)user_data;
    return 1.0f - x / 10.0f;
}

static void
test_water_patches (void)
{
    g_autoptr(LrgWaterSurface) w = lrg_water_surface_new ();
    g_autoptr(GError) error = NULL;
    const gfloat *foam;
    guint n;

    g_assert_true (lrg_water_surface_add_grid (w, 0.0f, 0.0f, 10.0f, 5.0f, 2.0f, 10, 5, edge_foam,
                                               NULL, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_water_surface_get_patch_count (w), ==, 1);
    g_assert_cmpuint (lrg_water_surface_get_vertex_count (w), ==, 11 * 6);
    g_assert_cmpuint (lrg_water_surface_get_triangle_count (w), ==, 100);
    foam = lrg_water_surface_get_patch_foam (w, 0, &n);
    g_assert_cmpuint (n, ==, 66);
    g_assert_cmpfloat_with_epsilon (foam[0], 1.0f, EPS);
    g_assert_cmpfloat_with_epsilon (foam[10], 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (foam[5], 0.5f, EPS);
    g_assert_null (lrg_water_surface_get_patch_foam (w, 1, &n));

    /* too large, no area: errors, nothing added */
    g_assert_false (lrg_water_surface_add_grid (w, 0, 0, 1, 1, 0, 300, 300, NULL, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    g_assert_false (lrg_water_surface_add_grid (w, 0, 0, 0, 1, 0, 2, 2, NULL, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_water_surface_get_patch_count (w), ==, 1);
    lrg_water_surface_clear (w);
    g_assert_cmpuint (lrg_water_surface_get_vertex_count (w), ==, 0);
}

/* --- LrgGrassField --- */

static gboolean
slope_height (gfloat    x,
              gfloat    z,
              gfloat   *out_y,
              gpointer  user_data)
{
    (void)user_data;
    if (z < -1000.0f)
        return FALSE;
    *out_y = x * 0.5f;
    return TRUE;
}

static void
test_grass_placement (void)
{
    g_autoptr(LrgGrassField) g = lrg_grass_field_new (5);
    g_autoptr(GArray) a = g_array_new (FALSE, FALSE, sizeof (LrgGrassBlade));
    g_autoptr(GArray) b = g_array_new (FALSE, FALSE, sizeof (LrgGrassBlade));
    g_autoptr(GArray) dense = g_array_new (FALSE, FALSE, sizeof (LrgGrassBlade));
    guint total;
    gint64 cx;
    guint i;
    guint j;

    lrg_grass_field_set_cell_size (g, 4.0f);
    lrg_grass_field_set_blades_per_cell (g, 40);

    /* the same cell twice: the same blades */
    lrg_grass_field_place_cell (g, 1234, -77, 0.5f, 100.0, 50.0, slope_height, NULL, a);
    lrg_grass_field_place_cell (g, 1234, -77, 0.5f, 100.0, 50.0, slope_height, NULL, b);
    g_assert_cmpuint (a->len, ==, b->len);
    g_assert_cmpint (memcmp (a->data, b->data, a->len * sizeof (LrgGrassBlade)), ==, 0);

    /* blades lie in the cell, on the ground */
    for (i = 0; i < a->len; i++)
    {
        const LrgGrassBlade *bl = &g_array_index (a, LrgGrassBlade, i);
        gdouble px = bl->position[0] + 100.0;
        gdouble pz = bl->position[2] + 50.0;

        g_assert_cmpfloat (px, >=, 1234.0 * 4.0);
        g_assert_cmpfloat (px, <=, 1235.0 * 4.0);
        g_assert_cmpfloat (pz, >=, -77.0 * 4.0);
        g_assert_cmpfloat (pz, <=, -76.0 * 4.0);
        g_assert_cmpfloat_with_epsilon (bl->position[1], bl->position[0] * 0.5f, EPS);
        g_assert_cmpfloat (bl->height, >, 0.0f);
    }

    /* a higher density only adds blades: the sparse ones are a subset */
    lrg_grass_field_place_cell (g, 1234, -77, 1.0f, 100.0, 50.0, slope_height, NULL, dense);
    g_assert_cmpuint (dense->len, ==, 40);
    for (i = 0; i < a->len; i++)
    {
        gboolean found = FALSE;

        for (j = 0; j < dense->len && !found; j++)
            found = memcmp (&g_array_index (a, LrgGrassBlade, i), &g_array_index (dense, LrgGrassBlade, j),
                            sizeof (LrgGrassBlade)) == 0;
        g_assert_true (found);
    }

    /* density: about half of 40 per cell over many cells */
    total = 0;
    g_array_set_size (a, 0);
    for (cx = 0; cx < 200; cx++)
        total += lrg_grass_field_place_cell (g, cx, 3, 0.5f, 0.0, 0.0, NULL, NULL, a);
    g_assert_cmpuint (total, ==, a->len);
    g_assert_cmpfloat ((gdouble)total / 200.0, >, 17.0);
    g_assert_cmpfloat ((gdouble)total / 200.0, <, 23.0);

    /* no density, no ground: no blades */
    g_assert_cmpuint (lrg_grass_field_place_cell (g, 1, 1, 0.0f, 0, 0, NULL, NULL, a), ==, 0);
    g_assert_cmpuint (lrg_grass_field_place_cell (g, 1, -1000, 1.0f, 0, 0, slope_height, NULL, a), ==, 0);

    /* instances and the card */
    lrg_grass_field_set_blades (g, (const LrgGrassBlade *)dense->data, dense->len);
    g_assert_cmpuint (lrg_grass_field_get_instance_count (g), ==, 40);
    {
        gfloat card[LRG_GRASS_FIELD_CARD_VERTICES * 3];
        gfloat top = 0.0f;

        lrg_grass_field_get_card (g, card);
        for (i = 0; i < LRG_GRASS_FIELD_CARD_VERTICES; i++)
        {
            g_assert_cmpfloat (fabsf (card[i * 3 + 0]), <=, 0.5f);
            g_assert_cmpfloat (fabsf (card[i * 3 + 2]), <=, 0.5f);
            top = MAX (top, card[i * 3 + 1]);
        }
        g_assert_cmpfloat_with_epsilon (top, 1.0f, EPS);
    }
}

/* --- LrgTreeMesh --- */

static void
test_tree_meshes (void)
{
    LrgTreeShape shape;

    for (shape = LRG_TREE_SHAPE_CONIFER; shape <= LRG_TREE_SHAPE_SHRUB; shape++)
    {
        g_autoptr(LrgTreeMesh) t = lrg_tree_mesh_new (shape, 77, 10.0f);
        g_autoptr(LrgTreeMesh) same = lrg_tree_mesh_new (shape, 77, 10.0f);
        g_autoptr(LrgTreeMesh) other = lrg_tree_mesh_new (shape, 78, 10.0f);
        const gfloat *pos;
        const gfloat *nor;
        const guint32 *idx;
        guint n_pos;
        guint n_nor;
        guint n_idx;
        guint n_col;
        gfloat min[3];
        gfloat max[3];
        guint i;

        g_assert_cmpint (lrg_tree_mesh_get_shape (t), ==, shape);
        /* low poly: tens to a few hundred triangles, flat shaded */
        g_assert_cmpuint (lrg_tree_mesh_get_triangle_count (t), >=, 20);
        g_assert_cmpuint (lrg_tree_mesh_get_triangle_count (t), <=, 400);
        g_assert_cmpuint (lrg_tree_mesh_get_vertex_count (t), ==, 3 * lrg_tree_mesh_get_triangle_count (t));
        pos = lrg_tree_mesh_get_positions (t, &n_pos);
        nor = lrg_tree_mesh_get_normals (t, &n_nor);
        idx = lrg_tree_mesh_get_indices (t, &n_idx);
        (void)lrg_tree_mesh_get_colors (t, &n_col);
        g_assert_cmpuint (n_pos, ==, lrg_tree_mesh_get_vertex_count (t) * 3);
        g_assert_cmpuint (n_nor, ==, n_pos);
        g_assert_cmpuint (n_col, ==, lrg_tree_mesh_get_vertex_count (t) * 4);
        g_assert_cmpuint (n_idx, ==, lrg_tree_mesh_get_triangle_count (t) * 3);
        for (i = 0; i < n_idx; i++)
            g_assert_cmpuint (idx[i], <, lrg_tree_mesh_get_vertex_count (t));
        for (i = 0; i < n_nor / 3; i++)
        {
            gfloat len = sqrtf (nor[i * 3] * nor[i * 3] + nor[i * 3 + 1] * nor[i * 3 + 1] +
                                nor[i * 3 + 2] * nor[i * 3 + 2]);

            g_assert_cmpfloat_with_epsilon (len, 1.0f, 1e-3f);
        }

        /* bounds: on the ground, no taller than asked, within reach */
        lrg_tree_mesh_get_bounds (t, min, max);
        g_assert_cmpfloat (min[1], >=, -EPS);
        g_assert_cmpfloat (max[1], <=, 10.0f + EPS);
        g_assert_cmpfloat (max[1], >=, 4.0f);
        for (i = 0; i < n_pos / 3; i++)
        {
            gfloat r = sqrtf (pos[i * 3] * pos[i * 3] + pos[i * 3 + 2] * pos[i * 3 + 2]);

            g_assert_cmpfloat (r, <=, 10.0f * 0.6f + EPS);
        }

        /* the same seed is the same tree; another seed another */
        g_assert_cmpuint (lrg_tree_mesh_get_vertex_count (same), ==, lrg_tree_mesh_get_vertex_count (t));
        g_assert_cmpint (memcmp (pos, lrg_tree_mesh_get_positions (same, NULL), n_pos * sizeof (gfloat)), ==, 0);
        g_assert_true (lrg_tree_mesh_get_vertex_count (other) != lrg_tree_mesh_get_vertex_count (t) ||
                       memcmp (pos, lrg_tree_mesh_get_positions (other, NULL), n_pos * sizeof (gfloat)) != 0);
    }
}

static void
test_tree_batch_and_colors (void)
{
    g_autoptr(LrgTreeMesh) t = lrg_tree_mesh_new (LRG_TREE_SHAPE_BROADLEAF, 3, 6.0f);
    g_autoptr(LrgStaticBatch) batch = lrg_static_batch_new ();
    g_autoptr(GError) error = NULL;
    g_autoptr(GrlColor) autumn = grl_color_new (180, 90, 30, 255);
    g_autoptr(GrlMatrix) m = grl_matrix_new_translate (10.0f, 0.0f, 5.0f);
    const guint8 *col;
    guint n;
    guint before;
    guint i;
    gboolean has_orange;

    before = lrg_tree_mesh_get_vertex_count (t);
    lrg_tree_mesh_set_colors (t, NULL, autumn);
    g_assert_cmpuint (lrg_tree_mesh_get_vertex_count (t), ==, before);
    col = lrg_tree_mesh_get_colors (t, &n);
    has_orange = FALSE;
    for (i = 0; i < n / 4; i++)
        has_orange = has_orange || (col[i * 4] > 150 && col[i * 4 + 2] < 60);
    g_assert_true (has_orange);

    g_assert_true (lrg_tree_mesh_add_to_batch (t, batch, m, 0, 0, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_static_batch_get_vertex_count (batch), ==, before);
}

/* --- LrgHorizonRing --- */

static gboolean
ring_sample (gdouble   x,
             gdouble   z,
             gfloat   *out_height,
             guint8   *out_rgba,
             gpointer  user_data)
{
    guint *calls = user_data;

    (*calls)++;
    if (x > 9000.0)
        return FALSE;
    *out_height = 50.0f;
    out_rgba[0] = 10;
    out_rgba[1] = 200;
    out_rgba[2] = 30;
    out_rgba[3] = 255;
    (void)z;
    return TRUE;
}

static void
test_horizon_ring (void)
{
    g_autoptr(LrgHorizonRing) ring = lrg_horizon_ring_new ();
    g_autoptr(GError) error = NULL;
    LrgStaticBatch *batch;
    guint calls = 0;
    guint c;
    gfloat inner;
    gfloat outer;

    lrg_horizon_ring_set_radii (ring, 1000.0f, 4000.0f);
    lrg_horizon_ring_set_radii (ring, 10.0f, 5.0f);
    lrg_horizon_ring_get_radii (ring, &inner, &outer);
    g_assert_cmpfloat (inner, ==, 1000.0f);
    g_assert_cmpfloat (outer, ==, 4000.0f);
    lrg_horizon_ring_set_resolution (ring, 32, 4);
    lrg_horizon_ring_set_sink (ring, 5.0f);
    g_assert_false (lrg_horizon_ring_is_built (ring));
    g_assert_true (lrg_horizon_ring_build (ring, 8000.0, 2000.0, 7000.0, 10.0, 1000.0,
                                           ring_sample, &calls, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_horizon_ring_is_built (ring));
    g_assert_cmpuint (calls, ==, 32 * 5);
    g_assert_cmpuint (lrg_horizon_ring_get_vertex_count (ring), ==, 32 * 5);
    g_assert_cmpuint (lrg_horizon_ring_get_triangle_count (ring), ==, 32 * 4 * 2);

    batch = lrg_horizon_ring_get_batch (ring);
    g_assert_cmpuint (lrg_static_batch_get_vertex_count (batch), ==, 32 * 5);
    for (c = 0; c < lrg_static_batch_get_chunk_count (batch); c++)
    {
        const gfloat *pos;
        const gfloat *nor;
        guint n;
        guint i;

        nor = lrg_static_batch_get_chunk_normals (batch, c, &n);
        pos = lrg_static_batch_get_chunk_positions (batch, c, &n);
        for (i = 0; i < n / 3; i++)
        {
            gfloat dx = pos[i * 3] - 1000.0f;
            gfloat dz = pos[i * 3 + 2] - 1000.0f;
            gfloat r = sqrtf (dx * dx + dz * dz);

            /* draw space: centre at (1000, 1000); radii 1000 .. 4000 */
            g_assert_cmpfloat (r, >=, 1000.0f - 0.5f);
            g_assert_cmpfloat (r, <=, 4000.0f + 0.5f);
            /* flat ground at 50 - 10, the inner edge sunk; off the
             * sampler's map (placement x > 9000) at draw y 0 */
            if (pos[i * 3] + 7000.0f > 9000.0f)
            {
                g_assert_cmpfloat (pos[i * 3 + 1], <=, EPS);
                continue;
            }
            g_assert_cmpfloat (pos[i * 3 + 1], <=, 40.0f + EPS);
            g_assert_cmpfloat (pos[i * 3 + 1], >=, 35.0f - EPS);
            if (pos[i * 3] + 7000.0f < 8500.0f)
                g_assert_cmpfloat (nor[i * 3 + 1], >, 0.9f);
        }
    }
}

/* --- LrgHeatHaze --- */

static void
test_heat_haze (void)
{
    g_autoptr(LrgHeatHaze) h = lrg_heat_haze_new ();
    gfloat du;
    gfloat dv;
    gfloat at_horizon;
    gfloat high;
    guint i;

    lrg_heat_haze_set_strength (h, 0.01f);
    lrg_heat_haze_set_shape (h, 9.0f, 2.0f, 0.5f, 0.2f);
    at_horizon = 0.0f;
    high = 0.0f;
    for (i = 0; i < 200; i++)
    {
        gfloat u = (gfloat)i / 200.0f;

        lrg_heat_haze_set_time (h, (gfloat)i * 0.05f);
        lrg_heat_haze_get_offset (h, u, 0.5f, &du, &dv);
        g_assert_cmpfloat (fabsf (du), <=, 0.01f + EPS);
        g_assert_cmpfloat (fabsf (dv), <=, 0.005f + EPS);
        at_horizon = MAX (at_horizon, fabsf (du));
        lrg_heat_haze_get_offset (h, u, 0.98f, &du, &dv);
        high = MAX (high, fabsf (du));
    }
    /* strongest on the horizon, faint at the top of the screen */
    g_assert_cmpfloat (at_horizon, >, 0.005f);
    g_assert_cmpfloat (high, <, at_horizon * 0.1f);

    g_assert_false (lrg_heat_haze_is_identity (h));
    lrg_post_effect_set_intensity (LRG_POST_EFFECT (h), 0.0f);
    g_assert_true (lrg_heat_haze_is_identity (h));
    lrg_heat_haze_get_offset (h, 0.3f, 0.5f, &du, &dv);
    g_assert_cmpfloat (du, ==, 0.0f);
    lrg_post_effect_set_intensity (LRG_POST_EFFECT (h), 1.0f);
    lrg_heat_haze_set_strength (h, 5.0f);
    g_assert_cmpfloat (lrg_heat_haze_get_strength (h), ==, 0.05f);
    lrg_heat_haze_set_strength (h, 0.0f);
    g_assert_true (lrg_heat_haze_is_identity (h));
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/world-look/detail/deterministic", test_detail_deterministic);
    g_test_add_func ("/world-look/detail/continuous", test_detail_continuous);
    g_test_add_func ("/world-look/detail/apply", test_detail_apply);
    g_test_add_func ("/world-look/water/waves", test_water_waves);
    g_test_add_func ("/world-look/water/origin", test_water_origin);
    g_test_add_func ("/world-look/water/patches", test_water_patches);
    g_test_add_func ("/world-look/grass/placement", test_grass_placement);
    g_test_add_func ("/world-look/trees/meshes", test_tree_meshes);
    g_test_add_func ("/world-look/trees/batch-and-colors", test_tree_batch_and_colors);
    g_test_add_func ("/world-look/horizon-ring", test_horizon_ring);
    g_test_add_func ("/world-look/heat-haze", test_heat_haze);

    return g_test_run ();
}
