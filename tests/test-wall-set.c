/* test-wall-set.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgWallSet: exact is_clear / move_slide / segment_clear
 * semantics, raycasts, interior volumes, invalid input, and a seeded
 * randomized equivalence check of the grid broadphase against both the
 * brute-force mode and an independent copy of the reference algorithm.
 */

#include <glib.h>
#include <glib-object.h>
#include <math.h>
#include <string.h>

#include "physics/lrg-wall-set.h"

#define EPS (1e-9)

/* ========================================================================== */
/*                         Reference implementation                           */
/* ========================================================================== */

/*
 * A verbatim transcription of the game-side algorithm the wall set must
 * reproduce bit for bit. It works on a plain array and never uses any
 * broadphase.
 */
typedef struct
{
    const LrgWall *walls;
    guint          n;
    gdouble        limit;
} RefWorld;

static gboolean
ref_valid (const RefWorld *w,
           gdouble         x,
           gdouble         z)
{
    return isfinite (x) && isfinite (z) && fabs (x) <= w->limit && fabs (z) <= w->limit;
}

static gboolean
ref_clear (const RefWorld *w,
           gdouble         x,
           gdouble         z,
           gdouble         radius)
{
    guint i;

    if (!ref_valid (w, x, z) || !isfinite (radius) || radius < 0 || radius > 5)
        return FALSE;
    for (i = 0; i < w->n; i++)
    {
        LrgWall b = w->walls[i];

        if (fabs (x - b.x) < b.half_x + radius && fabs (z - b.z) < b.half_z + radius)
            return FALSE;
    }
    return TRUE;
}

static gboolean
ref_sweep (gdouble        x,
           gdouble        z,
           gdouble        dx,
           gdouble        dz,
           const LrgWall *b,
           gdouble        radius,
           gdouble       *time,
           gdouble       *nx,
           gdouble       *nz)
{
    gdouble p[2], d[2], low[2], high[2];
    gdouble enter = -G_MAXDOUBLE, leave = G_MAXDOUBLE, normal[2] = { 0, 0 };
    guint   axis;

    p[0] = x; p[1] = z; d[0] = dx; d[1] = dz;
    low[0] = b->x - b->half_x - radius;
    low[1] = b->z - b->half_z - radius;
    high[0] = b->x + b->half_x + radius;
    high[1] = b->z + b->half_z + radius;
    for (axis = 0; axis < 2; axis++)
    {
        gdouble a, c, n;

        if (fabs (d[axis]) < 1e-12)
        {
            if (p[axis] <= low[axis] || p[axis] >= high[axis])
                return FALSE;
            continue;
        }
        a = (low[axis] - p[axis]) / d[axis];
        c = (high[axis] - p[axis]) / d[axis];
        n = -1;
        if (a > c)
        {
            gdouble temp = a;
            a = c;
            c = temp;
            n = 1;
        }
        if (a > enter)
        {
            enter = a;
            normal[0] = normal[1] = 0;
            normal[axis] = n;
        }
        leave = MIN (leave, c);
        if (enter > leave)
            return FALSE;
    }
    if (enter < 0 || enter > 1 || leave < 0)
        return FALSE;
    *time = enter; *nx = normal[0]; *nz = normal[1];
    return TRUE;
}

static gboolean
ref_move (const RefWorld *w,
          gdouble         radius,
          gdouble        *x,
          gdouble        *z,
          gdouble         dx,
          gdouble         dz)
{
    guint   iteration, i;
    gdouble px, pz;

    if (!ref_clear (w, *x, *z, radius) || !isfinite (dx) || !isfinite (dz) ||
        fabs (dx) > w->limit * 2 || fabs (dz) > w->limit * 2)
        return FALSE;
    px = *x; pz = *z;
    dx = CLAMP (px + dx, -w->limit, w->limit) - px;
    dz = CLAMP (pz + dz, -w->limit, w->limit) - pz;
    for (iteration = 0; iteration < 4 && (fabs (dx) + fabs (dz) > 1e-10); iteration++)
    {
        gdouble  first = 1, nx = 0, nz = 0;
        gboolean blocked = FALSE;

        for (i = 0; i < w->n; i++)
        {
            gdouble time, bx, bz;

            if (ref_sweep (px, pz, dx, dz, &w->walls[i], radius, &time, &bx, &bz) && time <= first)
            {
                first = time; nx = bx; nz = bz; blocked = TRUE;
            }
        }
        px += dx * first; pz += dz * first;
        if (!blocked)
            break;
        px += nx * 1e-7; pz += nz * 1e-7;
        dx *= 1 - first; dz *= 1 - first;
        if (nx) dx = 0;
        if (nz) dz = 0;
    }
    *x = px; *z = pz;
    return TRUE;
}

