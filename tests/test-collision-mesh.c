/* test-collision-mesh.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgCollisionMesh: input validation, ray casts against a
 * brute-force reference on a large mesh, determinism, build time, sphere
 * and capsule sweeps (resting contact, tunnelling, edges), overlaps,
 * instances with translation/rotation/scale, and translation.
 */

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <math.h>
#include <string.h>

#include "physics/lrg-collision-mesh.h"

#define EPS (1e-3f)

/* ========================================================================== */
/*                               Geometry helpers                             */
/* ========================================================================== */

static void
push_tri (GArray *v,
          gfloat  ax, gfloat ay, gfloat az,
          gfloat  bx, gfloat by, gfloat bz,
          gfloat  cx, gfloat cy, gfloat cz)
{
    gfloat t[9];

    t[0] = ax; t[1] = ay; t[2] = az;
    t[3] = bx; t[4] = by; t[5] = bz;
    t[6] = cx; t[7] = cy; t[8] = cz;
    g_array_append_vals (v, t, 9);
}

/* Axis-aligned quad on y = h spanning [x0,x1] x [z0,z1]. */
static void
push_floor (GArray *v,
            gfloat  x0,
            gfloat  z0,
            gfloat  x1,
            gfloat  z1,
            gfloat  h)
{
    push_tri (v, x0, h, z0, x1, h, z0, x1, h, z1);
    push_tri (v, x0, h, z0, x1, h, z1, x0, h, z1);
}

/* Twelve triangles of an axis-aligned box. */
static void
push_box (GArray       *v,
          const gfloat *mn,
          const gfloat *mx)
{
    gfloat x0 = mn[0], y0 = mn[1], z0 = mn[2];
    gfloat x1 = mx[0], y1 = mx[1], z1 = mx[2];

    /* bottom, top */
    push_tri (v, x0, y0, z0, x1, y0, z0, x1, y0, z1);
    push_tri (v, x0, y0, z0, x1, y0, z1, x0, y0, z1);
    push_tri (v, x0, y1, z0, x1, y1, z1, x1, y1, z0);
    push_tri (v, x0, y1, z0, x0, y1, z1, x1, y1, z1);
    /* x faces */
    push_tri (v, x0, y0, z0, x0, y1, z1, x0, y1, z0);
    push_tri (v, x0, y0, z0, x0, y0, z1, x0, y1, z1);
    push_tri (v, x1, y0, z0, x1, y1, z0, x1, y1, z1);
    push_tri (v, x1, y0, z0, x1, y1, z1, x1, y0, z1);
    /* z faces */
    push_tri (v, x0, y0, z0, x1, y1, z0, x1, y0, z0);
    push_tri (v, x0, y0, z0, x0, y1, z0, x1, y1, z0);
    push_tri (v, x0, y0, z1, x1, y0, z1, x1, y1, z1);
    push_tri (v, x0, y0, z1, x1, y1, z1, x0, y1, z1);
}

static LrgCollisionMesh *
mesh_from (GArray        *v,
           const guint32 *tags)
{
    LrgCollisionMesh *mesh;
    g_autoptr(GError) error = NULL;

    mesh = lrg_collision_mesh_new ();
    g_assert_true (lrg_collision_mesh_set_triangles (mesh, (const gfloat *) v->data, v->len,
                                                     tags, &error));
    g_assert_no_error (error);
    return mesh;
}

static LrgCollisionMesh *
unit_box_mesh (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    gfloat mn[3] = { -0.5f, -0.5f, -0.5f };
    gfloat mx[3] = { 0.5f, 0.5f, 0.5f };

    push_box (v, mn, mx);
    return mesh_from (v, NULL);
}

