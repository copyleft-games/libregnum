/* test-nav-poly-mesh.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavPolyMesh on synthetic levels: floors, walls with gaps, rooms
 * and corridors, stairs, ledges and drops, low ceilings, steep ramps,
 * tagged doors, off-mesh links, queries, the binary form and the
 * bake cache (LrgNavPolyMeshCache).
 */

#include <libregnum.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Geometry helpers                                                     */

static void
quad (GArray       *tris,
      const gfloat *a,
      const gfloat *b,
      const gfloat *c,
      const gfloat *d)
{
    g_array_append_vals (tris, a, 3);
    g_array_append_vals (tris, b, 3);
    g_array_append_vals (tris, c, 3);
    g_array_append_vals (tris, a, 3);
    g_array_append_vals (tris, c, 3);
    g_array_append_vals (tris, d, 3);
}

/* a horizontal rectangle at height @y */
static void
floor_rect (GArray *tris,
            gfloat  x0,
            gfloat  z0,
            gfloat  x1,
            gfloat  z1,
            gfloat  y)
{
    const gfloat a[3] = { x0, y, z0 };
    const gfloat b[3] = { x1, y, z0 };
    const gfloat c[3] = { x1, y, z1 };
    const gfloat d[3] = { x0, y, z1 };

    quad (tris, a, b, c, d);
}

/* a solid box (all six faces) */
static void
box (GArray *tris,
     gfloat  x0,
     gfloat  y0,
     gfloat  z0,
     gfloat  x1,
     gfloat  y1,
     gfloat  z1)
{
    const gfloat p[8][3] = {
        { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 },
        { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 },
    };

    quad (tris, p[0], p[1], p[2], p[3]);
    quad (tris, p[4], p[5], p[6], p[7]);
    quad (tris, p[0], p[1], p[5], p[4]);
    quad (tris, p[1], p[2], p[6], p[5]);
    quad (tris, p[2], p[3], p[7], p[6]);
    quad (tris, p[3], p[0], p[4], p[7]);
}

static LrgNavPolyMesh *
bake (GArray  *tris,
      gfloat   max_drop)
{
    LrgNavPolyMesh *mesh = lrg_nav_poly_mesh_new ();
    g_autoptr(GError) error = NULL;

    g_object_set (mesh, "max-drop", max_drop, NULL);
    g_assert_cmpuint (lrg_nav_poly_mesh_add_triangles (mesh, (const gfloat *) tris->data, tris->len, TRUE),
                      ==, tris->len / 9);
    g_assert_true (lrg_nav_poly_mesh_bake (mesh, &error));
    g_assert_no_error (error);
    return mesh;
}

static const gfloat extents[3] = { 1.0f, 1.0f, 1.0f };

static gfloat *
path (LrgNavPolyMesh *mesh,
      const gfloat   *a,
      const gfloat   *b,
      guint          *n)
{
    g_autoptr(GError) error = NULL;
    gfloat *p;

    p = lrg_nav_poly_mesh_find_path (mesh, a, b, extents, 0, NULL, NULL, NULL, n, &error);
    g_assert_no_error (error);
    g_assert_nonnull (p);
    g_assert_cmpuint (*n % 3, ==, 0);
    g_assert_cmpuint (*n, >=, 6);
    return p;
}

static gboolean
no_path (LrgNavPolyMesh       *mesh,
         const gfloat         *a,
         const gfloat         *b,
         LrgNavPolyFilterFunc  filter,
         gpointer              data)
{
    g_autoptr(GError) error = NULL;
    g_autofree gfloat *p = NULL;
    guint n = 99;

    p = lrg_nav_poly_mesh_find_path (mesh, a, b, extents, 0, filter, data, NULL, &n, &error);
    if (p != NULL)
        return FALSE;
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_cmpuint (n, ==, 0);
    return TRUE;
}

static gfloat
path_length (const gfloat *p,
             guint         n)
{
    gfloat len = 0.0f;
    guint i;

    for (i = 3; i < n; i += 3)
        len += sqrtf ((p[i] - p[i - 3]) * (p[i] - p[i - 3]) + (p[i + 2] - p[i - 1]) * (p[i + 2] - p[i - 1]));
    return len;
}

/* the least distance of a path's legs from the wall end x = @x, z @z0 .. @z1 */
static gfloat
clearance (const gfloat *p,
           guint         n,
           gfloat        x,
           gfloat        z0,
           gfloat        z1)
{
    gfloat best = G_MAXFLOAT;
    guint i;
    guint k;

    for (i = 3; i < n; i += 3)
    {
        for (k = 0; k <= 100; k++)
        {
            gfloat t = (gfloat) k / 100.0f;
            gfloat px = p[i - 3] + (p[i] - p[i - 3]) * t;
            gfloat pz = p[i - 1] + (p[i + 2] - p[i - 1]) * t;
            gfloat cz = CLAMP (pz, z0, z1);

            best = MIN (best, sqrtf ((px - x) * (px - x) + (pz - cz) * (pz - cz)));
        }
    }
    return best;
}