static gboolean
ref_los (const RefWorld *w,
         gdouble         ax,
         gdouble         az,
         gdouble         bx,
         gdouble         bz)
{
    guint i;

    if (!ref_clear (w, ax, az, 0) || !ref_clear (w, bx, bz, 0))
        return FALSE;
    for (i = 0; i < w->n; i++)
    {
        gdouble time, nx, nz;

        if (ref_sweep (ax, az, bx - ax, bz - az, &w->walls[i], 0, &time, &nx, &nz))
            return FALSE;
    }
    return TRUE;
}

/* same_bits:
 * Bitwise double comparison, so -0.0 / 0.0 or last-ulp drift is caught. */
static gboolean
same_bits (gdouble a,
           gdouble b)
{
    return memcmp (&a, &b, sizeof (gdouble)) == 0;
}

/* ========================================================================== */
/*                                  Tests                                     */
/* ========================================================================== */

static void
test_wall_boxed (void)
{
    g_autoptr(LrgWall) wall = lrg_wall_new (1, 2, 3, 4, 5, 6, 7);
    g_autoptr(LrgWall) copy = lrg_wall_copy (wall);
    GValue value = G_VALUE_INIT;

    g_assert_cmpfloat (copy->x, ==, 1);
    g_assert_cmpfloat (copy->z, ==, 2);
    g_assert_cmpfloat (copy->half_x, ==, 3);
    g_assert_cmpfloat (copy->half_z, ==, 4);
    g_assert_cmpfloat (copy->height, ==, 5);
    g_assert_cmpuint (copy->kind, ==, 6);
    g_assert_cmpuint (copy->tag, ==, 7);
    g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_WALL));

    g_value_init (&value, LRG_TYPE_WALL);
    g_value_set_boxed (&value, wall);
    g_assert_cmpfloat (((LrgWall *)g_value_get_boxed (&value))->half_z, ==, 4);
    g_value_unset (&value);
}

static void
test_wall_set_add_get (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();
    LrgWall wall;
    LrgWall out;

    g_assert_cmpuint (lrg_wall_set_get_count (set), ==, 0);
    g_assert_cmpfloat (lrg_wall_set_get_bounds_limit (set), ==, G_MAXDOUBLE);
    g_assert_cmpfloat (lrg_wall_set_get_cell_size (set), ==, LRG_WALL_SET_DEFAULT_CELL_SIZE);

    g_assert_cmpuint (lrg_wall_set_add_box (set, 0, 0, 1, 2, 3, 4, 5), ==, 0);
    wall = (LrgWall) { 10, -10, .5, .5, 4, 1, 99 };
    g_assert_cmpuint (lrg_wall_set_add (set, &wall), ==, 1);
    g_assert_cmpuint (lrg_wall_set_get_count (set), ==, 2);

    g_assert_true (lrg_wall_set_get (set, 1, &out));
    g_assert_cmpfloat (out.x, ==, 10);
    g_assert_cmpfloat (out.z, ==, -10);
    g_assert_cmpuint (out.tag, ==, 99);

    memset (&out, 0x5a, sizeof out);
    g_assert_false (lrg_wall_set_get (set, 2, &out));
    g_assert_cmpuint (out.kind, ==, 0x5a5a5a5a);

    /* Invalid walls are rejected without being stored. */
    g_assert_cmpuint (lrg_wall_set_add_box (set, NAN, 0, 1, 1, 1, 0, 0), ==, LRG_WALL_SET_INVALID_INDEX);
    g_assert_cmpuint (lrg_wall_set_add_box (set, 0, INFINITY, 1, 1, 1, 0, 0), ==, LRG_WALL_SET_INVALID_INDEX);
    g_assert_cmpuint (lrg_wall_set_add_box (set, 0, 0, -1, 1, 1, 0, 0), ==, LRG_WALL_SET_INVALID_INDEX);
    g_assert_cmpuint (lrg_wall_set_add_box (set, 0, 0, 1, 1, NAN, 0, 0), ==, LRG_WALL_SET_INVALID_INDEX);
    g_assert_cmpuint (lrg_wall_set_add_box (set, G_MAXDOUBLE, 0, G_MAXDOUBLE, 1, 1, 0, 0), ==,
                      LRG_WALL_SET_INVALID_INDEX);
    g_assert_cmpuint (lrg_wall_set_get_count (set), ==, 2);

    lrg_wall_set_add_volume (set, 0, 0, 5, 5, 3);
    lrg_wall_set_clear (set);
    g_assert_cmpuint (lrg_wall_set_get_count (set), ==, 0);
    g_assert_cmpuint (lrg_wall_set_get_volume_count (set), ==, 0);
    g_assert_true (lrg_wall_set_is_clear (set, 0, 0, 1));
}