static void
identity (gfloat *m)
{
    memset (m, 0, sizeof (gfloat) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* Deterministic pseudo-random sequence (xorshift). */
static guint32
rng_next (guint32 *state)
{
    guint32 x;

    x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static gfloat
rng_float (guint32 *state,
           gfloat   lo,
           gfloat   hi)
{
    return lo + (hi - lo) * (gfloat) (rng_next (state) % 1000000u) / 1000000.0f;
}

/* Brute-force double-sided ray/triangle reference. */
static gboolean
ref_ray (const gfloat *verts,
         guint         n_tris,
         const gfloat *o,
         const gfloat *dir,
         gfloat        max_distance,
         gdouble      *out_t)
{
    gdouble best;
    gboolean hit;
    gdouble d[3];
    gdouble len;
    guint i;

    len = sqrt ((gdouble) dir[0] * dir[0] + (gdouble) dir[1] * dir[1] + (gdouble) dir[2] * dir[2]);
    d[0] = dir[0] / len;
    d[1] = dir[1] / len;
    d[2] = dir[2] / len;
    best = max_distance;
    hit = FALSE;
    for (i = 0; i < n_tris; i++)
    {
        const gfloat *t = verts + i * 9;
        gdouble e1[3], e2[3], p[3], s[3], q[3];
        gdouble det, inv, u, v, tt;

        e1[0] = t[3] - t[0]; e1[1] = t[4] - t[1]; e1[2] = t[5] - t[2];
        e2[0] = t[6] - t[0]; e2[1] = t[7] - t[1]; e2[2] = t[8] - t[2];
        p[0] = d[1] * e2[2] - d[2] * e2[1];
        p[1] = d[2] * e2[0] - d[0] * e2[2];
        p[2] = d[0] * e2[1] - d[1] * e2[0];
        det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
        if (fabs (det) < 1e-12)
            continue;
        inv = 1.0 / det;
        s[0] = o[0] - t[0]; s[1] = o[1] - t[1]; s[2] = o[2] - t[2];
        u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
        if (u < 0.0 || u > 1.0)
            continue;
        q[0] = s[1] * e1[2] - s[2] * e1[1];
        q[1] = s[2] * e1[0] - s[0] * e1[2];
        q[2] = s[0] * e1[1] - s[1] * e1[0];
        v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
        if (v < 0.0 || u + v > 1.0)
            continue;
        tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
        if (tt >= 0.0 && tt < best)
        {
            best = tt;
            hit = TRUE;
        }
    }
    *out_t = best;
    return hit;
}

/* A bumpy height-field grid: n x n cells, two triangles each. */
static GArray *
make_grid (guint n)
{
    GArray *v;
    guint i;
    guint j;

    v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    for (j = 0; j < n; j++)
    {
        for (i = 0; i < n; i++)
        {
            gfloat x0 = (gfloat) i, x1 = (gfloat) i + 1;
            gfloat z0 = (gfloat) j, z1 = (gfloat) j + 1;
            gfloat h00 = sinf (x0 * 0.3f) * cosf (z0 * 0.2f);
            gfloat h10 = sinf (x1 * 0.3f) * cosf (z0 * 0.2f);
            gfloat h01 = sinf (x0 * 0.3f) * cosf (z1 * 0.2f);
            gfloat h11 = sinf (x1 * 0.3f) * cosf (z1 * 0.2f);

            push_tri (v, x0, h00, z0, x1, h10, z0, x1, h11, z1);
            push_tri (v, x0, h00, z0, x1, h11, z1, x0, h01, z1);
        }
    }
    return v;
}

/* ========================================================================== */
/*                                    Tests                                   */
/* ========================================================================== */

static void
test_empty (void)
{
    g_autoptr(LrgCollisionMesh) mesh = lrg_collision_mesh_new ();
    gfloat o[3] = { 0, 1, 0 };
    gfloat d[3] = { 0, -1, 0 };
    gfloat mn[3], mx[3];
    LrgCollisionHit hit;

    g_assert_cmpuint (lrg_collision_mesh_get_triangle_count (mesh), ==, 0);
    g_assert_cmpuint (lrg_collision_mesh_get_node_count (mesh), ==, 0);
    g_assert_cmpuint (lrg_collision_mesh_get_instance_count (mesh), ==, 0);
    g_assert_false (lrg_collision_mesh_get_bounds (mesh, mn, mx));
    g_assert_false (lrg_collision_mesh_raycast (mesh, o, d, 10.0f, &hit));
    g_assert_false (lrg_collision_mesh_sweep_sphere (mesh, o, 0.5f, d, &hit));
    g_assert_false (lrg_collision_mesh_sweep_capsule (mesh, o, 0.3f, 1.8f, d, &hit));
    g_assert_false (lrg_collision_mesh_overlap_capsule (mesh, o, 0.3f, 1.8f));
}

static void
test_invalid_input (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(GError) error = NULL;
    gfloat bad[9] = { 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    gfloat o[3] = { 0.2f, 1, 0.2f };
    gfloat d[3] = { 0, -1, 0 };
    gfloat zero[3] = { 0, 0, 0 };
    gfloat nan_o[3] = { NAN, 1, 0 };

    push_floor (v, -1, -1, 1, 1, 0);
    mesh = mesh_from (v, NULL);

    /* not a multiple of nine */
    g_assert_false (lrg_collision_mesh_set_triangles (mesh, bad, 8, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    /* non-finite coordinate: rejected, previous triangles kept */
    bad[4] = INFINITY;
    g_assert_false (lrg_collision_mesh_set_triangles (mesh, bad, 9, NULL, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);
    g_assert_cmpuint (lrg_collision_mesh_get_triangle_count (mesh), ==, 2);
    g_assert_true (lrg_collision_mesh_raycast (mesh, o, d, 5.0f, NULL));

    /* degenerate queries miss quietly */
    g_assert_false (lrg_collision_mesh_raycast (mesh, o, zero, 5.0f, NULL));
    g_assert_false (lrg_collision_mesh_raycast (mesh, nan_o, d, 5.0f, NULL));
    g_assert_false (lrg_collision_mesh_raycast (mesh, o, d, -1.0f, NULL));
    g_assert_false (lrg_collision_mesh_raycast (mesh, o, d, NAN, NULL));
    g_assert_false (lrg_collision_mesh_sweep_sphere (mesh, o, 0.0f, d, NULL));
    g_assert_false (lrg_collision_mesh_sweep_sphere (mesh, o, 0.1f, zero, NULL));
    g_assert_false (lrg_collision_mesh_overlap_capsule (mesh, nan_o, 0.1f, 1.0f));

    /* clearing */
    g_assert_true (lrg_collision_mesh_set_triangles (mesh, NULL, 0, NULL, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (lrg_collision_mesh_get_triangle_count (mesh), ==, 0);
    g_assert_false (lrg_collision_mesh_raycast (mesh, o, d, 5.0f, NULL));
}

static void
test_degenerate_triangles (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    gfloat o[3] = { 0.5f, 1, 0.2f };
    gfloat d[3] = { 0, -1, 0 };
    LrgCollisionHit hit;

    /* a zero-area triangle keeps its index but is never hit */
    push_tri (v, 0, 0, 0, 1, 0, 0, 2, 0, 0);
    push_floor (v, -1, -1, 2, 2, 0);
    mesh = mesh_from (v, NULL);
    g_assert_cmpuint (lrg_collision_mesh_get_triangle_count (mesh), ==, 3);
    g_assert_true (lrg_collision_mesh_raycast (mesh, o, d, 5.0f, &hit));
    g_assert_cmpuint (hit.triangle, >=, 1);
}

static void
test_raycast_floor (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    guint32 tags[2] = { 7, 7 };
    gfloat o[3] = { 0.25f, 5.0f, -0.5f };
    gfloat down[3] = { 0, -2, 0 };
    gfloat below[3] = { 0.25f, -3.0f, -0.5f };
    gfloat up[3] = { 0, 1, 0 };
    LrgCollisionHit hit;

    push_floor (v, -1, -1, 1, 1, 0);
    mesh = mesh_from (v, tags);

    g_assert_true (lrg_collision_mesh_raycast (mesh, o, down, 10.0f, &hit));
    g_assert_cmpfloat_with_epsilon (hit.distance, 5.0f, EPS);
    g_assert_cmpfloat_with_epsilon (hit.position[0], 0.25f, EPS);
    g_assert_cmpfloat_with_epsilon (hit.position[1], 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (hit.normal[1], 1.0f, EPS);
    g_assert_cmpuint (hit.tag, ==, 7);
    g_assert_cmpuint (hit.instance_id, ==, 0);

    /* double-sided: from below the normal faces down */
    g_assert_true (lrg_collision_mesh_raycast (mesh, below, up, 10.0f, &hit));
    g_assert_cmpfloat_with_epsilon (hit.distance, 3.0f, EPS);
    g_assert_cmpfloat_with_epsilon (hit.normal[1], -1.0f, EPS);

    /* too short */
    g_assert_false (lrg_collision_mesh_raycast (mesh, o, down, 4.9f, NULL));
}

static void
test_raycast_nearest (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    gfloat a_min[3] = { 5, -1, -1 }, a_max[3] = { 6, 1, 1 };
    gfloat b_min[3] = { 2, -1, -1 }, b_max[3] = { 3, 1, 1 };
    gfloat o[3] = { 0, 0, 0 };
    gfloat d[3] = { 1, 0, 0 };
    LrgCollisionHit hit;

    push_box (v, a_min, a_max);
    push_box (v, b_min, b_max);
    mesh = mesh_from (v, NULL);
    g_assert_true (lrg_collision_mesh_raycast (mesh, o, d, 100.0f, &hit));
    g_assert_cmpfloat_with_epsilon (hit.distance, 2.0f, EPS);
    g_assert_cmpfloat_with_epsilon (hit.normal[0], -1.0f, EPS);
    g_assert_cmpuint (hit.triangle, >=, 12);
}

/*
 * A 160x160 bumpy grid (51200 triangles, Privateer's Hold sized):
 * the build is fast and every ray matches the brute-force reference.
 */
static void
test_bvh_matches_brute_force (void)
{
    g_autoptr(GArray) v = make_grid (160);
    g_autoptr(LrgCollisionMesh) mesh = lrg_collision_mesh_new ();
    g_autoptr(GError) error = NULL;
    gint64 t0;
    gint64 t1;
    guint32 rng;
    guint i;
    guint hits;

    t0 = g_get_monotonic_time ();
    g_assert_true (lrg_collision_mesh_set_triangles (mesh, (const gfloat *) v->data, v->len, NULL, &error));
    t1 = g_get_monotonic_time ();
    g_assert_no_error (error);
    g_test_message ("BVH build of %u triangles: %.2f ms (%u nodes)",
                    v->len / 9, (t1 - t0) / 1000.0, lrg_collision_mesh_get_node_count (mesh));
    /* the goal is well under 50 ms; checked strictly with -m perf, and
     * loosely otherwise so busy machines and valgrind still pass */
    if (g_test_perf ())
        g_assert_cmpint (t1 - t0, <, 50000);
    else
        g_assert_cmpint (t1 - t0, <, 5000000);
    g_assert_cmpuint (lrg_collision_mesh_get_node_count (mesh), <, 2 * v->len / 9);

    rng = 0x1234567u;
    hits = 0;
    for (i = 0; i < 400; i++)
    {
        gfloat o[3];
        gfloat d[3];
        gdouble ref_t;
        gboolean ref_hit;
        gboolean hit_ok;
        LrgCollisionHit hit;

        o[0] = rng_float (&rng, -10.0f, 170.0f);
        o[1] = rng_float (&rng, -3.0f, 6.0f);
        o[2] = rng_float (&rng, -10.0f, 170.0f);
        d[0] = rng_float (&rng, -1.0f, 1.0f);
        d[1] = rng_float (&rng, -1.0f, 0.3f);
        d[2] = rng_float (&rng, -1.0f, 1.0f);
        if (fabsf (d[0]) + fabsf (d[1]) + fabsf (d[2]) < 0.01f)
            continue;

        ref_hit = ref_ray ((const gfloat *) v->data, v->len / 9, o, d, 300.0f, &ref_t);
        hit_ok = lrg_collision_mesh_raycast (mesh, o, d, 300.0f, &hit);
        g_assert_cmpint (hit_ok, ==, ref_hit);
        if (ref_hit)
        {
            g_assert_cmpfloat_with_epsilon (hit.distance, (gfloat) ref_t, 1e-3f);
            hits++;
        }
    }
    g_assert_cmpuint (hits, >, 50);
}

static void
test_determinism (void)
{
    g_autoptr(GArray) v = make_grid (40);
    g_autoptr(LrgCollisionMesh) a = mesh_from (v, NULL);
    g_autoptr(LrgCollisionMesh) b = mesh_from (v, NULL);
    guint32 rng;
    guint i;

    g_assert_cmpuint (lrg_collision_mesh_get_node_count (a), ==, lrg_collision_mesh_get_node_count (b));
    rng = 99u;
    for (i = 0; i < 200; i++)
    {
        gfloat o[3];
        gfloat m[3];
        LrgCollisionHit ha;
        LrgCollisionHit hb;
        gboolean ra;
        gboolean rb;

        o[0] = rng_float (&rng, 0.0f, 40.0f);
        o[1] = rng_float (&rng, 1.5f, 3.0f);
        o[2] = rng_float (&rng, 0.0f, 40.0f);
        m[0] = rng_float (&rng, -2.0f, 2.0f);
        m[1] = rng_float (&rng, -4.0f, -0.5f);
        m[2] = rng_float (&rng, -2.0f, 2.0f);
        memset (&ha, 0, sizeof (ha));
        memset (&hb, 0, sizeof (hb));
        ra = lrg_collision_mesh_sweep_capsule (a, o, 0.35f, 1.8f, m, &ha);
        rb = lrg_collision_mesh_sweep_capsule (b, o, 0.35f, 1.8f, m, &hb);
        g_assert_cmpint (ra, ==, rb);
        g_assert_cmpmem (&ha, sizeof (ha), &hb, sizeof (hb));
    }
}

static void
test_sweep_sphere_floor (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    gfloat c[3] = { 0, 2, 0 };
    gfloat down[3] = { 0, -3, 0 };
    gfloat side[3] = { 1, 0, 0 };
    gfloat rest[3] = { 0, 0.5f, 0 };
    LrgCollisionHit hit;

    push_floor (v, -5, -5, 5, 5, 0);
    mesh = mesh_from (v, NULL);

    g_assert_true (lrg_collision_mesh_sweep_sphere (mesh, c, 0.5f, down, &hit));
    g_assert_cmpfloat_with_epsilon (hit.distance, 1.5f, EPS);
    g_assert_cmpfloat_with_epsilon (hit.normal[1], 1.0f, EPS);
    g_assert_cmpfloat_with_epsilon (hit.position[1], 0.0f, EPS);

    /* short of the floor */
    down[1] = -1.0f;
    g_assert_false (lrg_collision_mesh_sweep_sphere (mesh, c, 0.5f, down, NULL));

    /* resting on the floor: sliding passes, pushing down blocks at once */
    g_assert_false (lrg_collision_mesh_sweep_sphere (mesh, rest, 0.5f, side, NULL));
    down[1] = -0.1f;
    g_assert_true (lrg_collision_mesh_sweep_sphere (mesh, rest, 0.5f, down, &hit));
    g_assert_cmpfloat_with_epsilon (hit.distance, 0.0f, EPS);

    /* moving away from it passes too */
    down[1] = 0.5f;
    g_assert_false (lrg_collision_mesh_sweep_sphere (mesh, rest, 0.5f, down, NULL));
}

/* A huge motion through a single thin quad never tunnels. */
static void
test_sweep_tunnelling (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    gfloat c[3] = { -1.0f, 0.0f, 0.0f };
    gfloat m[3] = { 1000.0f, 0.0f, 0.0f };
    gfloat base[3] = { -2.0f, -0.9f, 0.0f };
    LrgCollisionHit hit;

    /* the wall plane x = 0 */
    push_tri (v, 0, -1, -1, 0, 1, -1, 0, 1, 1);
    push_tri (v, 0, -1, -1, 0, 1, 1, 0, -1, 1);
    mesh = mesh_from (v, NULL);

    g_assert_true (lrg_collision_mesh_sweep_sphere (mesh, c, 0.25f, m, &hit));
    g_assert_cmpfloat_with_epsilon (hit.distance, 0.75f, EPS);
    g_assert_cmpfloat_with_epsilon (hit.normal[0], -1.0f, EPS);

    g_assert_true (lrg_collision_mesh_sweep_capsule (mesh, base, 0.3f, 1.8f, m, &hit));
    g_assert_cmpfloat_with_epsilon (hit.distance, 1.7f, EPS);

    /* a capsule passing above the wall misses it */
    base[1] = 1.4f;
    g_assert_false (lrg_collision_mesh_sweep_capsule (mesh, base, 0.3f, 1.8f, m, NULL));
}

/* A capsule's flank meets a box edge with a slanted normal. */
static void
test_sweep_capsule_edge (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    gfloat mn[3] = { 0, 0, 0 }, mx[3] = { 1, 1, 1 };
    gfloat base[3] = { -1.0f, 0.0f, -1.0f };
    gfloat m[3] = { 2.0f, 0.0f, 0.0f };
    LrgCollisionHit hit;
    gfloat expected;

    push_box (v, mn, mx);
    mesh = mesh_from (v, NULL);

    /* the axis passes z = -0.3 beside the edge x = 0, z = 0 (radius 0.5) */
    base[2] = -0.3f;
    g_assert_true (lrg_collision_mesh_sweep_capsule (mesh, base, 0.5f, 1.8f, m, &hit));
    expected = 1.0f - sqrtf (0.25f - 0.09f);
    g_assert_cmpfloat_with_epsilon (hit.distance, expected, 2e-3f);
    g_assert_cmpfloat (hit.normal[0], <, -0.5f);
    g_assert_cmpfloat (hit.normal[2], <, -0.4f);
    g_assert_cmpfloat_with_epsilon (hit.position[0], 0.0f, EPS);
    g_assert_cmpfloat_with_epsilon (hit.position[2], 0.0f, EPS);
}

static void
test_overlap_capsule (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    gfloat mn[3] = { 0, 0, 0 }, mx[3] = { 1, 1, 1 };
    gfloat base[3] = { -0.2f, 0.0f, 0.5f };

    push_box (v, mn, mx);
    mesh = mesh_from (v, NULL);

    g_assert_true (lrg_collision_mesh_overlap_capsule (mesh, base, 0.3f, 1.8f));
    base[0] = -0.5f;
    g_assert_false (lrg_collision_mesh_overlap_capsule (mesh, base, 0.3f, 1.8f));
    /* touching is not overlapping */
    base[0] = -0.3f;
    g_assert_false (lrg_collision_mesh_overlap_capsule (mesh, base, 0.3f, 1.8f));
    /* above the box */
    base[0] = 0.5f;
    base[1] = 1.01f;
    g_assert_false (lrg_collision_mesh_overlap_capsule (mesh, base, 0.3f, 1.8f));
}

static void
test_instances (void)
{
    g_autoptr(LrgCollisionMesh) shape = unit_box_mesh ();
    g_autoptr(LrgCollisionMesh) world = lrg_collision_mesh_new ();
    gfloat m[16];
    gfloat o[3] = { 0, 0, 0 };
    gfloat d[3] = { 1, 0, 0 };
    gfloat mn[3], mx[3];
    LrgCollisionHit hit;
    guint a;
    guint b;

    identity (m);
    m[12] = 10.0f;
    a = lrg_collision_mesh_add_instance (world, shape, m, 42);
    g_assert_cmpuint (a, !=, LRG_COLLISION_MESH_INVALID_INSTANCE);
    g_assert_cmpuint (lrg_collision_mesh_get_instance_count (world), ==, 1);

    g_assert_true (lrg_collision_mesh_raycast (world, o, d, 100.0f, &hit));
    g_assert_cmpfloat_with_epsilon (hit.distance, 9.5f, EPS);
    g_assert_cmpuint (hit.instance_id, ==, 42);
    g_assert_cmpfloat_with_epsilon (hit.normal[0], -1.0f, EPS);
    g_assert_cmpfloat_with_epsilon (hit.position[0], 9.5f, EPS);

    g_assert_true (lrg_collision_mesh_get_bounds (world, mn, mx));
    g_assert_cmpfloat_with_epsilon (mn[0], 9.5f, EPS);
    g_assert_cmpfloat_with_epsilon (mx[0], 10.5f, EPS);

    /* scale x2 along X and rotate 45 degrees about Y: the nearest face
     * is a slanted one */
    identity (m);
    m[0] = 2.0f * cosf (G_PI / 4);
    m[2] = -2.0f * sinf (G_PI / 4);
    m[8] = sinf (G_PI / 4);
    m[10] = cosf (G_PI / 4);
    m[12] = 5.0f;
    g_assert_true (lrg_collision_mesh_set_instance_transform (world, a, m));
    g_assert_true (lrg_collision_mesh_raycast (world, o, d, 100.0f, &hit));
    g_assert_cmpfloat (hit.distance, <, 5.0f);
    g_assert_cmpfloat (hit.distance, >, 3.0f);
    g_assert_cmpfloat_with_epsilon (hit.normal[0] * hit.normal[0] + hit.normal[1] * hit.normal[1] +
                                    hit.normal[2] * hit.normal[2], 1.0f, EPS);
    g_assert_cmpfloat (hit.normal[0], <, 0.0f);

    /* a sweep reports the instance as well */
    identity (m);
    m[12] = 3.0f;
    g_assert_true (lrg_collision_mesh_set_instance_transform (world, a, m));
    g_assert_true (lrg_collision_mesh_sweep_sphere (world, o, 0.5f, d, &hit) == FALSE);
    d[0] = 10.0f;
    g_assert_true (lrg_collision_mesh_sweep_sphere (world, o, 0.5f, d, &hit));
    g_assert_cmpfloat_with_epsilon (hit.distance, 2.0f, EPS);
    g_assert_cmpuint (hit.instance_id, ==, 42);

    /* a second instance, then removal and handle reuse */
    b = lrg_collision_mesh_add_instance (world, shape, m, 43);
    g_assert_cmpuint (b, ==, a + 1);
    g_assert_true (lrg_collision_mesh_remove_instance (world, a));
    g_assert_false (lrg_collision_mesh_remove_instance (world, a));
    g_assert_cmpuint (lrg_collision_mesh_get_instance_count (world), ==, 1);
    g_assert_true (lrg_collision_mesh_raycast (world, o, d, 100.0f, &hit));
    g_assert_cmpuint (hit.instance_id, ==, 43);
    g_assert_cmpuint (lrg_collision_mesh_add_instance (world, shape, m, 44), ==, a);
    g_assert_true (lrg_collision_mesh_remove_instance (world, a));
    g_assert_true (lrg_collision_mesh_remove_instance (world, b));
    g_assert_false (lrg_collision_mesh_raycast (world, o, d, 100.0f, NULL));
}

static void
test_instance_invalid (void)
{
    g_autoptr(LrgCollisionMesh) shape = unit_box_mesh ();
    g_autoptr(LrgCollisionMesh) world = lrg_collision_mesh_new ();
    gfloat m[16];
    guint inst;

    identity (m);
    g_assert_cmpuint (lrg_collision_mesh_add_instance (world, world, m, 1), ==,
                      LRG_COLLISION_MESH_INVALID_INSTANCE);

    /* singular */
    m[0] = 0.0f;
    g_assert_cmpuint (lrg_collision_mesh_add_instance (world, shape, m, 1), ==,
                      LRG_COLLISION_MESH_INVALID_INSTANCE);
    /* non-finite */
    identity (m);
    m[13] = NAN;
    g_assert_cmpuint (lrg_collision_mesh_add_instance (world, shape, m, 1), ==,
                      LRG_COLLISION_MESH_INVALID_INSTANCE);
    /* projective */
    identity (m);
    m[3] = 0.5f;
    g_assert_cmpuint (lrg_collision_mesh_add_instance (world, shape, m, 1), ==,
                      LRG_COLLISION_MESH_INVALID_INSTANCE);

    identity (m);
    inst = lrg_collision_mesh_add_instance (world, shape, m, 1);
    g_assert_cmpuint (inst, !=, LRG_COLLISION_MESH_INVALID_INSTANCE);
    m[5] = 0.0f;
    g_assert_false (lrg_collision_mesh_set_instance_transform (world, inst, m));
    g_assert_false (lrg_collision_mesh_set_instance_transform (world, inst + 5, m));
    g_assert_false (lrg_collision_mesh_remove_instance (world, inst + 5));
}

/* Changes to a shape after instancing are seen through the instance. */
static void
test_instance_shape_changes (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) shape = lrg_collision_mesh_new ();
    g_autoptr(LrgCollisionMesh) world = lrg_collision_mesh_new ();
    g_autoptr(GError) error = NULL;
    gfloat m[16];
    gfloat o[3] = { 0, 5, 0 };
    gfloat d[3] = { 0, -1, 0 };

    identity (m);
    lrg_collision_mesh_add_instance (world, shape, m, 9);
    g_assert_false (lrg_collision_mesh_raycast (world, o, d, 10.0f, NULL));
    push_floor (v, -1, -1, 1, 1, 0);
    g_assert_true (lrg_collision_mesh_set_triangles (shape, (const gfloat *) v->data, v->len, NULL, &error));
    g_assert_true (lrg_collision_mesh_raycast (world, o, d, 10.0f, NULL));
}

static void
test_translate (void)
{
    g_autoptr(GArray) v = g_array_new (FALSE, FALSE, sizeof (gfloat));
    g_autoptr(LrgCollisionMesh) mesh = NULL;
    g_autoptr(LrgCollisionMesh) shape = unit_box_mesh ();
    gfloat m[16];
    gfloat offset[3] = { 100.0f, -2.0f, 0.0f };
    gfloat o[3] = { 100.25f, 5.0f, 0.25f };
    gfloat d[3] = { 0, -1, 0 };
    gfloat o2[3] = { 90.0f, -1.5f, 0.0f };
    gfloat d2[3] = { 1, 0, 0 };
    gfloat mn[3], mx[3];
    LrgCollisionHit hit;

    push_floor (v, -1, -1, 1, 1, 0);
    mesh = mesh_from (v, NULL);
    identity (m);
    m[12] = 5.0f;
    m[13] = 0.5f;
    lrg_collision_mesh_add_instance (mesh, shape, m, 5);

    lrg_collision_mesh_translate (mesh, offset);
    g_assert_true (lrg_collision_mesh_raycast (mesh, o, d, 10.0f, &hit));
    g_assert_cmpfloat_with_epsilon (hit.position[1], -2.0f, EPS);
    g_assert_cmpuint (hit.instance_id, ==, 0);

    /* the instance moved too: box now spans x 104.5..105.5, y -2..-1 */
    g_assert_true (lrg_collision_mesh_raycast (mesh, o2, d2, 100.0f, &hit));
    g_assert_cmpfloat_with_epsilon (hit.distance, 14.5f, EPS);
    g_assert_cmpuint (hit.instance_id, ==, 5);

    g_assert_true (lrg_collision_mesh_get_bounds (mesh, mn, mx));
    g_assert_cmpfloat_with_epsilon (mn[0], 99.0f, EPS);
    g_assert_cmpfloat_with_epsilon (mx[0], 105.5f, EPS);
}

/* Sweeps and rays over a large mesh stay fast (microseconds each). */
static void
test_query_speed (void)
{
    g_autoptr(GArray) v = make_grid (160);
    g_autoptr(LrgCollisionMesh) mesh = mesh_from (v, NULL);
    gint64 t0;
    gint64 t1;
    guint i;
    guint32 rng;

    rng = 7u;
    t0 = g_get_monotonic_time ();
    for (i = 0; i < 2000; i++)
    {
        gfloat base[3];
        gfloat m[3];

        base[0] = rng_float (&rng, 5.0f, 150.0f);
        base[1] = 1.2f;
        base[2] = rng_float (&rng, 5.0f, 150.0f);
        m[0] = rng_float (&rng, -0.2f, 0.2f);
        m[1] = -0.5f;
        m[2] = rng_float (&rng, -0.2f, 0.2f);
        lrg_collision_mesh_sweep_capsule (mesh, base, 0.35f, 1.8f, m, NULL);
        lrg_collision_mesh_raycast (mesh, base, m, 20.0f, NULL);
    }
    t1 = g_get_monotonic_time ();
    g_test_message ("2000 capsule sweeps + rays: %.2f ms", (t1 - t0) / 1000.0);
    if (g_test_perf ())
        g_assert_cmpint (t1 - t0, <, 100000);
    else
        g_assert_cmpint (t1 - t0, <, 20000000);
}

/* queries skip instances by a cached world box; the cache must follow
 * moves, shape edits and changes inside a nested shape */
static void
test_instance_bounds_cache (void)
{
    g_autoptr(LrgCollisionMesh) shape = unit_box_mesh ();
    g_autoptr(LrgCollisionMesh) mid = lrg_collision_mesh_new ();
    g_autoptr(LrgCollisionMesh) world = lrg_collision_mesh_new ();
    gfloat m[16];
    gfloat o[3] = { 0, 0, 0 };
    gfloat d[3] = { 1, 0, 0 };
    LrgCollisionHit hit;
    guint inner;
    guint i;

    /* many instances off the ray, one on it */
    identity (m);
    for (i = 0; i < 200; i++)
    {
        m[12] = (gfloat) i;
        m[14] = 50.0f;
        lrg_collision_mesh_add_instance (world, shape, m, 1000 + i);
    }
    identity (m);
    m[12] = 10.0f;
    lrg_collision_mesh_add_instance (world, shape, m, 7);
    g_assert_true (lrg_collision_mesh_raycast (world, o, d, 100.0f, &hit));
    g_assert_cmpuint (hit.instance_id, ==, 7);
    g_assert_cmpfloat_with_epsilon (hit.distance, 9.5f, EPS);

    /* a nested shape: world -> mid -> box; moving the box inside mid
     * changes no generation the world sees, so it must not be cached */
    identity (m);
    m[12] = 4.0f;
    inner = lrg_collision_mesh_add_instance (mid, shape, m, 0);
    identity (m);
    lrg_collision_mesh_add_instance (world, mid, m, 8);
    g_assert_true (lrg_collision_mesh_raycast (world, o, d, 100.0f, &hit));
    g_assert_cmpuint (hit.instance_id, ==, 8);
    g_assert_cmpfloat_with_epsilon (hit.distance, 3.5f, EPS);
    m[12] = 20.0f;
    g_assert_true (lrg_collision_mesh_set_instance_transform (mid, inner, m));
    g_assert_true (lrg_collision_mesh_raycast (world, o, d, 100.0f, &hit));
    g_assert_cmpuint (hit.instance_id, ==, 7);
}

/* the instance tree must answer exactly what visiting every instance
 * answers: random boxes, random rays and sweeps, compared with the
 * nearest result over one-instance worlds (scanned linearly) */
static void
test_instance_tree_matches_linear (void)
{
    g_autoptr(LrgCollisionMesh) shape = unit_box_mesh ();
    g_autoptr(LrgCollisionMesh) world = lrg_collision_mesh_new ();
    g_autoptr(GPtrArray) singles = g_ptr_array_new_with_free_func (g_object_unref);
    guint32 state = 12345u;
    guint i;
    guint r;

    for (i = 0; i < 300; i++)
    {
        LrgCollisionMesh *single = lrg_collision_mesh_new ();
        gfloat m[16];
        gfloat s;

        identity (m);
        s = rng_float (&state, 0.2f, 3.0f);
        m[0] = s;
        m[5] = rng_float (&state, 0.2f, 3.0f);
        m[10] = s;
        m[12] = rng_float (&state, -50.0f, 50.0f);
        m[13] = rng_float (&state, -5.0f, 5.0f);
        m[14] = rng_float (&state, -50.0f, 50.0f);
        g_assert_cmpuint (lrg_collision_mesh_add_instance (world, shape, m, 100 + i), ==, i);
        lrg_collision_mesh_add_instance (single, shape, m, 100 + i);
        g_ptr_array_add (singles, single);
    }

    for (r = 0; r < 400; r++)
    {
        gfloat o[3];
        gfloat d[3];
        LrgCollisionHit hit;
        LrgCollisionHit best;
        gboolean any;
        gboolean got;

        o[0] = rng_float (&state, -60.0f, 60.0f);
        o[1] = rng_float (&state, -6.0f, 6.0f);
        o[2] = rng_float (&state, -60.0f, 60.0f);
        d[0] = rng_float (&state, -1.0f, 1.0f);
        d[1] = rng_float (&state, -0.2f, 0.2f);
        d[2] = rng_float (&state, -1.0f, 1.0f);

        any = FALSE;
        memset (&best, 0, sizeof (best));
        for (i = 0; i < singles->len; i++)
        {
            if (lrg_collision_mesh_raycast (g_ptr_array_index (singles, i), o, d, 200.0f, &hit) &&
                (!any || hit.distance < best.distance))
            {
                best = hit;
                any = TRUE;
            }
        }
        got = lrg_collision_mesh_raycast (world, o, d, 200.0f, &hit);
        g_assert_cmpint (got, ==, any);
        if (got)
        {
            g_assert_cmpfloat_with_epsilon (hit.distance, best.distance, 1e-4f);
            g_assert_cmpuint (hit.instance_id, ==, best.instance_id);
        }

        /* a sphere sweep along the same ray (box queries underneath) */
        d[0] *= 20.0f;
        d[1] *= 20.0f;
        d[2] *= 20.0f;
        any = FALSE;
        for (i = 0; i < singles->len; i++)
        {
            if (lrg_collision_mesh_sweep_sphere (g_ptr_array_index (singles, i), o, 0.5f, d, &hit) &&
                (!any || hit.distance < best.distance))
            {
                best = hit;
                any = TRUE;
            }
        }
        got = lrg_collision_mesh_sweep_sphere (world, o, 0.5f, d, &hit);
        g_assert_cmpint (got, ==, any);
        if (got)
            g_assert_cmpfloat_with_epsilon (hit.distance, best.distance, 1e-4f);
    }

    /* moving and removing instances rebuilds the tree */
    {
        gfloat m[16];
        gfloat o[3] = { 0, 100, 0 };
        gfloat d[3] = { 0, -1, 0 };
        LrgCollisionHit hit;

        identity (m);
        m[13] = 50.0f;
        g_assert_true (lrg_collision_mesh_set_instance_transform (world, 17, m));
        g_assert_true (lrg_collision_mesh_raycast (world, o, d, 200.0f, &hit));
        g_assert_cmpuint (hit.instance_id, ==, 117);
        g_assert_true (lrg_collision_mesh_remove_instance (world, 17));
        g_assert_true (!lrg_collision_mesh_raycast (world, o, d, 40.0f, &hit));
    }
}

/* Analytical contacts use unequal box dimensions so a capsule/sphere or a
 * single inflated radius cannot accidentally satisfy the same oracle. */
static void
test_sweep_box_faces(void)
{
	g_autoptr(GArray) vertices=g_array_new(FALSE,FALSE,sizeof(gfloat));
	g_autoptr(LrgCollisionMesh) mesh=NULL;
	g_autoptr(GError) error=NULL;
	gfloat center[3]={0,3,0},extent[3]={.2f,.5f,.8f},motion[3]={0,-4,0};
	LrgCollisionHit hit;
	gdouble time;
	push_floor(vertices,-20,-20,20,20,0); mesh=mesh_from(vertices,NULL);
	g_assert_true(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error)); g_assert_no_error(error);
	g_assert_cmpfloat_with_epsilon(time,.625,1e-12); g_assert_cmpfloat_with_epsilon(hit.normal[1],1,EPS);
	g_assert_cmpfloat_with_epsilon(hit.position[1],0,EPS); g_assert_cmpfloat_with_epsilon(hit.distance,2.5,EPS);
	center[1]=.5f; motion[0]=2; motion[1]=0;
	g_assert_false(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error)); g_assert_no_error(error);
	motion[1]=1; g_assert_false(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error));
	motion[1]=-1; g_assert_true(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error)); g_assert_cmpfloat(time,==,0);
	center[1]=.25f; motion[1]=0;
	g_assert_true(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error)); g_assert_cmpfloat(time,==,0);
	/* Exact end-time contact is retained. */
	center[1]=1.5f; motion[0]=0; motion[1]=-1;
	g_assert_true(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error)); g_assert_cmpfloat(time,==,1);
}

static void
test_sweep_box_corners(void)
{
	g_autoptr(GArray) vertices=g_array_new(FALSE,FALSE,sizeof(gfloat));
	g_autoptr(LrgCollisionMesh) mesh=NULL;
	g_autoptr(GError) error=NULL;
	gfloat center[3]={1.8f,1.8f,1},extent[3]={.2f,.2f,.25f},motion[3]={0,0,-2};
	LrgCollisionHit hit;
	gdouble time;
	guint32 tag=77;
	/* Overlapping triangle and box AABBs do not imply triangle intersection:
	 * the slanted edge's cross axis must separate this entire sweep. */
	push_tri(vertices,0,0,0,2,0,0,0,2,0); mesh=mesh_from(vertices,&tag);
	g_assert_false(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error)); g_assert_no_error(error);
	center[0]=.5f; center[1]=.5f;
	g_assert_true(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error));
	g_assert_cmpfloat_with_epsilon(time,.375,1e-12); g_assert_cmpuint(hit.tag,==,77); g_assert_cmpuint(hit.triangle,==,0);
	g_assert_cmpfloat_with_epsilon(hit.normal[2],1,EPS);
	g_assert_cmpfloat_with_epsilon(hit.position[2],0,EPS);
	/* Flat rectangles and lines retain the same plane crossing time. */
	extent[2]=0;
	g_assert_true(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error)); g_assert_cmpfloat_with_epsilon(time,.5,1e-12);
	extent[1]=0;
	g_assert_true(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error)); g_assert_cmpfloat_with_epsilon(time,.5,1e-12);
	extent[0]=0;
	g_assert_true(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error)); g_assert_cmpfloat_with_epsilon(time,.5,1e-6);
}