/* every leg of a path stays on the surface (the walk is not blocked) */
static void
assert_walkable_legs (LrgNavPolyMesh *mesh,
                      const gfloat   *p,
                      guint           n)
{
    guint i;

    for (i = 3; i < n; i += 3)
    {
        gfloat fraction;

        g_assert_false (lrg_nav_poly_mesh_raycast (mesh, p + i - 3, p + i, extents, NULL, NULL,
                                                   &fraction, NULL));
        g_assert_cmpfloat (fraction, ==, 1.0f);
    }
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */

static void
test_flat_floor (void)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgNavPolyMesh) mesh = NULL;
    g_autofree gfloat *p = NULL;
    const gfloat a[3] = { 1.0f, 0.0f, 1.0f };
    const gfloat b[3] = { 9.0f, 0.0f, 8.0f };
    const gfloat off[3] = { 30.0f, 0.0f, 30.0f };
    gfloat q[3];
    gfloat min[3];
    gfloat max[3];
    guint poly;
    guint n;

    floor_rect (tris, 0, 0, 10, 10, 0);
    mesh = bake (tris, 0.0f);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_count (mesh), >, 0);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_region_count (mesh), ==, 1);
    g_assert_cmpfloat (lrg_nav_poly_mesh_get_bake_seconds (mesh), >, 0.0);
    /* 40 x 40 cells, two rings eroded (the 0.3 m radius in whole cells) */
    g_assert_cmpuint (lrg_nav_poly_mesh_get_span_count (mesh), ==, 36 * 36);

    g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, a, extents, q, &poly));
    g_assert_cmpfloat_with_epsilon (q[0], 1.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (q[1], 0.0f, 0.15);
    g_assert_cmpfloat_with_epsilon (q[2], 1.0f, 1e-4);
    g_assert_true (lrg_nav_poly_mesh_get_polygon (mesh, poly, min, max, NULL, NULL));
    g_assert_cmpfloat (min[0], <=, 1.0f);
    g_assert_cmpfloat (max[0], >=, 1.0f);
    g_assert_false (lrg_nav_poly_mesh_find_nearest (mesh, off, extents, NULL, NULL));
    g_assert_false (lrg_nav_poly_mesh_get_polygon (mesh, 100000, NULL, NULL, NULL, NULL));

    /* open floor: one straight leg */
    p = path (mesh, a, b, &n);
    g_assert_cmpuint (n, ==, 6);
    g_assert_cmpfloat_with_epsilon (p[3], 9.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (p[5], 8.0f, 1e-4);
    assert_walkable_legs (mesh, p, n);
    /* the edge is eroded: the nearest point lies a cell inside */
    {
        const gfloat edge[3] = { 0.05f, 0.0f, 5.0f };

        g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, edge, extents, q, NULL));
        g_assert_cmpfloat (q[0], >=, 0.5f - 1e-3f);
    }
}

static void
test_wall_gap (void)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgNavPolyMesh) mesh = NULL;
    g_autofree gfloat *p = NULL;
    const gfloat a[3] = { 2.0f, 0.0f, 2.0f };
    const gfloat b[3] = { 2.0f, 0.0f, 10.0f };
    gfloat fraction;
    gfloat hit[3];
    guint n;
    guint i;
    gboolean through_gap;

    /* a 12 x 12 floor, a wall along z = 6 with a 2 m gap at x 8 .. 10 */
    floor_rect (tris, 0, 0, 12, 12, 0);
    box (tris, 0, 0, 5.9f, 8, 3, 6.1f);
    box (tris, 10, 0, 5.9f, 12, 3, 6.1f);
    mesh = bake (tris, 0.0f);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_region_count (mesh), ==, 1);

    /* the straight line is blocked by the wall */
    g_assert_true (lrg_nav_poly_mesh_raycast (mesh, a, b, extents, NULL, NULL, &fraction, hit));
    g_assert_cmpfloat (fraction, >, 0.3f);
    g_assert_cmpfloat (fraction, <, 0.5f);
    g_assert_cmpfloat (hit[2], <, 5.9f);

    p = path (mesh, a, b, &n);
    g_assert_cmpuint (n, >=, 12);  /* at least two corners */
    through_gap = FALSE;
    for (i = 0; i < n; i += 3)
    {
        if (p[i] > 7.9f && p[i] < 10.1f && p[i + 2] > 5.0f && p[i + 2] < 7.0f)
            through_gap = TRUE;
    }
    g_assert_true (through_gap);
    assert_walkable_legs (mesh, p, n);
    /* the funnel keeps it near the shortest way round the wall end */
    g_assert_cmpfloat (path_length (p, n), <, 2.0f * sqrtf (6.5f * 6.5f + 4.0f * 4.0f) + 1.0f);
    g_assert_cmpfloat (path_length (p, n), >, 8.0f);

    /* with a corner margin the path keeps further from the wall's ends */
    {
        g_autofree gfloat *wide = NULL;
        gfloat plain_clearance;
        gfloat wide_clearance;

        plain_clearance = clearance (p, n, 8.0f, 5.9f, 6.1f);
        g_object_set (mesh, "corner-margin", 0.3f, NULL);
        wide = path (mesh, a, b, &n);
        assert_walkable_legs (mesh, wide, n);
        wide_clearance = clearance (wide, n, 8.0f, 5.9f, 6.1f);
        g_assert_cmpfloat (wide_clearance, >=, plain_clearance + 0.1f);
    }
}