static void
test_wall_set_properties (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();
    gdouble limit = 0, cell = 0;

    g_object_set (set, "bounds-limit", 100.0, "cell-size", 4.0, NULL);
    g_object_get (set, "bounds-limit", &limit, "cell-size", &cell, NULL);
    g_assert_cmpfloat (limit, ==, 100.0);
    g_assert_cmpfloat (cell, ==, 4.0);

    /* Invalid values are ignored. */
    lrg_wall_set_set_bounds_limit (set, -1);
    lrg_wall_set_set_bounds_limit (set, NAN);
    lrg_wall_set_set_cell_size (set, -3);
    lrg_wall_set_set_cell_size (set, INFINITY);
    g_assert_cmpfloat (lrg_wall_set_get_bounds_limit (set), ==, 100.0);
    g_assert_cmpfloat (lrg_wall_set_get_cell_size (set), ==, 4.0);

    lrg_wall_set_set_cell_size (set, 0);
    g_assert_cmpfloat (lrg_wall_set_get_cell_size (set), ==, 0.0);
}

static void
test_wall_set_is_clear (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();

    lrg_wall_set_add_box (set, 0, 0, 1, 1, 2, 0, 0);

    /* Strictly overlapping blocks, touching is clear. */
    g_assert_false (lrg_wall_set_is_clear (set, 0, 0, 0));
    g_assert_false (lrg_wall_set_is_clear (set, 1.4, 0, .5));
    g_assert_true (lrg_wall_set_is_clear (set, 1.5, 0, .5));
    g_assert_true (lrg_wall_set_is_clear (set, 1.0, 1.0, 0));
    g_assert_true (lrg_wall_set_is_clear (set, 1.5, 1.5, .5));
    g_assert_false (lrg_wall_set_is_clear (set, 1.49, 1.49, .5));
    g_assert_true (lrg_wall_set_is_clear (set, 20, 20, 5));

    /* Invalid input. */
    g_assert_false (lrg_wall_set_is_clear (set, NAN, 20, 0));
    g_assert_false (lrg_wall_set_is_clear (set, 20, INFINITY, 0));
    g_assert_false (lrg_wall_set_is_clear (set, 20, 20, -0.1));
    g_assert_false (lrg_wall_set_is_clear (set, 20, 20, 5.0001));
    g_assert_false (lrg_wall_set_is_clear (set, 20, 20, NAN));

    lrg_wall_set_set_bounds_limit (set, 50);
    g_assert_true (lrg_wall_set_is_clear (set, 50, -50, 0));
    g_assert_false (lrg_wall_set_is_clear (set, 50.001, 0, 0));
    g_assert_false (lrg_wall_set_is_clear (set, 0, -50.001, 0));
}

static void
test_wall_set_move_free (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();
    gdouble x = 1, z = 2;

    /* Empty world: exact displacement. */
    g_assert_true (lrg_wall_set_move_slide (set, .4, &x, &z, 3, -4));
    g_assert_cmpfloat (x, ==, 4);
    g_assert_cmpfloat (z, ==, -2);
}

static void
test_wall_set_move_block (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();
    gdouble x = 0, z = 0;

    /* Wall face at x = 5; agent radius .5 stops at 4.5 minus push-out. */
    lrg_wall_set_add_box (set, 6, 0, 1, 10, 3, 0, 0);
    g_assert_true (lrg_wall_set_move_slide (set, .5, &x, &z, 10, 0));
    g_assert_cmpfloat_with_epsilon (x, 4.5 - 1e-7, 1e-12);
    g_assert_cmpfloat (z, ==, 0);
    g_assert_true (lrg_wall_set_is_clear (set, x, z, .5));
}