static void
test_sweep_box_instances(void)
{
	g_autoptr(LrgCollisionMesh) shape=unit_box_mesh(),world=lrg_collision_mesh_new();
	g_autoptr(GError) error=NULL;
	gfloat matrix[16],center[3]={0,0,0},extent[3]={.25f,.4f,.75f},motion[3]={10,0,0};
	LrgCollisionHit hit;
	gdouble time;
	/* Rotate ninety degrees, mirror and scale nonuniformly. Local Z now
	 * spans world X, so the near plane is x=3 and box entry x=2.75. */
	identity(matrix); matrix[0]=0; matrix[2]=2; matrix[5]=3; matrix[8]=4; matrix[10]=0; matrix[12]=5;
	g_assert_cmpuint(lrg_collision_mesh_add_instance(world,shape,matrix,123),!=,LRG_COLLISION_MESH_INVALID_INSTANCE);
	g_assert_true(lrg_collision_mesh_sweep_box(world,center,extent,motion,&hit,&time,&error)); g_assert_no_error(error);
	g_assert_cmpfloat_with_epsilon(time,.275,1e-12); g_assert_cmpuint(hit.instance_id,==,123);
	g_assert_cmpfloat_with_epsilon(hit.normal[0],-1,EPS); g_assert_cmpfloat_with_epsilon(hit.position[0],3,EPS);
	/* A box less wide in Z passes the wall where a max-radius proxy would hit. */
	center[2]=1.5f; extent[2]=.1f;
	g_assert_false(lrg_collision_mesh_sweep_box(world,center,extent,motion,&hit,&time,&error));
	/* An oblique, mirrored instance exposes a real edge contact. A local
	 * axis-aligned proxy would report the face too early (.25858 vs .26287). */
	{
		gfloat c=(gfloat)sqrt(.5);
		gdouble expected;
		identity(matrix); matrix[0]=2*c; matrix[2]=2*c; matrix[5]=3; matrix[8]=4*c; matrix[10]=-4*c; matrix[12]=5; matrix[14]=3;
		g_assert_true(lrg_collision_mesh_set_instance_transform(world,0,matrix));
		center[2]=3; extent[2]=.75f;
		expected=(5-(gdouble)matrix[0]*.5-(gdouble)matrix[8]*.5-.25)/10;
		g_assert_true(lrg_collision_mesh_sweep_box(world,center,extent,motion,&hit,&time,&error)); g_assert_no_error(error);
		g_assert_cmpfloat_with_epsilon(time,expected,1e-12); g_assert_cmpfloat_with_epsilon(hit.normal[0],-1,EPS);
		g_assert_cmpfloat_with_epsilon(hit.position[0],5-3*c,EPS);
	}
}