static void
test_rooms_and_corridor (void)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgNavPolyMesh) mesh = NULL;
    g_autofree gfloat *p = NULL;
    g_autofree gfloat *again = NULL;
    const gfloat a[3] = { 3.0f, 0.0f, 3.0f };
    const gfloat b[3] = { 13.0f, 0.0f, 13.0f };
    guint n;
    guint n2;
    guint expanded;
    g_autoptr(GError) error = NULL;
    gfloat *limited;

    /* room A (0..6), corridor east then north, room B (10..16, 10..16) */
    floor_rect (tris, 0, 0, 6, 6, 0);
    floor_rect (tris, 6, 2, 13.5f, 4, 0);
    floor_rect (tris, 11.5f, 4, 13.5f, 10, 0);
    floor_rect (tris, 10, 10, 16, 16, 0);
    mesh = bake (tris, 0.0f);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_region_count (mesh), ==, 1);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_portal_count (mesh), >, 0);

    p = path (mesh, a, b, &n);
    assert_walkable_legs (mesh, p, n);
    /* through the corridor's bend: a corner near (11.75, 3.75) */
    {
        gboolean bend = FALSE;
        guint i;

        for (i = 3; i + 3 < n; i += 3)
            if (p[i] > 11.0f && p[i] < 12.5f && p[i + 2] > 2.0f && p[i + 2] < 4.5f)
                bend = TRUE;
        g_assert_true (bend);
    }
    g_assert_cmpfloat (path_length (p, n), <, 25.0f);

    /* same query, same answer (scratch reuse) */
    again = path (mesh, a, b, &n2);
    g_assert_cmpuint (n, ==, n2);
    g_assert_cmpmem (p, n * sizeof (gfloat), again, n2 * sizeof (gfloat));

    /* the search limit */
    limited = lrg_nav_poly_mesh_find_path (mesh, a, b, extents, 1, NULL, NULL, &expanded, &n2, &error);
    g_assert_null (limited);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_cmpuint (expanded, ==, 2);

    /* path repair: block the corridor's first portal out of the room */
    {
        const gfloat in_corridor[3] = { 9.0f, 0.0f, 3.0f };
        const gfloat east[3] = { 12.0f, 0.0f, 3.0f };
        guint blocked = 0;

        while (lrg_nav_poly_mesh_block_portal (mesh, in_corridor, east) && blocked < 64)
        {
            blocked++;
            if (no_path (mesh, a, b, NULL, NULL))
                break;
        }
        g_assert_cmpuint (blocked, >, 0);
        g_assert_true (no_path (mesh, a, b, NULL, NULL));
    }
}

static void
test_stairs_and_ledges (void)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgNavPolyMesh) mesh = NULL;
    g_autoptr(LrgNavPolyMesh) drops = NULL;
    g_autofree gfloat *p = NULL;
    const gfloat low[3] = { 2.0f, 0.0f, 2.0f };
    const gfloat high[3] = { 2.0f, 2.4f, 14.0f };
    const gfloat ledge_low[3] = { 14.0f, 0.0f, 2.0f };
    gfloat q[3];
    guint n;
    guint i;

    /* lower floor 0..16 x 0..10; upper floor at 2.4 m, 0..16 x 10..16 */
    floor_rect (tris, 0, 0, 16, 10, 0);
    floor_rect (tris, 0, 10, 16, 16, 2.4f);
    /* the upper floor's slab and the cliff face along z = 10 */
    box (tris, 6, 0, 10, 16, 2.4f, 10.2f);
    /* stairs at x 0..6: 8 steps of 0.3 m up, 0.5 m deep, z 6 .. 10 */
    for (i = 0; i < 8; i++)
    {
        gfloat z0 = 6.0f + (gfloat) i * 0.5f;

        box (tris, 0, 0, z0, 6, 0.3f * (gfloat) (i + 1), z0 + 0.5f);
    }
    mesh = bake (tris, 0.0f);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_drop_link_count (mesh), ==, 0);

    /* the stairs join both floors */
    p = path (mesh, low, high, &n);
    g_assert_cmpfloat_with_epsilon (p[n - 3], 2.0f, 2e-3);
    g_assert_cmpfloat_with_epsilon (p[n - 1], 14.0f, 2e-3);
    g_assert_cmpfloat_with_epsilon (p[n - 2], 2.4f, 0.15f);
    for (i = 0; i < n; i += 3)
        g_assert_cmpfloat (p[i], <, 6.5f);   /* never through the cliff */
    g_clear_pointer (&p, g_free);

    /* the upper floor projects at its own height, not the lower one's */
    {
        const gfloat above[3] = { 12.0f, 2.5f, 12.0f };

        g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, above, extents, q, NULL));
        g_assert_cmpfloat_with_epsilon (q[1], 2.4f, 0.15f);
    }

    /* drops: down the cliff in one hop, never back up it */
    drops = bake (tris, 3.0f);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_drop_link_count (drops), >, 0);
    {
        const gfloat cliff_top[3] = { 12.0f, 2.4f, 12.0f };
        gboolean jumped = FALSE;

        p = path (drops, cliff_top, ledge_low, &n);
        for (i = 3; i < n; i += 3)
            if (p[i - 2] > 2.0f && p[i + 1] < 0.4f && fabsf (p[i + 2] - p[i - 1]) < 1.5f)
                jumped = TRUE;
        g_assert_true (jumped);
        for (i = 0; i < n; i += 3)
            g_assert_cmpfloat (p[i], >, 6.0f);   /* not round by the stairs */
        g_clear_pointer (&p, g_free);
        /* the way up still takes the stairs */
        p = path (drops, ledge_low, cliff_top, &n);
        {
            gboolean stairs = FALSE;

            for (i = 0; i < n; i += 3)
                if (p[i] < 6.5f)
                    stairs = TRUE;
            g_assert_true (stairs);
        }
    }
}