static void
test_wall_set_corner_slide (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();
    gdouble x = 0, z = 0;

    /* Diagonal into a long wall along Z: X stops, Z keeps its full share. */
    lrg_wall_set_add_box (set, 3, 0, .5, 50, 3, 0, 0);
    g_assert_true (lrg_wall_set_move_slide (set, .5, &x, &z, 4, 4));
    g_assert_cmpfloat_with_epsilon (x, 2 - 1e-7, 1e-12);
    g_assert_cmpfloat_with_epsilon (z, 4, 1e-9);

    /* Sliding off the end of a short wall around its corner. */
    lrg_wall_set_clear (set);
    lrg_wall_set_add_box (set, 3, 0, .5, 1, 3, 0, 0);
    x = 0;
    z = 0;
    g_assert_true (lrg_wall_set_move_slide (set, .5, &x, &z, 4, 4));
    /* Contact at x = 2 happens at t = .5 (z = 2 > 1.5 already clears the
     * expanded wall), so the path is actually unobstructed. */
    g_assert_cmpfloat_with_epsilon (x, 4, 1e-9);
    g_assert_cmpfloat_with_epsilon (z, 4, 1e-9);

    /* Concave inside corner: both axes stop. */
    lrg_wall_set_clear (set);
    lrg_wall_set_add_box (set, 3, 0, .5, 10, 3, 0, 0);
    lrg_wall_set_add_box (set, 0, 3, 10, .5, 3, 0, 1);
    x = 0;
    z = 0;
    g_assert_true (lrg_wall_set_move_slide (set, .5, &x, &z, 5, 7));
    g_assert_cmpfloat_with_epsilon (x, 2 - 1e-7, 1e-9);
    g_assert_cmpfloat_with_epsilon (z, 2 - 1e-7, 1e-9);
    g_assert_true (lrg_wall_set_is_clear (set, x, z, .5));
}

static void
test_wall_set_door_gap (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();
    gdouble x, z;

    /* A wall along X at z = 10 with a 1.2 m doorway centred on x = 0. */
    lrg_wall_set_add_box (set, -5.6, 10, 5, .5, 4, 2, 0);
    lrg_wall_set_add_box (set, 5.6, 10, 5, .5, 4, 2, 1);

    /* A 0.5 m agent (half .25) walks straight through. */
    x = 0;
    z = 0;
    g_assert_true (lrg_wall_set_move_slide (set, .25, &x, &z, 0, 20));
    g_assert_cmpfloat (x, ==, 0);
    g_assert_cmpfloat (z, ==, 20);

    /* A 1.4 m agent (half .7) does not fit and stops at the wall face. */
    x = 0;
    z = 0;
    g_assert_true (lrg_wall_set_move_slide (set, .7, &x, &z, 0, 20));
    g_assert_cmpfloat_with_epsilon (z, 9.5 - .7 - 1e-7, 1e-9);

    /* Offset approach slides along the jamb into the doorway only when it
     * aims inside the gap. */
    x = .3;
    z = 0;
    g_assert_true (lrg_wall_set_move_slide (set, .25, &x, &z, 0, 20));
    g_assert_cmpfloat (z, ==, 20);

    /* Line of sight through the door and across the wall. */
    g_assert_true (lrg_wall_set_segment_clear (set, 0, 0, 0, 20));
    g_assert_false (lrg_wall_set_segment_clear (set, -5, 0, -5, 20));
    g_assert_true (lrg_wall_set_segment_clear (set, -.5, 0, .5, 20));
}

static void
test_wall_set_move_invalid (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();
    gdouble x, z;

    lrg_wall_set_add_box (set, 0, 0, 1, 1, 2, 0, 0);
    lrg_wall_set_set_bounds_limit (set, 100);

    /* Starting inside a wall: untouched, FALSE. */
    x = .5;
    z = .5;
    g_assert_false (lrg_wall_set_move_slide (set, .4, &x, &z, 10, 0));
    g_assert_cmpfloat (x, ==, .5);
    g_assert_cmpfloat (z, ==, .5);

    x = 10;
    z = 10;
    g_assert_false (lrg_wall_set_move_slide (set, .4, &x, &z, NAN, 0));
    g_assert_false (lrg_wall_set_move_slide (set, .4, &x, &z, 0, INFINITY));
    g_assert_false (lrg_wall_set_move_slide (set, .4, &x, &z, 200.1, 0));
    g_assert_false (lrg_wall_set_move_slide (set, 6, &x, &z, 1, 0));
    g_assert_false (lrg_wall_set_move_slide (set, .4, NULL, &z, 1, 0));
    g_assert_cmpfloat (x, ==, 10);
    g_assert_cmpfloat (z, ==, 10);

    /* A long move is clamped into the bounds limit. */
    g_assert_true (lrg_wall_set_move_slide (set, .4, &x, &z, 199, -150));
    g_assert_cmpfloat (x, ==, 100);
    g_assert_cmpfloat (z, ==, -100);
}