static void
test_sweep_box_invalid(void)
{
	g_autoptr(LrgCollisionMesh) mesh=unit_box_mesh();
	guint attempt;
	for (attempt=0;attempt<6;attempt++) {
		g_autoptr(GError) error=NULL;
		gfloat center[3]={-3,0,0},extent[3]={.2f,.4f,.8f},motion[3]={6,0,0};
		LrgCollisionHit hit,before;
		gdouble time=-99;
		memset(&hit,0x5a,sizeof(hit)); before=hit;
		if (attempt==0) extent[0]=-1;
		else if (attempt==1) extent[2]=NAN;
		else if (attempt==2) center[1]=INFINITY;
		else if (attempt==3) motion[2]=NAN;
		else if (attempt==4) center[0]=1e20f;
		else { center[1]=10; }
		g_assert_false(lrg_collision_mesh_sweep_box(mesh,center,extent,motion,&hit,&time,&error));
		if (attempt<5) g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT); else g_assert_no_error(error);
		g_assert_cmpmem(&hit,sizeof(hit),&before,sizeof(before)); g_assert_cmpfloat(time,==,-99);
	}
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/collision-mesh/empty", test_empty);
    g_test_add_func ("/collision-mesh/invalid-input", test_invalid_input);
    g_test_add_func ("/collision-mesh/degenerate-triangles", test_degenerate_triangles);
    g_test_add_func ("/collision-mesh/raycast/floor", test_raycast_floor);
    g_test_add_func ("/collision-mesh/raycast/nearest", test_raycast_nearest);
    g_test_add_func ("/collision-mesh/raycast/brute-force", test_bvh_matches_brute_force);
    g_test_add_func ("/collision-mesh/determinism", test_determinism);
    g_test_add_func ("/collision-mesh/sweep/sphere-floor", test_sweep_sphere_floor);
    g_test_add_func ("/collision-mesh/sweep/tunnelling", test_sweep_tunnelling);
    g_test_add_func ("/collision-mesh/sweep/capsule-edge", test_sweep_capsule_edge);
	g_test_add_func("/collision-mesh/sweep/box-faces",test_sweep_box_faces);
	g_test_add_func("/collision-mesh/sweep/box-corners",test_sweep_box_corners);
	g_test_add_func("/collision-mesh/sweep/box-instances",test_sweep_box_instances);
	g_test_add_func("/collision-mesh/sweep/box-invalid",test_sweep_box_invalid);
    g_test_add_func ("/collision-mesh/overlap", test_overlap_capsule);
    g_test_add_func ("/collision-mesh/instances", test_instances);
    g_test_add_func ("/collision-mesh/instances/invalid", test_instance_invalid);
    g_test_add_func ("/collision-mesh/instances/shape-changes", test_instance_shape_changes);
    g_test_add_func ("/collision-mesh/instances/bounds-cache", test_instance_bounds_cache);
    g_test_add_func ("/collision-mesh/instances/tree-matches-linear", test_instance_tree_matches_linear);
    g_test_add_func ("/collision-mesh/translate", test_translate);
    g_test_add_func ("/collision-mesh/query-speed", test_query_speed);

    return g_test_run ();
}