static void
test_clearance_and_slope (void)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(GArray) steep = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgNavPolyMesh) mesh = NULL;
    g_autoptr(LrgNavPolyMesh) empty = lrg_nav_poly_mesh_new ();
    g_autoptr(GError) error = NULL;
    const gfloat open[3] = { 2.0f, 0.0f, 5.0f };
    const gfloat under[3] = { 8.0f, 0.0f, 5.0f };
    const gfloat ramp_a[3] = { 0.0f, 0.0f, 0.0f };
    const gfloat ramp_b[3] = { 4.0f, 0.0f, 0.0f };
    const gfloat ramp_c[3] = { 4.0f, 5.6f, 4.0f };
    const gfloat ramp_d[3] = { 0.0f, 5.6f, 4.0f };
    gfloat q[3];

    /* a floor half covered by a slab 1 m up: too low to stand under */
    floor_rect (tris, 0, 0, 10, 10, 0);
    box (tris, 6, 1.0f, -1, 11, 1.2f, 11);
    mesh = bake (tris, 0.0f);
    g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, open, extents, q, NULL));
    g_assert_false (lrg_nav_poly_mesh_find_nearest (mesh, under, extents, q, NULL) && q[0] > 6.0f);
    /* the slab's top is walkable and has room */
    {
        const gfloat top[3] = { 8.0f, 1.2f, 5.0f };

        g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, top, extents, q, NULL));
        g_assert_cmpfloat_with_epsilon (q[1], 1.2f, 0.15f);
        g_assert_true (no_path (mesh, open, top, NULL, NULL));
    }

    /* a 54 degree ramp alone: nothing walkable, the previous mesh stays */
    quad (steep, ramp_a, ramp_b, ramp_c, ramp_d);
    lrg_nav_poly_mesh_clear_input (mesh);
    lrg_nav_poly_mesh_add_triangles (mesh, (const gfloat *) steep->data, steep->len, TRUE);
    g_assert_false (lrg_nav_poly_mesh_bake (mesh, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error (&error);
    g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, open, extents, q, NULL));
    g_assert_cmpfloat_with_epsilon (q[0], 2.0f, 1e-4);
    /* with a steeper limit it is a floor */
    g_object_set (mesh, "max-slope", 60.0f, NULL);
    g_assert_true (lrg_nav_poly_mesh_bake (mesh, &error));
    g_assert_no_error (error);

    /* no input; only obstacles */
    g_assert_false (lrg_nav_poly_mesh_bake (empty, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    lrg_nav_poly_mesh_add_triangles (empty, (const gfloat *) tris->data, tris->len, FALSE);
    g_assert_false (lrg_nav_poly_mesh_bake (empty, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_count (empty), ==, 0);
    g_assert_false (lrg_nav_poly_mesh_find_nearest (empty, open, extents, NULL, NULL));
    g_assert_true (no_path (empty, open, open, NULL, NULL));
}

/* doors: tag 7 is a door that opens when the filter says so */
static gboolean
door_open (guint32  tag,
           gpointer user_data)
{
    guint *asked = user_data;

    asked[0]++;
    return tag == 7 && asked[1] != 0;
}

static void
test_doors (void)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgNavPolyMesh) mesh = lrg_nav_poly_mesh_new ();
    g_autoptr(GError) error = NULL;
    g_autofree gfloat *p = NULL;
    const gfloat a[3] = { 3.0f, 0.0f, 3.0f };
    const gfloat b[3] = { 17.0f, 0.0f, 3.0f };
    const gfloat door_min[3] = { 9.0f, -0.5f, 1.0f };
    const gfloat door_max[3] = { 11.0f, 0.5f, 5.0f };
    const gfloat bad[3] = { 1.0f, 1.0f, 1.0f };
    guint asked[2] = { 0, 0 };
    guint n;
    guint i;
    gboolean tagged;

    /* two rooms and a doorway between them */
    floor_rect (tris, 0, 0, 6, 6, 0);
    floor_rect (tris, 6, 2, 14, 4, 0);
    floor_rect (tris, 14, 0, 20, 6, 0);
    lrg_nav_poly_mesh_add_triangles (mesh, (const gfloat *) tris->data, tris->len, TRUE);
    g_assert_true (lrg_nav_poly_mesh_add_area_box (mesh, door_min, door_max, 7));
    g_assert_false (lrg_nav_poly_mesh_add_area_box (mesh, door_min, door_max, 0));
    g_assert_false (lrg_nav_poly_mesh_add_area_box (mesh, door_max, door_min, 3));
    g_assert_false (lrg_nav_poly_mesh_add_area_box (mesh, bad, bad + 0, 0));
    g_assert_true (lrg_nav_poly_mesh_bake (mesh, &error));
    g_assert_no_error (error);
    tagged = FALSE;
    for (i = 0; i < lrg_nav_poly_mesh_get_polygon_count (mesh); i++)
    {
        guint32 tag;

        g_assert_true (lrg_nav_poly_mesh_get_polygon (mesh, i, NULL, NULL, &tag, NULL));
        if (tag == 7)
            tagged = TRUE;
    }
    g_assert_true (tagged);
    /* the door is one region with both rooms */
    g_assert_cmpuint (lrg_nav_poly_mesh_get_region_count (mesh), ==, 1);

    /* closed for this agent */
    g_assert_true (no_path (mesh, a, b, door_open, asked));
    g_assert_cmpuint (asked[0], ==, 1);   /* one tag, asked once per query */
    /* open */
    asked[1] = 1;
    p = lrg_nav_poly_mesh_find_path (mesh, a, b, extents, 0, door_open, asked, NULL, &n, &error);
    g_assert_no_error (error);
    g_assert_nonnull (p);
    g_clear_pointer (&p, g_free);
    /* no filter: every tag allowed */
    g_assert_false (no_path (mesh, a, b, NULL, NULL));
    /* a dynamic obstacle: the tag switched off wins over the filter */
    lrg_nav_poly_mesh_set_tag_enabled (mesh, 7, FALSE);
    g_assert_false (lrg_nav_poly_mesh_get_tag_enabled (mesh, 7));
    g_assert_true (lrg_nav_poly_mesh_get_tag_enabled (mesh, 0));
    g_assert_true (no_path (mesh, a, b, door_open, asked));
    g_assert_true (no_path (mesh, a, b, NULL, NULL));
    {
        gfloat fraction;

        g_assert_true (lrg_nav_poly_mesh_raycast (mesh, a, b, extents, NULL, NULL, &fraction, NULL));
        g_assert_cmpfloat (fraction, <, 0.5f);
    }
    lrg_nav_poly_mesh_set_tag_enabled (mesh, 7, TRUE);
    g_assert_false (no_path (mesh, a, b, NULL, NULL));
    {
        gfloat fraction;

        g_assert_false (lrg_nav_poly_mesh_raycast (mesh, a, b, extents, NULL, NULL, &fraction, NULL));
        g_assert_true (lrg_nav_poly_mesh_raycast (mesh, a, b, extents, door_open, asked, &fraction, NULL) ==
                       FALSE);
    }
}