static void
test_wall_set_segment_clear (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();

    lrg_wall_set_add_box (set, 0, 0, 1, 1, 2, 0, 0);

    g_assert_false (lrg_wall_set_segment_clear (set, -5, 0, 5, 0));
    g_assert_true (lrg_wall_set_segment_clear (set, -5, 2, 5, 2));
    /* Grazing a face exactly is not an entry. */
    g_assert_true (lrg_wall_set_segment_clear (set, -5, 1, 5, 1));
    /* Endpoint inside the wall. */
    g_assert_false (lrg_wall_set_segment_clear (set, 0, 0, 5, 5));
    /* Endpoint on the wall face is a clear point. */
    g_assert_true (lrg_wall_set_segment_clear (set, 1, 0, 5, 0));
    g_assert_false (lrg_wall_set_segment_clear (set, NAN, 0, 5, 0));
}

static void
test_wall_set_raycast (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();
    guint kind = 0;
    gdouble d;

    lrg_wall_set_add_box (set, 10, 0, 1, 1, 3, 7, 0);
    lrg_wall_set_add_box (set, 20, 0, 1, 5, 3, 8, 1);

    d = lrg_wall_set_raycast_distance (set, 0, 0, 1, 0, 30, &kind);
    g_assert_cmpfloat_with_epsilon (d, 9, EPS);
    g_assert_cmpuint (kind, ==, 7);

    /* Unnormalised direction gives the same distance. */
    d = lrg_wall_set_raycast_distance (set, 0, 0, 25, 0, 30, &kind);
    g_assert_cmpfloat_with_epsilon (d, 9, EPS);

    /* Passing above the small wall, hitting the tall one. */
    d = lrg_wall_set_raycast_distance (set, 0, 3, 1, 0, 30, &kind);
    g_assert_cmpfloat_with_epsilon (d, 19, EPS);
    g_assert_cmpuint (kind, ==, 8);

    /* Out of range. */
    d = lrg_wall_set_raycast_distance (set, 0, 0, 1, 0, 5, &kind);
    g_assert_cmpfloat (d, ==, 5);
    g_assert_cmpuint (kind, ==, LRG_WALL_SET_NO_HIT);

    /* Diagonal. */
    d = lrg_wall_set_raycast_distance (set, 0, -9, 1, 1, 30, NULL);
    g_assert_cmpfloat_with_epsilon (d, sqrt (2) * 9, 1e-9);

    /* Equal distances report the lowest index. */
    {
        g_autoptr(LrgWallSet) tie = lrg_wall_set_new ();

        lrg_wall_set_add_box (tie, 5, 0, 1, 1, 1, 3, 0);
        lrg_wall_set_add_box (tie, 5, 0, 1, 2, 1, 4, 1);
        d = lrg_wall_set_raycast_distance (tie, 0, 0, 1, 0, 30, &kind);
        g_assert_cmpfloat_with_epsilon (d, 4, EPS);
        g_assert_cmpuint (kind, ==, 3);
        /* Hit exactly at the maximum distance counts. */
        d = lrg_wall_set_raycast_distance (tie, 0, 0, 1, 0, 4, &kind);
        g_assert_cmpfloat (d, ==, 4);
        g_assert_cmpuint (kind, ==, 3);
    }

    /* Origin inside: zero. */
    d = lrg_wall_set_raycast_distance (set, 10, 0, 1, 0, 30, &kind);
    g_assert_cmpfloat (d, ==, 0);
    g_assert_cmpuint (kind, ==, 7);

    /* Invalid. */
    d = lrg_wall_set_raycast_distance (set, 0, 0, 0, 0, 30, &kind);
    g_assert_cmpfloat (d, ==, 0);
    g_assert_cmpuint (kind, ==, LRG_WALL_SET_NO_HIT);
    g_assert_cmpfloat (lrg_wall_set_raycast_distance (set, NAN, 0, 1, 0, 30, NULL), ==, 0);
    g_assert_cmpfloat (lrg_wall_set_raycast_distance (set, 0, 0, 1, 0, -1, NULL), ==, 0);
    g_assert_cmpfloat (lrg_wall_set_raycast_distance (set, 0, 0, 1, 0, INFINITY, NULL), ==, 0);
}

static void
test_wall_set_volumes (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();

    g_assert_cmpint (lrg_wall_set_volume_at (set, 0, 0), ==, -1);
    g_assert_cmpuint (lrg_wall_set_add_volume (set, 0, 0, 5, 3, 11), ==, 0);
    g_assert_cmpuint (lrg_wall_set_add_volume (set, 4, 0, 2, 2, 22), ==, 1);
    g_assert_cmpuint (lrg_wall_set_get_volume_count (set), ==, 2);

    g_assert_cmpint (lrg_wall_set_volume_at (set, 0, 0), ==, 11);
    /* Overlap: first added wins. */
    g_assert_cmpint (lrg_wall_set_volume_at (set, 4, 0), ==, 11);
    g_assert_cmpint (lrg_wall_set_volume_at (set, 5.5, 0), ==, 22);
    /* Edges inclusive. */
    g_assert_cmpint (lrg_wall_set_volume_at (set, -5, 3), ==, 11);
    g_assert_cmpint (lrg_wall_set_volume_at (set, -5.01, 0), ==, -1);
    g_assert_cmpint (lrg_wall_set_volume_at (set, NAN, 0), ==, -1);

    /* Volumes never block. */
    g_assert_true (lrg_wall_set_is_clear (set, 0, 0, 1));

    g_assert_cmpuint (lrg_wall_set_add_volume (set, 0, 0, -1, 1, 1), ==, LRG_WALL_SET_INVALID_INDEX);
    g_assert_cmpuint (lrg_wall_set_add_volume (set, 0, 0, 1, 1, (guint)G_MAXINT + 1u), ==,
                      LRG_WALL_SET_INVALID_INDEX);
    g_assert_cmpuint (lrg_wall_set_add_volume (set, INFINITY, 0, 1, 1, 1), ==, LRG_WALL_SET_INVALID_INDEX);
}

static void
test_wall_set_grid_invalidation (void)
{
    g_autoptr(LrgWallSet) set = lrg_wall_set_new ();

    lrg_wall_set_set_cell_size (set, 2);
    lrg_wall_set_add_box (set, 0, 0, 1, 1, 2, 0, 0);
    lrg_wall_set_build_index (set);
    g_assert_true (lrg_wall_set_is_clear (set, 40, 40, 1));

    /* A wall far outside the old grid bounds must be seen. */
    lrg_wall_set_add_box (set, 40, 40, 1, 1, 2, 0, 1);
    g_assert_false (lrg_wall_set_is_clear (set, 40, 40, 1));

    lrg_wall_set_clear (set);
    g_assert_true (lrg_wall_set_is_clear (set, 40, 40, 1));
    lrg_wall_set_add_box (set, -40, 0, 1, 1, 2, 0, 0);
    g_assert_false (lrg_wall_set_is_clear (set, -40, 0, 0));

    /* Changing the cell size rebuilds. */
    lrg_wall_set_set_cell_size (set, 64);
    g_assert_false (lrg_wall_set_is_clear (set, -40, 0, 0));
    g_assert_true (lrg_wall_set_is_clear (set, 0, 0, 0));
}

/* Tie: an agent aimed exactly at the shared corner of two walls hits both
 * at the same time. The later wall decides the normal. */
static void
test_wall_set_tie_order (void)
{
    g_autoptr(LrgWallSet) first = lrg_wall_set_new ();
    g_autoptr(LrgWallSet) second = lrg_wall_set_new ();
    LrgWall walls[2];
    RefWorld ref;
    gdouble x, z, rx, rz;

    /* Wall A presents its -X face, wall B its -Z face, both at distance 1.5. */
    walls[0] = (LrgWall) { 3, 3, 1, 1, 1, 0, 0 };
    walls[1] = (LrgWall) { 3, 3, 1, 1, 1, 0, 1 };
    lrg_wall_set_add (first, &walls[0]);
    lrg_wall_set_add (first, &walls[1]);

    /* Two walls that the diagonal reaches at the same time via different
     * faces. */
    walls[0] = (LrgWall) { 3, 0, 1, 10, 1, 0, 0 };   /* face x = 2 */
    walls[1] = (LrgWall) { 0, 3, 10, 1, 1, 0, 1 };   /* face z = 2 */
    lrg_wall_set_add (second, &walls[0]);
    lrg_wall_set_add (second, &walls[1]);

    ref.walls = walls;
    ref.n = 2;
    ref.limit = G_MAXDOUBLE;

    x = rx = 0;
    z = rz = 0;
    g_assert_true (lrg_wall_set_move_slide (second, 0, &x, &z, 4, 4));
    g_assert_true (ref_move (&ref, 0, &rx, &rz, 4, 4));
    g_assert_true (same_bits (x, rx));
    g_assert_true (same_bits (z, rz));

    lrg_wall_set_set_cell_size (second, 0);
    x = 0;
    z = 0;
    g_assert_true (lrg_wall_set_move_slide (second, 0, &x, &z, 4, 4));
    g_assert_true (same_bits (x, rx));
    g_assert_true (same_bits (z, rz));

    x = 0;
    z = 0;
    g_assert_true (lrg_wall_set_move_slide (first, .5, &x, &z, 3, 3));
    g_assert_true (lrg_wall_set_is_clear (first, x, z, .5));

    /* Contact exactly at the end of the motion (t == 1) still counts as a
     * hit and pushes the agent back out along the normal. */
    lrg_wall_set_clear (first);
    lrg_wall_set_add_box (first, 3, 3, .5, 1.5, 1, 0, 0);
    x = 0;
    z = 0;
    g_assert_true (lrg_wall_set_move_slide (first, .5, &x, &z, 2, 1));
    g_assert_true (same_bits (x, 2 - 1e-7));
    g_assert_true (same_bits (z, 1));
}