static void
test_links (void)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgNavPolyMesh) mesh = lrg_nav_poly_mesh_new ();
    g_autoptr(GError) error = NULL;
    g_autofree gfloat *p = NULL;
    const gfloat a[3] = { 2.0f, 0.0f, 2.0f };
    const gfloat b[3] = { 2.0f, 4.0f, 12.0f };
    const gfloat ladder_bottom[3] = { 2.0f, 0.0f, 5.5f };
    const gfloat ladder_top[3] = { 2.0f, 4.0f, 10.5f };
    const gfloat nowhere[3] = { 50.0f, 0.0f, 50.0f };
    const gfloat nan3[3] = { NAN, 0.0f, 0.0f };
    guint ladder;
    guint late;
    guint n;
    guint i;
    gboolean climbed;

    /* a low platform and a high one 4 m up, apart: a ladder joins them */
    floor_rect (tris, 0, 0, 6, 6, 0);
    floor_rect (tris, 0, 10, 6, 16, 4.0f);
    lrg_nav_poly_mesh_add_triangles (mesh, (const gfloat *) tris->data, tris->len, TRUE);
    g_assert_cmpuint (lrg_nav_poly_mesh_add_link (mesh, nan3, a, FALSE, 0, 0), ==, LRG_NAV_POLY_MESH_INVALID);
    g_assert_cmpuint (lrg_nav_poly_mesh_add_link (mesh, a, b, FALSE, -1.0f, 0), ==, LRG_NAV_POLY_MESH_INVALID);
    ladder = lrg_nav_poly_mesh_add_link (mesh, ladder_bottom, ladder_top, TRUE, 2.0f, 0);
    g_assert_cmpuint (ladder, ==, 0);
    g_assert_false (lrg_nav_poly_mesh_get_link_connected (mesh, ladder));
    g_assert_true (lrg_nav_poly_mesh_bake (mesh, &error));
    g_assert_no_error (error);
    g_assert_true (lrg_nav_poly_mesh_get_link_connected (mesh, ladder));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_region_count (mesh), ==, 1);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_link_count (mesh), ==, 1);

    p = path (mesh, a, b, &n);
    climbed = FALSE;
    for (i = 3; i < n; i += 3)
        if (p[i - 2] < 0.5f && p[i + 1] > 3.5f)
            climbed = TRUE;
    g_assert_true (climbed);
    g_clear_pointer (&p, g_free);
    /* both ways */
    p = path (mesh, b, a, &n);
    g_clear_pointer (&p, g_free);

    /* switched off: no way up */
    g_assert_true (lrg_nav_poly_mesh_set_link_enabled (mesh, ladder, FALSE));
    g_assert_true (no_path (mesh, a, b, NULL, NULL));
    g_assert_false (lrg_nav_poly_mesh_set_link_enabled (mesh, 9, FALSE));
    g_assert_true (lrg_nav_poly_mesh_set_link_enabled (mesh, ladder, TRUE));

    /* a link added after the bake connects at once; a link into nothing does not */
    late = lrg_nav_poly_mesh_add_link (mesh, ladder_bottom, nowhere, FALSE, 0, 0);
    g_assert_cmpuint (late, ==, 1);
    g_assert_false (lrg_nav_poly_mesh_get_link_connected (mesh, late));
    g_assert_true (lrg_nav_poly_mesh_set_link_enabled (mesh, ladder, FALSE));
    late = lrg_nav_poly_mesh_add_link (mesh, ladder_bottom, ladder_top, FALSE, 0, 0);
    g_assert_true (lrg_nav_poly_mesh_get_link_connected (mesh, late));
    p = path (mesh, a, b, &n);
    g_clear_pointer (&p, g_free);
    /* one-way: not back down */
    g_assert_true (no_path (mesh, b, a, NULL, NULL));
}

static void
test_random_and_regions (void)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgNavPolyMesh) mesh = NULL;
    const gfloat in_a[3] = { 2.0f, 0.0f, 2.0f };
    const gfloat in_b[3] = { 22.0f, 0.0f, 2.0f };
    gfloat p1[3];
    gfloat p2[3];
    guint i;
    guint pa;
    guint pb;

    floor_rect (tris, 0, 0, 6, 6, 0);
    floor_rect (tris, 20, 0, 26, 6, 0);
    mesh = bake (tris, 0.0f);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_region_count (mesh), ==, 2);
    g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, in_a, extents, NULL, &pa));
    g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, in_b, extents, NULL, &pb));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_region (mesh, pa), !=,
                      lrg_nav_poly_mesh_get_polygon_region (mesh, pb));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_region (mesh, 999999), ==, 0);
    g_assert_true (no_path (mesh, in_a, in_b, NULL, NULL));

    /* nearest within a region: from between the rooms, each side on request */
    {
        const gfloat between[3] = { 12.0f, 0.0f, 3.0f };
        const gfloat wide[3] = { 12.0f, 1.0f, 12.0f };
        guint ra = lrg_nav_poly_mesh_get_polygon_region (mesh, pa);
        guint rb = lrg_nav_poly_mesh_get_polygon_region (mesh, pb);
        gfloat q[3];

        g_assert_true (lrg_nav_poly_mesh_find_nearest_in_region (mesh, between, wide, ra, q, NULL));
        g_assert_cmpfloat (q[0], <, 6.0f);
        g_assert_true (lrg_nav_poly_mesh_find_nearest_in_region (mesh, between, wide, rb, q, NULL));
        g_assert_cmpfloat (q[0], >, 20.0f);
        g_assert_false (lrg_nav_poly_mesh_find_nearest_in_region (mesh, between, extents, ra, q, NULL));
        g_assert_false (lrg_nav_poly_mesh_find_nearest_in_region (mesh, between, wide, 99, q, NULL));
    }

    /* deterministic, and always in the asked region */
    g_assert_true (lrg_nav_poly_mesh_random_point (mesh, in_a, NULL, 42, p1));
    g_assert_true (lrg_nav_poly_mesh_random_point (mesh, in_a, NULL, 42, p2));
    g_assert_cmpmem (p1, sizeof (p1), p2, sizeof (p2));
    for (i = 0; i < 200; i++)
    {
        g_assert_true (lrg_nav_poly_mesh_random_point (mesh, in_b, NULL, i, p1));
        g_assert_cmpfloat (p1[0], >=, 20.0f);
        g_assert_cmpfloat (p1[0], <=, 26.0f);
        g_assert_cmpfloat (p1[2], >=, 0.0f);
        g_assert_cmpfloat (p1[2], <=, 6.0f);
        g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, p1, extents, NULL, NULL));
    }
    {
        gboolean saw_a = FALSE;
        gboolean saw_b = FALSE;

        for (i = 0; i < 200; i++)
        {
            g_assert_true (lrg_nav_poly_mesh_random_point (mesh, NULL, NULL, i, p1));
            if (p1[0] < 10.0f)
                saw_a = TRUE;
            else
                saw_b = TRUE;
        }
        g_assert_true (saw_a && saw_b);
    }
    /* every polygon off: none */
    for (i = 0; i < lrg_nav_poly_mesh_get_polygon_count (mesh); i++)
        g_assert_true (lrg_nav_poly_mesh_set_polygon_enabled (mesh, i, FALSE));
    g_assert_false (lrg_nav_poly_mesh_set_polygon_enabled (mesh, i, FALSE));
    g_assert_false (lrg_nav_poly_mesh_random_point (mesh, NULL, NULL, 1, p1));
    g_assert_false (lrg_nav_poly_mesh_find_nearest (mesh, in_a, extents, NULL, NULL));
}