/* Random scene on a coarse lattice (many exact ties), mixing small props,
 * long walls and some huge boxes spanning many cells. */
static GArray *
random_walls (GRand *rand,
              guint  count,
              gdouble extent)
{
    GArray *walls = g_array_new (FALSE, FALSE, sizeof (LrgWall));
    guint   i;

    for (i = 0; i < count; i++)
    {
        LrgWall w;
        gdouble roll = g_rand_double (rand);

        w.x = floor (g_rand_double_range (rand, -extent, extent) * 2) / 2;
        w.z = floor (g_rand_double_range (rand, -extent, extent) * 2) / 2;
        if (roll < .6)
        {
            w.half_x = .25 + floor (g_rand_double_range (rand, 0, 6)) / 4;
            w.half_z = .25 + floor (g_rand_double_range (rand, 0, 6)) / 4;
        }
        else if (roll < .95)
        {
            gboolean along_x = g_rand_boolean (rand);

            w.half_x = along_x ? g_rand_double_range (rand, 4, 30) : .5;
            w.half_z = along_x ? .5 : g_rand_double_range (rand, 4, 30);
        }
        else
        {
            w.half_x = g_rand_double_range (rand, 20, 60);
            w.half_z = g_rand_double_range (rand, 20, 60);
        }
        w.height = 3;
        w.kind = i % 3;
        w.tag = i;
        g_array_append_val (walls, w);
    }
    return walls;
}