static void
test_save_load_obj (void)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgNavPolyMesh) mesh = NULL;
    g_autoptr(LrgNavPolyMesh) copy = lrg_nav_poly_mesh_new ();
    g_autoptr(GBytes) bytes = NULL;
    g_autoptr(GBytes) cut = NULL;
    g_autoptr(GBytes) garbage = NULL;
    g_autoptr(GError) error = NULL;
    g_autofree gfloat *p1 = NULL;
    g_autofree gfloat *p2 = NULL;
    g_autofree gchar *obj = NULL;
    g_autofree guint8 *flipped = NULL;
    const gfloat a[3] = { 3.0f, 0.0f, 3.0f };
    const gfloat b[3] = { 13.0f, 0.0f, 13.0f };
    const gfloat bottom[3] = { 3.0f, 0.0f, 5.0f };
    const gfloat top[3] = { 13.0f, 0.0f, 9.0f };
    gconstpointer data;
    gsize size;
    guint n1;
    guint n2;
    gchar **lines;
    guint v;
    guint f;
    guint l;
    guint i;
    gfloat cs;

    floor_rect (tris, 0, 0, 6, 6, 0);
    floor_rect (tris, 6, 2, 13.5f, 4, 0);
    floor_rect (tris, 11.5f, 4, 13.5f, 10, 0);
    floor_rect (tris, 10, 10, 16, 16, 0);
    mesh = lrg_nav_poly_mesh_new ();
    g_object_set (mesh, "cell-size", 0.2f, NULL);
    lrg_nav_poly_mesh_add_triangles (mesh, (const gfloat *) tris->data, tris->len, TRUE);
    g_assert_true (lrg_nav_poly_mesh_bake (mesh, &error));
    lrg_nav_poly_mesh_add_link (mesh, bottom, top, TRUE, 1.0f, 3);

    bytes = lrg_nav_poly_mesh_save (mesh);
    g_assert_true (lrg_nav_poly_mesh_load (copy, bytes, &error));
    g_assert_no_error (error);
    g_object_get (copy, "cell-size", &cs, NULL);
    g_assert_cmpfloat (cs, ==, 0.2f);
    g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_count (copy), ==, lrg_nav_poly_mesh_get_polygon_count (mesh));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_portal_count (copy), ==, lrg_nav_poly_mesh_get_portal_count (mesh));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_span_count (copy), ==, lrg_nav_poly_mesh_get_span_count (mesh));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_link_count (copy), ==, 1);
    g_assert_cmpfloat (lrg_nav_poly_mesh_get_bake_seconds (copy), ==, 0.0);
    p1 = path (mesh, a, b, &n1);
    p2 = path (copy, a, b, &n2);
    g_assert_cmpmem (p1, n1 * sizeof (gfloat), p2, n2 * sizeof (gfloat));

    /* truncated, flipped magic, garbage: refused, the copy unchanged */
    cut = g_bytes_new_from_bytes (bytes, 0, g_bytes_get_size (bytes) - 5);
    g_assert_false (lrg_nav_poly_mesh_load (copy, cut, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    data = g_bytes_get_data (bytes, &size);
    flipped = g_memdup2 (data, size);
    flipped[0] ^= 0xff;
    garbage = g_bytes_new (flipped, size);
    g_assert_false (lrg_nav_poly_mesh_load (copy, garbage, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error (&error);
    g_clear_pointer (&garbage, g_bytes_unref);
    flipped[0] ^= 0xff;
    /* a polygon index past the end in the first cell */
    for (i = 0; i < 4; i++)
        flipped[size / 2 + i] = 0xff;
    garbage = g_bytes_new (flipped, size);
    if (!lrg_nav_poly_mesh_load (copy, garbage, &error))
    {
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        g_clear_error (&error);
    }
    g_clear_pointer (&p2, g_free);
    g_clear_pointer (&copy, g_object_unref);
    copy = lrg_nav_poly_mesh_new ();
    g_assert_true (lrg_nav_poly_mesh_load (copy, bytes, &error));
    p2 = path (copy, a, b, &n2);
    g_assert_cmpuint (n1, ==, n2);

    /* OBJ: four vertices per polygon, a face each, the link as a line */
    obj = lrg_nav_poly_mesh_to_obj (mesh);
    lines = g_strsplit (obj, "\n", -1);
    v = f = l = 0;
    for (i = 0; lines[i] != NULL; i++)
    {
        if (g_str_has_prefix (lines[i], "v "))
            v++;
        else if (g_str_has_prefix (lines[i], "f "))
            f++;
        else if (g_str_has_prefix (lines[i], "l "))
            l++;
    }
    g_strfreev (lines);
    g_assert_cmpuint (f, ==, lrg_nav_poly_mesh_get_polygon_count (mesh));
    g_assert_cmpuint (v, ==, f * 4 + 2);
    g_assert_cmpuint (l, ==, 1);
}

/* the level of the cache tests: two rooms, a corridor, a ladder link */
static LrgNavPolyMesh *
cache_level (gfloat corridor_z)
{
    g_autoptr(GArray) tris = g_array_new (FALSE, FALSE, sizeof (gfloat));
    LrgNavPolyMesh *mesh;
    const gfloat bottom[3] = { 3.0f, 0.0f, 5.0f };
    const gfloat top[3] = { 13.0f, 0.0f, 9.0f };

    floor_rect (tris, 0, 0, 6, 6, 0);
    floor_rect (tris, 6, corridor_z, 13.5f, corridor_z + 2.0f, 0);
    floor_rect (tris, 11.5f, 4, 13.5f, 10, 0);
    floor_rect (tris, 10, 10, 16, 16, 0);
    box (tris, 2, 0, 2, 3, 1, 3);
    mesh = lrg_nav_poly_mesh_new ();
    g_object_set (mesh, "cell-size", 0.2f, "max-drop", 2.0f, NULL);
    lrg_nav_poly_mesh_add_triangles (mesh, (const gfloat *) tris->data, tris->len, TRUE);
    lrg_nav_poly_mesh_add_link (mesh, bottom, top, TRUE, 1.0f, 3);
    return mesh;
}

static void
test_cache_hit_equals_bake (void)
{
    g_autoptr(LrgNavPolyMeshCache) cache = lrg_nav_poly_mesh_cache_new (4);
    g_autoptr(LrgNavPolyMesh) baked = cache_level (2.0f);
    g_autoptr(LrgNavPolyMesh) loaded = cache_level (2.0f);
    g_autoptr(LrgNavPolyMesh) plain = cache_level (2.0f);
    g_autoptr(GError) error = NULL;
    g_autofree gchar *d1 = NULL;
    g_autofree gchar *d2 = NULL;
    g_autofree gchar *obj1 = NULL;
    g_autofree gchar *obj2 = NULL;
    g_autofree gfloat *p1 = NULL;
    g_autofree gfloat *p2 = NULL;
    const gfloat a[3] = { 1.0f, 0.0f, 1.0f };
    const gfloat b[3] = { 15.0f, 0.0f, 15.0f };
    guint n1;
    guint n2;

    /* the same input, the same digest (64 hex digits) */
    d1 = lrg_nav_poly_mesh_get_input_digest (baked);
    d2 = lrg_nav_poly_mesh_get_input_digest (loaded);
    g_assert_cmpstr (d1, ==, d2);
    g_assert_cmpuint (strlen (d1), ==, 64);

    /* a miss bakes and stores, the same input then hits */
    g_assert_true (lrg_nav_poly_mesh_bake_cached (baked, cache, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_misses (cache), ==, 1);
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_hits (cache), ==, 0);
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_size (cache), ==, 1);
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_bytes (cache), >, 0);
    g_assert_true (lrg_nav_poly_mesh_bake_cached (loaded, cache, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_hits (cache), ==, 1);
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_size (cache), ==, 1);
    g_assert_cmpfloat (lrg_nav_poly_mesh_get_bake_seconds (loaded), >=, 0.0);

    /* no cache: a plain bake */
    g_assert_true (lrg_nav_poly_mesh_bake_cached (plain, NULL, &error));
    g_assert_no_error (error);

    /* the loaded mesh is the baked one */
    g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_count (loaded), ==, lrg_nav_poly_mesh_get_polygon_count (plain));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_portal_count (loaded), ==, lrg_nav_poly_mesh_get_portal_count (plain));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_span_count (loaded), ==, lrg_nav_poly_mesh_get_span_count (plain));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_link_count (loaded), ==, lrg_nav_poly_mesh_get_link_count (plain));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_drop_link_count (loaded), ==,
                      lrg_nav_poly_mesh_get_drop_link_count (plain));
    g_assert_cmpuint (lrg_nav_poly_mesh_get_region_count (loaded), ==, lrg_nav_poly_mesh_get_region_count (plain));
    g_assert_cmpint (lrg_nav_poly_mesh_get_link_connected (loaded, 0), ==,
                     lrg_nav_poly_mesh_get_link_connected (plain, 0));
    obj1 = lrg_nav_poly_mesh_to_obj (plain);
    obj2 = lrg_nav_poly_mesh_to_obj (loaded);
    g_assert_cmpstr (obj1, ==, obj2);
    p1 = path (plain, a, b, &n1);
    p2 = path (loaded, a, b, &n2);
    g_assert_cmpmem (p1, n1 * sizeof (gfloat), p2, n2 * sizeof (gfloat));
}

static void
test_cache_digest_follows_input (void)
{
    g_autoptr(LrgNavPolyMesh) mesh = cache_level (2.0f);
    g_autoptr(LrgNavPolyMesh) moved = cache_level (2.5f);
    g_autofree gchar *base = lrg_nav_poly_mesh_get_input_digest (mesh);
    g_autofree gchar *other = NULL;
    g_autoptr(GError) error = NULL;
    const gfloat lo[3] = { 0.0f, -1.0f, 0.0f };
    const gfloat hi[3] = { 1.0f, 1.0f, 1.0f };

    /* moved geometry */
    other = lrg_nav_poly_mesh_get_input_digest (moved);
    g_assert_cmpstr (base, !=, other);
    g_clear_pointer (&other, g_free);

    /* a bake property */
    g_object_set (mesh, "agent-radius", 0.4f, NULL);
    other = lrg_nav_poly_mesh_get_input_digest (mesh);
    g_assert_cmpstr (base, !=, other);
    g_clear_pointer (&other, g_free);
    g_object_set (mesh, "agent-radius", 0.3f, NULL);
    other = lrg_nav_poly_mesh_get_input_digest (mesh);
    g_assert_cmpstr (base, ==, other);
    g_clear_pointer (&other, g_free);

    /* a link switched off (needs a bake for the link to exist as such) */
    g_assert_true (lrg_nav_poly_mesh_bake (mesh, &error));
    g_assert_true (lrg_nav_poly_mesh_set_link_enabled (mesh, 0, FALSE));
    other = lrg_nav_poly_mesh_get_input_digest (mesh);
    g_assert_cmpstr (base, !=, other);
    g_clear_pointer (&other, g_free);
    g_assert_true (lrg_nav_poly_mesh_set_link_enabled (mesh, 0, TRUE));

    /* an area box */
    lrg_nav_poly_mesh_add_area_box (mesh, lo, hi, 7);
    other = lrg_nav_poly_mesh_get_input_digest (mesh);
    g_assert_cmpstr (base, !=, other);
}

static void
test_cache_lru_and_failures (void)
{
    g_autoptr(LrgNavPolyMeshCache) cache = lrg_nav_poly_mesh_cache_new (2);
    g_autoptr(LrgNavPolyMesh) empty = lrg_nav_poly_mesh_new ();
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) gone = NULL;
    g_autoptr(GBytes) kept = NULL;
    gchar *digests[3];
    gfloat zs[3] = { 2.0f, 2.5f, 3.0f };
    guint i;

    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_max_entries (cache), ==, 2);
    for (i = 0; i < 3; i++)
    {
        g_autoptr(LrgNavPolyMesh) mesh = cache_level (zs[i]);

        digests[i] = lrg_nav_poly_mesh_get_input_digest (mesh);
        g_assert_true (lrg_nav_poly_mesh_bake_cached (mesh, cache, &error));
        g_assert_no_error (error);
    }
    /* the least recently used went first */
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_size (cache), ==, 2);
    gone = lrg_nav_poly_mesh_cache_lookup (cache, digests[0]);
    g_assert_null (gone);
    kept = lrg_nav_poly_mesh_cache_lookup (cache, digests[1]);
    g_assert_nonnull (kept);
    /* digests[1] is now the most recent: a new entry drops digests[2] */
    lrg_nav_poly_mesh_cache_insert (cache, digests[0], kept);
    g_clear_pointer (&gone, g_bytes_unref);
    gone = lrg_nav_poly_mesh_cache_lookup (cache, digests[2]);
    g_assert_null (gone);
    /* replacing an entry keeps the size */
    lrg_nav_poly_mesh_cache_insert (cache, digests[0], kept);
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_size (cache), ==, 2);

    /* a failed bake is not cached; the error is the bake's */
    g_assert_false (lrg_nav_poly_mesh_bake_cached (empty, cache, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_size (cache), ==, 2);

    /* clear: nothing held */
    lrg_nav_poly_mesh_cache_clear (cache);
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_size (cache), ==, 0);
    g_assert_cmpuint (lrg_nav_poly_mesh_cache_get_bytes (cache), ==, 0);
    for (i = 0; i < 3; i++)
        g_free (digests[i]);
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/nav-poly-mesh/flat-floor", test_flat_floor);
    g_test_add_func ("/nav-poly-mesh/wall-gap", test_wall_gap);
    g_test_add_func ("/nav-poly-mesh/rooms-and-corridor", test_rooms_and_corridor);
    g_test_add_func ("/nav-poly-mesh/stairs-and-ledges", test_stairs_and_ledges);
    g_test_add_func ("/nav-poly-mesh/clearance-and-slope", test_clearance_and_slope);
    g_test_add_func ("/nav-poly-mesh/doors", test_doors);
    g_test_add_func ("/nav-poly-mesh/links", test_links);
    g_test_add_func ("/nav-poly-mesh/random-and-regions", test_random_and_regions);
    g_test_add_func ("/nav-poly-mesh/save-load-obj", test_save_load_obj);
    g_test_add_func ("/nav-poly-mesh/cache/hit-equals-bake", test_cache_hit_equals_bake);
    g_test_add_func ("/nav-poly-mesh/cache/digest-follows-input", test_cache_digest_follows_input);
    g_test_add_func ("/nav-poly-mesh/cache/lru-and-failures", test_cache_lru_and_failures);
    return g_test_run ();
}