static void
run_equivalence (guint32 seed,
                 gdouble limit,
                 guint   n_walls,
                 guint   n_queries)
{
    g_autoptr(GRand)      rand = g_rand_new_with_seed (seed);
    g_autoptr(GArray)     walls = NULL;
    g_autoptr(LrgWallSet) brute = lrg_wall_set_new ();
    g_autoptr(LrgWallSet) grid = lrg_wall_set_new ();
    g_autoptr(LrgWallSet) fine = lrg_wall_set_new ();
    RefWorld ref;
    guint    i;
    guint    moved = 0, blocked_moves = 0, los_blocked = 0;

    walls = random_walls (rand, n_walls, 200);
    lrg_wall_set_set_cell_size (brute, 0);
    lrg_wall_set_set_cell_size (fine, 1.5);
    lrg_wall_set_set_bounds_limit (brute, limit);
    lrg_wall_set_set_bounds_limit (grid, limit);
    lrg_wall_set_set_bounds_limit (fine, limit);
    for (i = 0; i < walls->len; i++)
    {
        const LrgWall *w = &g_array_index (walls, LrgWall, i);

        g_assert_cmpuint (lrg_wall_set_add (brute, w), ==, i);
        g_assert_cmpuint (lrg_wall_set_add (grid, w), ==, i);
        g_assert_cmpuint (lrg_wall_set_add (fine, w), ==, i);
    }
    ref.walls = (const LrgWall *)walls->data;
    ref.n = walls->len;
    ref.limit = limit;

    for (i = 0; i < n_queries; i++)
    {
        gdouble x, z, dx, dz, r, bx, bz;
        gdouble rx, rz, gx, gz, fx, fz, ox, oz;
        gboolean ref_ok, ok;
        gdouble scale;

        /* Mix lattice-aligned points (ties, touching faces) with
         * arbitrary ones. */
        if (i % 2)
        {
            x = floor (g_rand_double_range (rand, -230, 230) * 4) / 4;
            z = floor (g_rand_double_range (rand, -230, 230) * 4) / 4;
            r = floor (g_rand_double_range (rand, 0, 5.25) * 4) / 4;
        }
        else
        {
            x = g_rand_double_range (rand, -230, 230);
            z = g_rand_double_range (rand, -230, 230);
            r = g_rand_double_range (rand, -0.1, 5.1);
        }
        scale = (i % 7 == 0) ? 200 : (i % 3 == 0) ? 20 : 3;
        dx = g_rand_double_range (rand, -scale, scale);
        dz = g_rand_double_range (rand, -scale, scale);
        if (i % 11 == 0)
            dz = 0;
        if (i % 13 == 0)
            dx = 0;

        /* is_clear */
        ref_ok = ref_clear (&ref, x, z, r);
        g_assert_cmpint (lrg_wall_set_is_clear (brute, x, z, r), ==, ref_ok);
        g_assert_cmpint (lrg_wall_set_is_clear (grid, x, z, r), ==, ref_ok);
        g_assert_cmpint (lrg_wall_set_is_clear (fine, x, z, r), ==, ref_ok);

        /* move_slide */
        rx = gx = fx = ox = x;
        rz = gz = fz = oz = z;
        ref_ok = ref_move (&ref, r, &rx, &rz, dx, dz);
        ok = lrg_wall_set_move_slide (brute, r, &ox, &oz, dx, dz);
        g_assert_cmpint (ok, ==, ref_ok);
        g_assert_true (same_bits (ox, rx));
        g_assert_true (same_bits (oz, rz));
        ok = lrg_wall_set_move_slide (grid, r, &gx, &gz, dx, dz);
        g_assert_cmpint (ok, ==, ref_ok);
        g_assert_true (same_bits (gx, rx));
        g_assert_true (same_bits (gz, rz));
        ok = lrg_wall_set_move_slide (fine, r, &fx, &fz, dx, dz);
        g_assert_cmpint (ok, ==, ref_ok);
        g_assert_true (same_bits (fx, rx));
        g_assert_true (same_bits (fz, rz));
        if (ref_ok)
        {
            moved++;
            if (!same_bits (rx, x + dx) || !same_bits (rz, z + dz))
                blocked_moves++;
        }

        /* segment_clear between the start and a random point. */
        bx = x + dx * 3;
        bz = z + dz * 3;
        ref_ok = ref_los (&ref, x, z, bx, bz);
        g_assert_cmpint (lrg_wall_set_segment_clear (brute, x, z, bx, bz), ==, ref_ok);
        g_assert_cmpint (lrg_wall_set_segment_clear (grid, x, z, bx, bz), ==, ref_ok);
        g_assert_cmpint (lrg_wall_set_segment_clear (fine, x, z, bx, bz), ==, ref_ok);
        if (!ref_ok)
            los_blocked++;

        /* raycast: grid and brute agree. */
        {
            guint kb, kg, kf;
            gdouble db, dg, df;

            db = lrg_wall_set_raycast_distance (brute, x, z, dx, dz, 60, &kb);
            dg = lrg_wall_set_raycast_distance (grid, x, z, dx, dz, 60, &kg);
            df = lrg_wall_set_raycast_distance (fine, x, z, dx, dz, 60, &kf);
            g_assert_true (same_bits (db, dg));
            g_assert_true (same_bits (db, df));
            g_assert_cmpuint (kb, ==, kg);
            g_assert_cmpuint (kb, ==, kf);
        }
    }

    /* The scene must actually exercise collisions. */
    g_assert_cmpuint (moved, >, n_queries / 4);
    g_assert_cmpuint (blocked_moves, >, n_queries / 50);
    g_assert_cmpuint (los_blocked, >, n_queries / 50);
}

static void
test_wall_set_equivalence (void)
{
    run_equivalence (0x5eed1234, G_MAXDOUBLE, 400, 20000);
}

static void
test_wall_set_equivalence_limited (void)
{
    /* A tight limit clamps many targets and invalidates many starts. */
    run_equivalence (0xc0ffee, 180, 250, 8000);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/wall-set/wall/boxed", test_wall_boxed);
    g_test_add_func ("/wall-set/add-get", test_wall_set_add_get);
    g_test_add_func ("/wall-set/properties", test_wall_set_properties);
    g_test_add_func ("/wall-set/is-clear", test_wall_set_is_clear);
    g_test_add_func ("/wall-set/move/free", test_wall_set_move_free);
    g_test_add_func ("/wall-set/move/block", test_wall_set_move_block);
    g_test_add_func ("/wall-set/move/corner-slide", test_wall_set_corner_slide);
    g_test_add_func ("/wall-set/move/door-gap", test_wall_set_door_gap);
    g_test_add_func ("/wall-set/move/invalid", test_wall_set_move_invalid);
    g_test_add_func ("/wall-set/move/tie-order", test_wall_set_tie_order);
    g_test_add_func ("/wall-set/segment-clear", test_wall_set_segment_clear);
    g_test_add_func ("/wall-set/raycast", test_wall_set_raycast);
    g_test_add_func ("/wall-set/volumes", test_wall_set_volumes);
    g_test_add_func ("/wall-set/grid/invalidation", test_wall_set_grid_invalidation);
    g_test_add_func ("/wall-set/grid/equivalence", test_wall_set_equivalence);
    g_test_add_func ("/wall-set/grid/equivalence-limited", test_wall_set_equivalence_limited);

    return g_test_run ();
}
